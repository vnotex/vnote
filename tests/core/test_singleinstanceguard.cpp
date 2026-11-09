// SingleInstanceGuard lock ownership and cross-process command delivery.
//
// The behavior under test is the FAIL-CLOSED change: the branch where the lock
// is held but the holder cannot be reached over IPC used to log a warning and
// return true, producing a SECOND primary. Two primaries write the same config,
// session snapshot and notebook state, and both run ConfigMgr2's
// bundled-resource install over the same folders.
//
// Every case uses a UNIQUE server name and lock path via the testing
// constructor, so running this suite can never disturb (or be disturbed by) a
// real VNote instance on the developer's machine.

#include <QtTest>

#include <QCoreApplication>
#include <QDataStream>
#include <QDeadlineTimer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <cstdio>

#include <core/singleinstanceguard.h>

using namespace vnotex;

namespace tests {

namespace {

const char *const c_modeHoldLock = "--hold-lock-forever";
const char *const c_heldMarker = "LOCK_HELD";

// Child stdout is a byte stream, not one notification per complete marker.
// Keep the primary event loop blocked for the slow-start forwarding case.
bool waitForMarker(QProcess *p_process, const QByteArray &p_marker) {
  QDeadlineTimer deadline(10000);
  auto output = p_process->readAllStandardOutput();
  while (!output.contains(p_marker)) {
    if (deadline.hasExpired() || !p_process->waitForReadyRead(deadline.remainingTime())) {
      return false;
    }
    output += p_process->readAllStandardOutput();
  }
  return true;
}
} // namespace

// Child modes isolate real lock ownership and synchronous secondary forwarding
// from the test runner's primary event loop.
int runChildMode(int argc, char *argv[]) {
  if (argc >= 6 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--send-ipc")) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    SingleInstanceGuard secondary(args.at(2), args.at(3));
    if (secondary.tryRun() != SingleInstanceGuard::TryRunResult::Secondary) {
      return 2;
    }
    fprintf(stdout, "SENDER_READY\n");
    fflush(stdout);
    const auto files = args.mid(6);
    const bool detached = args.at(4) == QLatin1String("detached");
    const auto mode = static_cast<ViewWindowMode>(args.at(5).toInt());
    const bool ok =
        secondary.requestOpenFiles(files, detached, mode) && (detached || secondary.requestShow());
    return ok ? 0 : 4;
  }
  if (argc < 3 || QString::fromLocal8Bit(argv[1]) != QLatin1String(c_modeHoldLock)) {
    return -1;
  }

  QLockFile lock(QString::fromLocal8Bit(argv[2]));
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) {
    return 2;
  }

  fprintf(stdout, "%s\n", c_heldMarker);
  fflush(stdout);
  for (;;) {
    QThread::msleep(50);
  }
}

class TestSingleInstanceGuard : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testFirstInstanceIsPrimary();
  void testSecondInstanceIsSecondary();
  void testUnreachableHolderIsBusyNotPrimary();
  void testStaleLockFromDeadHolderIsReclaimed();
  void testExitReleasesTheLock();
  void testFragmentedCommands();
  void testOverlappingConnections();
  void testChildForwarding_data();
  void testChildForwarding();
  void testUnacknowledgedRequestFails_data();
  void testUnacknowledgedRequestFails();
  void testDisconnectedPartialRequest();
  void testMalformedOptions_data();
  void testMalformedOptions();

private:
  QProcess *startSender(const QString &p_destination, const QStringList &p_files,
                        ViewWindowMode p_mode = ViewWindowMode::Invalid);

  QString lockPath() const { return m_dir->filePath(QStringLiteral("guard.lock")); }
  QString serverName() const { return m_serverName; }

  QScopedPointer<QTemporaryDir> m_dir;
  QString m_serverName;
  QVector<QProcess *> m_children;
};

void TestSingleInstanceGuard::init() {
  m_dir.reset(new QTemporaryDir());
  QVERIFY(m_dir->isValid());
  // Unique per test: a leftover named pipe / socket from a previous run must
  // never make a case pass or fail spuriously.
  m_serverName = QStringLiteral("vnote-test-%1")
                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
}

void TestSingleInstanceGuard::cleanup() {
  for (QProcess *child : m_children) {
    if (child->state() != QProcess::NotRunning) {
      child->kill();
      child->waitForFinished(5000);
    }
    delete child;
  }
  m_children.clear();
  m_dir.reset();
}

void TestSingleInstanceGuard::testFirstInstanceIsPrimary() {
  SingleInstanceGuard guard(serverName(), lockPath());
  QCOMPARE(guard.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
}

void TestSingleInstanceGuard::testSecondInstanceIsSecondary() {
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);

  // A second guard in the same process: the lock is held AND the IPC server is
  // reachable, so it must forward rather than run.
  SingleInstanceGuard secondary(serverName(), lockPath());
  QCOMPARE(secondary.tryRun(), SingleInstanceGuard::TryRunResult::Secondary);
}

// The regression this whole change exists for.
void TestSingleInstanceGuard::testUnreachableHolderIsBusyNotPrimary() {
  auto *holder = new QProcess();
  m_children.append(holder);
  holder->setProgram(QCoreApplication::applicationFilePath());
  holder->setArguments(QStringList{QString::fromLatin1(c_modeHoldLock), lockPath()});
  holder->start();
  QVERIFY2(holder->waitForStarted(10000), "lock holder did not start");
  QVERIFY2(waitForMarker(holder, c_heldMarker), "lock holder did not report the lock");

  // The holder is ALIVE (so the lock is not stale and cannot be reclaimed) but
  // it never started an IPC server, so tryConnect() times out.
  SingleInstanceGuard guard(serverName(), lockPath());
  const auto result = guard.tryRun();

  QVERIFY2(result != SingleInstanceGuard::TryRunResult::Primary,
           "a guard that cannot reach the lock holder must NOT become a second primary");
  QCOMPARE(result, SingleInstanceGuard::TryRunResult::BusyUnreachable);
}

void TestSingleInstanceGuard::testStaleLockFromDeadHolderIsReclaimed() {
  auto *holder = new QProcess();
  m_children.append(holder);
  holder->setProgram(QCoreApplication::applicationFilePath());
  holder->setArguments(QStringList{QString::fromLatin1(c_modeHoldLock), lockPath()});
  holder->start();
  QVERIFY(holder->waitForStarted(10000));
  QVERIFY(waitForMarker(holder, c_heldMarker));

  // Kill the holder: the lock file survives on disk but its owner is gone, so
  // QLockFile::removeStaleLockFile() can reclaim it and we become primary.
  holder->kill();
  QVERIFY(holder->waitForFinished(10000));

  SingleInstanceGuard guard(serverName(), lockPath());
  QCOMPARE(guard.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
}

void TestSingleInstanceGuard::testExitReleasesTheLock() {
  {
    SingleInstanceGuard guard(serverName(), lockPath());
    QCOMPARE(guard.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
    guard.exit();

    // After exit() the lock is free, so a fresh guard is primary again.
    SingleInstanceGuard next(serverName(), lockPath());
    QCOMPARE(next.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  }

  // The destructor calls exit() too, so this is a third clean acquisition.
  SingleInstanceGuard afterScope(serverName(), lockPath());
  QCOMPARE(afterScope.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
}

// Keep the existing Qt_5_12 request format: size is UTF-16 units, not bytes.
static QByteArray requestFrame(quint32 p_code, const QString &p_payload = QString()) {
  QByteArray frame;
  QDataStream stream(&frame, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_5_12);
  stream << p_code << quint32(p_payload.size());
  if (!p_payload.isEmpty()) {
    stream << p_payload;
  }
  return frame;
}

void TestSingleInstanceGuard::testFragmentedCommands() {
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  QSignalSpy opened(&primary, &SingleInstanceGuard::openFilesRequested);
  QSignalSpy shown(&primary, &SingleInstanceGuard::showRequested);
  QLocalSocket sender;
  sender.connectToServer(serverName());
  QVERIFY(sender.waitForConnected(1000));

  const QString path = m_dir->filePath(QString::fromUtf8("分包 note.md"));
  const auto frame = requestFrame(2, path);
  // Split the header, then stop halfway through the serialized UTF-16 body.
  QCOMPARE(sender.write(frame.left(3)), qint64(3));
  sender.flush();
  QTest::qWait(50);
  QCOMPARE(opened.count(), 0);
  const int split = 8 + path.size();
  QCOMPARE(sender.write(frame.mid(3, split - 3)), qint64(split - 3));
  sender.flush();
  QTest::qWait(50);
  QCOMPARE(opened.count(), 0);

  // Completing that body and appending Show must produce exactly one of each.
  const auto rest = frame.mid(split) + requestFrame(1);
  QCOMPARE(sender.write(rest), qint64(rest.size()));
  sender.flush();
  QTRY_COMPARE(opened.count(), 1);
  QTRY_COMPARE(shown.count(), 1);
  QCOMPARE(opened.at(0).at(0).toStringList(), QStringList{path});
}

void TestSingleInstanceGuard::testOverlappingConnections() {
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  QSignalSpy opened(&primary, &SingleInstanceGuard::openFilesRequested);
  QLocalSocket first;
  first.connectToServer(serverName());
  QVERIFY(first.waitForConnected(1000));
  const QString firstPath = m_dir->filePath(QStringLiteral("first.md"));
  const auto firstFrame = requestFrame(2, firstPath);
  QCOMPARE(first.write(firstFrame.left(8)), qint64(8));
  first.flush();
  QTest::qWait(50);

  QLocalSocket second;
  second.connectToServer(serverName());
  QVERIFY(second.waitForConnected(1000));
  const QString secondPath = m_dir->filePath(QStringLiteral("second.md"));
  const auto secondFrame = requestFrame(3, secondPath);
  QCOMPARE(second.write(secondFrame), qint64(secondFrame.size()));
  second.flush();
  QTRY_COMPARE(opened.count(), 1);
  QCOMPARE(opened.at(0).at(0).toStringList(), QStringList{secondPath});
  QCOMPARE(opened.at(0).at(1).toBool(), true);
  QCOMPARE(opened.at(0).at(2).value<ViewWindowMode>(), ViewWindowMode::Invalid);

  QCOMPARE(first.write(firstFrame.mid(8)), qint64(firstFrame.size() - 8));
  first.flush();
  QTRY_COMPARE(opened.count(), 2);
  QCOMPARE(opened.at(1).at(0).toStringList(), QStringList{firstPath});
  QCOMPARE(opened.at(1).at(1).toBool(), false);
  QCOMPARE(opened.at(1).at(2).value<ViewWindowMode>(), ViewWindowMode::Invalid);
}

QProcess *TestSingleInstanceGuard::startSender(const QString &p_destination,
                                               const QStringList &p_files, ViewWindowMode p_mode) {
  auto *child = new QProcess();
  m_children.append(child);
  child->setProgram(QCoreApplication::applicationFilePath());
  // Resolve temporary-directory aliases (such as macOS /var) before the child chdir.
  child->setWorkingDirectory(QDir(m_dir->path()).canonicalPath());
  child->setArguments(QStringList{QStringLiteral("--send-ipc"), serverName(), lockPath(),
                                  p_destination, QString::number(static_cast<int>(p_mode))} +
                      p_files);
  child->start();
  return child;
}

void TestSingleInstanceGuard::testChildForwarding_data() {
  QTest::addColumn<bool>("detached");
  QTest::addColumn<int>("delay");
  QTest::addColumn<bool>("empty");
  QTest::addColumn<int>("mode");
  QTest::newRow("normal-unicode-and-relative")
      << false << 0 << false << int(ViewWindowMode::Invalid);
  QTest::newRow("detached-no-show") << true << 0 << false << int(ViewWindowMode::Invalid);
  QTest::newRow("primary-busy-past-old-timeout")
      << false << 3500 << false << int(ViewWindowMode::Invalid);
  QTest::newRow("empty-files-still-show") << false << 0 << true << int(ViewWindowMode::Invalid);
  QTest::newRow("read-detached") << true << 0 << false << int(ViewWindowMode::Read);
  QTest::newRow("edit-main") << false << 0 << false << int(ViewWindowMode::Edit);
  QTest::newRow("read-main") << false << 0 << false << int(ViewWindowMode::Read);
  QTest::newRow("edit-detached") << true << 0 << false << int(ViewWindowMode::Edit);
  QTest::newRow("read-busy-primary") << true << 3500 << false << int(ViewWindowMode::Read);
  QTest::newRow("empty-edit-still-show") << false << 0 << true << int(ViewWindowMode::Edit);
}

void TestSingleInstanceGuard::testChildForwarding() {
  QFETCH(bool, detached);
  QFETCH(int, delay);
  QFETCH(bool, empty);
  QFETCH(int, mode);
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  QSignalSpy opened(&primary, &SingleInstanceGuard::openFilesRequested);
  QSignalSpy shown(&primary, &SingleInstanceGuard::showRequested);
  const QStringList files = empty ? QStringList{}
                                  : QStringList{m_dir->filePath(QString::fromUtf8("中文 文件.md")),
                                                QStringLiteral("relative note.md")};
  auto *child = startSender(detached ? QStringLiteral("detached") : QStringLiteral("normal"), files,
                            static_cast<ViewWindowMode>(mode));
  QVERIFY(child->waitForStarted(10000));
  QVERIFY(waitForMarker(child, "SENDER_READY"));
  // Deliberately do NOT pump the primary event loop during a slow startup.
  if (delay > 0) {
    QThread::msleep(delay);
  }
  QTRY_COMPARE_WITH_TIMEOUT(child->state(), QProcess::NotRunning, 10000);
  QCOMPARE(child->exitStatus(), QProcess::NormalExit);
  QVERIFY2(child->exitCode() == 0, child->readAllStandardError().constData());
  QTRY_COMPARE(opened.count(), empty ? 0 : 1);
  QTRY_COMPARE(shown.count(), detached ? 0 : 1);
  if (!empty) {
    const QStringList expected{files.at(0), QDir(child->workingDirectory()).filePath(files.at(1))};
    QCOMPARE(opened.at(0).at(0).toStringList(), expected);
    QCOMPARE(opened.at(0).at(1).toBool(), detached);
    QCOMPARE(opened.at(0).at(2).value<ViewWindowMode>(), static_cast<ViewWindowMode>(mode));
  }
}

void TestSingleInstanceGuard::testUnacknowledgedRequestFails_data() {
  QTest::addColumn<bool>("wrongAck");
  QTest::addColumn<bool>("explicitMode");
  QTest::newRow("peer-disconnects-without-ack") << false << false;
  QTest::newRow("wrong-opcode-ack") << true << false;
  QTest::newRow("older-primary-rejects-mode-opcode") << false << true;
  QTest::newRow("mode-opcode-wrong-ack") << true << true;
}

void TestSingleInstanceGuard::testUnacknowledgedRequestFails() {
  QFETCH(bool, wrongAck);
  QFETCH(bool, explicitMode);
  QLockFile lock(lockPath());
  QVERIFY(lock.tryLock(0));
  QLocalServer server;
  QVERIFY(server.listen(serverName()));
  QByteArray received;
  connect(&server, &QLocalServer::newConnection, this, [&]() {
    auto *socket = server.nextPendingConnection();
    const auto reject = [&, socket]() {
      received += socket->readAll();
      if (received.isEmpty()) {
        return;
      }
      if (wrongAck) {
        // Neither Show nor the new options opcode may accept an OpenFiles ACK.
        socket->write(requestFrame(2));
        socket->flush();
      } else {
        socket->abort();
      }
    };
    connect(socket, &QLocalSocket::readyRead, this, reject);
    reject();
  });
  const QStringList files =
      explicitMode ? QStringList{m_dir->filePath(QStringLiteral("note.md"))} : QStringList{};
  auto *child = startSender(QStringLiteral("normal"), files,
                            explicitMode ? ViewWindowMode::Edit : ViewWindowMode::Invalid);
  QVERIFY(child->waitForStarted(10000));
  QTRY_COMPARE_WITH_TIMEOUT(child->state(), QProcess::NotRunning, 10000);
  QCOMPARE(child->exitStatus(), QProcess::NormalExit);
  QCOMPARE(child->exitCode(), 4);
  if (explicitMode) {
    QDataStream request(received);
    request.setVersion(QDataStream::Qt_5_12);
    quint32 opcode = 0;
    request >> opcode;
    QCOMPARE(opcode, quint32(4));
  } else {
    QCOMPARE(received, requestFrame(1));
  }
}

void TestSingleInstanceGuard::testMalformedOptions_data() {
  QTest::addColumn<QString>("payload");
  const QJsonObject valid{{QStringLiteral("mode"), QStringLiteral("read")},
                          {QStringLiteral("detached"), true},
                          {QStringLiteral("files"), QJsonArray{QStringLiteral("note.md")}}};
  const auto add = [](const char *name, const QJsonObject &options) {
    QTest::newRow(name) << QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact));
  };
  for (const auto &mode : {QJsonValue(), QJsonValue(0), QJsonValue(1), QJsonValue(2),
                           QJsonValue(true), QJsonValue(QStringLiteral("Invalid")),
                           QJsonValue(QStringLiteral("Read")), QJsonValue(QString())}) {
    auto options = valid;
    options.insert(QStringLiteral("mode"), mode);
    const auto name = QJsonDocument(QJsonArray{mode}).toJson(QJsonDocument::Compact);
    add(name.constData(), options);
  }
  auto options = valid;
  options.remove(QStringLiteral("mode"));
  add("missing-mode", options);
  options = valid;
  options.insert(QStringLiteral("detached"), QStringLiteral("true"));
  add("invalid-destination", options);
  options = valid;
  options.insert(QStringLiteral("files"), QJsonArray{});
  add("empty-files", options);
  options.insert(QStringLiteral("files"), QJsonArray{42});
  add("non-string-file", options);
  QTest::newRow("broken-json") << QStringLiteral("{");
}

void TestSingleInstanceGuard::testMalformedOptions() {
  QFETCH(QString, payload);
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  QSignalSpy opened(&primary, &SingleInstanceGuard::openFilesRequested);
  QSignalSpy shown(&primary, &SingleInstanceGuard::showRequested);
  QLocalSocket sender;
  sender.connectToServer(serverName());
  QVERIFY(sender.waitForConnected(1000));
  // The rejected request must neither be ACKed nor permit a following Show.
  const auto frame = requestFrame(4, payload) + requestFrame(1);
  QCOMPARE(sender.write(frame), qint64(frame.size()));
  sender.flush();
  QTRY_COMPARE(sender.state(), QLocalSocket::UnconnectedState);
  QCOMPARE(sender.readAll(), QByteArray());
  QCOMPARE(opened.count(), 0);
  QCOMPARE(shown.count(), 0);
}

void TestSingleInstanceGuard::testDisconnectedPartialRequest() {
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  QSignalSpy opened(&primary, &SingleInstanceGuard::openFilesRequested);
  QSignalSpy shown(&primary, &SingleInstanceGuard::showRequested);
  {
    QLocalSocket sender;
    sender.connectToServer(serverName());
    QVERIFY(sender.waitForConnected(1000));
    sender.write(requestFrame(2, QStringLiteral("incomplete.md")).left(10));
    sender.flush();
    QTest::qWait(50);
    sender.abort();
  }
  auto *child = startSender(QStringLiteral("normal"), {});
  QVERIFY(child->waitForStarted(10000));
  QTRY_COMPARE_WITH_TIMEOUT(child->state(), QProcess::NotRunning, 10000);
  QCOMPARE(child->exitCode(), 0);
  QCOMPARE(opened.count(), 0);
  QCOMPARE(shown.count(), 1);
}

} // namespace tests

int main(int argc, char *argv[]) {
  const int childResult = tests::runChildMode(argc, argv);
  if (childResult >= 0) {
    return childResult;
  }

  QCoreApplication app(argc, argv);
  QTEST_SET_MAIN_SOURCE_PATH
  tests::TestSingleInstanceGuard testObject;
  return QTest::qExec(&testObject, argc, argv);
}

#include "test_singleinstanceguard.moc"
