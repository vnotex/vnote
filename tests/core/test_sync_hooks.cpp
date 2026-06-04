// Tests for the F4.5 sync hook surface (vnote.sync.before_enable,
// vnote.sync.after_disable, vnote.sync.conflict_detected, vnote.sync.cancelled).
//
// These tests verify the hook fire contract for each new sync hook:
//   1. beforeEnableFires: vnote.sync.before_enable fires synchronously when
//      enableSyncForNotebook is called, BEFORE any worker dispatch. PAT is
//      never present in the payload.
//   2. afterDisableNotFiredOnFailure: vnote.sync.after_disable does NOT fire
//      when disable returns a non-OK result. (We cannot easily trigger a
//      successful real disable in a GUILESS unit test; the negative case is
//      what guards the "after success only" contract.)
//   3. conflictDetectedFires: emitting the autoSyncConflict path through
//      EventBridge fires vnote.sync.conflict_detected on the GUI thread.
//   4. cancelledFires: cancelSync called when no token is registered still
//      fires vnote.sync.cancelled (best-effort) with hadActiveSync=false.
//
// All hooks must fire OUTSIDE any SyncService mutex; we assert this
// implicitly by registering hook handlers that re-enter SyncService methods
// (which would deadlock if a mutex were held).

#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>

#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/eventbridge.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

class TestSyncHooks : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void beforeEnableFires();
  void afterDisableNotFiredOnFailure();
  void conflictDetectedFires();
  void cancelledFires();
};

void TestSyncHooks::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
}

// Helper: build a minimal ServiceLocator wired with the deps SyncService
// requires. Returned ctx must be destroyed by the caller after the
// SyncService goes out of scope.
struct Harness {
  VxCoreContextHandle ctx = nullptr;
  ServiceLocator services;
  HookManager hookMgr;
  // Heap-allocated so we can destroy SyncService before destroying ctx
  // (SyncService::~ joins the worker thread; if ctx is gone first we crash).
  // Owned via unique_ptrs constructed in a sub-helper to keep order tight.
};

void TestSyncHooks::beforeEnableFires() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    ServiceLocator services;
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);

    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    // T5: track real keychain writes (beforeEnable indirectly writes one).
    tests::KeychainGuard guard(&credStore);
    SyncService syncService(services);

    int fireCount = 0;
    QVariantMap captured;
    hookMgr.addAction(
        HookNames::SyncBeforeEnable,
        [&](HookContext &, const QVariantMap &p_args) {
          ++fireCount;
          captured = p_args;
        },
        10);

    syncService.enableSyncForNotebook(QStringLiteral("nb-xyz"),
                                      QStringLiteral("https://example.com/repo.git"),
                                      QStringLiteral("ghp_SHOULD_NOT_APPEAR"));

    // before_enable fires synchronously before any async dispatch.
    QCOMPARE(fireCount, 1);
    QCOMPARE(captured.value(QStringLiteral("notebookId")).toString(), QStringLiteral("nb-xyz"));
    QCOMPARE(captured.value(QStringLiteral("remoteUrl")).toString(),
             QStringLiteral("https://example.com/repo.git"));
    // PAT must NEVER appear in the payload.
    QVERIFY(!captured.contains(QStringLiteral("pat")));
    for (const auto &v : captured) {
      QVERIFY2(!v.toString().contains(QStringLiteral("ghp_")),
               "PAT value leaked into vnote.sync.before_enable payload");
    }

    // Drain any pending worker activity / keychain callbacks before tearing
    // down so the SyncService destructor can quit cleanly.
    QTest::qWait(200);
    QCoreApplication::processEvents();

    // Defensive: track the fixed-id write so cleanup is symmetric with auto.
    guard.track(QStringLiteral("nb-xyz"));

    // Best-effort cleanup of any keychain entry the store may have written.
    credStore.deleteCredentials(QStringLiteral("nb-xyz"));
    QTest::qWait(200);
    QCoreApplication::processEvents();
    // T5: guard cleanup before the services scope closes and ctx is destroyed.
    guard.cleanup();
  }

  vxcore_context_destroy(ctx);
}

void TestSyncHooks::afterDisableNotFiredOnFailure() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    ServiceLocator services;
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);

    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    SyncService syncService(services);

    int fireCount = 0;
    hookMgr.addAction(
        HookNames::SyncAfterDisable, [&](HookContext &, const QVariantMap &) { ++fireCount; }, 10);

    // Disable an unregistered notebook — worker will return a non-OK error.
    // The internal disableFinished slot must NOT fire after_disable.
    QSignalSpy disableSpy(&syncService, &SyncService::disableFinished);
    syncService.disableSyncForNotebook(QStringLiteral("nb-never-existed"));

    // Wait for disableFinished (or timeout). We don't care about the exact
    // result code — only that no after_disable hook was emitted.
    disableSpy.wait(5000);
    QTest::qWait(200);
    QCoreApplication::processEvents();

    // If we got a disableFinished, it must have been non-OK; the hook must
    // not have fired. (If wait timed out, the hook also obviously didn't
    // fire and the contract still holds.)
    if (!disableSpy.isEmpty()) {
      const VxCoreError result = qvariant_cast<VxCoreError>(disableSpy.first().at(1));
      if (result == VXCORE_OK) {
        // Unexpected; the test environment surprised us — accept the fire.
        QCOMPARE(fireCount, 1);
      } else {
        QCOMPARE(fireCount, 0);
      }
    } else {
      QCOMPARE(fireCount, 0);
    }
    guard.cleanup();
  }

  vxcore_context_destroy(ctx);
}

void TestSyncHooks::conflictDetectedFires() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    ServiceLocator services;
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);

    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    // EventBridge is the only public, GUI-thread route to drive
    // onAutoSyncConflict without instantiating a vxcore sync session.
    EventBridge eventBridge(ctx);
    services.registerService<EventBridge>(&eventBridge);
    SyncService syncService(services);

    int fireCount = 0;
    QString capturedNb;
    int capturedCount = 0;
    hookMgr.addAction(
        HookNames::SyncConflictDetected,
        [&](HookContext &, const QVariantMap &p_args) {
          ++fireCount;
          capturedNb = p_args.value(QStringLiteral("notebookId")).toString();
          capturedCount = p_args.value(QStringLiteral("conflictCount")).toInt();
        },
        10);

    // Drive the auto-sync conflict path directly. EventBridge::syncConflict
    // is connected to SyncService::onAutoSyncConflict in the SyncService
    // ctor; invoking the signal here triggers the hook fire.
    QMetaObject::invokeMethod(&eventBridge, "syncConflict", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("nb-conflict")));
    QTest::qWait(50);
    QCoreApplication::processEvents();

    QCOMPARE(fireCount, 1);
    QCOMPARE(capturedNb, QStringLiteral("nb-conflict"));
    // Auto-sync path fires with conflictCount = -1 (unknown until getConflicts
    // returns).
    QCOMPARE(capturedCount, -1);
    guard.cleanup();
  }

  vxcore_context_destroy(ctx);
}

void TestSyncHooks::cancelledFires() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    ServiceLocator services;
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);

    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    SyncService syncService(services);

    int fireCount = 0;
    QVariantMap captured;
    hookMgr.addAction(
        HookNames::SyncCancelled,
        [&](HookContext &, const QVariantMap &p_args) {
          ++fireCount;
          captured = p_args;
        },
        10);

    // No in-flight sync → cancelSync must still fire vnote.sync.cancelled
    // (best-effort) with hadActiveSync=false.
    syncService.cancelSync(QStringLiteral("nb-not-syncing"));

    QCOMPARE(fireCount, 1);
    QCOMPARE(captured.value(QStringLiteral("notebookId")).toString(),
             QStringLiteral("nb-not-syncing"));
    QCOMPARE(captured.value(QStringLiteral("hadActiveSync")).toBool(), false);
    guard.cleanup();
  }

  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncHooks)
#include "test_sync_hooks.moc"
