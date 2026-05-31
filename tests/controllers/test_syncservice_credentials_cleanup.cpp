// Cleanup-behavior tests for the three keychain-PAT deletion paths (T1/T2/T3
// of the fix-qtkeychain-win32-error-8 plan):
//   T1: NewNotebookController::bootstrapSync failure branch calls
//       SyncCredentialsStore::deleteCredentials BEFORE closing the notebook.
//   T2: SyncService subscribes to NotebookAfterClose and calls
//       deleteCredentials when any notebook is closed/removed.
//   T3: SyncService::disableSyncForNotebook success branch calls
//       deleteCredentials; failure branch preserves the PAT for retry.
//
// Uses a FakeSyncCredentialsStore subclass (enabled by the public-virtual
// test seam added to SyncCredentialsStore) to record deleteCredentials /
// storeCredentials invocations WITHOUT touching the real Windows Credential
// Manager / keychain.
//
// Per ADR-1: never include sync/sync_manager.h.

#include <QtTest>

#include <QCoreApplication>
#include <QJsonObject>
#include <QMetaObject>
#include <QSignalSpy>
#include <QStringList>

#include <controllers/newnotebookcontroller.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/eventbridge.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

namespace {

// Records deleteCredentials / storeCredentials calls instead of routing them
// to QtKeychain. Inherits the parent's Q_OBJECT meta-object (no Q_OBJECT
// macro here — would cause moc duplicate-meta issues and is unnecessary since
// the parent already provides the meta-object).
class FakeSyncCredentialsStore : public SyncCredentialsStore {
public:
  using SyncCredentialsStore::SyncCredentialsStore;

  QStringList deleteCalls;
  QStringList storeCalls;

  void deleteCredentials(const QString &p_notebookId) override {
    deleteCalls.append(p_notebookId);
    // Mirror the async semantic of the real store: emit credentialsDeleted
    // on the next event-loop tick so any observers that bridge through the
    // signal see the same ordering as production.
    QMetaObject::invokeMethod(
        this, [this, p_notebookId]() { emit credentialsDeleted(p_notebookId); },
        Qt::QueuedConnection);
  }

  void storeCredentials(const QString &p_notebookId, const QString & /*p_pat*/) override {
    storeCalls.append(p_notebookId);
    QMetaObject::invokeMethod(
        this, [this, p_notebookId]() { emit credentialsStored(p_notebookId); },
        Qt::QueuedConnection);
  }
};

// Exposes test-only helpers to synthesize the enableFinished / disableFinished
// signals that the cleanup paths react to.
class TestableSyncService : public SyncService {
public:
  using SyncService::SyncService;

  void emitEnableFinishedForTest(const QString &p_notebookId, VxCoreError p_result,
                                 const QString &p_message) {
    emit enableFinished(p_notebookId, p_result, p_message);
  }

  void emitDisableFinishedForTest(const QString &p_notebookId, VxCoreError p_result) {
    emit disableFinished(p_notebookId, p_result);
  }
};

void pumpEvents(int iterations = 5) {
  for (int i = 0; i < iterations; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
}

} // namespace

class TestSyncServiceCredentialsCleanup : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testBootstrapSyncRollback_CallsDeleteCredentials();
  void testNotebookDeletion_CallsDeleteCredentials();
  void testSyncDisableSuccess_CallsDeleteCredentials();
  void testSyncDisableFailure_DoesNotCallDeleteCredentials();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestSyncServiceCredentialsCleanup::initTestCase() {
  // CRITICAL: enable test mode BEFORE vxcore_context_create so tests do not
  // touch real user AppData.
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);
}

void TestSyncServiceCredentialsCleanup::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSyncServiceCredentialsCleanup::testBootstrapSyncRollback_CallsDeleteCredentials() {
  ServiceLocator services;

  NotebookCoreService notebookSvc(m_context);
  services.registerService<NotebookCoreService>(&notebookSvc);

  FakeSyncCredentialsStore fakeStore(services);
  services.registerService<SyncCredentialsStore>(&fakeStore);

  TestableSyncService syncSvc(services);
  services.registerService<SyncService>(&syncSvc);

  NewNotebookController controller(services);
  QSignalSpy failedSpy(&controller, &NewNotebookController::bootstrapFailed);

  const QString notebookId = QStringLiteral("nb-bootstrap-rollback");
  controller.bootstrapSync(notebookId, QStringLiteral("https://example.invalid/repo.git"),
                           QStringLiteral("fake-pat"), /*p_dialogParent=*/nullptr);

  // Let enableSyncForNotebook wire its one-shot enableFinished listener.
  pumpEvents();

  // Synthesize the failure outcome that the bootstrap rollback branch reacts to.
  syncSvc.emitEnableFinishedForTest(notebookId, VXCORE_ERR_UNKNOWN,
                                    QStringLiteral("injected enable failure"));

  // Wait briefly for the bootstrapFailed signal so we know the rollback ran.
  if (failedSpy.isEmpty()) {
    failedSpy.wait(1000);
  }
  pumpEvents();

  QVERIFY2(fakeStore.deleteCalls.contains(notebookId),
           qPrintable(QStringLiteral("Expected deleteCredentials to be called for %1 "
                                     "during bootstrap rollback; deleteCalls=[%2]")
                          .arg(notebookId, fakeStore.deleteCalls.join(QLatin1Char(',')))));

  syncSvc.shutdown();
}

void TestSyncServiceCredentialsCleanup::testNotebookDeletion_CallsDeleteCredentials() {
  ServiceLocator services;

  HookManager hookMgr;
  services.registerService<HookManager>(&hookMgr);

  NotebookCoreService notebookSvc(m_context);
  notebookSvc.setHookManager(&hookMgr);
  services.registerService<NotebookCoreService>(&notebookSvc);

  FakeSyncCredentialsStore fakeStore(services);
  services.registerService<SyncCredentialsStore>(&fakeStore);

  // SyncService's constructor subscribes to NotebookAfterClose; that subscription
  // is the production cleanup path we are validating here.
  TestableSyncService syncSvc(services);
  services.registerService<SyncService>(&syncSvc);

  const QString notebookId = QStringLiteral("nb-deleted");

  // Fire the NotebookAfterClose hook directly so the test does not depend on
  // a real vxcore notebook being created (vxcore_notebook_create requires a
  // valid on-disk root, which is orthogonal to the cleanup contract under test).
  NotebookCloseEvent event;
  event.notebookId = notebookId;
  hookMgr.doAction(HookNames::NotebookAfterClose, event);

  pumpEvents();

  QVERIFY2(fakeStore.deleteCalls.contains(notebookId),
           qPrintable(QStringLiteral("Expected deleteCredentials to be called for %1 "
                                     "when NotebookAfterClose fires; deleteCalls=[%2]")
                          .arg(notebookId, fakeStore.deleteCalls.join(QLatin1Char(',')))));

  syncSvc.shutdown();
}

void TestSyncServiceCredentialsCleanup::testSyncDisableSuccess_CallsDeleteCredentials() {
  ServiceLocator services;

  NotebookCoreService notebookSvc(m_context);
  services.registerService<NotebookCoreService>(&notebookSvc);

  FakeSyncCredentialsStore fakeStore(services);
  services.registerService<SyncCredentialsStore>(&fakeStore);

  TestableSyncService syncSvc(services);
  services.registerService<SyncService>(&syncSvc);

  const QString notebookId = QStringLiteral("nb-disable-success");

  // Drive disableSyncForNotebook so its one-shot disableFinished listener is
  // attached; the real worker may also run against a non-registered notebook
  // but its later emit hits a disconnected lambda (one-shot self-disconnect).
  syncSvc.disableSyncForNotebook(notebookId);

  // Emit the success outcome BEFORE the worker can bounce back so the cleanup
  // branch runs deterministically with VXCORE_OK.
  syncSvc.emitDisableFinishedForTest(notebookId, VXCORE_OK);

  pumpEvents();

  QVERIFY2(fakeStore.deleteCalls.contains(notebookId),
           qPrintable(QStringLiteral("Expected deleteCredentials to be called for %1 "
                                     "on disable success; deleteCalls=[%2]")
                          .arg(notebookId, fakeStore.deleteCalls.join(QLatin1Char(',')))));

  syncSvc.shutdown();
}

void TestSyncServiceCredentialsCleanup::testSyncDisableFailure_DoesNotCallDeleteCredentials() {
  ServiceLocator services;

  NotebookCoreService notebookSvc(m_context);
  services.registerService<NotebookCoreService>(&notebookSvc);

  FakeSyncCredentialsStore fakeStore(services);
  services.registerService<SyncCredentialsStore>(&fakeStore);

  TestableSyncService syncSvc(services);
  services.registerService<SyncService>(&syncSvc);

  const QString notebookId = QStringLiteral("nb-disable-failure");

  syncSvc.disableSyncForNotebook(notebookId);
  syncSvc.emitDisableFinishedForTest(notebookId, VXCORE_ERR_UNKNOWN);

  pumpEvents();

  QVERIFY2(!fakeStore.deleteCalls.contains(notebookId),
           qPrintable(QStringLiteral("deleteCredentials must NOT be called for %1 on "
                                     "disable failure (PAT preserved for retry); "
                                     "deleteCalls=[%2]")
                          .arg(notebookId, fakeStore.deleteCalls.join(QLatin1Char(',')))));

  syncSvc.shutdown();
}

} // namespace tests

QTEST_MAIN(tests::TestSyncServiceCredentialsCleanup)
#include "test_syncservice_credentials_cleanup.moc"
