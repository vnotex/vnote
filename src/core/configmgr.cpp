#include "configmgr.h"

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

#include "coreconfig.h"
#include "exception.h"
#include "mainconfig.h"
#include "sessionconfig.h"
#include "vxcore.h"

#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <utils/utils.h>

using namespace vnotex;

#ifndef QT_NO_DEBUG
#define VX_DEBUG_REFRESH
#endif

const QVersionNumber kVersion {4, 0, 0};

constexpr int kWriteIntervalMs = 500;

constexpr char kMainConfigFileBaseName[] = "vnotex";

constexpr char kSessionFileBaseName[] = "session";

const QString ConfigMgr::c_orgName = QStringLiteral("VNoteX");

const QString ConfigMgr::c_appName = QStringLiteral("VNote");

ConfigMgr::ConfigMgr(QObject *p_parent)
    : QObject(p_parent),
      m_mainConfig(new MainConfig(this)),
      m_sessionConfig(new SessionConfig(this)) {
  // Create debounced write timers
  m_mainConfigWriteTimer = new QTimer(this);
  m_mainConfigWriteTimer->setSingleShot(true);
  m_mainConfigWriteTimer->setInterval(kWriteIntervalMs);
  connect(m_mainConfigWriteTimer, &QTimer::timeout, this, &ConfigMgr::doWriteMainConfig);

  m_sessionConfigWriteTimer = new QTimer(this);
  m_sessionConfigWriteTimer->setSingleShot(true);
  m_sessionConfigWriteTimer->setInterval(kWriteIntervalMs);
  connect(m_sessionConfigWriteTimer, &QTimer::timeout, this, &ConfigMgr::doWriteSessionConfig);

  m_configFolderPath = VxCore::getInst().getDataPath(DataLocation::App);

  // Load and initialize main config
  {
    auto mainConfig = VxCore::getInst().getConfigByName(
      DataLocation::App, kMainConfigFileBaseName);
    m_version_changed = MainConfig::peekVersion(mainConfig) != kVersion.toString();

    #if defined(VX_DEBUG_REFRESH)
    mainConfig = loadDefaultMainConfig();
    #else
      if (m_version_changed) {
        auto defaultConfig = loadDefaultMainConfig();
        // QJsonObject does not have merge function, so we do it manually
        mainConfig = VxCore::getInst().getConfigByNameWithDefaults(
          DataLocation::App, kMainConfigFileBaseName, defaultConfig);
      }
    #endif

    m_mainConfig->fromJson(mainConfig);
  }

  // Load and initialize session config
  {
    auto sessionConfig = VxCore::getInst().getConfigByName(
      DataLocation::Local, kSessionFileBaseName);
    if (!sessionConfig.isEmpty()) {
      m_sessionConfig->fromJson(sessionConfig);
    }
  }
}

QJsonObject ConfigMgr::loadDefaultMainConfig() const {
  auto filePath = QStringLiteral(":/vnotex/data/core/") + kMainConfigFileBaseName + ".json";
  QJsonObject jobj;
  auto err = FileUtils2::readJsonFile(filePath, &jobj);
  if (!err.isOk()) {
    qWarning() << "failed to read default main config from" << filePath << " : " << err.what();
    return QJsonObject();
  } else {
    return jobj;
  }
}

void ConfigMgr::initAfterQtAppStarted() {
  initAppPrefixPath();

  if (m_version_changed) {
    qInfo() << "application version changed from"
      << m_mainConfig->getVersion() << "to" << kVersion.toString();
    upgradeMainConfigOnVersionChange();
  } else {
#if defined(VX_DEBUG_REFRESH)
    qInfo() << "application version not changed, but forced to update for debugging";
    upgradeMainConfigOnVersionChange();
#endif
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
}

ConfigMgr &ConfigMgr::getInst() {
  static ConfigMgr inst;
  return inst;
}

void ConfigMgr::upgradeMainConfigOnVersionChange() {
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

  // Copy themes.
  qApp->processEvents();
  splash->showMessage("Copying themes");
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/themes"),
                     getConfigDataFolder(ConfigDataType::Themes));

  // Copy tasks.
  qApp->processEvents();
  splash->showMessage("Copying tasks");
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/tasks"),
                     getConfigDataFolder(ConfigDataType::Tasks));

  // Copy syntax-highlighting.
  qApp->processEvents();
  splash->showMessage("Copying syntax-highlighting");
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/syntax-highlighting"),
                     getConfigDataFolder(ConfigDataType::SyntaxHighlighting));

  // Copy web.
  qApp->processEvents();
  splash->showMessage("Copying web");
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/web"), getConfigDataFolder(ConfigDataType::Web));

  // Copy dicts.
  qApp->processEvents();
  splash->showMessage("Copying dicts");
  FileUtils2::copyDir(extraDataRoot + QStringLiteral("/dicts"),
                     getConfigDataFolder(ConfigDataType::Dicts));

  m_mainConfig->setVersion(kVersion.toString());
  m_mainConfig->update();
}

void ConfigMgr::updateMainConfig(const QJsonObject &p_jobj) {
  m_pendingMainConfig = p_jobj;
  scheduleMainConfigWrite();
}

void ConfigMgr::updateSessionConfig(const QJsonObject &p_jobj) {
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
  if (m_pendingMainConfig.isEmpty()) {
    return;
  }

  QJsonDocument doc(m_pendingMainConfig);
  QString jsonString = QString::fromUtf8(doc.toJson());
  VxCore::getInst().updateConfigByName(DataLocation::App, kMainConfigFileBaseName, jsonString);

  m_pendingMainConfig = QJsonObject();
}

void ConfigMgr::doWriteSessionConfig() {
  if (m_pendingSessionConfig.isEmpty()) {
    return;
  }

  QJsonDocument doc(m_pendingSessionConfig);
  QString jsonString = QString::fromUtf8(doc.toJson());
  VxCore::getInst().updateConfigByName(DataLocation::Local, kSessionFileBaseName, jsonString);

  m_pendingSessionConfig = QJsonObject();
}

MainConfig &ConfigMgr::getConfig() { return *m_mainConfig; }

SessionConfig &ConfigMgr::getSessionConfig() { return *m_sessionConfig; }

CoreConfig &ConfigMgr::getCoreConfig() { return m_mainConfig->getCoreConfig(); }

EditorConfig &ConfigMgr::getEditorConfig() { return m_mainConfig->getEditorConfig(); }

WidgetConfig &ConfigMgr::getWidgetConfig() { return m_mainConfig->getWidgetConfig(); }

QString ConfigMgr::getConfigDataFolder(ConfigDataType p_type) const {
  switch (p_type) {
  case ConfigDataType::Main:
    return m_configFolderPath;
  case ConfigDataType::Themes:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("themes"));
  case ConfigDataType::Tasks:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("tasks"));
  case ConfigDataType::WebStyles:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("web-styles"));
  case ConfigDataType::SyntaxHighlighting:
    return PathUtils::concatenateFilePath(m_configFolderPath,
                                          QStringLiteral("syntax-highlighting"));
  case ConfigDataType::Dicts:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("dicts"));
  case ConfigDataType::Templates:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("templates"));
  case ConfigDataType::Snippets:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("snippets"));
  case ConfigDataType::Web:
    return PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("web"));
  default:
    Q_UNREACHABLE();
    return QString();
  }
}

QString ConfigMgr::getMarkdownUserStyleFile() const {
  auto folderPath =
      PathUtils::concatenateFilePath(m_configFolderPath, QStringLiteral("web/css"));
  auto filePath = PathUtils::concatenateFilePath(folderPath, QStringLiteral("user.css"));
  if (!QFileInfo::exists(filePath)) {
    QDir().mkpath(folderPath);
    FileUtils2::writeFile(filePath, QByteArray());
  }
  return filePath;
}

QString ConfigMgr::getFileFromConfigFolder(const QString &p_filePath) const {
  QFileInfo fi(p_filePath);
  if (fi.isAbsolute()) {
    return p_filePath;
  }
  QDir appConfigDir(m_configFolderPath);
  return appConfigDir.absoluteFilePath(p_filePath);
}

QString ConfigMgr::getLogFile() const {
  return PathUtils::concatenateFilePath(m_configFolderPath, "vnotex.log");
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
  auto exePath = VxCore::GetInst().getExecutionFilePath();
  const QString exeName = c_appName.toLower() + ".app";
  int idx = exePath.indexOf(exeName + QStringLiteral("/Contents/MacOS/"));
  if (idx != -1) {
    return exePath.left(idx + exeName.size());
  }
#endif

return VxCore::getInst().getExecutionFilePath();
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
  return kVersion.toString();
}

void ConfigMgr::initAppPrefixPath() {
  // Support QFile("app:abc.txt").
  QStringList potential_dirs;
  auto app_dir_path = VxCore::getInst().getExecutionFolderPath();
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
    return Utils::parseAndReadJson(m_mainConfig->toJson(), p_exp.mid(5));
  } else if (p_exp.startsWith(QStringLiteral("session."))) {
    return Utils::parseAndReadJson(m_sessionConfig->toJson(), p_exp.mid(8));
  } else {
    return QJsonValue();
  }
}
