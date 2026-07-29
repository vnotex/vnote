// Unit + multi-process tests for UpdateLease, the machine-wide sentinel that
// serializes the incremental updater.
//
// The multi-process cases are the important ones: the whole reason the lease is
// an exclusively-opened FILE HANDLE rather than a QLockFile or a `Local\` named
// mutex is cross-process, cross-session behavior that a single-process test
// cannot observe.
//
// This file defines its own main() so it can also run as the CHILD helper
// process (see runChildMode below).

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <utility>

#include <core/updatelease.h>

using namespace vnotex;

namespace tests {

namespace {

const char *const c_modeHoldForever = "--lease-hold-forever";
const char *const c_modeHold = "--lease-hold";
const char *const c_modeTry = "--lease-try";

// Printed by the child on stdout so the parent knows when it is safe to test
// contention. Without this handshake the parent could race the child's acquire.
const char *const c_acquiredMarker = "LEASE_ACQUIRED";
const char *const c_timeoutMarker = "LEASE_TIMEOUT";
const char *const c_failedMarker = "LEASE_FAILED";

bool sentinelExists(const QString &p_installDir) {
  // Deliberately a directory listing rather than QFileInfo::exists(): the
  // holder has the file open with dwShareMode = 0, and enumerating the parent
  // never touches the file itself.
  return QDir(p_installDir)
      .entryList(QStringList{QStringLiteral(".vnote-update.lease")},
                 QDir::Files | QDir::Hidden | QDir::System)
      .size() == 1;
}

} // namespace

// Child entry point. Returns the process exit code, or -1 when argv does not
// select a child mode.
int runChildMode(int argc, char *argv[]) {
  if (argc < 3) {
    return -1;
  }
  const QString mode = QString::fromLocal8Bit(argv[1]);
  const QString installDir = QString::fromLocal8Bit(argv[2]);

  if (mode == QLatin1String(c_modeHoldForever)) {
    UpdateLease lease = UpdateLease::acquire(installDir, 0);
    if (!lease) {
      fprintf(stdout, "%s\n", c_failedMarker);
      fflush(stdout);
      return 2;
    }
    fprintf(stdout, "%s\n", c_acquiredMarker);
    fflush(stdout);
    // Wait to be killed. The kernel must release the lease on process death.
    for (;;) {
      QThread::msleep(50);
    }
  }

  if (mode == QLatin1String(c_modeHold)) {
    const int holdMs = argc >= 4 ? QString::fromLocal8Bit(argv[3]).toInt() : 500;
    UpdateLease lease = UpdateLease::acquire(installDir, 0);
    if (!lease) {
      fprintf(stdout, "%s\n", c_failedMarker);
      fflush(stdout);
      return 2;
    }
    fprintf(stdout, "%s\n", c_acquiredMarker);
    fflush(stdout);
    QThread::msleep(static_cast<unsigned long>(holdMs));
    lease.release();
    return 0;
  }

  if (mode == QLatin1String(c_modeTry)) {
    const int timeoutMs = argc >= 4 ? QString::fromLocal8Bit(argv[3]).toInt() : 0;
    UpdateLease::AcquireError error = UpdateLease::AcquireError::None;
    UpdateLease lease = UpdateLease::acquire(installDir, timeoutMs, &error);
    if (lease) {
      fprintf(stdout, "%s\n", c_acquiredMarker);
      fflush(stdout);
      return 0;
    }
    fprintf(stdout, "%s\n",
            error == UpdateLease::AcquireError::Timeout ? c_timeoutMarker : c_failedMarker);
    fflush(stdout);
    return error == UpdateLease::AcquireError::Timeout ? 3 : 4;
  }

  return -1;
}

class TestUpdateLease : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testLeasePath();
  void testAcquireCreatesAndReleaseRemovesSentinel();
  void testSecondAcquireInSameProcessIsRefused();
  void testMoveSemantics();
  void testReleaseIsIdempotent();
  void testAcquireFailsClosedOnMissingInstallDir();

  // Multi-process.
  void testMutualExclusionAcrossProcesses();
  void testBoundedTimeoutWhileHeld();
  void testWaiterAcquiresAfterHolderReleases();
  void testKillingHolderFreesLeaseImmediately();

private:
  QString installDir() const { return m_dir->path(); }

  // Starts this very executable in a child mode and waits for its handshake.
  QProcess *startChild(const char *p_mode, const QStringList &p_extraArgs = QStringList());

  QScopedPointer<QTemporaryDir> m_dir;
  QVector<QProcess *> m_children;
};

void TestUpdateLease::init() {
  m_dir.reset(new QTemporaryDir());
  QVERIFY(m_dir->isValid());
}

void TestUpdateLease::cleanup() {
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

QProcess *TestUpdateLease::startChild(const char *p_mode, const QStringList &p_extraArgs) {
  auto *child = new QProcess();
  m_children.append(child);

  QStringList args;
  args << QString::fromLatin1(p_mode) << installDir();
  args += p_extraArgs;

  child->setProgram(QCoreApplication::applicationFilePath());
  child->setArguments(args);
  child->start();
  return child;
}

// ------------------------------------------------------------- single process

void TestUpdateLease::testLeasePath() {
  const QString path = UpdateLease::leasePath(QStringLiteral("C:/tmp/vnote"));
  QCOMPARE(path, QStringLiteral("C:/tmp/vnote/.vnote-update.lease"));

  // The sentinel is a SIBLING of .vnote-update/, never inside it: committing
  // deletes that whole directory and must not race the lock.
  QVERIFY(!path.contains(QStringLiteral("/.vnote-update/")));

  QVERIFY(UpdateLease::leasePath(QString()).isEmpty());
}

void TestUpdateLease::testAcquireCreatesAndReleaseRemovesSentinel() {
  QVERIFY(!sentinelExists(installDir()));

  UpdateLease::AcquireError error = UpdateLease::AcquireError::Timeout;
  UpdateLease lease = UpdateLease::acquire(installDir(), 1000, &error);
  QCOMPARE(error, UpdateLease::AcquireError::None);
  QVERIFY(lease.isHeld());
  QVERIFY(static_cast<bool>(lease));
  QCOMPARE(lease.path(), UpdateLease::leasePath(installDir()));
  QVERIFY(sentinelExists(installDir()));

  lease.release();
  QVERIFY(!lease.isHeld());
  QVERIFY2(!sentinelExists(installDir()), "the sentinel must be gone after release");
}

void TestUpdateLease::testSecondAcquireInSameProcessIsRefused() {
  UpdateLease first = UpdateLease::acquire(installDir(), 0);
  QVERIFY(first.isHeld());

  // dwShareMode = 0 is per-HANDLE, so even the owning process cannot open a
  // second handle. This is what makes the lease unambiguous.
  UpdateLease::AcquireError error = UpdateLease::AcquireError::None;
  UpdateLease second = UpdateLease::acquire(installDir(), 0, &error);
  QVERIFY(!second.isHeld());
  QCOMPARE(error, UpdateLease::AcquireError::Timeout);

  first.release();
  UpdateLease third = UpdateLease::acquire(installDir(), 0);
  QVERIFY(third.isHeld());
}

void TestUpdateLease::testMoveSemantics() {
  UpdateLease lease = UpdateLease::acquire(installDir(), 0);
  QVERIFY(lease.isHeld());
  const QString path = lease.path();

  UpdateLease moved = std::move(lease);
  QVERIFY(moved.isHeld());
  QCOMPARE(moved.path(), path);
  QVERIFY(!lease.isHeld()); // NOLINT: intentional use-after-move check
  QVERIFY(sentinelExists(installDir()));

  {
    // Move-assign onto a held lease: the old handle must be released first,
    // not leaked.
    UpdateLease target;
    target = std::move(moved);
    QVERIFY(target.isHeld());
    QVERIFY(sentinelExists(installDir()));
  }

  // The destructor of `target` released it.
  QVERIFY(!sentinelExists(installDir()));
}

void TestUpdateLease::testReleaseIsIdempotent() {
  UpdateLease lease = UpdateLease::acquire(installDir(), 0);
  QVERIFY(lease.isHeld());
  lease.release();
  lease.release();
  lease.release();
  QVERIFY(!lease.isHeld());
  QVERIFY(!sentinelExists(installDir()));
}

void TestUpdateLease::testAcquireFailsClosedOnMissingInstallDir() {
  const QString missing = installDir() + QStringLiteral("/does/not/exist");
  UpdateLease::AcquireError error = UpdateLease::AcquireError::None;
  UpdateLease lease = UpdateLease::acquire(missing, 1000, &error);
  QVERIFY(!lease.isHeld());
  // Fail CLOSED: this must not be reported as a mere timeout, and it must not
  // silently create the directory.
  QCOMPARE(error, UpdateLease::AcquireError::Fatal);
  QVERIFY(!QDir(missing).exists());

  error = UpdateLease::AcquireError::None;
  QVERIFY(!UpdateLease::acquire(QString(), 0, &error).isHeld());
  QCOMPARE(error, UpdateLease::AcquireError::Fatal);
}

// -------------------------------------------------------------- multi-process

void TestUpdateLease::testMutualExclusionAcrossProcesses() {
  QProcess *holder = startChild(c_modeHoldForever);
  QVERIFY2(holder->waitForStarted(10000), "child did not start");
  QVERIFY2(holder->waitForReadyRead(10000), "child did not report acquisition");
  QVERIFY(QString::fromLocal8Bit(holder->readAllStandardOutput())
              .contains(QLatin1String(c_acquiredMarker)));

  // This process must NOT be able to acquire while the child holds it.
  UpdateLease::AcquireError error = UpdateLease::AcquireError::None;
  UpdateLease lease = UpdateLease::acquire(installDir(), 0, &error);
  QVERIFY2(!lease.isHeld(), "two processes acquired the same lease");
  QCOMPARE(error, UpdateLease::AcquireError::Timeout);

  // And neither can a third process.
  QProcess *contender = startChild(c_modeTry, QStringList{QStringLiteral("0")});
  QVERIFY(contender->waitForFinished(10000));
  QCOMPARE(contender->exitCode(), 3);
}

void TestUpdateLease::testBoundedTimeoutWhileHeld() {
  QProcess *holder = startChild(c_modeHoldForever);
  QVERIFY(holder->waitForStarted(10000));
  QVERIFY(holder->waitForReadyRead(10000));

  QElapsedTimer timer;
  timer.start();
  UpdateLease::AcquireError error = UpdateLease::AcquireError::None;
  UpdateLease lease = UpdateLease::acquire(installDir(), 400, &error);
  const qint64 elapsed = timer.elapsed();

  QVERIFY(!lease.isHeld());
  QCOMPARE(error, UpdateLease::AcquireError::Timeout);
  // Bounded: it polls, it does not block forever, and it does not return early.
  QVERIFY2(elapsed >= 350, qPrintable(QStringLiteral("returned after only %1 ms").arg(elapsed)));
  QVERIFY2(elapsed < 5000, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
}

void TestUpdateLease::testWaiterAcquiresAfterHolderReleases() {
  QProcess *holder = startChild(c_modeHold, QStringList{QStringLiteral("600")});
  QVERIFY(holder->waitForStarted(10000));
  QVERIFY(holder->waitForReadyRead(10000));

  // The poll loop must survive ERROR_DELETE_PENDING, which is exactly what the
  // kernel reports while the holder's FILE_FLAG_DELETE_ON_CLOSE unwinds.
  UpdateLease::AcquireError error = UpdateLease::AcquireError::Timeout;
  UpdateLease lease = UpdateLease::acquire(installDir(), 15000, &error);
  QCOMPARE(error, UpdateLease::AcquireError::None);
  QVERIFY(lease.isHeld());

  QVERIFY(holder->waitForFinished(10000));
  QCOMPARE(holder->exitCode(), 0);
}

void TestUpdateLease::testKillingHolderFreesLeaseImmediately() {
  QProcess *holder = startChild(c_modeHoldForever);
  QVERIFY(holder->waitForStarted(10000));
  QVERIFY(holder->waitForReadyRead(10000));

  QVERIFY(!UpdateLease::acquire(installDir(), 0).isHeld());

  // No stale timeout, no PID-reuse ambiguity: the OS closes the handle on
  // process death, so the lease is free as soon as the process is gone.
  holder->kill();
  QVERIFY(holder->waitForFinished(10000));

  UpdateLease::AcquireError error = UpdateLease::AcquireError::Timeout;
  UpdateLease lease = UpdateLease::acquire(installDir(), 5000, &error);
  QCOMPARE(error, UpdateLease::AcquireError::None);
  QVERIFY(lease.isHeld());

  // A killed holder cannot run FILE_FLAG_DELETE_ON_CLOSE cleanup itself, but
  // the kernel does it, so the sentinel we now hold is a fresh one.
  QVERIFY(sentinelExists(installDir()));
}

} // namespace tests

// NOTE (manual): the lease is a file handle rather than a `Local\` named mutex
// specifically so it is honored ACROSS Windows sessions (fast user switching /
// RDP against a shared portable install), without needing
// SeCreateGlobalPrivilege. CI runners have a single interactive session, so
// that property is verified manually:
//
//   1. Put a VNote install on a path both users can write (e.g. D:\vnote).
//   2. Log in as user A, run `test_updatelease --lease-hold-forever D:\vnote`.
//   3. Switch users (do NOT log A out), log in as user B, run
//      `test_updatelease --lease-try D:\vnote 2000`.
//   4. B must print LEASE_TIMEOUT and exit 3.

int main(int argc, char *argv[]) {
  const int childResult = tests::runChildMode(argc, argv);
  if (childResult >= 0) {
    return childResult;
  }

  QCoreApplication app(argc, argv);
  QTEST_SET_MAIN_SOURCE_PATH
  tests::TestUpdateLease testObject;
  return QTest::qExec(&testObject, argc, argv);
}

#include "test_updatelease.moc"
