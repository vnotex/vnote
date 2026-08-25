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

} // namespace tests
QTEST_GUILESS_MAIN(tests::TestConfigMgr2)
#include "test_configmgr2.moc"