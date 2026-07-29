// Tests for SingleInstanceGuard's tri-state tryRun().
//
// The behavior under test is the FAIL-CLOSED change: the branch where the lock
// is held but the holder cannot be reached over IPC used to log a warning and
// return true, producing a SECOND primary. Under the incremental updater that
// second primary can reach normal initialization -- mapping Qt, VTextEdit and
// vxcore -- while an applier is swapping those very files underneath it.
//
// Every case uses a UNIQUE server name and lock path via the testing
// constructor, so running this suite can never disturb (or be disturbed by) a
// real VNote instance on the developer's machine.

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
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

// Child mode: hold a QLockFile at the given path forever WITHOUT ever starting
// an IPC server. That is exactly the "lock held, holder unreachable" condition
// that used to fail open.
int runChildMode(int argc, char *argv[]) {
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

private:
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
