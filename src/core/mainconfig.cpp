#include "mainconfig.h"

#include <QDebug>
#include <QJsonObject>

#include "configmgr.h"
#include "coreconfig.h"
#include "editorconfig.h"
#include "markdowneditorconfig.h"
#include "texteditorconfig.h"
#include "widgetconfig.h"

using namespace vnotex;

bool MainConfig::s_versionChanged = false;

MainConfig::MainConfig(ConfigMgr *p_mgr)
    : IConfig(p_mgr, nullptr), m_coreConfig(new CoreConfig(p_mgr, this)),
      m_editorConfig(new EditorConfig(p_mgr, this)), m_widgetConfig(new WidgetConfig(p_mgr, this)) {
}

MainConfig::~MainConfig() {}

void MainConfig::fromJson(const QJsonObject &p_jobj) {
  // p_jobj is already merged (defaults + user overrides)
  loadMetadata(p_jobj);

  // Extract child config objects from merged JSON
  auto coreObj = p_jobj.value(QStringLiteral("core")).toObject();
  m_coreConfig->fromJson(coreObj);

  auto editorObj = p_jobj.value(QStringLiteral("editor")).toObject();
  m_editorConfig->fromJson(editorObj);

  auto widgetObj = p_jobj.value(QStringLiteral("widget")).toObject();
  m_widgetConfig->fromJson(widgetObj);

  if (isVersionChanged()) {
    doVersionSpecificOverride();

    // Update user config.
    writeToSettings();
  }
}

void MainConfig::loadMetadata(const QJsonObject &p_jobj) {
  // Extract metadata from merged JSON
  const auto metaObj = p_jobj.value(QStringLiteral("metadata")).toObject();

  // Get current app version from the merged config
  m_version = metaObj.value(QStringLiteral("version")).toString();
  
  // User version is what was previously saved (may differ if app upgraded)
  // For version change detection, we'll need to compare with app version
  m_userVersion = m_version;  // Will be overridden if user config had different version
  
  s_versionChanged = false;  // Will be set properly during ConfigMgr initialization
  qDebug() << "version" << m_version << "user version" << m_userVersion;
}

QJsonObject MainConfig::saveMetaData() const {
  QJsonObject metaObj;
  metaObj[QStringLiteral("version")] = m_version;

  return metaObj;
}

bool MainConfig::isVersionChanged() { return s_versionChanged; }

CoreConfig &MainConfig::getCoreConfig() { return *m_coreConfig; }

EditorConfig &MainConfig::getEditorConfig() { return *m_editorConfig; }

WidgetConfig &MainConfig::getWidgetConfig() { return *m_widgetConfig; }

void MainConfig::writeToSettings() const { getMgr()->writeUserSettings(toJson()); }

QJsonObject MainConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("metadata")] = saveMetaData();
  obj[m_coreConfig->getSessionName()] = m_coreConfig->toJson();
  obj[m_editorConfig->getSessionName()] = m_editorConfig->toJson();
  obj[m_widgetConfig->getSessionName()] = m_widgetConfig->toJson();
  return obj;
}

const QString &MainConfig::getVersion() const { return m_version; }

QString MainConfig::getVersion(const QJsonObject &p_jobj) {
  const auto metadataObj = p_jobj.value(QStringLiteral("metadata")).toObject();
  return metadataObj.value(QStringLiteral("version")).toString();
}

void MainConfig::doVersionSpecificOverride() {
  // In a new version, we may want to change one value by force.
}
