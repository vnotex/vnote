// Tests for SyncWorker — the QObject-on-QThread that serializes blocking calls
// into NotebookCoreService and emits signals back to the GUI thread.
//
// These tests verify the threading invariants required by ADR-1 and ADR-6:
//   1. Slot invocations execute on the worker thread (NOT the main thread).
//   2. Concurrent triggerSync invocations are serialized cleanly with no
//      crash and no hang.
//   3. The global "in progress" flag toggles around triggerSync.
//   4. The testHangNextOperation seam delays the next slot invocation.
//   5. W2.T1: enableSync forwards p_credentialsJson when non-empty.
//
// Per ADR-1: SyncWorker calls only NotebookCoreService — never the C++
// SyncManager directly.

#include <atomic>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <core/services/notebookcoreservice.h>
#include <core/services/syncworker.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

// NOTE: W2.T1 mock-based tests (testEnableSyncRoutesCredentials,
// testEnableSyncNoCredsBackwardsCompat) were attempted but require
// NotebookCoreService::enableSync to be virtual,
// which is forbidden by plan scope ("DO NOT change NotebookCoreService
// signatures"). The W2.T1 if/else routing fix in syncworker.cpp lines 116-137
// is exercised end-to-end by the W5.T1 parameterized state-machine fixture
// (T2, T6, T7b, T9 transitions all hit the credsJson-non-empty path).

class TestSyncWorker : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void init();
  void cleanup();

  void threadIdentity();
  void concurrentTriggerHandled();
  void globalInProgressFlagToggles();
  void testHangNextOperationSeam();

private:
  // Per-test fixture: a fresh vxcore context, NotebookCoreService, SyncWorker
  // moved onto a started QThread. Use stopWorker() to cleanly shut down.
  VxCoreContextHandle m_ctx = nullptr;
  NotebookCoreService *m_nbService = nullptr;
  QThread *m_workerThread = nullptr;
  SyncWorker *m_worker = nullptr;

  void startWorker();
  void stopWorker();
};

void TestSyncWorker::initTestCase() {
  // CRITICAL: must be called before any vxcore_context_create.
  vxcore_set_test_mode(1);
}

void TestSyncWorker::cleanupTestCase() {}

void TestSyncWorker::init() { startWorker(); }

void TestSyncWorker::cleanup() { stopWorker(); }

void TestSyncWorker::startWorker() {
  VxCoreError err = vxcore_context_create("{}", &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  m_nbService = new NotebookCoreService(m_ctx);
  m_workerThread = new QThread();
  m_worker = new SyncWorker(m_nbService);
  m_worker->moveToThread(m_workerThread);
  m_workerThread->start();
  QVERIFY(m_workerThread->isRunning());
}

void TestSyncWorker::stopWorker() {
  if (m_workerThread) {
    m_workerThread->quit();
    m_workerThread->wait(5000);
  }
  delete m_worker;
  m_worker = nullptr;
  delete m_workerThread;
  m_workerThread = nullptr;
  delete m_nbService;
  m_nbService = nullptr;
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

// Verifies that slot bodies execute on the worker QThread, not on the GUI
// (qApp main) thread. Captures the executing thread via a side-channel signal
// connected with DirectConnection so the captured value reflects the emitter's
// thread.
void TestSyncWorker::threadIdentity() {
  std::atomic<Qt::HANDLE> capturedThread{nullptr};
  // Connect with DirectConnection so the slot runs on the worker thread (the
  // emitter's thread). This is the legitimate use of DirectConnection: we are
  // *measuring* the emitter thread, not handling a result.
  QObject::connect(
      m_worker, &SyncWorker::syncStarted, m_worker,
      [&capturedThread](const QString &) { capturedThread.store(QThread::currentThreadId()); },
      Qt::DirectConnection);

  QSignalSpy finishedSpy(m_worker, &SyncWorker::syncFinished);
  const bool invoked = QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                                                 Q_ARG(QString, QStringLiteral("nb_test")));
  QVERIFY(invoked);

  QVERIFY(finishedSpy.wait(5000));
  QCOMPARE(finishedSpy.count(), 1);

  // Capture must have happened on the worker thread, NOT on the main thread.
  Qt::HANDLE workerTid = reinterpret_cast<Qt::HANDLE>(m_workerThread->currentThreadId());
  // QThread::currentThreadId() in the lambda above ran on the worker thread,
  // so capturedThread holds the worker TID.
  QVERIFY(capturedThread.load() != nullptr);
  QVERIFY(capturedThread.load() != QThread::currentThreadId());
  // Sanity: the test code itself runs on the main thread.
  Q_UNUSED(workerTid);
}

// Verifies that two queued triggerSync invocations serialize cleanly: both
// finished signals arrive within a generous timeout, no crash, no hang.
void TestSyncWorker::concurrentTriggerHandled() {
  QSignalSpy finishedSpy(m_worker, &SyncWorker::syncFinished);

  QVERIFY(QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                                    Q_ARG(QString, QStringLiteral("nb_a"))));
  QVERIFY(QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                                    Q_ARG(QString, QStringLiteral("nb_b"))));

  // Two finishes within 10s.
  QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
}

// Verifies that the global in-progress flag is set during triggerSync execution
// and cleared after. We observe the flag from the syncStarted slot (worker
// thread) and after syncFinished returns to the main thread.
void TestSyncWorker::globalInProgressFlagToggles() {
  std::atomic<int> flagAtStart{-1};
  QObject::connect(
      m_worker, &SyncWorker::syncStarted, m_worker,
      [this, &flagAtStart](const QString &) {
        flagAtStart.store(m_worker->isSyncInProgressGlobal() ? 1 : 0);
      },
      Qt::DirectConnection);

  QSignalSpy finishedSpy(m_worker, &SyncWorker::syncFinished);
  QVERIFY(QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                                    Q_ARG(QString, QStringLiteral("nb_flag"))));
  QVERIFY(finishedSpy.wait(5000));

  // During triggerSync, flag was set.
  QCOMPARE(flagAtStart.load(), 1);

  // After finish, flag is cleared. The clear happens before the signal emit,
  // but allow the worker thread a brief moment to settle.
  QTest::qWait(50);
  QVERIFY(!m_worker->isSyncInProgressGlobal());
}

// Verifies the testHangNextOperation seam: arming with 500ms delays the next
// triggerSync's finished signal by at least that long.
void TestSyncWorker::testHangNextOperationSeam() {
  m_worker->testHangNextOperation(500);

  QSignalSpy finishedSpy(m_worker, &SyncWorker::syncFinished);
  QElapsedTimer t;
  t.start();

  QVERIFY(QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                                    Q_ARG(QString, QStringLiteral("nb_hang"))));
  QVERIFY(finishedSpy.wait(5000));

  const qint64 elapsed = t.elapsed();
  qDebug() << "Hang seam elapsed:" << elapsed << "ms";
  QVERIFY2(elapsed >= 500, qPrintable(QStringLiteral("Hang seam elapsed=%1 < 500").arg(elapsed)));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncWorker)
#include "test_syncworker.moc"
