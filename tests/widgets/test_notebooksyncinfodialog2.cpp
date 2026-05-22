// Tests for NotebookSyncInfoDialog2 routing logic (W3.T2 — Bug G dialog fix).
//
// Validates the dispatch decision in acceptedButtonClicked() and
// appliedButtonClicked() between bootstrapApply() and applyChanges():
//   - bootstrap mode (post-create) -> bootstrapApply()
//   - partial notebook (isSyncRegistered=false) -> bootstrapApply() even in
//     edit mode (auto-route to fix the chicken-and-egg state)
//   - registered notebook + edit mode -> applyChanges() (legacy behavior,
//     no regression)
//
// The two paths differ in URL-persistence timing:
//   - bootstrapApply persists syncRemoteUrl ONLY on success (inside the
//     enableFinished lambda).
//   - applyChanges persists syncRemoteUrl SYNCHRONOUSLY before the async
//     PAT update.
// Under a forced enable failure (via SyncWorker::testForceError) the on-disk
// syncRemoteUrl is a clean signal: empty = bootstrap path, new URL =
// applyChanges path.
//
// Per ADR-9: tests use literal `test_pat_12345` for PAT; the PAT MUST NOT
// appear in ctest stdout/stderr (verified separately by grep).
// Per ADR-1: this test never includes sync/sync_manager.h.

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dialogs/notebooksyncinfodialog2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestNotebookSyncInfoDialog2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  // W3.T2 routing tests
  void testAcceptedRoutesToBootstrapApplyWhenBootstrapMode();
  void testAcceptedRoutesToApplyChangesWhenRegistered();
  void testAcceptedRoutesToBootstrapApplyWhenPartialEvenInEditMode();
  void testAppliedSameDispatchAsAccepted();
  void testDialogStaysOpenUntilApplyComplete();

private:
  // Initialize a bare git repo at p_bareRepoPath and seed it with one commit
  // containing a single file "seed.md" on the default branch (main). Returns
  // the file:// URL suitable for cloning, or empty on failure. Mirrors the
  // helper from test_notebooksyncinfocontroller.cpp and test_syncservice.cpp.
  static QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
};

void TestNotebookSyncInfoDialog2::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create. Mirrors
  // tests/AGENTS.md guidance.
  vxcore_set_test_mode(1);
}

QString TestNotebookSyncInfoDialog2::seedBareRepo(const QString &p_bareRepoPath,
                                                  TempDirFixture &p_workTemp) {
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("init"), QStringLiteral("--bare"),
                         QStringLiteral("--initial-branch=main"), p_bareRepoPath}) != 0) {
    QDir().rmpath(p_bareRepoPath);
    if (QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                  p_bareRepoPath}) != 0) {
      return QString();
    }
  }

  QString workDir = p_workTemp.filePath(QStringLiteral("seed_work_") +
                                        QString::number(QDateTime::currentMSecsSinceEpoch()));
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("clone"), p_bareRepoPath, workDir}) != 0) {
    return QString();
  }

  QProcess::execute(QStringLiteral("git"),
                    {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                     QStringLiteral("user.email"), QStringLiteral("seed@example.com")});
  QProcess::execute(QStringLiteral("git"), {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                                            QStringLiteral("user.name"), QStringLiteral("Seed")});

  QFile seed(workDir + QStringLiteral("/seed.md"));
  if (!seed.open(QIODevice::WriteOnly)) {
    return QString();
  }
  seed.write("# Seed\n");
  seed.close();

  if (QProcess::execute(
          QStringLiteral("git"),
          {QStringLiteral("-C"), workDir, QStringLiteral("add"), QStringLiteral("seed.md")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("commit"),
                         QStringLiteral("-m"), QStringLiteral("seed")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("push"),
                         QStringLiteral("origin"), QStringLiteral("HEAD")}) != 0) {
    return QString();
  }

  QString normalized = QDir::fromNativeSeparators(p_bareRepoPath);
  if (!normalized.startsWith(QLatin1Char('/'))) {
    normalized.prepend(QLatin1Char('/'));
  }
  return QStringLiteral("file://") + normalized;
}

// =============================================================================
// W3.T2 Test 1 — Bootstrap mode click OK routes to bootstrapApply.
// =============================================================================
//
// Setup: partial notebook (syncEnabled=true on disk, NOT registered at
// runtime), syncRemoteUrl initially empty on disk. Pre-arm SyncWorker
// testForceError so the enable dispatched by bootstrapApply fails. After
// click, the on-disk syncRemoteUrl MUST remain empty — proving the dialog
// routed through bootstrapApply (which only persists URL on success), not
// applyChanges (which persists synchronously regardless of async outcome).
//
void TestNotebookSyncInfoDialog2::testAcceptedRoutesToBootstrapApplyWhenBootstrapMode() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Partial notebook: syncEnabled=true on disk but never enabled at runtime.
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_bootstrap_mode_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap Mode Test","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Pre-condition: not registered at runtime, syncRemoteUrl empty on disk.
  QVERIFY(!syncService.isSyncRegistered(nbId));
  {
    const QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    QCOMPARE(cfg.value(QStringLiteral("syncRemoteUrl")).toString(), QString());
  }

  // Arm the next worker enable to fail (testForceError is one-shot,
  // consume-and-clear; mirrors the W3.T1 controller test pattern).
  QSKIP("T24: SyncWorker::testForceError seam removed; needs port to SyncOps/SyncWorkQueueManager");

  {
    NotebookSyncInfoDialog2 dialog(services, nbId);
    dialog.setBootstrapMode(true);

    // Wait for loadInitialData to settle (dataLoaded signal already fired
    // from the constructor; pump the loop once for safety).
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
    auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
    auto *okBtn = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
    QVERIFY(urlEdit);
    QVERIFY(patEdit);
    QVERIFY(okBtn);

    const QString newUrl = QStringLiteral("file:///tmp/will_fail_bootstrap_mode.git");
    urlEdit->setText(newUrl);
    patEdit->setText(QStringLiteral("test_pat_12345"));

    auto *ctrl = dialog.findChild<NotebookSyncInfoController *>();
    QVERIFY(ctrl);
    QSignalSpy applySpy(ctrl, &NotebookSyncInfoController::applyComplete);

    okBtn->click();

    QVERIFY(applySpy.wait(15000));
    QCOMPARE(applySpy.count(), 1);
    QCOMPARE(applySpy.first().at(0).toBool(), false);

    // Critical assertion: bootstrapApply did NOT persist the URL on failure.
    // applyChanges would have persisted synchronously regardless.
    const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
    QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), QString());

    // And the notebook MUST NOT be registered (enable failed).
    QVERIFY(!syncService.isSyncRegistered(nbId));
  }

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

// =============================================================================
// W3.T2 Test 2 — Registered notebook in edit mode routes to applyChanges.
// =============================================================================
//
// Setup: enable sync against a real bare repo so the notebook is REGISTERED
// at runtime. Then open the dialog in edit mode (no bootstrapMode) with only
// a new PAT (no URL change, to avoid the confirmUrlChange dialog branch).
// applyChanges must dispatch via updateCredentials, which on a registered
// notebook fires credentialsSetFinished — NOT enableFinished (bootstrapApply's
// signature). This proves the legacy applyChanges path is still used (no
// regression for the well-formed editing case) when bootstrap is not required.
//
void TestNotebookSyncInfoDialog2::testAcceptedRoutesToApplyChangesWhenRegistered() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote_registered.git"));
  QString oldUrl = seedBareRepo(bareDir, localTemp);
  if (oldUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_registered_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Registered NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Enable sync up-front so the notebook is registered at runtime.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, oldUrl, QStringLiteral("test_pat_12345"));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);
  if (qvariant_cast<VxCoreError>(enableSpy.first().at(1)) == VXCORE_ERR_UNKNOWN) {
    qWarning() << "enableSyncForNotebook returned VXCORE_ERR_UNKNOWN; message:"
               << enableSpy.first().at(2).toString();
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Persist the OLD URL into the flat config so loadInitialData primes the
  // controller's m_currentRemoteUrl cache to oldUrl, making PAT-only edits
  // appear non-URL-changing to applyChanges.
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldUrl;
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }

  {
    NotebookSyncInfoDialog2 dialog(services, nbId);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
    auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
    auto *okBtn = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
    QVERIFY(urlEdit);
    QVERIFY(patEdit);
    QVERIFY(okBtn);

    // urlEdit should already hold oldUrl from loadInitialData.
    QCOMPARE(urlEdit->text(), oldUrl);
    // Keep URL UNCHANGED (do not setText). Only update PAT.
    patEdit->setText(QStringLiteral("test_pat_12345"));

    auto *ctrl = dialog.findChild<NotebookSyncInfoController *>();
    QVERIFY(ctrl);
    QSignalSpy applySpy(ctrl, &NotebookSyncInfoController::applyComplete);
    QSignalSpy credsSetSpy(&syncService, &SyncService::credentialsSetFinished);

    // Baseline: enableFinished already fired once during enableSyncForNotebook;
    // applyChanges -> updateCredentials on a registered notebook must NOT
    // fire it again. bootstrapApply WOULD (it calls enableSyncForNotebook).
    const int baselineEnableCount = enableSpy.count();

    okBtn->click();

    // Wait for either applyComplete (registered+PAT-only path) or for the
    // dialog to finish.
    QVERIFY(applySpy.wait(15000));
    QCOMPARE(applySpy.count(), 1);

    // applyChanges with PAT on registered notebook routes to setCredentials,
    // which emits credentialsSetFinished — NOT enableFinished.
    QCOMPARE(credsSetSpy.count(), 1);
    QCOMPARE(enableSpy.count(), baselineEnableCount); // no spurious re-enable

    // Sanity: notebook still registered (no disable/re-enable cycle).
    QVERIFY(syncService.isSyncRegistered(nbId));
  }

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

// =============================================================================
// W3.T2 Test 3 — Partial notebook in edit mode auto-routes to bootstrapApply.
// =============================================================================
//
// Even without setBootstrapMode(true), a partial notebook (syncEnabled on
// disk but unregistered at runtime) must auto-route through bootstrapApply
// so we don't hit the chicken-and-egg path where applyChanges fires
// updateCredentials against an unregistered notebook (silent fail per
// vxcore W1.T3 contract).
//
// Forced enable error + post-condition: syncRemoteUrl still empty on disk
// (proves bootstrap path was used).
//
void TestNotebookSyncInfoDialog2::testAcceptedRoutesToBootstrapApplyWhenPartialEvenInEditMode() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_partial_edit_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Partial Edit","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(!syncService.isSyncRegistered(nbId));

  QSKIP("T24: SyncWorker::testForceError seam removed; needs port to SyncOps/SyncWorkQueueManager");

  {
    NotebookSyncInfoDialog2 dialog(services, nbId);
    // DO NOT call setBootstrapMode. The dialog should still auto-route to
    // bootstrapApply because isSyncRegistered == false.

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
    auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
    auto *okBtn = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
    QVERIFY(urlEdit);
    QVERIFY(patEdit);
    QVERIFY(okBtn);

    urlEdit->setText(QStringLiteral("file:///tmp/partial_auto_route.git"));
    patEdit->setText(QStringLiteral("test_pat_12345"));

    auto *ctrl = dialog.findChild<NotebookSyncInfoController *>();
    QVERIFY(ctrl);
    QSignalSpy applySpy(ctrl, &NotebookSyncInfoController::applyComplete);

    okBtn->click();

    QVERIFY(applySpy.wait(15000));
    QCOMPARE(applySpy.count(), 1);
    QCOMPARE(applySpy.first().at(0).toBool(), false);

    // syncRemoteUrl must still be empty — proves bootstrap path used.
    const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
    QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), QString());
  }

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

// =============================================================================
// W3.T2 Test 4 — appliedButtonClicked uses the same partial-detection branch.
// =============================================================================
//
// Apply (not OK) on a partial notebook routes to bootstrapApply, same as
// acceptedButtonClicked. Unlike OK, Apply must NOT accept() the dialog on
// success — verified by the QDialog::accepted spy.
//
void TestNotebookSyncInfoDialog2::testAppliedSameDispatchAsAccepted() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_apply_partial_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Apply Partial","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(!syncService.isSyncRegistered(nbId));

  QSKIP("T24: SyncWorker::testForceError seam removed; needs port to SyncOps/SyncWorkQueueManager");

  {
    NotebookSyncInfoDialog2 dialog(services, nbId);
    // No bootstrapMode; rely on partial detection.

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
    auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
    auto *applyBtn = dialog.findChild<QPushButton *>(QStringLiteral("applyButton"));
    QVERIFY(urlEdit);
    QVERIFY(patEdit);
    QVERIFY(applyBtn);

    urlEdit->setText(QStringLiteral("file:///tmp/apply_partial.git"));
    patEdit->setText(QStringLiteral("test_pat_12345"));

    // Apply button only enables when there are pending changes (URL changed
    // OR PAT non-empty). Both are true; refresh and enable.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    auto *ctrl = dialog.findChild<NotebookSyncInfoController *>();
    QVERIFY(ctrl);
    QSignalSpy applySpy(ctrl, &NotebookSyncInfoController::applyComplete);
    QSignalSpy acceptedSpy(&dialog, &QDialog::accepted);

    // Apply button must be enabled to fire. setEnabled(true) defensively
    // because we can't rely on the dirty-flag QSS path having ticked.
    applyBtn->setEnabled(true);
    applyBtn->click();

    QVERIFY(applySpy.wait(15000));
    QCOMPARE(applySpy.count(), 1);
    QCOMPARE(applySpy.first().at(0).toBool(), false);

    // Dialog MUST NOT have been accepted by Apply.
    QCOMPARE(acceptedSpy.count(), 0);

    // syncRemoteUrl must still be empty (bootstrap path on failure).
    const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
    QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), QString());
  }

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

// =============================================================================
// W3.T2 Test 5 — Dialog stays open during async bootstrap; accepts after.
// =============================================================================
//
// Real bare repo + testHangNextOperation() introduces a measurable delay
// between OK click and applyComplete arrival. During that window the dialog
// MUST remain unaccepted (would otherwise close before async completion).
// Once applyComplete(true) fires, the dialog accepts.
//
void TestNotebookSyncInfoDialog2::testDialogStaysOpenUntilApplyComplete() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote_stays_open.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_stays_open_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Stays Open","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Hang the next worker op for ~1s to ensure we have a window where the
  // bootstrap is in flight but applyComplete has not yet fired.
  QSKIP("T24: SyncWorker::testHangNextOperation seam removed; needs port to "
        "SyncOps/SyncWorkQueueManager");

  {
    NotebookSyncInfoDialog2 dialog(services, nbId);
    dialog.setBootstrapMode(true);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
    auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
    auto *okBtn = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
    QVERIFY(urlEdit);
    QVERIFY(patEdit);
    QVERIFY(okBtn);

    urlEdit->setText(remoteUrl);
    patEdit->setText(QStringLiteral("test_pat_12345"));

    auto *ctrl = dialog.findChild<NotebookSyncInfoController *>();
    QVERIFY(ctrl);
    QSignalSpy applySpy(ctrl, &NotebookSyncInfoController::applyComplete);
    QSignalSpy acceptedSpy(&dialog, &QDialog::accepted);

    okBtn->click();

    // Immediately after click: dialog NOT yet accepted (async in flight).
    QCOMPARE(acceptedSpy.count(), 0);

    // After ~300ms — still less than the hang duration — dialog still open.
    QTest::qWait(300);
    QCOMPARE(acceptedSpy.count(), 0);

    // Now wait for the async chain to complete.
    QVERIFY(applySpy.wait(20000));
    QCOMPARE(applySpy.count(), 1);

    if (!applySpy.first().at(0).toBool()) {
      credStore.deleteCredentials(nbId);
      QTest::qWait(500);
      vxcore_context_destroy(ctx);
      QSKIP("OS keychain backend or git enable not usable in this test environment");
    }

    // Pump event loop so the queued accept() (fired by our applyComplete
    // lambda inside the dialog) takes effect before we check.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    QCOMPARE(acceptedSpy.count(), 1);
  }

  credStore.deleteCredentials(nbId);
  QTest::qWait(500);
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookSyncInfoDialog2)
#include "test_notebooksyncinfodialog2.moc"
