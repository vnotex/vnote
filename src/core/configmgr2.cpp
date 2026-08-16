#include "configmgr2.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QResource>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include "core/logging.h"
#include "coreconfig.h"
#include "editorconfig.h"
#include "mainconfig.h"
#include "services/configcoreservice.h"
#include "sessionconfig.h"
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

const QVersionNumber ConfigMgr2::c_version{4, 4, 3};

const QString ConfigMgr2::c_orgName = QStringLiteral("VNoteX");

const QString ConfigMgr2::c_appName = QStringLiteral("VNote");

ConfigMgr2::ConfigMgr2(ConfigCoreService *p_configService, QObject *p_parent)
    : QObject(p_parent), m_configService(p_configService), m_mainConfig(new MainConfig(this)),
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
  qCDebug(lcConfig) << "ConfigMgr2 initializing with paths:"
                    << "app=" << m_appDataPath << "user=" << m_localDataPath;

  // Load and initialize main config
  {
    auto mainConfigJson =
        m_configService->getConfigByName(DataLocation::App, kMainConfigFileBaseName);
    m_versionChanged = MainConfig::peekVersion(mainConfigJson) != c_version.toString();

    if (mainConfigJson.isEmpty()) {
      // Fresh start: no config file on disk. Keep the default-constructed config
      // objects as-is (their C++ initDefaults() provide correct defaults).
      qInfo() << "Fresh start detected, using default-constructed config";
    } else {
      m_mainConfig->fromJson(mainConfigJson);
    }
  }

  // Load and initialize session config
  {
    auto sessionConfigJson =
        m_configService->getConfigByName(DataLocation::Local, kSessionFileBaseName);
    if (!sessionConfigJson.isEmpty()) {
      m_sessionConfig->fromJson(sessionConfigJson);
    }
  }

  qCDebug(lcConfig) << "ConfigMgr2 initialized successfully";
}

void ConfigMgr2::initAfterQtAppStarted() {
  // Handle version upgrade after Qt app is ready
  initAppPrefixPath();

#if defined(VX_DEBUG_REFRESH)
  qInfo() << "application version may not have changed, but forced to update for debugging";
  ensureExtraData(true);
  upgradeMainConfigOnVersionChange();
#else
  // Always make sure the bundled extra data is present and current. The
  // per-folder version stamp turns this into a cheap no-op once a folder is
  // fully installed, and it is what retries a folder whose copy was only
  // partially completed on a previous launch. This subsumes the old
  // copyNecessaryExtraData() folder-existence guard (deleting a folder deletes
  // its stamp, which re-triggers the copy) and additionally covers all bundled
  // folders instead of three.
  ensureExtraData(false);

  if (m_versionChanged) {
    qInfo() << "Application version changed from" << m_mainConfig->getVersion() << "to"
            << c_version.toString();
    upgradeMainConfigOnVersionChange();
  }
#endif

  qCDebug(lcConfig) << "ConfigMgr2 post-initialized successfully";
}

MainConfig &ConfigMgr2::getConfig() { return *m_mainConfig; }

const MainConfig &ConfigMgr2::getConfig() const { return *m_mainConfig; }

SessionConfig &ConfigMgr2::getSessionConfig() { return *m_sessionConfig; }

const SessionConfig &ConfigMgr2::getSessionConfig() const { return *m_sessionConfig; }

CoreConfig &ConfigMgr2::getCoreConfig() { return m_mainConfig->getCoreConfig(); }

const CoreConfig &ConfigMgr2::getCoreConfig() const { return m_mainConfig->getCoreConfig(); }

EditorConfig &ConfigMgr2::getEditorConfig() { return m_mainConfig->getEditorConfig(); }

WidgetConfig &ConfigMgr2::getWidgetConfig() { return m_mainConfig->getWidgetConfig(); }

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

QString ConfigMgr2::getAppDataPath() const { return m_appDataPath; }

QString ConfigMgr2::getUserConfigPath() const { return m_localDataPath; }

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

QString ConfigMgr2::getApplicationVersion() { return c_version.toString(); }

bool ConfigMgr2::isVersionChanged() const { return m_versionChanged; }

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

void ConfigMgr2::scheduleMainConfigWrite() { m_mainConfigWriteTimer->start(); }

void ConfigMgr2::scheduleSessionConfigWrite() { m_sessionConfigWriteTimer->start(); }

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

void ConfigMgr2::upgradeMainConfigOnVersionChange() {
  // Config migration only. The bundled extra-data dump lives in
  // ensureExtraData(), which runs on every launch and owns its own per-folder
  // retry. Keeping the two apart is deliberate: coupling the version stamp to
  // the copy would make doVersionSpecificOverride re-run every launch and pin
  // the config version to the old value forever whenever a copy is
  // permanently broken.

  // Apply version-gated forced overrides using the still-persisted previous
  // version, BEFORE stamping the new version below.
  m_mainConfig->doVersionSpecificOverride(m_mainConfig->getVersion());

  m_mainConfig->setVersion(c_version.toString());
  m_mainConfig->update();
}

const QVector<ConfigMgr2::ExtraDataFailure> &ConfigMgr2::extraDataCopyFailures() const {
  return m_extraDataFailures;
}

void ConfigMgr2::setExtraDataSourceRootOverrideForTesting(const QString &p_root) {
  m_extraDataSourceRootOverride = p_root;
}

void ConfigMgr2::ensureExtraData(bool p_force) {
  // Cleared on entry so a successful retry within the same process empties the
  // list rather than accumulating stale entries.
  m_extraDataFailures.clear();

  struct FolderSpec {
    // Folder name inside the bundle; also the destination leaf name.
    QString m_name;
    QString m_destPath;
    // Destination-relative paths that must NOT be overwritten when present.
    QSet<QString> m_preserve;
  };

  QVector<FolderSpec> folders;
  folders.append(FolderSpec{QStringLiteral("themes"), getConfigDataFolder(ConfigDataType::Themes),
                            QSet<QString>()});
  folders.append(FolderSpec{QStringLiteral("tasks"), getConfigDataFolder(ConfigDataType::Tasks),
                            QSet<QString>()});
  folders.append(FolderSpec{QStringLiteral("syntax-highlighting"),
                            getConfigDataFolder(ConfigDataType::SyntaxHighlighting),
                            QSet<QString>()});
  // web/css/user.css is USER-OWNED: the settings page seeds it only when it is
  // absent (src/widgets/dialogs/settings/markdowneditorpage.cpp:314-326), so
  // dumping the bundled 2-line stub over it would destroy the user's global
  // CSS at every version bump.
  folders.append(FolderSpec{QStringLiteral("web"), getConfigDataFolder(ConfigDataType::Web),
                            QSet<QString>{QStringLiteral("css/user.css")}});
  folders.append(FolderSpec{QStringLiteral("dicts"), getConfigDataFolder(ConfigDataType::Dicts),
                            QSet<QString>()});
  // Bundled note templates. No preserve list: the shipped title.md is refreshed
  // on every version bump; user-created templates in this folder are untouched
  // because only bundled paths are written.
  folders.append(FolderSpec{QStringLiteral("templates"),
                            getConfigDataFolder(ConfigDataType::Templates), QSet<QString>()});

  QString extraDataRoot = m_extraDataSourceRootOverride;

  const QString extraRcc(QStringLiteral("app:vnote_extra.rcc"));
  bool rccRegistered = false;
  // Keep the resource's lifetime scoped: FirstRunController registers the same
  // .rcc independently, so making it global here would be cross-cutting.
  auto cleanup = qScopeGuard([&extraRcc, &rccRegistered]() {
    if (rccRegistered) {
      QResource::unregisterResource(extraRcc);
    }
  });

  if (extraDataRoot.isEmpty()) {
    if (!QResource::registerResource(extraRcc)) {
      // Without the resource there is nothing to copy FROM. Record a failure
      // for every folder and install nothing: falling through (as the old code
      // did) would let a tolerant copy stamp empty folders as complete.
      // ONE aggregate warning here rather than the per-folder warning below --
      // repeating identical lines carries no extra information, and the shared cause
      // is the resource, not any individual folder.
      const QString message = QStringLiteral("failed to register resource file %1").arg(extraRcc);
      qWarning() << "ConfigMgr2:" << message;
      for (const auto &folder : folders) {
        m_extraDataFailures.append(ExtraDataFailure{folder.m_name, message, QStringList()});
      }
      return;
    }
    rccRegistered = true;
    extraDataRoot = QStringLiteral(":/vnotex/data/extra");
  }

  const QString version = c_version.toString();
  constexpr int kMaxLoggedFailedPaths = 20;

  for (const auto &folder : folders) {
    // Keep the splash/UI responsive during a large web/ dump.
    if (qApp) {
      qApp->processEvents();
    }

    const QString srcPath = extraDataRoot + QLatin1Char('/') + folder.m_name;
    const QSet<QString> *skip = folder.m_preserve.isEmpty() ? nullptr : &folder.m_preserve;

    QStringList failedPaths;
    Error err = FileUtils2::installVersionedDir(srcPath, folder.m_destPath, version, &failedPaths,
                                                p_force, skip);
    if (!err) {
      continue;
    }

    m_extraDataFailures.append(ExtraDataFailure{folder.m_name, err.what(), failedPaths});

    const QStringList loggedPaths = failedPaths.mid(0, kMaxLoggedFailedPaths);
    qWarning() << "Failed to install extra data directory from" << srcPath << "to"
               << folder.m_destPath << ":" << err.what() << "-" << failedPaths.size()
               << "failed path(s), showing" << loggedPaths.size() << ":" << loggedPaths;
  }
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
