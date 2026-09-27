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
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaObject>
#include <QSignalSpy>
#include <QStringList>
#include <QUrl>
#include <temp_dir_fixture.h>

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

  void storeCredentials(const QString &p_notebookId, const SyncCredential &) override {
    storeCalls.append(p_notebookId);
    QMetaObject::invokeMethod(
        this, [this, p_notebookId]() { emit credentialsStored(p_notebookId); },
        Qt::QueuedConnection);
  }
};

// Simulates the issue #2718 race: on storeCredentials the store emits a stray
// generic credentialsError (as a concurrent NotebookAfterClose delete on the
// same id would on macOS) WITHOUT emitting credentialsStored. The enable flow
// must ignore the generic credentialsError (it now filters on the dedicated
// credentialsStoreError), so it must NOT abort with VXCORE_ERR_UNKNOWN.
class StrayErrorCredentialsStore : public SyncCredentialsStore {
public:
  using SyncCredentialsStore::SyncCredentialsStore;

  void storeCredentials(const QString &p_notebookId, const SyncCredential &) override {
    QMetaObject::invokeMethod(
        this,
        [this, p_notebookId]() {
          emit credentialsError(
              p_notebookId,
              QStringLiteral("Could not remove private key from keystore: not found"));
        },
        Qt::QueuedConnection);
  }
};

// Inject a worker failure without making the public completion signal an input.
class TestableSyncService : public SyncService {
public:
  using SyncService::SyncService;

  void emitDisableFinishedForTest(const QString &p_notebookId, VxCoreError p_result) {
    QMetaObject::invokeMethod(this, "onWorkerDisableFinished", Qt::DirectConnection,
                              Q_ARG(QString, p_notebookId), Q_ARG(VxCoreError, p_result));
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
  void testStrayCredentialsError_DoesNotAbortEnable();

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

  TempDirFixture temporary;
  QVERIFY(temporary.isValid());
  NewNotebookController controller(services);
  NewNotebookInput input;
  input.name = QStringLiteral("Bootstrap rollback");
  input.rootFolderPath = temporary.filePath(QStringLiteral("owned-notebook"));
  const auto created = controller.createNotebook(input);
  QVERIFY(created.success);
  const auto notebookId = created.notebookId;
  QSignalSpy failedSpy(&controller, &NewNotebookController::bootstrapFailed);
  controller.bootstrapSync(
      notebookId,
      {QStringLiteral("git"),
       QUrl::fromLocalFile(temporary.filePath(QStringLiteral("missing-remote.git"))).toString(),
       {QStringLiteral("git"), {}, QStringLiteral("fake-pat")}},
      nullptr);
  QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 15000);
  QVERIFY(fakeStore.deleteCalls.contains(notebookId));
  QVERIFY(!QFileInfo::exists(input.rootFolderPath));
  for (const auto &notebook : notebookSvc.listNotebooks()) {
    QVERIFY(notebook.toObject().value(QStringLiteral("id")).toString() != notebookId);
  }

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

  TempDirFixture directory;
  QVERIFY(directory.isValid());
  const auto notebookId = notebookSvc.createNotebook(
      directory.filePath(QStringLiteral("notebook")),
      QStringLiteral("{\"name\":\"Disable\",\"syncEnabled\":true,\"syncBackend\":\"git\","
                     "\"syncRemoteUrl\":\"https://example.test/notes.git\"}"),
      NotebookType::Bundled);
  QVERIFY(!notebookId.isEmpty());
  QSignalSpy finished(&syncSvc, &SyncService::disableFinished);
  syncSvc.disableSyncForNotebook(notebookId);
  QTRY_COMPARE(finished.count(), 1);
  QCOMPARE(qvariant_cast<VxCoreError>(finished.first().at(1)), VXCORE_OK);
  QVERIFY(!notebookSvc.getNotebookConfig(notebookId).value(QStringLiteral("syncEnabled")).toBool());

  QVERIFY2(fakeStore.deleteCalls.contains(notebookId),
           qPrintable(QStringLiteral("Expected deleteCredentials to be called for %1 "
                                     "on disable success; deleteCalls=[%2]")
                          .arg(notebookId, fakeStore.deleteCalls.join(QLatin1Char(',')))));

  syncSvc.shutdown();
  notebookSvc.closeNotebook(notebookId);
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

  syncSvc.emitDisableFinishedForTest(notebookId, VXCORE_ERR_UNKNOWN);

  pumpEvents();

  QVERIFY2(!fakeStore.deleteCalls.contains(notebookId),
           qPrintable(QStringLiteral("deleteCredentials must NOT be called for %1 on "
                                     "disable failure (PAT preserved for retry); "
                                     "deleteCalls=[%2]")
                          .arg(notebookId, fakeStore.deleteCalls.join(QLatin1Char(',')))));

  syncSvc.shutdown();
}

void TestSyncServiceCredentialsCleanup::testStrayCredentialsError_DoesNotAbortEnable() {
  ServiceLocator services;

  NotebookCoreService notebookSvc(m_context);
  services.registerService<NotebookCoreService>(&notebookSvc);

  StrayErrorCredentialsStore strayStore(services);
  services.registerService<SyncCredentialsStore>(&strayStore);

  TestableSyncService syncSvc(services);
  services.registerService<SyncService>(&syncSvc);

  const QString notebookId = QStringLiteral("nb-stray-error");

  QSignalSpy enableSpy(&syncSvc, &SyncService::enableFinished);

  syncSvc.enableSyncForNotebook(notebookId,
                                {QStringLiteral("git"),
                                 QStringLiteral("https://example.invalid/repo.git"),
                                 {QStringLiteral("git"), {}, QStringLiteral("fake-pat")}});

  // Let the store emit its stray generic credentialsError.
  pumpEvents();

  // The enable flow filters on credentialsStoreError, so the stray generic
  // credentialsError must NOT drive an enable failure. Since credentialsStored
  // was never emitted either, the enable simply stays pending and no
  // enableFinished(VXCORE_ERR_UNKNOWN, <stray msg>) is produced.
  for (int i = 0; i < enableSpy.count(); ++i) {
    const auto args = enableSpy.at(i);
    const auto code = static_cast<VxCoreError>(args.at(1).toInt());
    const QString msg = args.at(2).toString();
    QVERIFY2(
        !(code == VXCORE_ERR_UNKNOWN && msg.contains(QStringLiteral("not found"))),
        qPrintable(QStringLiteral("Stray credentialsError wrongly aborted enable: %1").arg(msg)));
  }

  syncSvc.shutdown();
}

} // namespace tests

QTEST_MAIN(tests::TestSyncServiceCredentialsCleanup)
#include "test_syncservice_credentials_cleanup.moc"
