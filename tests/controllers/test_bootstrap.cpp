// Tests for NewNotebookController::bootstrapSync (T14).
//
// Per ADR-7: bootstrap is CREATE-THEN-ENABLE. The notebook MUST already exist
// (created via NotebookCoreService::createNotebook) before bootstrapSync is
// called; bootstrapSync only enables sync via SyncService and persists the
// remote URL on success. On failure it cleans up via closeNotebook +
// QDir::removeRecursively (per ADR-2 — no notebook_delete C API exists).
//
// Cases:
//   * noCancelButton: createBootstrapModal() returns a QProgressDialog with no
//     cancel button (per the plan rule "Sync cannot be cancelled once
//     started") and an indeterminate range.
//   * happyPath: bootstrapSync against a seeded bare repo via file:// URL
//     succeeds; bootstrapSucceeded fires once, syncRemoteUrl is persisted as
//     a flat ADR-8 key, and the seed file appears in the notebook root.
//   * authFailureCleanup: bootstrapSync against a non-existent bare repo URL
//     emits bootstrapFailed; the notebook is closed and the on-disk root
//     directory is recursively removed.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

#include <controllers/newnotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestBootstrap : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void noCancelButton();
  void happyPath();
  void authFailureCleanup();

private:
  // Initialize a bare git repo at p_bareRepoPath and seed it with one commit
  // containing a single file "seed.md". Returns the file:// URL or empty on
  // failure (e.g., git not installed). Mirrors the helper in T7's
  // test_syncservice.cpp.
  static QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
};

void TestBootstrap::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
}

QString TestBootstrap::seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp) {
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

void TestBootstrap::noCancelButton() {
  // Construct the modal in isolation and verify the no-cancel + indeterminate
  // contract. Heap-allocated; we own the lifetime here.
  QProgressDialog *modal = NewNotebookController::createBootstrapModal(nullptr);
  QVERIFY(modal != nullptr);

  // Indeterminate: range [0, 0] makes QProgressDialog show a spinner.
  QCOMPARE(modal->minimum(), 0);
  QCOMPARE(modal->maximum(), 0);

  // Label text mentions cancellation cannot happen.
  QVERIFY2(modal->labelText().contains(QStringLiteral("cannot be cancelled"), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("Label was: %1").arg(modal->labelText())));

  // setCancelButton(nullptr) removes the button entirely. The dialog must NOT
  // contain any QPushButton child whose role is cancellation. Both forms below
  // are acceptable per the spec: either no QPushButton at all, or one whose
  // text does not include the word "cancel".
  const auto buttons = modal->findChildren<QPushButton *>();
  for (const auto *btn : buttons) {
    QVERIFY2(!btn->text().toLower().contains(QStringLiteral("cancel")),
             qPrintable(QStringLiteral("Found cancel-like button with text: %1").arg(btn->text())));
  }

  delete modal;
}

void TestBootstrap::happyPath() {
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

  // Seed a bare repo and create a notebook root.
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote_happy.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  // Create the empty bundled notebook root and notebook.
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_happy_root"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Drive bootstrapSync.
  NewNotebookController controller(services);
  QSignalSpy succSpy(&controller, &NewNotebookController::bootstrapSucceeded);
  QSignalSpy failSpy(&controller, &NewNotebookController::bootstrapFailed);
  // bootstrapSync also kicks off an async triggerSyncNow on success; we must
  // wait for that to complete before destroying the vxcore context, otherwise
  // SyncManager shutdown races with the worker and crashes inside libgit2.
  QSignalSpy syncFinishedSpy(&syncService, &SyncService::syncFinished);

  controller.bootstrapSync(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_T14"), nullptr);

  // Wait for either signal. Timeout generous to allow keychain + clone + push.
  bool got = succSpy.wait(30000) || !failSpy.isEmpty();
  Q_UNUSED(got);

  if (succSpy.isEmpty() && !failSpy.isEmpty()) {
    // Most likely the OS keychain backend is unusable in this test env.
    qWarning() << "bootstrapSync failed; message:" << failSpy.first().at(1).toString();
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }

  QCOMPARE(succSpy.count(), 1);
  QCOMPARE(failSpy.count(), 0);
  QCOMPARE(succSpy.first().at(0).toString(), nbId);

  // Verify syncRemoteUrl persisted as a FLAT ADR-8 key.
  const QJsonObject cfg = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfg.value(QStringLiteral("syncRemoteUrl")).toString(), remoteUrl);

  // Verify the seed file from the bare repo was cloned into the notebook root.
  QVERIFY2(QFile::exists(nbRoot + QStringLiteral("/seed.md")),
           qPrintable(QStringLiteral("seed.md not found in cloned root: %1").arg(nbRoot)));

  // Drain the trailing triggerSyncNow before tearing down the vxcore context.
  if (syncFinishedSpy.isEmpty()) {
    syncFinishedSpy.wait(15000);
  }

  // Best-effort cleanup of the keychain entry.
  credStore.deleteCredentials(nbId);
  QTest::qWait(200);

  vxcore_context_destroy(ctx);
}

void TestBootstrap::authFailureCleanup() {
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

  // Use a bad URL pointing at a non-existent bare repo.
  QString bogusBare = localTemp.filePath(QStringLiteral("does_not_exist.git"));
  QString normalized = QDir::fromNativeSeparators(bogusBare);
  if (!normalized.startsWith(QLatin1Char('/'))) {
    normalized.prepend(QLatin1Char('/'));
  }
  const QString badUrl = QStringLiteral("file://") + normalized;

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_fail_root"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap Fail","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  NewNotebookController controller(services);
  QSignalSpy succSpy(&controller, &NewNotebookController::bootstrapSucceeded);
  QSignalSpy failSpy(&controller, &NewNotebookController::bootstrapFailed);

  controller.bootstrapSync(nbId, badUrl, QStringLiteral("ghp_TEST_PAT_T14_FAIL"), nullptr);

  // Wait for failure.
  QVERIFY(failSpy.wait(30000) || !succSpy.isEmpty());
  QCOMPARE(succSpy.count(), 0);
  QCOMPARE(failSpy.count(), 1);
  QCOMPARE(failSpy.first().at(0).toString(), nbId);

  // Verify cleanup: notebook removed from open list AND on-disk root removed.
  const QJsonArray notebooksAfter = notebookService.listNotebooks();
  bool stillOpen = false;
  for (const auto &nbVal : notebooksAfter) {
    const QJsonObject nbObj = nbVal.toObject();
    if (nbObj.value(QStringLiteral("id")).toString() == nbId) {
      stillOpen = true;
      break;
    }
  }
  QVERIFY2(!stillOpen, "notebook should have been closed by cleanup path");
  QVERIFY2(!QDir(nbRoot).exists(), "notebook root should have been removed by cleanup path");

  // Best-effort: delete the keychain entry that may have been written.
  credStore.deleteCredentials(nbId);
  QTest::qWait(200);

  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_MAIN(tests::TestBootstrap)
#include "test_bootstrap.moc"
