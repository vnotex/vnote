#include "configmgr2.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

#include "services/configservice.h"

using namespace vnotex;

constexpr int kWriteIntervalMs = 500;

constexpr char kMainConfigFileBaseName[] = "vnotex";
constexpr char kSessionFileBaseName[] = "session";

ConfigMgr2::ConfigMgr2(ConfigService *p_configService, QObject *p_parent)
    : QObject(p_parent), m_configService(p_configService) {
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
  m_userConfigPath = m_configService->getDataPath(DataLocation::Local);
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
  qInfo() << "ConfigMgr2 initialized with paths:"
          << "app=" << m_appDataPath << "user=" << m_userConfigPath;
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
  return m_userConfigPath;
}

QString ConfigMgr2::getLogFile() const {
  return m_userConfigPath + QDir::separator() + QStringLiteral("vnote.log");
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
  Error err = m_configService->updateConfigByName(
      DataLocation::App, kMainConfigFileBaseName, m_pendingMainConfig);

  if (!err.isOk()) {
    qWarning() << "Failed to write main config:" << err.message();
  }
}

void ConfigMgr2::doWriteSessionConfig() {
  if (m_pendingSessionConfig.isEmpty()) {
    return;
  }

  qDebug() << "Writing session config";
  Error err = m_configService->updateConfigByName(
      DataLocation::Local, kSessionFileBaseName, m_pendingSessionConfig);

  if (!err.isOk()) {
    qWarning() << "Failed to write session config:" << err.message();
  }
}
