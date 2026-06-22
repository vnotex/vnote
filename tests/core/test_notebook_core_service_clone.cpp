// T20 (open-notebook-remote-readonly): NotebookCoreService::cloneNotebookFromUrl
// and openNotebookEx Qt wrappers around vxcore_sync_clone (T19) and
// vxcore_notebook_open_ex (T14).
//
// Verifies:
//   1. cloneNotebookFromUrl returns a notebook id when the remote is a valid
//      bare repo seeded with a VNote notebook; the vnote.notebook.after_clone
//      hook fires; and listNotebooks() includes the new notebook.
//   2. cloneNotebookFromUrl returns an empty string when config_json is
//      malformed and the after_clone hook does NOT fire (no false-positive
//      notification to plugins).
//   3. openNotebookEx(path, "{\"readOnly\":true}") opens the notebook in
//      read-only mode -- verified via vxcore_notebook_is_read_only.

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookCoreServiceClone : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testCloneFromBareRepoFiresHookAndRegistersNotebook();
  void testCloneWithMalformedConfigReturnsEmptyAndSkipsHook();
  void testOpenNotebookExReadOnlyTrueOpensReadOnly();

private:
  // Initialize a bare git repo at p_bareRepoPath and seed it with a VNote
  // bundled-notebook layout (vx_notebook/config.json + minimal metadata) on
  // the default branch (main). Returns the file:// URL suitable for cloning,
  // or empty on failure (callers should QSKIP).
  // Borrowed from test_notebookcoreservice_sync.cpp::seedBareRepo and
  // extended to commit a VNote notebook structure so SyncManager::CloneNotebook
  // accepts the result.
  QString seedVNoteBareRepo(const QString &p_bareRepoPath, const QString &p_notebookGuid,
                            const QString &p_notebookName);

  TempDirFixture m_tempDir;
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_service = nullptr;
};

void TestNotebookCoreServiceClone::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // CRITICAL: enable test mode BEFORE vxcore_context_create so vxcore points
  // at %TEMP%/vxcore_test* instead of real AppData.
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_hookMgr = new HookManager(this);
  m_service = new NotebookCoreService(m_context, this);
  m_service->setHookManager(m_hookMgr);
}

void TestNotebookCoreServiceClone::cleanupTestCase() {
  // Close any notebooks registered during tests so vxcore can tear down cleanly.
  if (m_service) {
    const QJsonArray nbs = m_service->listNotebooks();
    for (const auto &val : nbs) {
      const QString id = val.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        m_service->closeNotebook(id);
      }
    }
  }
  delete m_service;
  m_service = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

QString TestNotebookCoreServiceClone::seedVNoteBareRepo(const QString &p_bareRepoPath,
                                                        const QString &p_notebookGuid,
                                                        const QString &p_notebookName) {
  // Initialize the bare repo (force main branch if supported).
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("init"), QStringLiteral("--bare"),
                         QStringLiteral("--initial-branch=main"), p_bareRepoPath}) != 0) {
    // Older git: retry without --initial-branch.
    QDir().rmpath(p_bareRepoPath);
    if (QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                  p_bareRepoPath}) != 0) {
      return QString();
    }
  }

  // Clone into a working directory and create a VNote bundled-notebook layout.
  const QString workDir = m_tempDir.filePath(QStringLiteral("seed_work_") +
                                             QString::number(QDateTime::currentMSecsSinceEpoch()));
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("clone"), p_bareRepoPath, workDir}) != 0) {
    return QString();
  }

  // Configure local user identity (some git installs reject commits without).
  QProcess::execute(QStringLiteral("git"),
                    {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                     QStringLiteral("user.email"), QStringLiteral("seed@example.com")});
  QProcess::execute(QStringLiteral("git"), {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                                            QStringLiteral("user.name"), QStringLiteral("Seed")});

  // Lay down vx_notebook/config.json with a minimal-but-valid NotebookConfig.
  // SyncManager::CloneNotebook validates the presence of this file AND that
  // the "id" field is non-empty (per T18 contract).
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

  // Stage + commit + push.
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

  // Construct file:// URL with Windows-safe forward slashes and triple slash.
  QString normalized = QDir::fromNativeSeparators(p_bareRepoPath);
  if (!normalized.startsWith(QLatin1Char('/'))) {
    normalized.prepend(QLatin1Char('/'));
  }
  return QStringLiteral("file://") + normalized;
}

void TestNotebookCoreServiceClone::testCloneFromBareRepoFiresHookAndRegistersNotebook() {
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  const QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  const QString notebookGuid = QStringLiteral("clone-test-guid-001");
  const QString notebookName = QStringLiteral("Cloned Notebook 1");
  const QString remoteUrl = seedVNoteBareRepo(bareDir, notebookGuid, notebookName);
  if (remoteUrl.isEmpty()) {
    QSKIP("git not available or bare-repo seeding failed");
  }

  // Target dir must exist + be empty (caller's responsibility per the API).
  const QString targetDir = localTemp.filePath(QStringLiteral("clone_target_1"));
  QVERIFY(QDir().mkpath(targetDir));

  // Subscribe to the after_clone hook via the typed API so we capture the
  // event payload (notebookId, targetDir, isReadOnly).
  bool hookFired = false;
  NotebookCloneEvent capturedEvent;
  const int actionId = m_hookMgr->addAction<NotebookCloneEvent>(
      HookNames::NotebookAfterClone,
      [&hookFired, &capturedEvent](HookContext &, const NotebookCloneEvent &p_event) {
        hookFired = true;
        capturedEvent = p_event;
      },
      10);

  const QString configJson =
      QStringLiteral(R"({"backend":"git","remoteUrl":"%1","autoSyncEnabled":true})").arg(remoteUrl);
  // Empty credentials -> anonymous clone (file:// remote needs no auth).
  const QString credsJson = QString();

  const QString newId = m_service->cloneNotebookFromUrl(targetDir, configJson, credsJson);
  m_hookMgr->removeAction(actionId);

  QVERIFY2(!newId.isEmpty(), "cloneNotebookFromUrl should return notebook id on success");
  QCOMPARE(newId, notebookGuid);

  // Hook fired with the right payload.
  QVERIFY2(hookFired, "vnote.notebook.after_clone hook should fire on success");
  QCOMPARE(capturedEvent.notebookId, newId);
  QCOMPARE(capturedEvent.targetDir, targetDir);
  // Clone path does NOT set RO by itself per snapshot-only MVP design.
  QCOMPARE(capturedEvent.isReadOnly, false);

  // Notebook is registered with NotebookManager.
  const QJsonArray nbs = m_service->listNotebooks();
  bool found = false;
  for (const auto &val : nbs) {
    if (val.toObject().value(QStringLiteral("id")).toString() == newId) {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "Cloned notebook should appear in listNotebooks()");
}

void TestNotebookCoreServiceClone::testCloneWithMalformedConfigReturnsEmptyAndSkipsHook() {
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  const QString targetDir = localTemp.filePath(QStringLiteral("clone_target_bad"));
  QVERIFY(QDir().mkpath(targetDir));

  bool hookFired = false;
  const int actionId = m_hookMgr->addAction<NotebookCloneEvent>(
      HookNames::NotebookAfterClone,
      [&hookFired](HookContext &, const NotebookCloneEvent &) { hookFired = true; }, 10);

  // Malformed JSON: not parseable as an object.
  const QString badConfigJson = QStringLiteral("not-valid-json{");
  // Suppress the expected qWarning from cloneNotebookFromUrl so the test log
  // stays clean (we deliberately exercise the failure path).
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("cloneNotebookFromUrl failed.*")));

  const QString newId = m_service->cloneNotebookFromUrl(targetDir, badConfigJson, QString());
  m_hookMgr->removeAction(actionId);

  QVERIFY2(newId.isEmpty(), "cloneNotebookFromUrl should return empty on malformed config");
  QVERIFY2(!hookFired, "after_clone hook MUST NOT fire on failure (no false-positive)");
}

void TestNotebookCoreServiceClone::testOpenNotebookExReadOnlyTrueOpensReadOnly() {
  // Create a fresh bundled notebook so we have a real on-disk root to open.
  const QString nbRoot = m_tempDir.filePath(QStringLiteral("open_ex_ro_root"));
  const QString createCfg =
      QStringLiteral(R"({"name":"OpenEx RO","description":"openNotebookEx test","version":"1"})");
  const QString nbId = m_service->createNotebook(nbRoot, createCfg, NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Close it so we can re-open via openNotebookEx with readOnly:true.
  QVERIFY(m_service->closeNotebook(nbId));

  // Re-open with the readOnly option set.
  const QString reopenedId =
      m_service->openNotebookEx(nbRoot, QStringLiteral("{\"readOnly\":true}"));
  QVERIFY(!reopenedId.isEmpty());
  QCOMPARE(reopenedId, nbId);

  // Verify the runtime read-only flag flipped via the vxcore C API.
  bool readOnly = false;
  const QByteArray idBytes = reopenedId.toUtf8();
  VxCoreError err = vxcore_notebook_is_read_only(m_context, idBytes.constData(), &readOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(readOnly, true);

  // Verify the legacy openNotebook shim still works and produces RW state.
  // Close + reopen via the unmodified API to make sure T20's refactor of
  // openNotebook -> openNotebookEx("{}") preserves back-compat.
  QVERIFY(m_service->closeNotebook(reopenedId));
  const QString rwId = m_service->openNotebook(nbRoot);
  QVERIFY(!rwId.isEmpty());
  QCOMPARE(rwId, nbId);
  bool rwReadOnly = true;
  err = vxcore_notebook_is_read_only(m_context, rwId.toUtf8().constData(), &rwReadOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(rwReadOnly, false);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookCoreServiceClone)
#include "test_notebook_core_service_clone.moc"
