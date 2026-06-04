// T27 (sync-queue-convergence): NotebookBeforeClose hook refuses to close a
// notebook while sync is in-flight OR while sync work is queued-but-not-running.
// Auto-flush is NOT performed — user must explicitly cancel.
//
// Test scenarios:
//   * sync running (no queued items)  -> close cancelled, pendingCount=0
//   * sync queued (not running)       -> close cancelled, pendingCount>0
//   * neither                         -> close allowed

#include <QApplication>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworkqueuemanager.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

class TestSyncCloseBlock : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void closeBlockedWhenInProgress();
  void closeBlockedWhenQueuedNotRunning();
  void closeAllowedWhenIdle();

private:
  // Auto-dismiss any modal QMessageBox so synchronous QMessageBox::warning
  // calls inside the close-hook handler don't deadlock the test.
  void armDialogDismisser();
};

void TestSyncCloseBlock::initTestCase() { vxcore_set_test_mode(1); }

void TestSyncCloseBlock::armDialogDismisser() {
  QTimer::singleShot(50, this, []() {
    for (QWidget *w : QApplication::topLevelWidgets()) {
      if (auto *mb = qobject_cast<QMessageBox *>(w)) {
        mb->close();
      }
    }
  });
}

void TestSyncCloseBlock::closeBlockedWhenInProgress() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService nbSvc(ctx);
    services.registerService<NotebookCoreService>(&nbSvc);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    // T5: this test uses testForceInFlight (no real keychain write) but the
    // guard wires the rollout uniformly.
    tests::KeychainGuard guard(&credStore);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    SyncWorkQueueManager wq;
    services.registerService<SyncWorkQueueManager>(&wq);
    SyncService svc(services);

    const QString nbId = QStringLiteral("nbInFlight");
    wq.testForceInFlight(nbId, true);
    QVERIFY(svc.isSyncInProgress(nbId));

    NotebookCloseEvent ev;
    ev.notebookId = nbId;

    armDialogDismisser();
    HookContext lastCtx;
    hookMgr.addAction<NotebookCloseEvent>(
        HookNames::NotebookBeforeClose,
        [&lastCtx](HookContext &p_ctx, const NotebookCloseEvent &) { lastCtx = p_ctx; },
        /*priority=*/1000);
    const bool cancelled = hookMgr.doAction(HookNames::NotebookBeforeClose, ev);
    QVERIFY2(cancelled, "Close must be cancelled while sync is in-flight");
    QCOMPARE(lastCtx.metadata().value(QStringLiteral("pendingCount")).toInt(), 0);
    QVERIFY(!lastCtx.metadata().value(QStringLiteral("syncCancelReason")).toString().isEmpty());

    wq.testForceInFlight(nbId, false);
    guard.cleanup();
  }
  vxcore_context_destroy(ctx);
}

void TestSyncCloseBlock::closeBlockedWhenQueuedNotRunning() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService nbSvc(ctx);
    services.registerService<NotebookCoreService>(&nbSvc);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    SyncWorkQueueManager wq;
    services.registerService<SyncWorkQueueManager>(&wq);
    SyncService svc(services);

    const QString nbId = QStringLiteral("nbQueued");

    // Hold an item in the queue: enqueue a long-running work item so a 2nd
    // enqueue piles up as pending.
    QSemaphore release;
    wq.enqueue(nbId, [&release]() { release.acquire(); });
    // Wait briefly for the runner to pick up the first item.
    for (int i = 0; i < 100 && !wq.isRunning(nbId); ++i) {
      QTest::qWait(10);
    }
    QVERIFY(wq.isRunning(nbId));
    wq.enqueue(nbId, []() {}); // pending
    QVERIFY(wq.queueDepth(nbId) >= 1);
    // Predicate must cancel because pendingCount > 0 (even if running is also
    // true while the first item executes).

    NotebookCloseEvent ev;
    ev.notebookId = nbId;

    armDialogDismisser();
    HookContext lastCtx;
    hookMgr.addAction<NotebookCloseEvent>(
        HookNames::NotebookBeforeClose,
        [&lastCtx](HookContext &p_ctx, const NotebookCloseEvent &) { lastCtx = p_ctx; },
        /*priority=*/1000);
    const bool cancelled = hookMgr.doAction(HookNames::NotebookBeforeClose, ev);
    QVERIFY2(cancelled, "Close must be cancelled while sync items are queued");
    QVERIFY(lastCtx.metadata().value(QStringLiteral("pendingCount")).toInt() >= 1);
    QVERIFY(!lastCtx.metadata().value(QStringLiteral("syncCancelReason")).toString().isEmpty());

    release.release();
    QVERIFY(wq.shutdown(5000));
    guard.cleanup();
  }
  vxcore_context_destroy(ctx);
}

void TestSyncCloseBlock::closeAllowedWhenIdle() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService nbSvc(ctx);
    services.registerService<NotebookCoreService>(&nbSvc);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    SyncWorkQueueManager wq;
    services.registerService<SyncWorkQueueManager>(&wq);
    SyncService svc(services);

    const QString nbId = QStringLiteral("nbIdle");
    QVERIFY(!svc.isSyncInProgress(nbId));

    NotebookCloseEvent ev;
    ev.notebookId = nbId;
    const bool cancelled = hookMgr.doAction(HookNames::NotebookBeforeClose, ev);
    QVERIFY2(!cancelled, "Close must proceed when sync is idle");
    guard.cleanup();
  }
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_MAIN(tests::TestSyncCloseBlock)
#include "test_sync_close_block.moc"
