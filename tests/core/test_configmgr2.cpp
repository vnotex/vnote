#include <QtTest>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/error.h>
#include <core/mainconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/services/configcoreservice.h>
#include <utils/fileutils2.h>

#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestConfigMgr2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testConstruction();
  void testInitialization();
  void testPathAccessors();
  void testConfigDataFolders();
  void testUpdateMainConfig();
  void testUpdateSessionConfig();
  void testDebouncing();

  // Extra-data install / recovery.
  void testExtraData_cleanRunInstallsEveryFolderAndStamps();
  void testExtraData_failureIsReportedThenHealsOnRetry();
  void testExtraData_successfulRetryOnTheSameInstanceClearsTheFailureList();
  void testExtraData_missingBundleFailsEveryFolderAndInstallsNothing();
  void testExtraData_userCssSurvivesInstallAndFailedRecovery();
  void testExtraData_configVersionIsStampedEvenWhenACopyFailed();

  // In-place preview sources: persistence + the 4.4.4 table migration.
  void testInplacePreviewSources_jsonRoundTripCarriesTable();
  void testInplacePreviewSources_persistedStringWinsOverTheCppDefault();
  void testTableMigration_addsTableAndKeepsOtherChoices();
  void testTableMigration_leavesABlanketOptOutAlone();
  void testTableMigration_doesNotRunOnTheSameOrANewerVersion();

private:
  // Build an on-disk stand-in for the bundled vnote_extra.rcc tree.
  QString buildExtraDataFixture(TempDirFixture &p_tmp) const;

  // Wipe the installed extra-data folders (and therefore their stamps) so each
  // scenario starts from a known state.
  void resetInstalledExtraData() const;

  QString stampPath(ConfigMgr2::ConfigDataType p_type) const;

  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
};

void TestConfigMgr2::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_configService = new ConfigCoreService(m_context);

  m_configMgr = new ConfigMgr2(m_configService);
}

void TestConfigMgr2::cleanupTestCase() {
  delete m_configMgr;
  delete m_configService;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestConfigMgr2::testConstruction() { QVERIFY(m_configMgr != nullptr); }

void TestConfigMgr2::testInitialization() {
  m_configMgr->init();
  QVERIFY(!m_configMgr->getAppDataPath().isEmpty());
  QVERIFY(!m_configMgr->getUserConfigPath().isEmpty());
}

void TestConfigMgr2::testPathAccessors() {
  QString appData = m_configMgr->getAppDataPath();
  QString userData = m_configMgr->getUserConfigPath();
  QString logFile = m_configMgr->getLogFile();

  QVERIFY(!appData.isEmpty());
  QVERIFY(!userData.isEmpty());
  QVERIFY(!logFile.isEmpty());
  QVERIFY(logFile.endsWith("vnote.log"));

  qDebug() << "AppData:" << appData;
  qDebug() << "UserData:" << userData;
  qDebug() << "LogFile:" << logFile;
}

void TestConfigMgr2::testConfigDataFolders() {
  QString mainFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Main);
  QString themesFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Themes);
  QString webFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Web);

  QCOMPARE(mainFolder, m_configMgr->getAppDataPath());
  QVERIFY(themesFolder.contains("themes"));
  QVERIFY(webFolder.contains("web"));

  qDebug() << "Main:" << mainFolder;
  qDebug() << "Themes:" << themesFolder;
  qDebug() << "Web:" << webFolder;
}

void TestConfigMgr2::testUpdateMainConfig() {
  QJsonObject testConfig;
  testConfig["test_key"] = "test_value";
  testConfig["version"] = "1.0";

  m_configMgr->updateMainConfig(testConfig);

  // Wait for debounced write (500ms + buffer)
  QTest::qWait(700);

  // Verify config was written via ConfigService
  QJsonObject loaded = m_configService->getConfigByName(DataLocation::App, "vnotex");
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded["test_key"].toString(), QString("test_value"));
}

void TestConfigMgr2::testUpdateSessionConfig() {
  QJsonObject testConfig;
  testConfig["session_key"] = "session_value";

  m_configMgr->updateSessionConfig(testConfig);

  // Wait for debounced write
  QTest::qWait(700);

  // Verify config was written
  QJsonObject loaded = m_configService->getConfigByName(DataLocation::Local, "session");
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded["session_key"].toString(), QString("session_value"));
}

void TestConfigMgr2::testDebouncing() {
  // Send multiple rapid updates
  for (int i = 0; i < 5; ++i) {
    QJsonObject config;
    config["counter"] = i;
    m_configMgr->updateMainConfig(config);
    QTest::qWait(50); // Small delay between updates
  }

  // Wait for debounced write
  QTest::qWait(700);

  // Verify only last update was written (debouncing)
  QJsonObject loaded = m_configService->getConfigByName(DataLocation::App, "vnotex");
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded["counter"].toInt(), 4); // Last value
}

// =============================================================================
// Extra-data install / recovery
// =============================================================================

// The five folders ensureExtraData() installs, in bundle order.
static const char *const kExtraFolders[] = {"themes", "tasks", "syntax-highlighting", "web",
                                            "dicts"};

QString TestConfigMgr2::buildExtraDataFixture(TempDirFixture &p_tmp) const {
  const QString root = p_tmp.createDir("extra");
  for (const char *folder : kExtraFolders) {
    const QString name = QString::fromLatin1(folder);
    p_tmp.createDir(QStringLiteral("extra/") + name);
    p_tmp.createTextFile(QStringLiteral("extra/%1/marker.txt").arg(name),
                         QStringLiteral("bundled %1").arg(name));
  }

  // The one user-owned file the install must never clobber.
  p_tmp.createDir(QStringLiteral("extra/web/css"));
  p_tmp.createTextFile(QStringLiteral("extra/web/css/user.css"),
                       QStringLiteral("/* bundled stub */"));
  return root;
}

void TestConfigMgr2::resetInstalledExtraData() const {
  for (auto type : {ConfigMgr2::ConfigDataType::Themes, ConfigMgr2::ConfigDataType::Tasks,
                    ConfigMgr2::ConfigDataType::SyntaxHighlighting, ConfigMgr2::ConfigDataType::Web,
                    ConfigMgr2::ConfigDataType::Dicts}) {
    QDir(m_configMgr->getConfigDataFolder(type)).removeRecursively();
  }
}

QString TestConfigMgr2::stampPath(ConfigMgr2::ConfigDataType p_type) const {
  return QDir(m_configMgr->getConfigDataFolder(p_type))
      .filePath(QLatin1String(FileUtils2::c_versionStampFileName));
}

void TestConfigMgr2::testExtraData_cleanRunInstallsEveryFolderAndStamps() {
  resetInstalledExtraData();

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  ConfigMgr2 mgr(m_configService);
  mgr.setExtraDataSourceRootOverrideForTesting(fixture);
  mgr.init();
  mgr.initAfterQtAppStarted();

  QVERIFY2(mgr.extraDataCopyFailures().isEmpty(), "a clean install reported failures");

  for (auto type : {ConfigMgr2::ConfigDataType::Themes, ConfigMgr2::ConfigDataType::Tasks,
                    ConfigMgr2::ConfigDataType::SyntaxHighlighting, ConfigMgr2::ConfigDataType::Web,
                    ConfigMgr2::ConfigDataType::Dicts}) {
    const QString folder = mgr.getConfigDataFolder(type);
    QVERIFY2(QFileInfo::exists(folder + QStringLiteral("/marker.txt")),
             qPrintable(QStringLiteral("not installed: %1").arg(folder)));
    QFile stamp(stampPath(type));
    QVERIFY2(stamp.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("no stamp in %1").arg(folder)));
    QCOMPARE(QString::fromUtf8(stamp.readAll()).trimmed(), ConfigMgr2::getApplicationVersion());
  }
}

void TestConfigMgr2::testExtraData_failureIsReportedThenHealsOnRetry() {
  resetInstalledExtraData();

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  // Failure injection: a DIRECTORY where web/marker.txt must land. copyFile()
  // reaches QFile::remove(), which uses file-removal semantics and fails on a
  // directory on every platform.
  const QString webFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Web);
  const QString blocker = webFolder + QStringLiteral("/marker.txt");
  QVERIFY(QDir().mkpath(blocker));

  {
    ConfigMgr2 mgr(m_configService);
    mgr.setExtraDataSourceRootOverrideForTesting(fixture);
    mgr.init();
    mgr.initAfterQtAppStarted();

    const auto &failures = mgr.extraDataCopyFailures();
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.at(0).m_folderName, QStringLiteral("web"));
    QVERIFY(!failures.at(0).m_failedPaths.isEmpty());
    QVERIFY2(!QFileInfo::exists(stampPath(ConfigMgr2::ConfigDataType::Web)),
             "a partial web/ install was stamped as complete");
    // The healthy folders still installed and stamped.
    QVERIFY(QFileInfo::exists(stampPath(ConfigMgr2::ConfigDataType::Themes)));
  }

  QVERIFY(QDir(blocker).removeRecursively());

  ConfigMgr2 healed(m_configService);
  healed.setExtraDataSourceRootOverrideForTesting(fixture);
  healed.init();
  healed.initAfterQtAppStarted();

  QVERIFY2(healed.extraDataCopyFailures().isEmpty(), "the retry did not heal web/");
  QCOMPARE(QFileInfo::exists(webFolder + QStringLiteral("/marker.txt")), true);
  QVERIFY(QFileInfo::exists(stampPath(ConfigMgr2::ConfigDataType::Web)));
}

// The failure list is per-RUN, not cumulative: a second call on the SAME
// manager must empty it once the folder installs cleanly, otherwise the
// notification would keep reporting a healed folder.
void TestConfigMgr2::testExtraData_successfulRetryOnTheSameInstanceClearsTheFailureList() {
  resetInstalledExtraData();

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  const QString webFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Web);
  const QString blocker = webFolder + QStringLiteral("/marker.txt");
  QVERIFY(QDir().mkpath(blocker));

  ConfigMgr2 mgr(m_configService);
  mgr.setExtraDataSourceRootOverrideForTesting(fixture);
  mgr.init();
  mgr.initAfterQtAppStarted();
  QCOMPARE(mgr.extraDataCopyFailures().size(), 1);

  QVERIFY(QDir(blocker).removeRecursively());

  // Same object, second run.
  mgr.initAfterQtAppStarted();
  QVERIFY2(mgr.extraDataCopyFailures().isEmpty(), "the healed failure was not cleared");
}

// A missing/unregisterable bundle must fail ALL five folders and install
// nothing. Falling through (as the old code did) would let a tolerant copy
// stamp five empty folders as complete -- strictly worse than the old bug.
void TestConfigMgr2::testExtraData_missingBundleFailsEveryFolderAndInstallsNothing() {
  resetInstalledExtraData();

  ConfigMgr2 mgr(m_configService);
  // No source override, so ensureExtraData() tries the production
  // "app:vnote_extra.rcc" -- which is not deployed next to the test binary.
  mgr.init();
  mgr.initAfterQtAppStarted();

  QCOMPARE(mgr.extraDataCopyFailures().size(), 5);
  for (const auto &failure : mgr.extraDataCopyFailures()) {
    QVERIFY(!failure.m_folderName.isEmpty());
    QVERIFY(failure.m_errorMessage.contains(QStringLiteral("vnote_extra.rcc")));
  }

  for (auto type : {ConfigMgr2::ConfigDataType::Themes, ConfigMgr2::ConfigDataType::Tasks,
                    ConfigMgr2::ConfigDataType::SyntaxHighlighting, ConfigMgr2::ConfigDataType::Web,
                    ConfigMgr2::ConfigDataType::Dicts}) {
    QVERIFY2(
        !QFileInfo::exists(stampPath(type)),
        qPrintable(
            QStringLiteral("stamped without a bundle: %1").arg(mgr.getConfigDataFolder(type))));
  }
}

void TestConfigMgr2::testExtraData_userCssSurvivesInstallAndFailedRecovery() {
  resetInstalledExtraData();

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  const QString webFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Web);
  const QString userCss = webFolder + QStringLiteral("/css/user.css");

  {
    ConfigMgr2 mgr(m_configService);
    mgr.setExtraDataSourceRootOverrideForTesting(fixture);
    mgr.init();
    mgr.initAfterQtAppStarted();
    QVERIFY(mgr.extraDataCopyFailures().isEmpty());
    // Seeded from the bundle on the initial install.
    QVERIFY(QFileInfo::exists(userCss));
  }

  // The user customizes it.
  {
    QFile file(userCss);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("body { color: red; }");
    file.close();
  }

  // Invalidate web/'s stamp so the next launch re-copies it, and make that
  // re-copy FAIL, i.e. the recovery scenario this whole feature exists for.
  {
    QFile stamp(stampPath(ConfigMgr2::ConfigDataType::Web));
    QVERIFY(stamp.open(QIODevice::WriteOnly | QIODevice::Truncate));
    stamp.write("0.0.1");
    stamp.close();
  }
  const QString blocker = webFolder + QStringLiteral("/marker.txt");
  QVERIFY(QFile::remove(blocker));
  QVERIFY(QDir().mkpath(blocker));

  {
    ConfigMgr2 mgr(m_configService);
    mgr.setExtraDataSourceRootOverrideForTesting(fixture);
    mgr.init();
    mgr.initAfterQtAppStarted();
    QCOMPARE(mgr.extraDataCopyFailures().size(), 1);
  }

  QFile file(userCss);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("body { color: red; }"));

  QVERIFY(QDir(blocker).removeRecursively());
}

// Pins the DELIBERATE decoupling: the data dump and the config migration are
// separate concerns, so a broken copy must NOT pin the config version to the
// old value forever (which would also re-run doVersionSpecificOverride on every
// launch). The per-folder stamp owns the retry instead.
void TestConfigMgr2::testExtraData_configVersionIsStampedEvenWhenACopyFailed() {
  resetInstalledExtraData();

  // Force a version change regardless of what previous runs persisted.
  QJsonObject metadata;
  metadata[QStringLiteral("version")] = QStringLiteral("0.0.1");
  QJsonObject mainConfig;
  mainConfig[QStringLiteral("metadata")] = metadata;
  QVERIFY(!m_configService->updateConfigByName(DataLocation::App, QStringLiteral("vnotex"),
                                               mainConfig));

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  const QString webFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Web);
  const QString blocker = webFolder + QStringLiteral("/marker.txt");
  QVERIFY(QDir().mkpath(blocker));

  ConfigMgr2 mgr(m_configService);
  mgr.setExtraDataSourceRootOverrideForTesting(fixture);
  mgr.init();
  QVERIFY2(mgr.isVersionChanged(), "the fixture did not produce a version change");

  mgr.initAfterQtAppStarted();

  QCOMPARE(mgr.extraDataCopyFailures().size(), 1);
  QCOMPARE(mgr.getConfig().getVersion(), ConfigMgr2::getApplicationVersion());

  QVERIFY(QDir(blocker).removeRecursively());
}

// ============ In-place preview sources ============

using Sources = MarkdownEditorConfig::InplacePreviewSources;
using Source = MarkdownEditorConfig::InplacePreviewSource;

void TestConfigMgr2::testInplacePreviewSources_jsonRoundTripCarriesTable() {
  MainConfig config(m_configMgr);
  auto &mdConfig = config.getEditorConfig().getMarkdownEditorConfig();

  mdConfig.setInplacePreviewSources(Sources(Source::ImageLink | Source::Table));

  const auto json = mdConfig.toJson();
  const auto persisted = json.value(QStringLiteral("inplacePreviewSources")).toString();
  QVERIFY2(persisted.contains(QStringLiteral("table")), qPrintable(persisted));
  QVERIFY(!persisted.contains(QStringLiteral("math")));

  MainConfig reloaded(m_configMgr);
  auto &reloadedMd = reloaded.getEditorConfig().getMarkdownEditorConfig();
  reloadedMd.fromJson(json);
  QCOMPARE(reloadedMd.getInplacePreviewSources(), Sources(Source::ImageLink | Source::Table));
}

void TestConfigMgr2::testInplacePreviewSources_persistedStringWinsOverTheCppDefault() {
  MainConfig config(m_configMgr);
  auto &mdConfig = config.getEditorConfig().getMarkdownEditorConfig();

  // What every pre-4.4.4 installation has on disk.
  QJsonObject json = mdConfig.toJson();
  json[QStringLiteral("inplacePreviewSources")] = QStringLiteral("imagelink;codeblock;math");
  mdConfig.fromJson(json);

  QCOMPARE(mdConfig.getInplacePreviewSources(),
           Sources(Source::ImageLink | Source::CodeBlock | Source::Math));
}

void TestConfigMgr2::testTableMigration_addsTableAndKeepsOtherChoices() {
  MainConfig config(m_configMgr);
  auto &mdConfig = config.getEditorConfig().getMarkdownEditorConfig();

  // A user who kept image links but turned code blocks and math off.
  mdConfig.setInplacePreviewSources(Sources(Source::ImageLink));

  config.doVersionSpecificOverride(QStringLiteral("4.4.3"));

  QCOMPARE(mdConfig.getInplacePreviewSources(), Sources(Source::ImageLink | Source::Table));
}

void TestConfigMgr2::testTableMigration_leavesABlanketOptOutAlone() {
  MainConfig config(m_configMgr);
  auto &mdConfig = config.getEditorConfig().getMarkdownEditorConfig();

  mdConfig.setInplacePreviewSources(Sources(Source::NoInplacePreview));

  config.doVersionSpecificOverride(QStringLiteral("4.4.3"));

  QCOMPARE(mdConfig.getInplacePreviewSources(), Sources(Source::NoInplacePreview));
}

void TestConfigMgr2::testTableMigration_doesNotRunOnTheSameOrANewerVersion() {
  MainConfig config(m_configMgr);
  auto &mdConfig = config.getEditorConfig().getMarkdownEditorConfig();

  // A later upgrade must not re-enable what the user turned off on 4.4.4+.
  mdConfig.setInplacePreviewSources(Sources(Source::ImageLink));
  config.doVersionSpecificOverride(QStringLiteral("4.4.4"));
  QCOMPARE(mdConfig.getInplacePreviewSources(), Sources(Source::ImageLink));

  config.doVersionSpecificOverride(QStringLiteral("4.5.0"));
  QCOMPARE(mdConfig.getInplacePreviewSources(), Sources(Source::ImageLink));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestConfigMgr2)
#include "test_configmgr2.moc"