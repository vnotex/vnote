#include "mindmapeditorconfig.h"

#include "mainconfig.h"

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

using namespace vnotex;

MindMapEditorConfig::MindMapEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("mindmap_editor");
}

void MindMapEditorConfig::fromJson(const QJsonObject &p_jobj) {
  loadEditorResource(p_jobj);
}

QJsonObject MindMapEditorConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("editorResource")] = saveEditorResource();
  return obj;
}

void MindMapEditorConfig::loadEditorResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("editorResource"));
  m_editorResource.init(p_jobj.value(name).toObject());
}

QJsonObject MindMapEditorConfig::saveEditorResource() const { return m_editorResource.toJson(); }

const WebResource &MindMapEditorConfig::getEditorResource() const { return m_editorResource; }
