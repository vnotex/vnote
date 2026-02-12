#include "configmgr2.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QResource>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>

#include "coreconfig.h"
#include "editorconfig.h"
#include "mainconfig.h"
#include "sessionconfig.h"
#include "services/configservice.h"
#include "widgetconfig.h"

#include <utils/fileutils2.h>
#include <utils/utils.h>

using namespace vnotex;

#ifndef QT_NO_DEBUG
// #define VX_DEBUG_REFRESH
#endif

constexpr int kWriteIntervalMs = 500;

constexpr char kMainConfigFileBaseName[] = "vnotex";
constexpr char kSessionFileBaseName[] = "session";

const QVersionNumber ConfigMgr2::c_version{4, 0, 0};

const QString ConfigMgr2::c_orgName = QStringLiteral("VNoteX");

const QString ConfigMgr2::c_appName = QStringLiteral("VNote");

ConfigMgr2::ConfigMgr2(ConfigService *p_configService, QObject *p_parent)
    : QObject(p_parent),
      m_configService(p_configService),
      m_mainConfig(new MainConfig(this)),
      m_sessionConfig(new SessionConfig(this)) {
  Q_ASSERT(m_configService != nullptr);

  // Create debounced write timers
  m_mainConfigWriteTimer = new QTimer(this);
  m_mainConfigWriteTimer->setSingleShot(true);
  m_mainConfigWriteTimer->setInterval(kWriteIntervalMs);
  connect(m_mainConfigWriteTimer, &QTimer::timeout, this, &ConfigMgr2::doWriteMainConfig);

  m_sessionConfigWriteTimer = new QTimer(this);
  m_sessionConfigWriteTimer->setSingleShot(true);
  m_sessionConfigWriteTimer->setInterval(kWriteIntervalMs);
  connect(m_sessionConfigWriteTimer, &QTimer::timeout, this, &ConfigMgr2::doWriteSessionConfig);

  // Cache paths
  m_appDataPath = m_configService->getDataPath(DataLocation::App);
  m_localDataPath = m_configService->getDataPath(DataLocation::Local);
}

ConfigMgr2::~ConfigMgr2() {
  // Flush any pending writes before destruction
  if (m_mainConfigWriteTimer && m_mainConfigWriteTimer->isActive()) {
    m_mainConfigWriteTimer->stop();
    doWriteMainConfig();
  }
  if (m_sessionConfigWriteTimer && m_sessionConfigWriteTimer->isActive()) {
    m_sessionConfigWriteTimer->stop();
    doWriteSessionConfig();
  }
}

void ConfigMgr2::init() {
  qInfo() << "ConfigMgr2 initializing with paths:"
          << "app=" << m_appDataPath << "user=" << m_localDataPath;

  // Load and initialize main config
  {
    auto mainConfigJson =
        m_configService->getConfigByName(DataLocation::App, kMainConfigFileBaseName);
    m_versionChanged = MainConfig::peekVersion(mainConfigJson) != c_version.toString();

#if defined(VX_DEBUG_REFRESH)
    mainConfig = loadDefaultMainConfig();
#else
    if (m_versionChanged) {
      auto defaultConfig = loadDefaultMainConfig();
      // Merge defaults with user config
      mainConfigJson = m_configService->getConfigByNameWithDefaults(
          DataLocation::App, kMainConfigFileBaseName, defaultConfig);
    }
#endif

    m_mainConfig->fromJson(mainConfigJson);
  }

  // Load and initialize session config
  {
    auto sessionConfigJson =
        m_configService->getConfigByName(DataLocation::Local, kSessionFileBaseName);
    if (!sessionConfigJson.isEmpty()) {
      m_sessionConfig->fromJson(sessionConfigJson);
    }
  }

  qInfo() << "ConfigMgr2 initialized successfully";
}

void ConfigMgr2::initAfterQtAppStarted() {
  // Handle version upgrade after Qt app is ready
  initAppPrefixPath();

  if (m_versionChanged) {
    qInfo() << "Application version changed from" << m_mainConfig->getVersion() << "to"
            << c_version.toString();
    upgradeMainConfigOnVersionChange();
  } else {
#if defined(VX_DEBUG_REFRESH)
    qInfo() << "application version not changed, but forced to update for debugging";
    upgradeMainConfigOnVersionChange();
#endif
  }

  qInfo() << "ConfigMgr2 post-initialized successfully";
}

MainConfig &ConfigMgr2::getConfig() {
  return *m_mainConfig;
}

const MainConfig &ConfigMgr2::getConfig() const {
  return *m_mainConfig;
}

SessionConfig &ConfigMgr2::getSessionConfig() {
  return *m_sessionConfig;
}

const SessionConfig &ConfigMgr2::getSessionConfig() const {
  return *m_sessionConfig;
}

CoreConfig &ConfigMgr2::getCoreConfig() {
  return m_mainConfig->getCoreConfig();
}

const CoreConfig &ConfigMgr2::getCoreConfig() const {
  return m_mainConfig->getCoreConfig();
}

EditorConfig &ConfigMgr2::getEditorConfig() {
  return m_mainConfig->getEditorConfig();
}

WidgetConfig &ConfigMgr2::getWidgetConfig() {
  return m_mainConfig->getWidgetConfig();
}

QString ConfigMgr2::getConfigDataFolder(ConfigDataType p_type) const {
  QString folderName;
  switch (p_type) {
  case ConfigDataType::Main:
    folderName = QStringLiteral(".");
    break;
  case ConfigDataType::Themes:
    folderName = QStringLiteral("themes");
    break;
  case ConfigDataType::Tasks:
    folderName = QStringLiteral("tasks");
    break;
  case ConfigDataType::WebStyles:
    folderName = QStringLiteral("web/css");
    break;
  case ConfigDataType::SyntaxHighlighting:
    folderName = QStringLiteral("syntax-highlighting");
    break;
  case ConfigDataType::Dicts:
    folderName = QStringLiteral("dicts");
    break;
  case ConfigDataType::Templates:
    folderName = QStringLiteral("templates");
    break;
  case ConfigDataType::Snippets:
    folderName = QStringLiteral("snippets");
    break;
  case ConfigDataType::Web:
    folderName = QStringLiteral("web");
    break;
  }

  if (folderName == QStringLiteral(".")) {
    return m_appDataPath;
  }

  return m_appDataPath + QDir::separator() + folderName;
}

QString ConfigMgr2::getAppDataPath() const {
  return m_appDataPath;
}

QString ConfigMgr2::getUserConfigPath() const {
  return m_localDataPath;
}

QString ConfigMgr2::getLogFile() const {
  return m_localDataPath + QDir::separator() + QStringLiteral("vnote.log");
}

QString ConfigMgr2::getMarkdownUserStyleFile() const {
  auto webFolder = getConfigDataFolder(ConfigDataType::Web);
  QDir dir(webFolder);
  auto cssDir = dir.filePath(QStringLiteral("css"));
  QDir().mkpath(cssDir);
  return QDir(cssDir).filePath(QStringLiteral("user.css"));
}

QString ConfigMgr2::getFileFromConfigFolder(const QString &p_filePath) const {
  QFileInfo info(p_filePath);
  if (info.isAbsolute()) {
    return p_filePath;
  }

  return QDir(m_appDataPath).filePath(p_filePath);
}

QJsonValue ConfigMgr2::parseAndReadConfig(const QString &p_exp) const {
  if (p_exp.startsWith(QStringLiteral("main."))) {
    return Utils::parseAndReadJson(m_mainConfig->toJson(), p_exp.mid(5));
  } else if (p_exp.startsWith(QStringLiteral("session."))) {
    return Utils::parseAndReadJson(m_sessionConfig->toJson(), p_exp.mid(8));
  } else {
    return QJsonValue();
  }
}

QString ConfigMgr2::getApplicationVersion() {
  return c_version.toString();
}

QString ConfigMgr2::getApplicationFilePath() {
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

QString ConfigMgr2::getDocumentOrHomePath() {
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

void ConfigMgr2::updateMainConfig(const QJsonObject &p_jobj) {
  m_pendingMainConfig = p_jobj;
  scheduleMainConfigWrite();
}

void ConfigMgr2::updateSessionConfig(const QJsonObject &p_jobj) {
  m_pendingSessionConfig = p_jobj;
  scheduleSessionConfigWrite();
}

void ConfigMgr2::scheduleMainConfigWrite() {
  m_mainConfigWriteTimer->start();
}

void ConfigMgr2::scheduleSessionConfigWrite() {
  m_sessionConfigWriteTimer->start();
}

void ConfigMgr2::doWriteMainConfig() {
  if (m_pendingMainConfig.isEmpty()) {
    return;
  }

  qDebug() << "Writing main config";
  Error err = m_configService->updateConfigByName(DataLocation::App, kMainConfigFileBaseName,
                                                  m_pendingMainConfig);

  if (!err.isOk()) {
    qWarning() << "Failed to write main config:" << err.message();
  }

  m_pendingMainConfig = QJsonObject();
}

void ConfigMgr2::doWriteSessionConfig() {
  if (m_pendingSessionConfig.isEmpty()) {
    return;
  }

  qDebug() << "Writing session config";
  Error err = m_configService->updateConfigByName(DataLocation::Local, kSessionFileBaseName,
                                                  m_pendingSessionConfig);

  if (!err.isOk()) {
    qWarning() << "Failed to write session config:" << err.message();
  }

  m_pendingSessionConfig = QJsonObject();
}

QJsonObject ConfigMgr2::loadDefaultMainConfig() const {
  auto filePath = QStringLiteral(":/vnotex/data/core/") + kMainConfigFileBaseName + ".json";
  QJsonObject jobj;
  auto err = FileUtils2::readJsonFile(filePath, &jobj);
  if (!err.isOk()) {
    qWarning() << "Failed to read default main config from" << filePath << ":" << err.what();
    return QJsonObject();
  }
  return jobj;
}

void ConfigMgr2::upgradeMainConfigOnVersionChange() {
  // Load extra data.
  const QString extraRcc("app:vnote_extra.rcc");
  bool ret = QResource::registerResource(extraRcc);
  if (!ret) {
    qWarning() << "Failed to register resource file" << extraRcc;
    // Don't throw - just log warning and continue
  }
  auto cleanup = qScopeGuard([extraRcc]() { QResource::unregisterResource(extraRcc); });

  const QString extraDataRoot(QStringLiteral(":/vnotex/data/extra"));

  // Copy themes.
  if (qApp) {
    qApp->processEvents();
  }
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/themes"),
                      getConfigDataFolder(ConfigDataType::Themes));

  // Copy tasks.
  if (qApp) {
    qApp->processEvents();
  }
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/tasks"),
                      getConfigDataFolder(ConfigDataType::Tasks));

  // Copy syntax-highlighting.
  if (qApp) {
    qApp->processEvents();
  }
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/syntax-highlighting"),
                      getConfigDataFolder(ConfigDataType::SyntaxHighlighting));

  // Copy web.
  if (qApp) {
    qApp->processEvents();
  }
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/web"),
                      getConfigDataFolder(ConfigDataType::Web));

  // Copy dicts.
  if (qApp) {
    qApp->processEvents();
  }
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/dicts"),
                      getConfigDataFolder(ConfigDataType::Dicts));

  m_mainConfig->setVersion(c_version.toString());
  m_mainConfig->update();
}

void ConfigMgr2::initAppPrefixPath() {
  // Support QFile("app:abc.txt").
  QStringList potentialDirs;
  auto appDirPath = m_configService->getExecutionFolderPath();
  qInfo() << "App prefix path:" << appDirPath;
  potentialDirs << appDirPath;

#if defined(Q_OS_LINUX)
  QDir localBinDir(appDirPath);
  if (localBinDir.exists("../local/bin/vnote")) {
    auto appDirPath2 = localBinDir.cleanPath(localBinDir.filePath("../local/share"));
    qInfo() << "App prefix path:" << appDirPath2;
    potentialDirs << appDirPath2;
  }
  if (localBinDir.exists("../share")) {
    auto appDirPath3 = localBinDir.cleanPath(localBinDir.filePath("../share"));
    qInfo() << "App prefix path:" << appDirPath3;
    potentialDirs << appDirPath3;
  }
#elif defined(Q_OS_MACOS)
  QDir localBinDir(appDirPath);
  if (localBinDir.exists("../Resources")) {
    auto appDirPath2 = localBinDir.cleanPath(localBinDir.filePath("../Resources"));
    qInfo() << "App prefix path:" << appDirPath2;
    potentialDirs << appDirPath2;
  }
#endif

  QDir::setSearchPaths("app", potentialDirs);
}
