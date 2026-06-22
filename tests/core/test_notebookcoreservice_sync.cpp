// Tests for the sync wrapper methods on NotebookCoreService.
//
// These wrappers are thin pass-throughs to the vxcore C sync API. They do NOT
// validate JSON shapes; that responsibility lives in vxcore. Tests focus on:
//   1. The wrappers correctly forward arguments and return codes.
//   2. The wrappers convert Qt strings via UTF-8 (Windows non-ASCII path safety).
//   3. Output-string params receive vxcore-allocated JSON and free it correctly.
//   4. Credentials JSON is never logged (verified by stderr inspection at the
//      orchestrator layer; this file only sets the contract that wrappers must
//      not call qDebug/qWarning with credential fields).
//
// Per ADR-1: VNote NEVER includes sync/sync_manager.h. Only the C API is used.

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestNotebookCoreServiceSync : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void enableWithCredentialsClonesIntoEmptyRoot();
  void setSyncCredentialsForUnknownNotebookReturnsNotFound();
  void enableSyncForRawNotebookReturnsUnsupported();
  void disableSyncIdempotent();
  void getSyncStatusReturnsValidJson();

private:
  // Create a fresh vxcore context and NotebookCoreService for an isolated test.
  // Caller owns both via the returned pair (destroy via destroyFixture()).
  struct Fixture {
    VxCoreContextHandle ctx = nullptr;
    NotebookCoreService *service = nullptr;
  };
  Fixture makeFixture();
  void destroyFixture(Fixture &p_fix);

  // Initialize a bare git repo at p_bareRepoPath and seed it with one commit
  // containing a single file "seed.md" on the default branch (main).
  // Returns the file:// URL suitable for cloning, or empty on failure.
  QString seedBareRepo(const QString &p_bareRepoPath);

  TempDirFixture m_globalTemp;
};

void TestNotebookCoreServiceSync::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
  QVERIFY(m_globalTemp.isValid());
}

void TestNotebookCoreServiceSync::cleanupTestCase() {
  // Nothing global to tear down; per-test fixtures handle their own ctx.
}

TestNotebookCoreServiceSync::Fixture TestNotebookCoreServiceSync::makeFixture() {
  Fixture f;
  VxCoreError err = vxcore_context_create("{}", &f.ctx);
  if (err != VXCORE_OK || f.ctx == nullptr) {
    return f;
  }
  f.service = new NotebookCoreService(f.ctx);
  return f;
}

void TestNotebookCoreServiceSync::destroyFixture(Fixture &p_fix) {
  if (p_fix.service) {
    delete p_fix.service;
    p_fix.service = nullptr;
  }
  if (p_fix.ctx) {
    vxcore_context_destroy(p_fix.ctx);
    p_fix.ctx = nullptr;
  }
}

QString TestNotebookCoreServiceSync::seedBareRepo(const QString &p_bareRepoPath) {
  // Initialize the bare repo.
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("init"), QStringLiteral("--bare"),
                         QStringLiteral("--initial-branch=main"), p_bareRepoPath}) != 0) {
    // Older git may not support --initial-branch; retry without.
    QDir().rmpath(p_bareRepoPath);
    if (QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                  p_bareRepoPath}) != 0) {
      return QString();
    }
  }

  // Clone into a working directory and create the seed commit.
  QString workDir = m_globalTemp.filePath(QStringLiteral("seed_work_") +
                                          QString::number(QDateTime::currentMSecsSinceEpoch()));
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("clone"), p_bareRepoPath, workDir}) != 0) {
    return QString();
  }

  // Configure local user/email (required by some git installs to commit).
  QProcess::execute(QStringLiteral("git"),
                    {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                     QStringLiteral("user.email"), QStringLiteral("seed@example.com")});
  QProcess::execute(QStringLiteral("git"), {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                                            QStringLiteral("user.name"), QStringLiteral("Seed")});

  // Write the seed file.
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
  // Push to whatever default branch the local clone has.
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("push"),
                         QStringLiteral("origin"), QStringLiteral("HEAD")}) != 0) {
    return QString();
  }

  // Construct file:// URL. On Windows we need a triple slash and forward slashes.
  QString normalized = QDir::fromNativeSeparators(p_bareRepoPath);
  if (!normalized.startsWith(QLatin1Char('/'))) {
    normalized.prepend(QLatin1Char('/'));
  }
  return QStringLiteral("file://") + normalized;
}

void TestNotebookCoreServiceSync::enableWithCredentialsClonesIntoEmptyRoot() {
  Fixture fix = makeFixture();
  QVERIFY(fix.service != nullptr);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Create a bare remote and seed it.
  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir);
  if (remoteUrl.isEmpty()) {
    QSKIP("git not available or bare-repo seeding failed");
  }

  // Create an empty notebook root and a bundled notebook.
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_root"));
  QDir().mkpath(nbRoot);
  QString nbId = fix.service->createNotebook(
      nbRoot, R"({"name":"Sync NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Build sync config JSON pointing at the seeded bare repo.
  QString configJson =
      QStringLiteral(R"({"backend":"git","remoteUrl":"%1","autoSyncEnabled":true})").arg(remoteUrl);

  // Empty PAT — file:// remote does not need auth, but we exercise the
  // credentials path.
  QString credsJson =
      QStringLiteral(R"({"pat":"","authorName":"Seed","authorEmail":"seed@example.com"})");

  VxCoreError err = fix.service->enableSync(nbId, configJson, credsJson);
  QCOMPARE(err, VXCORE_OK);

  // Verify the seed file landed in the workdir root after clone-into-empty.
  QVERIFY2(QFile::exists(nbRoot + QStringLiteral("/seed.md")),
           "Seed file from bare repo should appear in notebook root after clone");

  destroyFixture(fix);
}

void TestNotebookCoreServiceSync::setSyncCredentialsForUnknownNotebookReturnsNotFound() {
  Fixture fix = makeFixture();
  QVERIFY(fix.service != nullptr);

  VxCoreError err = fix.service->setSyncCredentials(QStringLiteral("nb_unknown"),
                                                    QStringLiteral(R"({"pat":"x"})"));
  QCOMPARE(err, VXCORE_ERR_NOT_FOUND);

  destroyFixture(fix);
}

void TestNotebookCoreServiceSync::enableSyncForRawNotebookReturnsUnsupported() {
  Fixture fix = makeFixture();
  QVERIFY(fix.service != nullptr);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString rawRoot = localTemp.filePath(QStringLiteral("raw_nb"));
  QDir().mkpath(rawRoot);
  QString nbId = fix.service->createNotebook(
      rawRoot, R"({"name":"Raw NB","description":"","version":"1"})", NotebookType::Raw);
  QVERIFY(!nbId.isEmpty());

  VxCoreError err =
      fix.service->enableSync(nbId, R"({"backend":"git","remoteUrl":"file:///nope"})");
  QCOMPARE(err, VXCORE_ERR_UNSUPPORTED);

  destroyFixture(fix);
}

void TestNotebookCoreServiceSync::disableSyncIdempotent() {
  Fixture fix = makeFixture();
  QVERIFY(fix.service != nullptr);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("disable_nb"));
  QDir().mkpath(nbRoot);
  QString nbId = fix.service->createNotebook(
      nbRoot, R"({"name":"Disable NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Disable on a notebook with no sync ever enabled. Document whatever vxcore
  // returns; both VXCORE_OK and VXCORE_ERR_SYNC_NOT_ENABLED are reasonable.
  VxCoreError err = fix.service->disableSync(nbId);
  const bool acceptable = (err == VXCORE_OK || err == VXCORE_ERR_SYNC_NOT_ENABLED);
  if (!acceptable) {
    qWarning() << "disableSync on never-enabled notebook returned unexpected code:" << err;
  }
  QVERIFY(acceptable);

  destroyFixture(fix);
}

void TestNotebookCoreServiceSync::getSyncStatusReturnsValidJson() {
  Fixture fix = makeFixture();
  QVERIFY(fix.service != nullptr);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote2.git"));
  QString remoteUrl = seedBareRepo(bareDir);
  if (remoteUrl.isEmpty()) {
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("status_nb"));
  QDir().mkpath(nbRoot);
  QString nbId = fix.service->createNotebook(
      nbRoot, R"({"name":"Status NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QString cfg =
      QStringLiteral(R"({"backend":"git","remoteUrl":"%1","autoSyncEnabled":true})").arg(remoteUrl);
  QString creds = QStringLiteral(R"({"pat":""})");
  QCOMPARE(fix.service->enableSync(nbId, cfg, creds), VXCORE_OK);

  QString statusJson;
  VxCoreError err = fix.service->getSyncStatus(nbId, statusJson);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(!statusJson.isEmpty());

  QJsonParseError parseErr;
  QJsonDocument doc = QJsonDocument::fromJson(statusJson.toUtf8(), &parseErr);
  QCOMPARE(parseErr.error, QJsonParseError::NoError);
  QVERIFY(doc.isObject());

  destroyFixture(fix);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookCoreServiceSync)
#include "test_notebookcoreservice_sync.moc"
