#include <QtTest>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/error.h>
#include <core/mainconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/services/commenttypes.h>
#include <core/services/configcoreservice.h>
#include <core/webresource.h>
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
  void testPdfJsMigration_persistedListWinsOverTheCppDefault();
  void testPdfJsMigration_resetsTheV3ScriptListInMemoryAndOnSave();
  void testPdfJsMigration_doesNotRunOnTheSameOrANewerVersion();
  void testPdfJsMigration_realUpgradeRewritesTheConfigFile();
  void testPdfToolOptions_defaultsAndRoundTrips();

  void testPdfToolOptions_normalization_data();
  void testPdfToolOptions_normalization();

  void testAlignTableSource_jsonRoundTripAndAbsentKeyDefault();

  // Absent-key safety, which is provided by the defaults merge in ConfigMgr2::init().
  void testAbsentKeyKeepsTheCppDefaultForEveryField();
  void testAbsentSectionKeepsTheWholeSectionDefault();
  void testUserValueStillWinsOverTheDefault();
  void testWebResourceMergesPerKeyButReplacesArrays();
  void testAnExplicitlyEmptiedUserOwnedMapIsNotResurrected();
  void testAnExplicitNullDeletesTheKey();
  void testAnUnreadableConfigIsNeverOverwritten();

private:
  // Build an on-disk stand-in for the bundled vnote_extra.rcc tree.
  QString buildExtraDataFixture(TempDirFixture &p_tmp) const;

  // Write @p_onDisk as vnotex.json and load it back through a fresh ConfigMgr2, i.e. through
  // the real getConfigByNameWithDefaults() merge path. Returns the loaded MainConfig::toJson().
  QJsonObject loadThroughMergePath(const QJsonObject &p_onDisk) const;

  // Every leaf (non-object) key of @p_obj, as a path of key names.
  static QVector<QStringList> leafPaths(const QJsonObject &p_obj);

  static QJsonValue valueAt(const QJsonObject &p_obj, const QStringList &p_path);

  static QJsonObject withoutKeyAt(const QJsonObject &p_obj, const QStringList &p_path);

  static QString pathToString(const QStringList &p_path);

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

// The folders ensureExtraData() installs, in bundle order.
static const char *const kExtraFolders[] = {"themes", "tasks", "syntax-highlighting",
                                            "web",    "dicts", "templates"};

// The ConfigDataType of each entry in kExtraFolders, same order.
static const ConfigMgr2::ConfigDataType kExtraTypes[] = {
    ConfigMgr2::ConfigDataType::Themes,
    ConfigMgr2::ConfigDataType::Tasks,
    ConfigMgr2::ConfigDataType::SyntaxHighlighting,
    ConfigMgr2::ConfigDataType::Web,
    ConfigMgr2::ConfigDataType::Dicts,
    ConfigMgr2::ConfigDataType::Templates};

// Content of the bundled note template shipped in templates/.
static const char *const kBundledTemplateContent = "# %no%\n\n@@";

QString TestConfigMgr2::buildExtraDataFixture(TempDirFixture &p_tmp) const {
  const QString root = p_tmp.createDir("extra");
  for (const char *folder : kExtraFolders) {
    const QString name = QString::fromLatin1(folder);
    p_tmp.createDir(QStringLiteral("extra/") + name);
    p_tmp.createTextFile(QStringLiteral("extra/%1/marker.txt").arg(name),
                         QStringLiteral("bundled %1").arg(name));
  }

  // The bundled note template, asserted byte-for-byte after install.
  p_tmp.createTextFile(QStringLiteral("extra/templates/title.md"),
                       QString::fromLatin1(kBundledTemplateContent));

  // The one user-owned file the install must never clobber.
  p_tmp.createDir(QStringLiteral("extra/web/css"));
  p_tmp.createTextFile(QStringLiteral("extra/web/css/user.css"),
                       QStringLiteral("/* bundled stub */"));
  return root;
}

void TestConfigMgr2::resetInstalledExtraData() const {
  for (auto type : kExtraTypes) {
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

  for (auto type : kExtraTypes) {
    const QString folder = mgr.getConfigDataFolder(type);
    QVERIFY2(QFileInfo::exists(folder + QStringLiteral("/marker.txt")),
             qPrintable(QStringLiteral("not installed: %1").arg(folder)));
    QFile stamp(stampPath(type));
    QVERIFY2(stamp.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("no stamp in %1").arg(folder)));
    QCOMPARE(QString::fromUtf8(stamp.readAll()).trimmed(), ConfigMgr2::getApplicationVersion());
  }

  // The bundled note template lands byte-for-byte.
  QFile title(QDir(mgr.getConfigDataFolder(ConfigMgr2::ConfigDataType::Templates))
                  .filePath(QStringLiteral("title.md")));
  QVERIFY2(title.open(QIODevice::ReadOnly), "templates/title.md was not installed");
  QCOMPARE(title.readAll(), QByteArray(kBundledTemplateContent));
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

// A missing/unregisterable bundle must fail ALL folders and install
// nothing. Falling through (as the old code did) would let a tolerant copy
// stamp empty folders as complete -- strictly worse than the old bug.
void TestConfigMgr2::testExtraData_missingBundleFailsEveryFolderAndInstallsNothing() {
  resetInstalledExtraData();

  ConfigMgr2 mgr(m_configService);
  // No source override, so ensureExtraData() tries the production
  // "app:vnote_extra.rcc" -- which is not deployed next to the test binary.
  mgr.init();
  mgr.initAfterQtAppStarted();

  QCOMPARE(mgr.extraDataCopyFailures().size(),
           static_cast<int>(sizeof(kExtraFolders) / sizeof(kExtraFolders[0])));
  for (const auto &failure : mgr.extraDataCopyFailures()) {
    QVERIFY(!failure.m_folderName.isEmpty());
    QVERIFY(failure.m_errorMessage.contains(QStringLiteral("vnote_extra.rcc")));
  }

  for (auto type : kExtraTypes) {
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

void TestConfigMgr2::testAlignTableSource_jsonRoundTripAndAbsentKeyDefault() {
  MainConfig config(m_configMgr);
  auto &mdConfig = config.getEditorConfig().getMarkdownEditorConfig();

  QVERIFY2(!mdConfig.getAlignTableSourceEnabled(), "aligned table source must default to off");

  mdConfig.setAlignTableSourceEnabled(true);
  QJsonObject json = mdConfig.toJson();
  QCOMPARE(json.value(QStringLiteral("alignTableSource")).toBool(), true);

  MainConfig reloaded(m_configMgr);
  auto &reloadedMd = reloaded.getEditorConfig().getMarkdownEditorConfig();
  reloadedMd.fromJson(json);
  QCOMPARE(reloadedMd.getAlignTableSourceEnabled(), true);

  // An existing vnotex.json has no such key; it must read back as off.
  json.remove(QStringLiteral("alignTableSource"));
  reloadedMd.fromJson(json);
  QCOMPARE(reloadedMd.getAlignTableSourceEnabled(), false);
}

// ============ Absent-key safety (the defaults merge) ============

QJsonObject TestConfigMgr2::loadThroughMergePath(const QJsonObject &p_onDisk) const {
  const Error err =
      m_configService->updateConfigByName(DataLocation::App, QStringLiteral("vnotex"), p_onDisk);
  if (!err.isOk()) {
    qWarning() << "failed to write the vnotex.json fixture:" << err.message();
    return QJsonObject();
  }

  ConfigMgr2 mgr(m_configService);
  mgr.init();
  return mgr.getConfig().toJson();
}

QVector<QStringList> TestConfigMgr2::leafPaths(const QJsonObject &p_obj) {
  QVector<QStringList> paths;
  for (auto it = p_obj.begin(); it != p_obj.end(); ++it) {
    if (it.value().isObject()) {
      const auto childPaths = leafPaths(it.value().toObject());
      if (childPaths.isEmpty()) {
        // An empty object is itself a leaf.
        paths.append(QStringList{it.key()});
        continue;
      }
      for (const auto &child : childPaths) {
        paths.append(QStringList{it.key()} + child);
      }
    } else {
      paths.append(QStringList{it.key()});
    }
  }
  return paths;
}

QJsonValue TestConfigMgr2::valueAt(const QJsonObject &p_obj, const QStringList &p_path) {
  QJsonValue value(p_obj);
  for (const auto &key : p_path) {
    value = value.toObject().value(key);
  }
  return value;
}

QJsonObject TestConfigMgr2::withoutKeyAt(const QJsonObject &p_obj, const QStringList &p_path) {
  Q_ASSERT(!p_path.isEmpty());
  QJsonObject obj = p_obj;
  if (p_path.size() == 1) {
    obj.remove(p_path.first());
    return obj;
  }

  obj[p_path.first()] = withoutKeyAt(obj.value(p_path.first()).toObject(), p_path.mid(1));
  return obj;
}

QString TestConfigMgr2::pathToString(const QStringList &p_path) {
  return p_path.join(QLatin1Char('.'));
}

void TestConfigMgr2::testAbsentKeyKeepsTheCppDefaultForEveryField() {
  // The generic gate. Serialize a default-constructed MainConfig, then drop each leaf key in
  // turn, load the result through the real merge path, and assert the value still equals the
  // default. This is what makes adding a config field with a non-zero default safe: the
  // IConfig read helpers are not presence-aware, so absent-key safety comes entirely from the
  // defaults merge in ConfigMgr2::init().
  MainConfig defaults(m_configMgr);
  const QJsonObject defaultsJson = defaults.toJson();

  const auto paths = leafPaths(defaultsJson);
  QVERIFY2(paths.size() > 50, qPrintable(QStringLiteral("only %1 leaf keys found; the defaults "
                                                        "document looks wrong")
                                             .arg(paths.size())));

  for (const auto &path : paths) {
    // widget.newNoteDefaultTemplates is owned wholesale by the user (a present but empty
    // object means "none"), so it is deliberately restored from the RAW document and does
    // not follow per-key merge semantics. testAnExplicitlyEmptiedUserOwnedMapIsNotResurrected
    // covers it instead.
    if (path.size() > 1 && path.first() == QStringLiteral("widget") &&
        path.at(1) == QStringLiteral("newNoteDefaultTemplates")) {
      continue;
    }

    const QJsonObject loaded = loadThroughMergePath(withoutKeyAt(defaultsJson, path));
    QVERIFY2(!loaded.isEmpty(), "the config could not be loaded back");
    QVERIFY2(
        valueAt(loaded, path) == valueAt(defaultsJson, path),
        qPrintable(QStringLiteral("dropping \"%1\" did not keep its default (got %2, "
                                  "expected %3)")
                       .arg(pathToString(path),
                            QString::fromUtf8(QJsonDocument(QJsonArray{valueAt(loaded, path)})
                                                  .toJson(QJsonDocument::Compact)),
                            QString::fromUtf8(QJsonDocument(QJsonArray{valueAt(defaultsJson, path)})
                                                  .toJson(QJsonDocument::Compact)))));
  }
}

void TestConfigMgr2::testAbsentSectionKeepsTheWholeSectionDefault() {
  // A whole missing section (the shape a hand-truncated vnotex.json has) must not zero out
  // every field in it. merge_patch merges objects per key, so the section comes back whole.
  MainConfig defaults(m_configMgr);
  const QJsonObject defaultsJson = defaults.toJson();

  QJsonObject onDisk = defaultsJson;
  onDisk.remove(QStringLiteral("editor"));

  const QJsonObject loaded = loadThroughMergePath(onDisk);
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded.value(QStringLiteral("editor")), defaultsJson.value(QStringLiteral("editor")));
}

void TestConfigMgr2::testUserValueStillWinsOverTheDefault() {
  // The merge must not turn into "defaults always win": a persisted value, including a
  // non-default one nested inside the WebResource objects, has to survive.
  MainConfig defaults(m_configMgr);
  const QJsonObject defaultsJson = defaults.toJson();

  MainConfig source(m_configMgr);
  auto &sourceMd = source.getEditorConfig().getMarkdownEditorConfig();
  sourceMd.setAutoFoldPreviewedBlocksEnabled(false);
  sourceMd.setAlignTableSourceEnabled(true);

  const QJsonObject loaded = loadThroughMergePath(source.toJson());
  QVERIFY(!loaded.isEmpty());

  MainConfig reloaded(m_configMgr);
  reloaded.fromJson(loaded);
  auto &reloadedMd = reloaded.getEditorConfig().getMarkdownEditorConfig();
  QCOMPARE(reloadedMd.getAutoFoldPreviewedBlocksEnabled(), false);
  QCOMPARE(reloadedMd.getAlignTableSourceEnabled(), true);

  // And an untouched neighbour still reads as its default.
  QCOMPARE(valueAt(loaded, {QStringLiteral("editor"), QStringLiteral("markdown_editor"),
                            QStringLiteral("zoomFactorInReadMode")}),
           valueAt(defaultsJson, {QStringLiteral("editor"), QStringLiteral("markdown_editor"),
                                  QStringLiteral("zoomFactorInReadMode")}));
}

void TestConfigMgr2::testWebResourceMergesPerKeyButReplacesArrays() {
  // The nested WebResource objects used to be replaced wholesale by whatever the file held.
  // After the merge they merge per key - a custom "template" with no "resources" keeps the
  // default resources - while the "resources" ARRAY itself is still replaced wholesale
  // (RFC 7386 does not merge arrays element-wise).
  MainConfig defaults(m_configMgr);
  const QJsonObject defaultsJson = defaults.toJson();
  const QStringList mdPath{QStringLiteral("editor"), QStringLiteral("markdown_editor")};
  const QJsonObject defaultViewer =
      valueAt(defaultsJson, mdPath + QStringList{QStringLiteral("viewerResource")}).toObject();
  QVERIFY2(!defaultViewer.value(QStringLiteral("resources")).toArray().isEmpty(),
           "the default viewerResource carries no resources");

  // Case 1: only "template" on disk.
  {
    QJsonObject onDisk = defaultsJson;
    QJsonObject editor = onDisk.value(QStringLiteral("editor")).toObject();
    QJsonObject md = editor.value(QStringLiteral("markdown_editor")).toObject();
    md[QStringLiteral("viewerResource")] =
        QJsonObject{{QStringLiteral("template"), QStringLiteral("web/custom.html")}};
    editor[QStringLiteral("markdown_editor")] = md;
    onDisk[QStringLiteral("editor")] = editor;

    const QJsonObject loaded = loadThroughMergePath(onDisk);
    QVERIFY(!loaded.isEmpty());
    const QJsonObject viewer =
        valueAt(loaded, mdPath + QStringList{QStringLiteral("viewerResource")}).toObject();
    QCOMPARE(viewer.value(QStringLiteral("template")).toString(),
             QStringLiteral("web/custom.html"));
    QCOMPARE(viewer.value(QStringLiteral("resources")),
             defaultViewer.value(QStringLiteral("resources")));
  }

  // Case 2: only "resources" on disk - the array replaces, the template defaults.
  {
    QJsonObject onDisk = defaultsJson;
    QJsonObject editor = onDisk.value(QStringLiteral("editor")).toObject();
    QJsonObject md = editor.value(QStringLiteral("markdown_editor")).toObject();
    QJsonObject only{{QStringLiteral("name"), QStringLiteral("global_styles")},
                     {QStringLiteral("enabled"), true},
                     {QStringLiteral("styles"), QJsonArray{QStringLiteral("web/only.css")}},
                     {QStringLiteral("scripts"), QJsonArray{}}};
    md[QStringLiteral("viewerResource")] =
        QJsonObject{{QStringLiteral("resources"), QJsonArray{only}}};
    editor[QStringLiteral("markdown_editor")] = md;
    onDisk[QStringLiteral("editor")] = editor;

    const QJsonObject loaded = loadThroughMergePath(onDisk);
    QVERIFY(!loaded.isEmpty());
    const QJsonObject viewer =
        valueAt(loaded, mdPath + QStringList{QStringLiteral("viewerResource")}).toObject();
    QCOMPARE(viewer.value(QStringLiteral("resources")).toArray().size(), 1);
    QCOMPARE(viewer.value(QStringLiteral("template")).toString(),
             defaultViewer.value(QStringLiteral("template")).toString());
  }
}

void TestConfigMgr2::testAnExplicitlyEmptiedUserOwnedMapIsNotResurrected() {
  // widget.newNoteDefaultTemplates is owned wholesale by the user: a PRESENT but empty object
  // means "no default templates at all". merge_patch merges objects per key, so without the
  // user-owned-object restore in ConfigMgr2::init() the bundled Markdown entry would come
  // back and the setting could never be turned off.
  MainConfig defaults(m_configMgr);
  const QJsonObject defaultsJson = defaults.toJson();
  const QStringList path{QStringLiteral("widget"), QStringLiteral("newNoteDefaultTemplates")};
  QVERIFY2(!valueAt(defaultsJson, path).toObject().isEmpty(),
           "the bundled default mapping is expected to be non-empty");

  QJsonObject onDisk = withoutKeyAt(defaultsJson, path);
  QJsonObject widget = onDisk.value(QStringLiteral("widget")).toObject();
  widget[QStringLiteral("newNoteDefaultTemplates")] = QJsonObject();
  onDisk[QStringLiteral("widget")] = widget;

  const QJsonObject loaded = loadThroughMergePath(onDisk);
  QVERIFY(!loaded.isEmpty());
  QVERIFY2(valueAt(loaded, path).toObject().isEmpty(),
           "an explicitly emptied newNoteDefaultTemplates was resurrected by the merge");

  // A user-chosen mapping survives too, without the bundled entries leaking back in.
  widget[QStringLiteral("newNoteDefaultTemplates")] =
      QJsonObject{{QStringLiteral("Text"), QStringLiteral("mine.txt")}};
  onDisk[QStringLiteral("widget")] = widget;
  const QJsonObject loaded2 = loadThroughMergePath(onDisk);
  QVERIFY(!loaded2.isEmpty());
  QCOMPARE(valueAt(loaded2, path).toObject().size(), 1);
  QCOMPARE(valueAt(loaded2, path).toObject().value(QStringLiteral("Text")).toString(),
           QStringLiteral("mine.txt"));
}

void TestConfigMgr2::testAnExplicitNullDeletesTheKey() {
  // Pinning merge_patch's (RFC 7386) null semantics: a JSON null DELETES the key rather than
  // selecting the default, so the read helper's zero value applies. Nothing VNote writes ever
  // emits null - this documents the one hole in absent-key safety so a future change that
  // starts emitting nulls is caught here rather than in the field.
  MainConfig defaults(m_configMgr);
  const QJsonObject defaultsJson = defaults.toJson();

  const QStringList path{QStringLiteral("editor"), QStringLiteral("markdown_editor"),
                         QStringLiteral("autoFoldPreviewedBlocks")};
  QCOMPARE(valueAt(defaultsJson, path).toBool(), true);

  QJsonObject onDisk = defaultsJson;
  QJsonObject editor = onDisk.value(QStringLiteral("editor")).toObject();
  QJsonObject md = editor.value(QStringLiteral("markdown_editor")).toObject();
  md[QStringLiteral("autoFoldPreviewedBlocks")] = QJsonValue(QJsonValue::Null);
  editor[QStringLiteral("markdown_editor")] = md;
  onDisk[QStringLiteral("editor")] = editor;

  const QJsonObject loaded = loadThroughMergePath(onDisk);
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(valueAt(loaded, path).toBool(), false);
}

void TestConfigMgr2::testAnUnreadableConfigIsNeverOverwritten() {
  // A present-but-unparseable vnotex.json must not be mistaken for an absent one: the merge
  // hands back the defaults so the app can still run, but ConfigMgr2 must then refuse EVERY
  // main-config write for the session, or the user's settings would be replaced by defaults
  // the moment anything touched the config.
  const QString configFile =
      QDir(m_configService->getDataPath(DataLocation::App)).filePath(QStringLiteral("vnotex.json"));

  // Drain any debounced write the shared manager still owes (constructing a MainConfig on it
  // schedules one), or it would land on our corrupt fixture mid-test.
  const QJsonObject healthy = m_configMgr->getConfig().toJson();
  QTest::qWait(700);

  const QByteArray corrupt("{ this is not json");
  const auto writeCorrupt = [&]() {
    QFile file(configFile);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(corrupt) == static_cast<qint64>(corrupt.size());
  };
  const auto onDiskBytes = [&]() {
    QFile file(configFile);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<unreadable>");
  };
  // Whatever happens below, do not leave the corrupt fixture behind for the next test.
  const auto restore = qScopeGuard([&]() {
    m_configService->updateConfigByName(DataLocation::App, QStringLiteral("vnotex"), healthy);
  });

  QVERIFY(writeCorrupt());

  // Path 1: the debounced timer fires while the manager is alive.
  {
    ConfigMgr2 mgr(m_configService);
    mgr.init();

    // Running on defaults, and no upgrade migration is attempted.
    QVERIFY(!mgr.isVersionChanged());
    QVERIFY(mgr.getConfig()
                .getEditorConfig()
                .getMarkdownEditorConfig()
                .getAutoFoldPreviewedBlocksEnabled());

    mgr.getConfig().getEditorConfig().getMarkdownEditorConfig().setAlignTableSourceEnabled(true);
    QTest::qWait(700);
    QCOMPARE(onDiskBytes(), corrupt);
  }
  QCOMPARE(onDiskBytes(), corrupt);

  // Path 2: the manager is destroyed with the write still PENDING, so the destructor's flush
  // is the one that has to refuse.
  QVERIFY(writeCorrupt());
  {
    ConfigMgr2 mgr(m_configService);
    mgr.init();
    mgr.getConfig().getEditorConfig().getMarkdownEditorConfig().setAlignTableSourceEnabled(true);
    // No qWait: the 500 ms timer is still armed when the manager goes out of scope.
  }
  QCOMPARE(onDiskBytes(), corrupt);

  // Path 3: syntactically valid JSON that is not an OBJECT. It parses, so it must not be
  // mistaken for an empty config and overwritten either.
  const QByteArray notAnObject("[1, 2, 3]");
  {
    QFile file(configFile);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(notAnObject), static_cast<qint64>(notAnObject.size()));
  }
  {
    ConfigMgr2 mgr(m_configService);
    mgr.init();
    QVERIFY(!mgr.isVersionChanged());
    mgr.getConfig().getEditorConfig().getMarkdownEditorConfig().setAlignTableSourceEnabled(true);
    QTest::qWait(700);
  }
  QCOMPARE(onDiskBytes(), notAnObject);
}

// ============ pdf.js v6 migration (4.6.0) ============

namespace {

// The exact `editor.pdf_viewer.viewerResource` object every pre-4.6.0
// installation has on disk (pdf.js v3.11.174, classic scripts).
QJsonObject legacyPdfViewerResourceJson() {
  auto makeResource = [](const QString &p_name, const QStringList &p_scripts,
                         const QStringList &p_styles) {
    QJsonObject obj;
    obj[QStringLiteral("name")] = p_name;
    obj[QStringLiteral("enabled")] = true;
    obj[QStringLiteral("scripts")] = QJsonArray::fromStringList(p_scripts);
    obj[QStringLiteral("styles")] = QJsonArray::fromStringList(p_styles);
    return obj;
  };

  QJsonArray resources;
  resources.append(makeResource(
      QStringLiteral("built_in"),
      {QStringLiteral("web/js/qwebchannel.js"), QStringLiteral("web/js/eventemitter.js"),
       QStringLiteral("web/js/utils.js"), QStringLiteral("web/js/vxcore.js"),
       QStringLiteral("web/pdf.js/pdfviewercore.js")},
      {}));
  resources.append(makeResource(
      QStringLiteral("pdf.js"),
      {QStringLiteral("web/pdf.js/build/pdf.js"), QStringLiteral("web/pdf.js/web/viewer.js")},
      {QStringLiteral("web/pdf.js/web/viewer.css")}));
  resources.append(makeResource(QStringLiteral("pdf_viewer"),
                                {QStringLiteral("web/pdf.js/pdfviewer.js")},
                                {QStringLiteral("web/pdf.js/pdfviewer.css")}));

  QJsonObject viewerResource;
  viewerResource[QStringLiteral("template")] =
      QStringLiteral("web/pdf.js/web/pdf-viewer-template.html");
  viewerResource[QStringLiteral("resources")] = resources;

  QJsonObject section;
  section[QStringLiteral("viewerResource")] = viewerResource;
  return section;
}

QStringList allScripts(const WebResource &p_resource) {
  QStringList out;
  for (const auto &r : p_resource.m_resources) {
    out += r.m_scripts;
  }
  return out;
}

} // namespace

// The load-bearing precondition of the whole migration: WebResource::init takes
// the persisted object WHOLESALE. If this ever stopped being true the override
// below could be relaxed; while it IS true, the override is mandatory.
void TestConfigMgr2::testPdfJsMigration_persistedListWinsOverTheCppDefault() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  pdfConfig.fromJson(legacyPdfViewerResourceJson());

  const auto scripts = allScripts(pdfConfig.getViewerResource());
  QVERIFY2(scripts.contains(QStringLiteral("web/pdf.js/build/pdf.js")),
           "the persisted v3 list must survive fromJson, or this test proves nothing");
  QVERIFY(!scripts.contains(QStringLiteral("web/pdf.js/build/pdf.mjs")));
}

void TestConfigMgr2::testPdfJsMigration_resetsTheV3ScriptListInMemoryAndOnSave() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  pdfConfig.fromJson(legacyPdfViewerResourceJson());
  config.doVersionSpecificOverride(QStringLiteral("4.5.0"));

  const auto scripts = allScripts(pdfConfig.getViewerResource());
  // The v3 entries are gone...
  QVERIFY2(
      !scripts.contains(QStringLiteral("web/pdf.js/build/pdf.js")),
      qPrintable(
          QStringLiteral("stale v3 script survived: %1").arg(scripts.join(QStringLiteral(", ")))));
  QVERIFY(!scripts.contains(QStringLiteral("web/pdf.js/web/viewer.js")));
  QVERIFY(!scripts.contains(QStringLiteral("web/pdf.js/pdfviewer.js")));
  // ...and the v6 ones are in.
  QVERIFY(scripts.contains(QStringLiteral("web/pdf.js/build/pdf.mjs")));
  QVERIFY(scripts.contains(QStringLiteral("web/pdf.js/web/viewer.mjs")));
  QVERIFY(scripts.contains(QStringLiteral("web/pdf.js/pdfviewer.mjs")));

  // The reset must also be what gets PERSISTED, or the next launch reloads v3.
  const QString persisted =
      QString::fromUtf8(QJsonDocument(pdfConfig.toJson()).toJson(QJsonDocument::Compact));
  QVERIFY2(persisted.contains(QStringLiteral("web/pdf.js/build/pdf.mjs")), qPrintable(persisted));
  QVERIFY2(!persisted.contains(QStringLiteral("web/pdf.js/build/pdf.js\"")), qPrintable(persisted));
}

void TestConfigMgr2::testPdfJsMigration_doesNotRunOnTheSameOrANewerVersion() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  pdfConfig.fromJson(legacyPdfViewerResourceJson());

  config.doVersionSpecificOverride(QStringLiteral("4.6.0"));
  QVERIFY(allScripts(pdfConfig.getViewerResource())
              .contains(QStringLiteral("web/pdf.js/build/pdf.js")));

  config.doVersionSpecificOverride(QStringLiteral("4.7.0"));
  QVERIFY(allScripts(pdfConfig.getViewerResource())
              .contains(QStringLiteral("web/pdf.js/build/pdf.js")));
}

// The three cases above drive doVersionSpecificOverride() directly. This one
// drives the PRODUCTION path end to end — an old vnotex.json on disk, a real
// ConfigMgr2::init() + initAfterQtAppStarted(), and the debounced write back —
// because that wiring (m_versionChanged, override-before-stamp, MainConfig::update)
// is what actually has to reach an upgrading user, and none of it is exercised
// by constructing a MainConfig by hand.
void TestConfigMgr2::testPdfJsMigration_realUpgradeRewritesTheConfigFile() {
  resetInstalledExtraData();

  // A complete pre-4.6.0 vnotex.json: an old version stamp plus the v3 script list.
  QJsonObject metadata;
  metadata[QStringLiteral("version")] = QStringLiteral("4.5.0");
  QJsonObject editor;
  editor[QStringLiteral("pdf_viewer")] = legacyPdfViewerResourceJson();
  QJsonObject mainConfig;
  mainConfig[QStringLiteral("metadata")] = metadata;
  mainConfig[QStringLiteral("editor")] = editor;
  QVERIFY(!m_configService->updateConfigByName(DataLocation::App, QStringLiteral("vnotex"),
                                               mainConfig));

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  {
    ConfigMgr2 mgr(m_configService);
    mgr.setExtraDataSourceRootOverrideForTesting(fixture);
    mgr.init();
    QVERIFY2(mgr.isVersionChanged(), "the fixture did not produce a version change");

    // The persisted v3 list must have been loaded, or the migration below would
    // be proving nothing.
    QVERIFY(allScripts(mgr.getConfig().getEditorConfig().getPdfViewerConfig().getViewerResource())
                .contains(QStringLiteral("web/pdf.js/build/pdf.js")));

    mgr.initAfterQtAppStarted();

    QCOMPARE(mgr.getConfig().getVersion(), ConfigMgr2::getApplicationVersion());
    const auto scripts =
        allScripts(mgr.getConfig().getEditorConfig().getPdfViewerConfig().getViewerResource());
    QVERIFY(!scripts.contains(QStringLiteral("web/pdf.js/build/pdf.js")));
    QVERIFY(scripts.contains(QStringLiteral("web/pdf.js/build/pdf.mjs")));

    // MainConfig::update() goes through ConfigMgr2's debounced writer, so the
    // file only changes once the timer fires.
    QTest::qWait(1500);
  }

  // Reload from disk exactly as the next launch would.
  const auto persisted =
      m_configService->getConfigByName(DataLocation::App, QStringLiteral("vnotex"));
  QVERIFY2(!persisted.isEmpty(), "the upgraded config was never written back");
  QCOMPARE(MainConfig::peekVersion(persisted), ConfigMgr2::getApplicationVersion());

  ConfigMgr2 reloaded(m_configService);
  reloaded.init();
  const auto reloadedScripts =
      allScripts(reloaded.getConfig().getEditorConfig().getPdfViewerConfig().getViewerResource());
  QVERIFY2(!reloadedScripts.contains(QStringLiteral("web/pdf.js/build/pdf.js")),
           qPrintable(QStringLiteral("the v3 list came back from disk: %1")
                          .arg(reloadedScripts.join(QStringLiteral(", ")))));
  QVERIFY(reloadedScripts.contains(QStringLiteral("web/pdf.js/build/pdf.mjs")));
  QVERIFY(reloadedScripts.contains(QStringLiteral("web/pdf.js/web/viewer.mjs")));
  QVERIFY(reloadedScripts.contains(QStringLiteral("web/pdf.js/pdfviewer.mjs")));
  // And a second launch must not be treated as another upgrade.
  QVERIFY(!reloaded.isVersionChanged());
}

// Per-tool authoring options are persisted so the toolbar comes back armed the
// way it was left, and each tool owns its OWN colour. The colour is a semantic
// token, never a literal; the scalars are clamped to the anchor validators'
// bounds so config can never express an anchor those validators would reject.
//
// The normalization table under test (see the plan's Task 0):
//   key absent / wrong type -> default; invalid colour -> default;
//   non-finite scalar -> default; out-of-range scalar -> CLAMPED.
void TestConfigMgr2::testPdfToolOptions_defaultsAndRoundTrips() {
  const auto highlight = PdfToolOptions::highlightTool();
  const auto ink = PdfToolOptions::inkTool();
  const auto freetext = PdfToolOptions::freeTextTool();

  {
    MainConfig config(m_configMgr);
    auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

    // Absent key -> C++ defaults, which is what makes this safe to add without
    // a config migration.
    for (const auto &tool : PdfViewerConfig::toolNames()) {
      QCOMPARE(pdfConfig.getToolOptions(tool).m_color, CommentColor::defaultToken());
    }
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, PdfToolOptions::defaultWidth());
    QCOMPARE(pdfConfig.getToolOptions(freetext).m_fontSize, PdfToolOptions::defaultFontSize());
    // Opacity is ink-only, defaults to solid, and needs no config migration:
    // an existing vnotex.json simply has no `opacity` key.
    QCOMPARE(pdfConfig.getToolOptions(ink).m_opacity, PdfToolOptions::defaultOpacity());

    pdfConfig.fromJson(QJsonObject());
    QCOMPARE(pdfConfig.getToolOptions(highlight).m_color, CommentColor::defaultToken());

    // === Per-tool independence (requirement 4) ===
    auto inkOptions = pdfConfig.getToolOptions(ink);
    inkOptions.m_color = QStringLiteral("blue");
    inkOptions.m_width = 3.0;
    inkOptions.m_opacity = 0.4;
    pdfConfig.setToolOptions(ink, inkOptions);

    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, QStringLiteral("blue"));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, 3.0);
    QCOMPARE(pdfConfig.getToolOptions(ink).m_opacity, 0.4);
    // The other two are untouched: a shared-value regression would break this
    // silently otherwise.
    QCOMPARE(pdfConfig.getToolOptions(highlight).m_color, CommentColor::defaultToken());
    QCOMPARE(pdfConfig.getToolOptions(freetext).m_color, CommentColor::defaultToken());

    auto textOptions = pdfConfig.getToolOptions(freetext);
    textOptions.m_color = QStringLiteral("purple");
    textOptions.m_fontSize = 16.0;
    pdfConfig.setToolOptions(freetext, textOptions);
    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, QStringLiteral("blue"));

    // Round-trips through toJson/fromJson, exactly.
    MainConfig reloaded(m_configMgr);
    auto &reloadedPdf = reloaded.getEditorConfig().getPdfViewerConfig();
    reloadedPdf.fromJson(pdfConfig.toJson());
    QCOMPARE(reloadedPdf.getToolOptions(ink).m_color, QStringLiteral("blue"));
    QCOMPARE(reloadedPdf.getToolOptions(ink).m_width, 3.0);
    QCOMPARE(reloadedPdf.getToolOptions(ink).m_opacity, 0.4);
    QCOMPARE(reloadedPdf.getToolOptions(freetext).m_color, QStringLiteral("purple"));
    QCOMPARE(reloadedPdf.getToolOptions(freetext).m_fontSize, 16.0);
    QCOMPARE(reloadedPdf.getToolOptions(highlight).m_color, CommentColor::defaultToken());

    // The serialized JSON carries the same values, and only the keys the tool
    // actually owns.
    const auto tools = pdfConfig.toJson().value(QStringLiteral("tools")).toObject();
    QCOMPARE(tools.value(ink).toObject().value(QStringLiteral("width")).toDouble(), 3.0);
    QCOMPARE(tools.value(ink).toObject().value(QStringLiteral("opacity")).toDouble(), 0.4);
    QVERIFY(!tools.value(highlight).toObject().contains(QStringLiteral("width")));
    QVERIFY(!tools.value(highlight).toObject().contains(QStringLiteral("fontSize")));
    // Opacity belongs to ink alone.
    QVERIFY(!tools.value(highlight).toObject().contains(QStringLiteral("opacity")));
    QVERIFY(!tools.value(freetext).toObject().contains(QStringLiteral("opacity")));
  }

  {
    // Normalization, asserting the EXACT resulting value -- "the anchor still
    // validates" would pass for both a default and a clamped value and could
    // not distinguish the two policies.
    MainConfig config(m_configMgr);
    auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

    const auto build = [&](const QJsonObject &p_inkObj) {
      QJsonObject tools;
      tools.insert(ink, p_inkObj);
      QJsonObject obj;
      obj.insert(QStringLiteral("tools"), tools);
      return obj;
    };

    // An invalid or literal colour falls back to the default, so a hand-edited
    // config cannot produce an unstyled highlight.
    QJsonObject inkObj;
    inkObj.insert(QStringLiteral("color"), QStringLiteral("#ff00ff"));
    pdfConfig.fromJson(build(inkObj));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, CommentColor::defaultToken());

    inkObj.insert(QStringLiteral("color"), QStringLiteral("chartreuse"));
    pdfConfig.fromJson(build(inkObj));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, CommentColor::defaultToken());

    // Wrong JSON type is treated as absent.
    inkObj.insert(QStringLiteral("color"), QJsonValue(7));
    inkObj.insert(QStringLiteral("width"), QStringLiteral("3.0"));
    pdfConfig.fromJson(build(inkObj));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, CommentColor::defaultToken());
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, PdfToolOptions::defaultWidth());

    // Out of range CLAMPS: "width 1e9" plainly means "as thick as possible".
    inkObj.insert(QStringLiteral("color"), QStringLiteral("green"));
    inkObj.insert(QStringLiteral("width"), 1.0e9);
    pdfConfig.fromJson(build(inkObj));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, PdfInkAnchor::maxWidth());

    inkObj.insert(QStringLiteral("width"), -5.0);
    pdfConfig.fromJson(build(inkObj));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, PdfInkAnchor::minWidth());

    // Non-finite carries no intent to preserve, so it takes the DEFAULT rather
    // than a bound.
    inkObj.insert(QStringLiteral("width"), QJsonValue(qQNaN()));
    pdfConfig.fromJson(build(inkObj));
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, PdfToolOptions::defaultWidth());

    // Font size, same table.
    {
      QJsonObject tools;
      QJsonObject textObj;
      textObj.insert(QStringLiteral("fontSize"), 1000.0);
      tools.insert(freetext, textObj);
      QJsonObject obj;
      obj.insert(QStringLiteral("tools"), tools);
      pdfConfig.fromJson(obj);
      QCOMPARE(pdfConfig.getToolOptions(freetext).m_fontSize, PdfFreeTextAnchor::maxFontSize());
    }

    // fromJson RESETS before overlaying, so a second call cannot retain stale
    // state from the first: the ink colour set two calls ago is gone.
    pdfConfig.fromJson(QJsonObject());
    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, CommentColor::defaultToken());
    QCOMPARE(pdfConfig.getToolOptions(freetext).m_fontSize, PdfToolOptions::defaultFontSize());

    // The setter normalizes too, rather than storing garbage.
    auto bad = pdfConfig.getToolOptions(ink);
    bad.m_color = QStringLiteral("not-a-token");
    bad.m_width = 1.0e9;
    pdfConfig.setToolOptions(ink, bad);
    QCOMPARE(pdfConfig.getToolOptions(ink).m_color, CommentColor::defaultToken());
    QCOMPARE(pdfConfig.getToolOptions(ink).m_width, PdfInkAnchor::maxWidth());
  }
}

// The Task 0 normalization table, one row per class, asserted on BOTH the
// getter and the serialized JSON.
//
// The serialized value matters independently: a getter that normalizes while
// toJson() writes the raw value would persist something fromJson() then
// rewrites, and the config would silently change under the user across a
// restart. (The adapter payload for the same table is gated by
// test_pdfvieweradapter_comments, which shares the PdfToolOptions choke point.)
void TestConfigMgr2::testPdfToolOptions_normalization_data() {
  QTest::addColumn<QString>("tool");
  QTest::addColumn<QString>("key");
  QTest::addColumn<QJsonValue>("input");
  QTest::addColumn<QJsonValue>("expected");

  const auto ink = PdfToolOptions::inkTool();
  const auto freetext = PdfToolOptions::freeTextTool();
  const auto highlight = PdfToolOptions::highlightTool();
  const auto colorKey = PdfToolOptions::colorKey();
  const auto widthKey = PdfToolOptions::widthKey();
  const auto sizeKey = PdfToolOptions::fontSizeKey();

  const QJsonValue defaultColor(CommentColor::defaultToken());
  const QJsonValue defaultWidth(PdfToolOptions::defaultWidth());
  const QJsonValue defaultSize(PdfToolOptions::defaultFontSize());

  // --- colour ---
  QTest::newRow("colour valid") << ink << colorKey << QJsonValue(QStringLiteral("blue"))
                                << QJsonValue(QStringLiteral("blue"));
  QTest::newRow("colour absent") << ink << colorKey << QJsonValue(QJsonValue::Undefined)
                                 << defaultColor;
  QTest::newRow("colour literal hex")
      << ink << colorKey << QJsonValue(QStringLiteral("#ff00ff")) << defaultColor;
  QTest::newRow("colour css name")
      << ink << colorKey << QJsonValue(QStringLiteral("chartreuse")) << defaultColor;
  QTest::newRow("colour wrong type") << ink << colorKey << QJsonValue(7) << defaultColor;
  QTest::newRow("colour null") << ink << colorKey << QJsonValue(QJsonValue::Null) << defaultColor;
  QTest::newRow("colour on highlight")
      << highlight << colorKey << QJsonValue(QStringLiteral("pink"))
      << QJsonValue(QStringLiteral("pink"));

  // --- ink width ---
  QTest::newRow("width in range") << ink << widthKey << QJsonValue(3.0) << QJsonValue(3.0);
  QTest::newRow("width absent") << ink << widthKey << QJsonValue(QJsonValue::Undefined)
                                << defaultWidth;
  QTest::newRow("width wrong type")
      << ink << widthKey << QJsonValue(QStringLiteral("3.0")) << defaultWidth;
  // Non-finite carries no intent to preserve -> DEFAULT, not a bound.
  QTest::newRow("width nan") << ink << widthKey << QJsonValue(qQNaN()) << defaultWidth;
  QTest::newRow("width +inf") << ink << widthKey << QJsonValue(qInf()) << defaultWidth;
  QTest::newRow("width -inf") << ink << widthKey << QJsonValue(-qInf()) << defaultWidth;
  // Finite but out of range -> CLAMPED: "width 1e9" plainly means "as thick as
  // possible", and the bound is the anchor validator's.
  QTest::newRow("width above max")
      << ink << widthKey << QJsonValue(1.0e9) << QJsonValue(PdfInkAnchor::maxWidth());
  QTest::newRow("width below min")
      << ink << widthKey << QJsonValue(-5.0) << QJsonValue(PdfInkAnchor::minWidth());
  QTest::newRow("width exactly max") << ink << widthKey << QJsonValue(PdfInkAnchor::maxWidth())
                                     << QJsonValue(PdfInkAnchor::maxWidth());
  QTest::newRow("width exactly min") << ink << widthKey << QJsonValue(PdfInkAnchor::minWidth())
                                     << QJsonValue(PdfInkAnchor::minWidth());

  // --- free-text font size ---
  QTest::newRow("size in range") << freetext << sizeKey << QJsonValue(16.0) << QJsonValue(16.0);
  QTest::newRow("size absent") << freetext << sizeKey << QJsonValue(QJsonValue::Undefined)
                               << defaultSize;
  QTest::newRow("size wrong type") << freetext << sizeKey << QJsonValue(true) << defaultSize;
  QTest::newRow("size nan") << freetext << sizeKey << QJsonValue(qQNaN()) << defaultSize;
  QTest::newRow("size +inf") << freetext << sizeKey << QJsonValue(qInf()) << defaultSize;
  QTest::newRow("size above max") << freetext << sizeKey << QJsonValue(1.0e9)
                                  << QJsonValue(PdfFreeTextAnchor::maxFontSize());
  QTest::newRow("size below min") << freetext << sizeKey << QJsonValue(0.0)
                                  << QJsonValue(PdfFreeTextAnchor::minFontSize());
  QTest::newRow("size exactly min")
      << freetext << sizeKey << QJsonValue(PdfFreeTextAnchor::minFontSize())
      << QJsonValue(PdfFreeTextAnchor::minFontSize());

  // --- ink opacity (same table as the width) ---
  const auto opacityKey = PdfToolOptions::opacityKey();
  const QJsonValue defaultOpacity(PdfToolOptions::defaultOpacity());
  QTest::newRow("opacity in range") << ink << opacityKey << QJsonValue(0.35) << QJsonValue(0.35);
  QTest::newRow("opacity absent") << ink << opacityKey << QJsonValue(QJsonValue::Undefined)
                                  << defaultOpacity;
  QTest::newRow("opacity wrong type")
      << ink << opacityKey << QJsonValue(QStringLiteral("0.5")) << defaultOpacity;
  QTest::newRow("opacity nan") << ink << opacityKey << QJsonValue(qQNaN()) << defaultOpacity;
  QTest::newRow("opacity above max")
      << ink << opacityKey << QJsonValue(1.0e9) << QJsonValue(PdfInkAnchor::maxOpacity());
  QTest::newRow("opacity below min")
      << ink << opacityKey << QJsonValue(0.0) << QJsonValue(PdfInkAnchor::minOpacity());
}

void TestConfigMgr2::testPdfToolOptions_normalization() {
  QFETCH(QString, tool);
  QFETCH(QString, key);
  QFETCH(QJsonValue, input);
  QFETCH(QJsonValue, expected);

  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  QJsonObject toolObj;
  if (!input.isUndefined()) {
    toolObj.insert(key, input);
  }
  QJsonObject tools;
  tools.insert(tool, toolObj);
  QJsonObject obj;
  obj.insert(QStringLiteral("tools"), tools);

  pdfConfig.fromJson(obj);

  // QJsonValue is not debug-streamable, so compare a canonical serialization.
  const auto canonical = [](const QJsonValue &p_value) {
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("v"), p_value}}).toJson(QJsonDocument::Compact));
  };

  // 1. the getter
  const auto options = pdfConfig.getToolOptions(tool);
  const QJsonValue fromGetter = key == PdfToolOptions::colorKey()   ? QJsonValue(options.m_color)
                                : key == PdfToolOptions::widthKey() ? QJsonValue(options.m_width)
                                : key == PdfToolOptions::opacityKey()
                                    ? QJsonValue(options.m_opacity)
                                    : QJsonValue(options.m_fontSize);
  QCOMPARE(canonical(fromGetter), canonical(expected));

  // 2. the serialized JSON
  const auto serialized =
      pdfConfig.toJson().value(QStringLiteral("tools")).toObject().value(tool).toObject();
  QCOMPARE(canonical(serialized.value(key)), canonical(expected));

  // 3. and it is a FIXED POINT: re-reading what was written changes nothing.
  QJsonObject reTools;
  reTools.insert(tool, serialized);
  QJsonObject reObj;
  reObj.insert(QStringLiteral("tools"), reTools);
  pdfConfig.fromJson(reObj);
  QCOMPARE(canonical(pdfConfig.toJson()
                         .value(QStringLiteral("tools"))
                         .toObject()
                         .value(tool)
                         .toObject()
                         .value(key)),
           canonical(expected));
}

} // namespace tests
QTEST_GUILESS_MAIN(tests::TestConfigMgr2)
#include "test_configmgr2.moc"
