#include "mainconfig.h"

#include <QDebug>
#include <QJsonObject>

#include "coreconfig.h"
#include "editorconfig.h"
#include "iconfigmgr.h"
#include "texteditorconfig.h"
#include "widgetconfig.h"

using namespace vnotex;

MainConfig::MainConfig(IConfigMgr *p_mgr) : IConfig(p_mgr, nullptr) {
  m_childConfigs.resize(ChildConfigIndex::ChildConfigCount);
  m_childConfigs[ChildConfigIndex::CoreConfigIndex].reset(new CoreConfig(p_mgr, this));
  m_childConfigs[ChildConfigIndex::EditorConfigIndex].reset(new EditorConfig(p_mgr, this));
  m_childConfigs[ChildConfigIndex::WidgetConfigIndex].reset(new WidgetConfig(p_mgr, this));
}

MainConfig::~MainConfig() {}

void MainConfig::fromJson(const QJsonObject &p_jobj) {
  // p_jobj is already merged (defaults + user overrides)
  loadMetadata(p_jobj);

  for (auto &childConfig : m_childConfigs) {
    Q_ASSERT(childConfig);
    childConfig->fromJson(p_jobj.value(childConfig->getSectionName()).toObject());
  }
}

void MainConfig::loadMetadata(const QJsonObject &p_jobj) {
  // Extract metadata from merged JSON
  const auto metaObj = p_jobj.value(QStringLiteral("metadata")).toObject();

  m_version = metaObj.value(QStringLiteral("version")).toString();
}

QJsonObject MainConfig::saveMetaData() const {
  QJsonObject metaObj;
  metaObj[QStringLiteral("version")] = m_version;
  return metaObj;
}

CoreConfig &MainConfig::getCoreConfig() { return *static_cast<CoreConfig *>(
    m_childConfigs[ChildConfigIndex::CoreConfigIndex].data()); }

EditorConfig &MainConfig::getEditorConfig() { return *static_cast<EditorConfig *>(
    m_childConfigs[ChildConfigIndex::EditorConfigIndex].data()); }

WidgetConfig &MainConfig::getWidgetConfig() { return *static_cast<WidgetConfig *>(
    m_childConfigs[ChildConfigIndex::WidgetConfigIndex].data()); }

void MainConfig::update() { getMgr()->updateMainConfig(toJson()); }

QJsonObject MainConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("metadata")] = saveMetaData();
  for (const auto &childConfig : m_childConfigs) {
    Q_ASSERT(childConfig);
    obj[childConfig->getSectionName()] = childConfig->toJson();
  }
  return obj;
}

void MainConfig::doVersionSpecificOverride() {
  // In a new version, we may want to change one value by force.
}

QString MainConfig::peekVersion(const QJsonObject &p_jboj) {
  const auto metaObj = p_jboj.value(QStringLiteral("metadata")).toObject();
  return metaObj.value(QStringLiteral("version")).toString();
}
