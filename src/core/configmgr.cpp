#include "configmgr.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPixmap>
#include <QResource>
#include <QScopeGuard>
#include <QScopedPointer>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QTimer>

#include <vxcore/vxcore.h>

#include "exception.h"
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/utils.h>

#include "coreconfig.h"
#include "mainconfig.h"
#include "sessionconfig.h"

using namespace vnotex;

#ifndef QT_NO_DEBUG
// #define VX_DEBUG_WEB
#endif

const QString ConfigMgr::c_orgName = QStringLiteral("VNoteX");

const QString ConfigMgr::c_appName = QStringLiteral("VNote");

static constexpr char c_configFileName[] = "vnotex.json";

static constexpr char c_sessionFileName[] = "session.json";

static constexpr char c_appFilesFolder[] = "vnotex_files";

static constexpr char c_userFilesFolder[] = "vnotex_user";

ConfigMgr::ConfigMgr(QObject *p_parent)
    : QObject(p_parent),
      m_config(new MainConfig(this)),
      m_sessionConfig(new SessionConfig(this)),
      m_vxcoreContext(nullptr),
      m_mainConfigWriteTimer(nullptr),
      m_sessionConfigWriteTimer(nullptr) {

  // Initialize vxcore context early
  VxCoreError err = vxcore_context_create(nullptr, &m_vxcoreContext);
  if (err != VXCORE_OK) {
    qCritical() << "Failed to create vxcore context:" << vxcore_error_message(err);
  } else {
    // Set VNote's app info so vxcore uses VNote's config paths
    vxcore_set_app_info(c_orgName.toUtf8().constData(), c_appName.toUtf8().constData());
  }

  // Create debounced write timers
  m_mainConfigWriteTimer = new QTimer(this);
  m_mainConfigWriteTimer->setSingleShot(true);
  m_mainConfigWriteTimer->setInterval(500);
  connect(m_mainConfigWriteTimer, &QTimer::timeout, this, &ConfigMgr::doWriteMainConfig);

  m_sessionConfigWriteTimer = new QTimer(this);
  m_sessionConfigWriteTimer->setSingleShot(true);
  m_sessionConfigWriteTimer->setInterval(500);
  connect(m_sessionConfigWriteTimer, &QTimer::timeout, this, &ConfigMgr::doWriteSessionConfig);

  locateConfigFolder();

  bool needUpdate = checkAppConfig();
  if (needUpdate) {
    checkUserConfig();
  }

  // Load and initialize main config
  {
    QString defaultConfigPath = getDefaultConfigFilePath();
    QString appConfigPath = getConfigFilePath(Source::App);
    
    // Read default config from Qt resources
    QByteArray defaultBytes = FileUtils::readFile(defaultConfigPath);
    QString defaultJson = QString::fromUtf8(defaultBytes);
    
    // Load and merge with app config using vxcore
    char *mergedJson = nullptr;
    VxCoreError err = vxcore_json_load_with_defaults(
        m_vxcoreContext,
        appConfigPath.toUtf8().constData(),
        defaultJson.toUtf8().constData(),
        &mergedJson);
    
    if (err == VXCORE_OK && mergedJson) {
      QJsonDocument doc = QJsonDocument::fromJson(QByteArray(mergedJson));
      m_config->fromJson(doc.object());
      vxcore_string_free(mergedJson);
    } else {
      qCritical() << "Failed to load main config:" << vxcore_error_message(err);
      // Fallback to default config
      QJsonDocument doc = QJsonDocument::fromJson(defaultBytes);
      m_config->fromJson(doc.object());
    }
  }

  // Load and initialize session config
  {
    QString sessionConfigPath = getConfigFilePath(Source::Session);
    
    // Session config has minimal defaults (empty object)
    QString defaultSessionJson = QStringLiteral("{}");
    
    // Load session config using vxcore
    char *mergedJson = nullptr;
    VxCoreError err = vxcore_json_load_with_defaults(
        m_vxcoreContext,
        sessionConfigPath.toUtf8().constData(),
        defaultSessionJson.toUtf8().constData(),
        &mergedJson);
    
    if (err == VXCORE_OK && mergedJson) {
      QJsonDocument doc = QJsonDocument::fromJson(QByteArray(mergedJson));
      m_sessionConfig->fromJson(doc.object());
      vxcore_string_free(mergedJson);
    } else {
      // Session config may not exist on first run - that's fine
      qDebug() << "No existing session config, using defaults";
      QJsonDocument doc = QJsonDocument::fromJson(defaultSessionJson.toUtf8());
      m_sessionConfig->fromJson(doc.object());
    }
  }
}

ConfigMgr::~ConfigMgr() {
  // Flush any pending writes before destruction
  if (m_mainConfigWriteTimer && m_mainConfigWriteTimer->isActive()) {
    m_mainConfigWriteTimer->stop();
    doWriteMainConfig();
  }
  if (m_sessionConfigWriteTimer && m_sessionConfigWriteTimer->isActive()) {
    m_sessionConfigWriteTimer->stop();
    doWriteSessionConfig();
  }

  // Clean up vxcore context
  if (m_vxcoreContext) {
    vxcore_context_destroy(m_vxcoreContext);
    m_vxcoreContext = nullptr;
  }
}

ConfigMgr &ConfigMgr::getInst() {
  static ConfigMgr inst;
  return inst;
}

void ConfigMgr::locateConfigFolder() {
  const auto appDirPath = getApplicationDirPath();
  qInfo() << "app folder" << appDirPath;
  // Check app config.
  {
    QString folderPath(appDirPath + '/' + c_appFilesFolder);
    if (QDir(folderPath).exists()) {
      // Config folder in app/.
      m_appConfigFolderPath = PathUtils::cleanPath(folderPath);
    } else {
      m_appConfigFolderPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
  }

  // Check user config.
  {
    QString folderPath(appDirPath + '/' + c_userFilesFolder);
    if (QDir(folderPath).exists()) {
      // Config folder in app/.
      m_userConfigFolderPath = PathUtils::cleanPath(folderPath);
    } else {
      m_userConfigFolderPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

      // Make sure it exists.
      QDir dir(m_userConfigFolderPath);
      dir.mkpath(m_userConfigFolderPath);
    }
  }

  Q_ASSERT(m_appConfigFolderPath != m_userConfigFolderPath);
  qInfo() << "app config folder" << m_appConfigFolderPath;
  qInfo() << "user config folder" << m_userConfigFolderPath;
}

bool ConfigMgr::checkAppConfig() {
  bool needUpdate = false;
  QDir appConfigDir(m_appConfigFolderPath);
  if (!appConfigDir.exists()) {
    needUpdate = true;
    appConfigDir.mkpath(m_appConfigFolderPath);
  } else {
    if (!appConfigDir.exists(c_configFileName)) {
      needUpdate = true;
    } else {
      // Check version of config file using vxcore JSON utilities
      if (m_vxcoreContext) {
        QString defaultPath = getConfigFilePath(Source::App);
        QString defaultConfigPath = getDefaultConfigFilePath();

        // Read version from default config
        char *defaultVersionStr = nullptr;
        VxCoreError err =
            vxcore_json_read_value(defaultConfigPath.toStdString().c_str(),
                                   "metadata.version", &defaultVersionStr);
        QString defaultVersion;
        if (err == VXCORE_OK && defaultVersionStr) {
          defaultVersion = QString::fromUtf8(defaultVersionStr);
          vxcore_string_free(defaultVersionStr);
        }

        // Read version from app config
        char *appVersionStr = nullptr;
        err = vxcore_json_read_value(defaultPath.toStdString().c_str(), "metadata.version",
                                     &appVersionStr);
        QString appVersion;
        if (err == VXCORE_OK && appVersionStr) {
          appVersion = QString::fromUtf8(appVersionStr);
          vxcore_string_free(appVersionStr);
        }

        if (!defaultVersion.isEmpty() && defaultVersion != appVersion) {
          qInfo() << "new version" << appVersion << defaultVersion;
          needUpdate = true;
        }
      }
    }

    if (needUpdate) {
      FileUtils::removeDir(m_appConfigFolderPath);
      // Wait for the OS delete the folder.
      Utils::sleepWait(1000);
      appConfigDir.mkpath(m_appConfigFolderPath);
    }
  }

  const auto mainConfigFilePath = appConfigDir.filePath(c_configFileName);

#ifndef VX_DEBUG_WEB
  if (!needUpdate) {
    return false;
  }
#endif

  qInfo() << "update app config files in" << m_appConfigFolderPath;

  Q_ASSERT(appConfigDir.exists());

  QPixmap pixmap(":/vnotex/data/core/logo/vnote.png");
  QScopedPointer<QSplashScreen> splash(new QSplashScreen(pixmap));
  splash->show();

  // Load extra data.
  splash->showMessage("Loading extra resource data");
  const QString extraRcc("app:vnote_extra.rcc");
  bool ret = QResource::registerResource(extraRcc);
  if (!ret) {
    Exception::throwOne(Exception::Type::FailToReadFile,
                        QStringLiteral("failed to register resource file %1").arg(extraRcc));
  }
  auto cleanup = qScopeGuard([extraRcc]() { QResource::unregisterResource(extraRcc); });

  const QString extraDataRoot(QStringLiteral(":/vnotex/data/extra"));

#ifdef VX_DEBUG_WEB
  if (!needUpdate) {
    // Always update main config file and web folder.
    qDebug() << "forced to update main config file and web folder for debugging";
    splash->showMessage("update main config file and web folder for debugging");

    // Cancel the read-only permission of the main config file.
    QFile::setPermissions(mainConfigFilePath, QFile::WriteUser);
    FileUtils::removeFile(mainConfigFilePath);
    FileUtils::removeDir(appConfigDir.filePath(QStringLiteral("web")));

    // Wait for the OS delete the folder.
    Utils::sleepWait(1000);

    FileUtils::copyFile(getDefaultConfigFilePath(), mainConfigFilePath);
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/web"),
                       appConfigDir.filePath(QStringLiteral("web")));
    return false;
  }
#else
  Q_ASSERT(needUpdate);
#endif

  // Copy themes.
  qApp->processEvents();
  splash->showMessage("Copying themes");
  FileUtils::copyDir(extraDataRoot + QStringLiteral("/themes"),
                     appConfigDir.filePath(QStringLiteral("themes")));

  // Copy tasks.
  qApp->processEvents();
  splash->showMessage("Copying tasks");
  FileUtils::copyDir(extraDataRoot + QStringLiteral("/tasks"),
                     appConfigDir.filePath(QStringLiteral("tasks")));

  // Copy docs.
  qApp->processEvents();
  splash->showMessage("Copying docs");
  FileUtils::copyDir(extraDataRoot + QStringLiteral("/docs"),
                     appConfigDir.filePath(QStringLiteral("docs")));

  // Copy syntax-highlighting.
  qApp->processEvents();
  splash->showMessage("Copying syntax-highlighting");
  FileUtils::copyDir(extraDataRoot + QStringLiteral("/syntax-highlighting"),
                     appConfigDir.filePath(QStringLiteral("syntax-highlighting")));

  // Copy web.
  qApp->processEvents();
  splash->showMessage("Copying web");
  FileUtils::copyDir(extraDataRoot + QStringLiteral("/web"),
                     appConfigDir.filePath(QStringLiteral("web")));

  // Copy dicts.
  qApp->processEvents();
  splash->showMessage("Copying dicts");
  FileUtils::copyDir(extraDataRoot + QStringLiteral("/dicts"),
                     appConfigDir.filePath(QStringLiteral("dicts")));

  // Main config file.
  FileUtils::copyFile(getDefaultConfigFilePath(), appConfigDir.filePath(c_configFileName));

  return needUpdate;
}

void ConfigMgr::checkUserConfig() {
  // Mainly check if the session config is read-only.
  const auto sessionFile = getConfigFilePath(Source::Session);
  if (QFileInfo::exists(sessionFile)) {
    if (!(QFile::permissions(sessionFile) & QFile::WriteUser)) {
      qDebug() << "make session config file writable" << sessionFile;
      QFile::setPermissions(sessionFile, QFile::WriteUser);
    }
  }
}

QString ConfigMgr::getConfigFilePath(Source p_src) const {
  QString configPath;
  switch (p_src) {
  case Source::App:
    configPath = m_appConfigFolderPath + QLatin1Char('/') + c_configFileName;
    break;

  case Source::Session:
    configPath = m_userConfigFolderPath + QLatin1Char('/') + c_sessionFileName;
    break;

  default:
    Q_ASSERT(false);
  }

  return configPath;
}

QString ConfigMgr::getDefaultConfigFilePath() {
  return QStringLiteral(":/vnotex/data/core/") + c_configFileName;
}

void ConfigMgr::writeUserSettings(const QJsonObject &p_jobj) {
  m_pendingMainConfig = p_jobj;
  scheduleMainConfigWrite();
}

void ConfigMgr::writeSessionSettings(const QJsonObject &p_jobj) {
  m_pendingSessionConfig = p_jobj;
  scheduleSessionConfigWrite();
}

void ConfigMgr::scheduleMainConfigWrite() {
  if (m_mainConfigWriteTimer) {
    m_mainConfigWriteTimer->start(); // Restarts if already running
  }
}

void ConfigMgr::scheduleSessionConfigWrite() {
  if (m_sessionConfigWriteTimer) {
    m_sessionConfigWriteTimer->start(); // Restarts if already running
  }
}

void ConfigMgr::doWriteMainConfig() {
  if (m_pendingMainConfig.isEmpty() || !m_vxcoreContext) {
    return;
  }

  QString configPath = getConfigFilePath(Source::App);
  QJsonDocument doc(m_pendingMainConfig);
  QString jsonString = QString::fromUtf8(doc.toJson());

  VxCoreError err = vxcore_json_save(m_vxcoreContext, configPath.toStdString().c_str(),
                                     jsonString.toStdString().c_str());
  if (err != VXCORE_OK) {
    qWarning() << "Failed to save main config:" << vxcore_error_message(err);
  } else {
    qDebug() << "Main config saved:" << configPath;
  }

  m_pendingMainConfig = QJsonObject();
}

void ConfigMgr::doWriteSessionConfig() {
  if (m_pendingSessionConfig.isEmpty() || !m_vxcoreContext) {
    return;
  }

  QString configPath = getConfigFilePath(Source::Session);
  QJsonDocument doc(m_pendingSessionConfig);
  QString jsonString = QString::fromUtf8(doc.toJson());

  VxCoreError err = vxcore_json_save(m_vxcoreContext, configPath.toStdString().c_str(),
                                     jsonString.toStdString().c_str());
  if (err != VXCORE_OK) {
    qWarning() << "Failed to save session config:" << vxcore_error_message(err);
  } else {
    qDebug() << "Session config saved:" << configPath;
  }

  m_pendingSessionConfig = QJsonObject();
}

MainConfig &ConfigMgr::getConfig() { return *m_config; }

SessionConfig &ConfigMgr::getSessionConfig() { return *m_sessionConfig; }

CoreConfig &ConfigMgr::getCoreConfig() { return m_config->getCoreConfig(); }

EditorConfig &ConfigMgr::getEditorConfig() { return m_config->getEditorConfig(); }

WidgetConfig &ConfigMgr::getWidgetConfig() { return m_config->getWidgetConfig(); }

QString ConfigMgr::getAppFolder() const { return m_appConfigFolderPath; }

QString ConfigMgr::getUserFolder() const { return m_userConfigFolderPath; }

QString ConfigMgr::getAppThemeFolder() const {
  return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("themes"));
}

QString ConfigMgr::getUserThemeFolder() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("themes"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getAppTaskFolder() const {
  return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("tasks"));
}

QString ConfigMgr::getUserTaskFolder() const {
  auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("tasks"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getAppWebStylesFolder() const {
  return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("web-styles"));
}

QString ConfigMgr::getUserWebStylesFolder() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("web-styles"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getAppDocsFolder() const {
  return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("docs"));
}

QString ConfigMgr::getUserDocsFolder() const {
  auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("docs"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getAppSyntaxHighlightingFolder() const {
  return PathUtils::concatenateFilePath(m_appConfigFolderPath,
                                        QStringLiteral("syntax-highlighting"));
}

QString ConfigMgr::getUserSyntaxHighlightingFolder() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("syntax-highlighting"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getAppDictsFolder() const {
  return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("dicts"));
}

QString ConfigMgr::getUserDictsFolder() const {
  auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("dicts"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getUserTemplateFolder() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("templates"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getUserSnippetFolder() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("snippets"));
  QDir().mkpath(folderPath);
  return folderPath;
}

QString ConfigMgr::getUserMarkdownUserStyleFile() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("web/css"));
  auto filePath = PathUtils::concatenateFilePath(folderPath, QStringLiteral("user.css"));
  if (!QFileInfo::exists(filePath)) {
    QDir().mkpath(folderPath);
    FileUtils::writeFile(filePath, QByteArray());
  }
  return filePath;
}

QString ConfigMgr::getUserOrAppFile(const QString &p_filePath) const {
  QFileInfo fi(p_filePath);
  if (fi.isAbsolute()) {
    return p_filePath;
  }

  // Check user folder first.
  QDir userConfigDir(m_userConfigFolderPath);
  if (userConfigDir.exists(p_filePath)) {
    return userConfigDir.absoluteFilePath(p_filePath);
  }

  // App folder.
  QDir appConfigDir(m_appConfigFolderPath);
  return appConfigDir.absoluteFilePath(p_filePath);
}

QString ConfigMgr::locateSessionConfigFilePathAtBootstrap() {
  // QApplication is not init yet, so org and app name are empty here.
  auto folderPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  folderPath = PathUtils::concatenateFilePath(folderPath, c_orgName + "/" + c_appName);
  QDir dir(folderPath);
  if (dir.exists(c_sessionFileName)) {
    qInfo() << "locateSessionConfigFilePathAtBootstrap" << folderPath;
    return dir.filePath(c_sessionFileName);
  }

  return QString();
}

QString ConfigMgr::getLogFile() const {
  return PathUtils::concatenateFilePath(ConfigMgr::getInst().getUserFolder(), "vnotex.log");
}

QString ConfigMgr::getApplicationFilePath() {
#if defined(Q_OS_LINUX)
  // We could get the APPIMAGE env variable from the AppRun script.
  auto appImageVar = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
  qInfo() << "APPIMAGE" << appImageVar;
  if (!appImageVar.isEmpty()) {
    return appImageVar;
  }
#elif defined(Q_OS_MACOS)
  auto exePath = QCoreApplication::applicationFilePath();
  const QString exeName = c_appName.toLower() + ".app";
  int idx = exePath.indexOf(exeName + QStringLiteral("/Contents/MacOS/"));
  if (idx != -1) {
    return exePath.left(idx + exeName.size());
  }
#endif

  return QCoreApplication::applicationFilePath();
}

QString ConfigMgr::getApplicationDirPath() {
  return PathUtils::parentDirPath(getApplicationFilePath());
}

QString ConfigMgr::getDocumentOrHomePath() {
  static QString docHomePath;
  if (docHomePath.isEmpty()) {
    QStringList folders = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
    if (folders.isEmpty()) {
      docHomePath = QDir::homePath();
    } else {
      docHomePath = folders[0];
    }
  }

  return docHomePath;
}

QString ConfigMgr::getApplicationVersion() {
  static QString appVersion;

  if (appVersion.isEmpty()) {
    QString defaultConfigPath = getDefaultConfigFilePath();
    char *versionStr = nullptr;
    VxCoreError err = vxcore_json_read_value(defaultConfigPath.toStdString().c_str(),
                                             "metadata.version", &versionStr);
    if (err == VXCORE_OK && versionStr) {
      appVersion = QString::fromUtf8(versionStr);
      vxcore_string_free(versionStr);
    }
  }

  return appVersion;
}

void ConfigMgr::initAppPrefixPath() {
  // Support QFile("app:abc.txt").
  QStringList potential_dirs;
  auto app_dir_path = QCoreApplication::applicationDirPath();
  qInfo() << "app prefix path: " << app_dir_path;
  potential_dirs << app_dir_path;

#if defined(Q_OS_LINUX)
  QDir localBinDir(app_dir_path);
  if (localBinDir.exists("../local/bin/vnote")) {
    auto app_dir_path2 = localBinDir.cleanPath(localBinDir.filePath("../local/share"));
    qInfo() << "app prefix path: " << app_dir_path2;
    potential_dirs << app_dir_path2;
  }
  if (localBinDir.exists("../share")) {
    auto app_dir_path3 = localBinDir.cleanPath(localBinDir.filePath("../share"));
    qInfo() << "app prefix path: " << app_dir_path3;
    potential_dirs << app_dir_path3;
  }
#elif defined(Q_OS_MACOS)
  QDir localBinDir(app_dir_path);
  if (localBinDir.exists("../Resources")) {
    auto app_dir_path2 = localBinDir.cleanPath(localBinDir.filePath("../Resources"));
    qInfo() << "app prefix path: " << app_dir_path2;
    potential_dirs << app_dir_path2;
  }
#endif

  QDir::setSearchPaths("app", potential_dirs);
}

QJsonValue ConfigMgr::parseAndReadConfig(const QString &p_exp) const {
  if (p_exp.startsWith(QStringLiteral("main."))) {
    return Utils::parseAndReadJson(m_config->toJson(), p_exp.mid(5));
  } else if (p_exp.startsWith(QStringLiteral("session."))) {
    return Utils::parseAndReadJson(m_sessionConfig->toJson(), p_exp.mid(8));
  } else {
    return QJsonValue();
  }
}
