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
#include <QDir>
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

} // namespace

// Child modes isolate real lock ownership and synchronous secondary forwarding
// from the test runner's primary event loop.
int runChildMode(int argc, char *argv[]) {
  if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--send-ipc")) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    SingleInstanceGuard secondary(args.at(2), args.at(3));
    if (secondary.tryRun() != SingleInstanceGuard::TryRunResult::Secondary) {
      return 2;
    }
    fprintf(stdout, "SENDER_READY\n");
    fflush(stdout);
    const auto files = args.mid(5);
    const bool ok = args.at(4) == QLatin1String("detached")
                        ? secondary.requestOpenFilesDetached(files)
                        : secondary.requestOpenFiles(files) && secondary.requestShow();
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

private:
  QProcess *startSender(const QString &p_mode, const QStringList &p_files);

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
  QVERIFY2(holder->waitForReadyRead(10000), "lock holder did not report the lock");
  QVERIFY(QString::fromLocal8Bit(holder->readAllStandardOutput())
              .contains(QLatin1String(c_heldMarker)));

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
  QVERIFY(holder->waitForReadyRead(10000));

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
  QSignalSpy detached(&primary, &SingleInstanceGuard::openFilesDetachedRequested);
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
  QTRY_COMPARE(detached.count(), 1);
  QCOMPARE(detached.at(0).at(0).toStringList(), QStringList{secondPath});
  QCOMPARE(opened.count(), 0);

  QCOMPARE(first.write(firstFrame.mid(8)), qint64(firstFrame.size() - 8));
  first.flush();
  QTRY_COMPARE(opened.count(), 1);
  QCOMPARE(opened.at(0).at(0).toStringList(), QStringList{firstPath});
}

QProcess *TestSingleInstanceGuard::startSender(const QString &p_mode, const QStringList &p_files) {
  auto *child = new QProcess();
  m_children.append(child);
  child->setProgram(QCoreApplication::applicationFilePath());
  child->setWorkingDirectory(m_dir->path());
  child->setArguments(QStringList{QStringLiteral("--send-ipc"), serverName(), lockPath(), p_mode} +
                      p_files);
  child->start();
  return child;
}

void TestSingleInstanceGuard::testChildForwarding_data() {
  QTest::addColumn<bool>("detached");
  QTest::addColumn<int>("delay");
  QTest::addColumn<bool>("empty");
  QTest::newRow("normal-unicode-and-relative") << false << 0 << false;
  QTest::newRow("detached-no-show") << true << 0 << false;
  QTest::newRow("primary-busy-past-old-timeout") << false << 3500 << false;
  QTest::newRow("empty-files-still-show") << false << 0 << true;
}

void TestSingleInstanceGuard::testChildForwarding() {
  QFETCH(bool, detached);
  QFETCH(int, delay);
  QFETCH(bool, empty);
  SingleInstanceGuard primary(serverName(), lockPath());
  QCOMPARE(primary.tryRun(), SingleInstanceGuard::TryRunResult::Primary);
  QSignalSpy opened(&primary, &SingleInstanceGuard::openFilesRequested);
  QSignalSpy detachedOpened(&primary, &SingleInstanceGuard::openFilesDetachedRequested);
  QSignalSpy shown(&primary, &SingleInstanceGuard::showRequested);
  const QStringList files = empty ? QStringList{}
                                  : QStringList{m_dir->filePath(QString::fromUtf8("中文 文件.md")),
                                                QStringLiteral("relative note.md")};
  auto *child =
      startSender(detached ? QStringLiteral("detached") : QStringLiteral("normal"), files);
  QVERIFY(child->waitForStarted(10000));
  QVERIFY(child->waitForReadyRead(10000));
  QVERIFY(child->readAllStandardOutput().contains("SENDER_READY"));
  // Deliberately do NOT pump the primary event loop during a slow startup.
  if (delay > 0) {
    QThread::msleep(delay);
  }
  QTRY_COMPARE_WITH_TIMEOUT(child->state(), QProcess::NotRunning, 10000);
  QCOMPARE(child->exitStatus(), QProcess::NormalExit);
  QVERIFY2(child->exitCode() == 0, child->readAllStandardError().constData());
  QTRY_COMPARE(opened.count(), !empty && !detached ? 1 : 0);
  QTRY_COMPARE(detachedOpened.count(), !empty && detached ? 1 : 0);
  QTRY_COMPARE(shown.count(), detached ? 0 : 1);
  if (!empty) {
    const QStringList expected{files.at(0), m_dir->filePath(files.at(1))};
    const auto &spy = detached ? detachedOpened : opened;
    QCOMPARE(spy.at(0).at(0).toStringList(), expected);
  }
}

void TestSingleInstanceGuard::testUnacknowledgedRequestFails_data() {
  QTest::addColumn<bool>("wrongAck");
  QTest::newRow("peer-disconnects-without-ack") << false;
  QTest::newRow("wrong-opcode-ack") << true;
}

void TestSingleInstanceGuard::testUnacknowledgedRequestFails() {
  QFETCH(bool, wrongAck);
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
        // Sender requested Show; acknowledge a different operation.
        socket->write(requestFrame(2));
        socket->flush();
      } else {
        socket->abort();
      }
    };
    connect(socket, &QLocalSocket::readyRead, this, reject);
    reject();
  });
  auto *child = startSender(QStringLiteral("normal"), {});
  QVERIFY(child->waitForStarted(10000));
  QTRY_COMPARE_WITH_TIMEOUT(child->state(), QProcess::NotRunning, 10000);
  QCOMPARE(child->exitStatus(), QProcess::NormalExit);
  QCOMPARE(child->exitCode(), 4);
  QCOMPARE(received, requestFrame(1));
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
