// Tests for OpenNotebookController::cloneAndOpen (T22).
//
// Per the open-notebook-remote-readonly plan, cloneAndOpen runs the clone on
// a worker thread (libgit2 fetch/checkout can take many seconds) and
// implements the staging-dir-then-rename pattern so the user's chosen final
// destination is never touched if anything fails. This test exercises the
// four key contract points enumerated in the plan:
//
//   1. testCloneNoPatWritableS2HappyPath
//      Empty PAT against a real bare-repo fixture seeded with a VNote
//      bundled-notebook layout. The clone succeeds, the notebook opens
//      WRITABLE with partial sync info persisted (sync state S2): syncEnabled
//      + git backend + remote URL on disk, but NO keychain entry and NO vxcore
//      sync registration. The final dir contains the cloned contents and the
//      classifier reports S2.
//
//   2. testValidationRejectsExistingDestDir
//      Pre-create the user-chosen destination dir. The controller's
//      pre-flight validateCloneInput rejects it BEFORE any staging dir is
//      generated, so nothing on disk changes. We also verify the existing
//      directory is preserved (we MUST NOT delete user content).
//
//   3. testNonNotebookRemoteCleansUp
//      Clone a bare repo that contains a single text file but NO
//      vx_notebook/config.json. vxcore_sync_clone validates the post-clone
//      contents and returns VXCORE_ERR_NOT_FOUND; the controller cleans up
//      the staging dir and the final dir is never created.
//
//   4. testCloneFailureRollback
//      Point at a file:// URL whose target does not exist. libgit2 fails the
//      clone outright; the controller cleans up the staging dir and emits
//      a failure cloneFinished signal. The final dir is never created.
//
// Note on "invalid PAT" coverage: file:// remotes do not authenticate, and
// network-backed PAT-checked URLs are too slow/flaky for unit tests. The
// invalid-PAT contract (hard fail, NEVER silent downgrade) is equivalent
// from the controller's perspective to "clone failed for whatever reason",
// which subtest 4 covers via the unreachable-remote path.

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QString>
#include <QtTest>

#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncstateclassifier.h>
#include <temp_dir_fixture.h>

#include "../helpers/keychain_guard.h"

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestOpenNotebookControllerClone : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testCloneNoPatWritableS2HappyPath();
  void testValidationRejectsExistingDestDir();
  void testNonNotebookRemoteCleansUp();
  void testCloneFailureRollback();

private:
  // Initialize a bare git repo at p_bareRepoPath and seed it with a VNote
  // bundled-notebook layout (vx_notebook/config.json + id + name) on main.
  // Returns the file:// URL on success or empty on failure (git missing,
  // execution error, ...) so callers can QSKIP.
  //
  // Borrowed from tests/core/test_notebook_core_service_clone.cpp::
  // seedVNoteBareRepo and duplicated here verbatim to keep the controllers
  // test target self-contained (no transitive dep on the core test target).
  QString seedVNoteBareRepo(const QString &p_bareRepoPath, const QString &p_notebookGuid,
                            const QString &p_notebookName, TempDirFixture &p_workTemp);

  // Initialize a bare git repo seeded with a single non-VNote text file so
  // vxcore_sync_clone's vx_notebook/config.json validation rejects it.
  QString seedNonVNoteBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);

  // Convert a native Windows / POSIX path into a file:// URL acceptable to
  // libgit2 (always uses forward slashes + leading slash).
  static QString toFileUrl(const QString &p_nativePath);

  // VxCoreContextHandle is the canonical typedef for the opaque vxcore
  // context pointer; see libs/vxcore/include/vxcore/vxcore_types.h:73.
  VxCoreContextHandle m_ctx = nullptr;
  ServiceLocator m_services;
  tests::KeychainGuard *m_keychainGuard = nullptr;
};

void TestOpenNotebookControllerClone::initTestCase() {
  // CRITICAL: enable test mode BEFORE vxcore_context_create so the test
  // operates in %TEMP%\vxcore_test* instead of real AppData.
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  // ServiceLocator stores non-owning pointers, so the service must outlive
  // m_services. NotebookCoreService takes (context, parent) and we want it
  // parented to this so QObject cleanup handles destruction at teardown.
  auto *nbSvc = new NotebookCoreService(m_ctx, this);
  m_services.registerService<NotebookCoreService>(nbSvc);

  // Register the sync services so the test can drive SyncStateClassifier::classify
  // against the live notebook config written by the no-PAT (S2) clone path.
  m_services.registerService<SyncCredentialsStore>(new SyncCredentialsStore(m_services));
  m_services.registerService<SyncService>(new SyncService(m_services));
  m_services.registerService<SyncStateClassifier>(new SyncStateClassifier(m_services));
  auto *credStore = m_services.get<SyncCredentialsStore>();
  m_keychainGuard = new tests::KeychainGuard(credStore, this);
}

void TestOpenNotebookControllerClone::cleanupTestCase() {
  // Close any notebooks registered during tests so vxcore can tear down
  // cleanly (matches the pattern in test_notebook_core_service_clone).
  auto *nbSvc = m_services.get<NotebookCoreService>();
  if (nbSvc) {
    const QJsonArray nbs = nbSvc->listNotebooks();
    for (const auto &val : nbs) {
      const QString id = val.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        nbSvc->closeNotebook(id);
      }
    }
  }
  if (m_keychainGuard) {
    m_keychainGuard->cleanup();
    delete m_keychainGuard;
    m_keychainGuard = nullptr;
  }
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

QString TestOpenNotebookControllerClone::toFileUrl(const QString &p_nativePath) {
  QString normalized = QDir::fromNativeSeparators(p_nativePath);
  if (!normalized.startsWith(QLatin1Char('/'))) {
    normalized.prepend(QLatin1Char('/'));
  }
  return QStringLiteral("file://") + normalized;
}

QString TestOpenNotebookControllerClone::seedVNoteBareRepo(const QString &p_bareRepoPath,
                                                           const QString &p_notebookGuid,
                                                           const QString &p_notebookName,
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

  const QString workDir = p_workTemp.filePath(QStringLiteral("seed_work_") +
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

  const QString vxDir = workDir + QStringLiteral("/vx_notebook");
  if (!QDir().mkpath(vxDir)) {
    return QString();
  }
  QFile cfg(vxDir + QStringLiteral("/config.json"));
  if (!cfg.open(QIODevice::WriteOnly)) {
    return QString();
  }
  const QString cfgJson =
      QStringLiteral(
          "{\"id\":\"%1\",\"name\":\"%2\",\"description\":\"Clone test\",\"version\":\"1\","
          "\"imageFolder\":\"_v_images\",\"attachmentFolder\":\"_v_attachments\","
          "\"recycleBinFolder\":\"_v_recycle_bin\",\"createdUtc\":1700000000000}")
          .arg(p_notebookGuid, p_notebookName);
  cfg.write(cfgJson.toUtf8());
  cfg.close();

  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("add"),
                         QStringLiteral("vx_notebook/config.json")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("commit"),
                         QStringLiteral("-m"), QStringLiteral("seed vnote notebook")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("push"),
                         QStringLiteral("origin"), QStringLiteral("HEAD")}) != 0) {
    return QString();
  }

  return toFileUrl(p_bareRepoPath);
}

QString TestOpenNotebookControllerClone::seedNonVNoteBareRepo(const QString &p_bareRepoPath,
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

  const QString workDir = p_workTemp.filePath(QStringLiteral("seed_work_nonvnote_") +
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

  QFile readme(workDir + QStringLiteral("/README.md"));
  if (!readme.open(QIODevice::WriteOnly)) {
    return QString();
  }
  readme.write("# Not a VNote notebook\n");
  readme.close();

  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("add"),
                         QStringLiteral("README.md")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("commit"),
                         QStringLiteral("-m"), QStringLiteral("seed plain repo")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("push"),
                         QStringLiteral("origin"), QStringLiteral("HEAD")}) != 0) {
    return QString();
  }

  return toFileUrl(p_bareRepoPath);
}

void TestOpenNotebookControllerClone::testCloneNoPatWritableS2HappyPath() {
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  const QString bareDir = localTemp.filePath(QStringLiteral("remote_happy.git"));
  const QString notebookGuid = QStringLiteral("clone-ctrl-test-001");
  const QString notebookName = QStringLiteral("CtrlClone NB 1");
  const QString remoteUrl = seedVNoteBareRepo(bareDir, notebookGuid, notebookName, localTemp);
  if (remoteUrl.isEmpty()) {
    QSKIP("git not available or bare-repo seeding failed");
  }

  CloneAndOpenInput input;
  input.remoteUrl = remoteUrl;
  input.pat = QString(); // no PAT -> writable partial-sync (S2), opens silently
  input.finalDestDir = localTemp.filePath(QStringLiteral("clone_dest_happy"));
  input.autoSyncEnabled = true;

  OpenNotebookController controller(m_services);
  QSignalSpy finishedSpy(&controller, &OpenNotebookController::cloneFinished);
  QVERIFY(finishedSpy.isValid());

  controller.cloneAndOpen(input);

  // cloneFinished is emitted via Qt::QueuedConnection from the worker (or
  // from this call frame for synchronous validation failures). 30s allows
  // for libgit2 fetch + checkout on slower CI hardware.
  QVERIFY2(finishedSpy.wait(30000), "cloneAndOpen must emit cloneFinished within 30s");
  QCOMPARE(finishedSpy.count(), 1);

  const CloneAndOpenResult result = finishedSpy.first().at(0).value<CloneAndOpenResult>();
  QVERIFY2(result.success,
           qPrintable(QStringLiteral("expected success; error: %1").arg(result.errorMessage)));
  QCOMPARE(result.notebookId, notebookGuid);
  QCOMPARE(result.notebookName, notebookName);
  QVERIFY2(!result.isReadOnly, "no-PAT clone must be WRITABLE (partial-sync S2), not read-only");
  QVERIFY2(result.partialSyncNoPat, "no-PAT clone must set partialSyncNoPat (silent S2 open)");

  // Final dir exists and contains the cloned VNote layout. The staging dir
  // must have been renamed away (no leftover .vnote-clone-pending-* sibling).
  QVERIFY2(QDir(input.finalDestDir).exists(), "final dest dir must exist after success");
  QVERIFY2(QFile::exists(input.finalDestDir + QStringLiteral("/vx_notebook/config.json")),
           "cloned vx_notebook/config.json must be present in final dir");

  const QFileInfoList pending =
      QDir(localTemp.path())
          .entryInfoList({QStringLiteral(".*.vnote-clone-pending-*")},
                         QDir::Hidden | QDir::Dirs | QDir::NoDotAndDotDot);
  QVERIFY2(pending.isEmpty(), "staging dir should not remain after a successful rename");

  // Regression: staging-marker.json must not be left behind in the final dir
  // after the staging-to-final rename. Without this gate, the marker surfaces
  // as an "external file" in the notebook explorer.
  QVERIFY2(!QFile::exists(input.finalDestDir + QStringLiteral("/staging-marker.json")),
           "staging-marker.json must not leak into the cloned notebook root");

  // Vxcore confirms the notebook is WRITABLE (not read-only) after the no-PAT clone.
  bool readOnly = true;
  QCOMPARE(vxcore_notebook_is_read_only(m_ctx, result.notebookId.toUtf8().constData(), &readOnly),
           VXCORE_OK);
  QCOMPARE(readOnly, false);

  // Partial sync info (S2) persisted to notebook config: enabled + git + url, no PAT.
  auto *nbSvc = m_services.get<NotebookCoreService>();
  const QJsonObject cfg = nbSvc->getNotebookConfig(result.notebookId);
  QCOMPARE(cfg.value(QStringLiteral("syncEnabled")).toBool(), true);
  QCOMPARE(cfg.value(QStringLiteral("syncBackend")).toString(), QStringLiteral("git"));
  QCOMPARE(cfg.value(QStringLiteral("syncRemoteUrl")).toString(), remoteUrl);

  // No PAT was stored in the keychain (no empty-PAT write).
  auto *credStore = m_services.get<SyncCredentialsStore>();
  QVERIFY2(!credStore->hasCredentials(result.notebookId),
           "no-PAT clone must NOT create a keychain entry");

  // Classifier confirms the canonical partial state S2.
  auto *classifier = m_services.get<SyncStateClassifier>();
  QCOMPARE(static_cast<int>(classifier->classify(result.notebookId)),
           static_cast<int>(SyncState::S2));
}

void TestOpenNotebookControllerClone::testValidationRejectsExistingDestDir() {
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Pre-create the user-chosen destination as a NON-EMPTY dir --
  // validateCloneInput must reject. Per commit e6caf855 ("allow
  // OpenNotebookController clone into non-existing or empty dest"), EMPTY
  // existing dirs are now accepted (the worker rmdirs them before the
  // staging->final rename); only NON-EMPTY existing dirs are rejected. The
  // sentinel file below both verifies post-rollback safety AND makes the dir
  // non-empty, which is what triggers the validation failure tested here.
  const QString existingDestDir = localTemp.filePath(QStringLiteral("non_empty_exists"));
  QVERIFY(QDir().mkpath(existingDestDir));
  // Add a sentinel file so we can confirm we don't touch it on rollback.
  QFile sentinel(existingDestDir + QStringLiteral("/sentinel.txt"));
  QVERIFY(sentinel.open(QIODevice::WriteOnly));
  sentinel.write("DO NOT TOUCH");
  sentinel.close();

  CloneAndOpenInput input;
  // URL must pass scheme validation so we reach the dest-exists check.
  input.remoteUrl = QStringLiteral("file:///nonexistent.git");
  input.finalDestDir = existingDestDir;

  OpenNotebookController controller(m_services);
  // Pre-flight validate: synchronous answer, no worker dispatch.
  const auto validation = controller.validateCloneInput(input);
  QVERIFY2(!validation.valid, "non-empty existing dest dir must be rejected by validation");
  QVERIFY2(validation.message.contains(QStringLiteral("must be empty")),
           qPrintable(QStringLiteral("validation message must mention 'must be empty' (got: %1)")
                          .arg(validation.message)));

  // cloneAndOpen with the same input must short-circuit through
  // cloneFinished without ever creating a staging dir.
  QSignalSpy finishedSpy(&controller, &OpenNotebookController::cloneFinished);
  controller.cloneAndOpen(input);
  QVERIFY(finishedSpy.wait(5000));
  QCOMPARE(finishedSpy.count(), 1);

  const CloneAndOpenResult result = finishedSpy.first().at(0).value<CloneAndOpenResult>();
  QVERIFY2(!result.success, "validation failure must surface as cloneFinished(success=false)");
  QVERIFY(!result.errorMessage.isEmpty());

  // Sentinel intact (we never touched user content).
  QVERIFY(QFile::exists(existingDestDir + QStringLiteral("/sentinel.txt")));

  // No staging dir was generated alongside.
  const QFileInfoList pending =
      QDir(localTemp.path())
          .entryInfoList({QStringLiteral(".*.vnote-clone-pending-*")},
                         QDir::Hidden | QDir::Dirs | QDir::NoDotAndDotDot);
  QVERIFY2(pending.isEmpty(), "validation failure must not create a staging dir");
}

void TestOpenNotebookControllerClone::testNonNotebookRemoteCleansUp() {
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  const QString bareDir = localTemp.filePath(QStringLiteral("remote_nonvnote.git"));
  const QString remoteUrl = seedNonVNoteBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    QSKIP("git not available or bare-repo seeding failed");
  }

  CloneAndOpenInput input;
  input.remoteUrl = remoteUrl;
  input.pat = QString();
  input.finalDestDir = localTemp.filePath(QStringLiteral("clone_dest_nonvnote"));

  OpenNotebookController controller(m_services);
  QSignalSpy finishedSpy(&controller, &OpenNotebookController::cloneFinished);

  // Suppress the expected qWarning from cloneNotebookFromUrl (vxcore returns
  // VXCORE_ERR_NOT_FOUND when the clone has no vx_notebook/config.json).
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("cloneNotebookFromUrl failed.*")));

  controller.cloneAndOpen(input);
  QVERIFY2(finishedSpy.wait(30000), "cloneAndOpen must emit cloneFinished within 30s");
  QCOMPARE(finishedSpy.count(), 1);

  const CloneAndOpenResult result = finishedSpy.first().at(0).value<CloneAndOpenResult>();
  QVERIFY2(!result.success, "non-notebook remote must surface as failure");
  QVERIFY(!result.errorMessage.isEmpty());

  // Final dir must NOT exist (we never reached the rename step).
  QVERIFY2(!QDir(input.finalDestDir).exists(),
           "final dest dir must not be created when clone fails post-staging");

  // Staging dir was cleaned up.
  const QFileInfoList pending =
      QDir(localTemp.path())
          .entryInfoList({QStringLiteral(".*.vnote-clone-pending-*")},
                         QDir::Hidden | QDir::Dirs | QDir::NoDotAndDotDot);
  QVERIFY2(pending.isEmpty(), "staging dir must be cleaned up after a non-notebook clone failure");
}

void TestOpenNotebookControllerClone::testCloneFailureRollback() {
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Point at a file:// URL whose bare repo does not exist. libgit2 will fail
  // with a transport / not-found error. This exercises the "clone returned
  // non-OK" cleanup path which is the controller's response to ANY clone
  // failure -- network down, auth rejected, repo missing, etc.
  const QString badBareDir = localTemp.filePath(QStringLiteral("does_not_exist.git"));
  CloneAndOpenInput input;
  input.remoteUrl = toFileUrl(badBareDir);
  input.pat = QString();
  input.finalDestDir = localTemp.filePath(QStringLiteral("clone_dest_unreachable"));

  OpenNotebookController controller(m_services);
  QSignalSpy finishedSpy(&controller, &OpenNotebookController::cloneFinished);

  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("cloneNotebookFromUrl failed.*")));

  controller.cloneAndOpen(input);
  QVERIFY2(finishedSpy.wait(30000), "cloneAndOpen must emit cloneFinished within 30s");
  QCOMPARE(finishedSpy.count(), 1);

  const CloneAndOpenResult result = finishedSpy.first().at(0).value<CloneAndOpenResult>();
  QVERIFY2(!result.success, "unreachable remote must surface as failure");
  QVERIFY(!result.errorMessage.isEmpty());

  // Final dir never created; staging dir cleaned up.
  QVERIFY2(!QDir(input.finalDestDir).exists(),
           "final dest dir must not be created when clone fails outright");
  const QFileInfoList pending =
      QDir(localTemp.path())
          .entryInfoList({QStringLiteral(".*.vnote-clone-pending-*")},
                         QDir::Hidden | QDir::Dirs | QDir::NoDotAndDotDot);
  QVERIFY2(pending.isEmpty(), "staging dir must be cleaned up after a clone-step failure");
}

} // namespace tests

QTEST_MAIN(tests::TestOpenNotebookControllerClone)
#include "test_open_notebook_controller_clone.moc"
