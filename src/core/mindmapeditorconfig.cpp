#include "mindmapeditorconfig.h"

#include "mainconfig.h"

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

using namespace vnotex;

MindMapEditorConfig::MindMapEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("mindmap_editor");
  initDefaults();
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

void MindMapEditorConfig::initDefaults() {
  m_editorResource = defaultEditorResource();
}

WebResource MindMapEditorConfig::defaultEditorResource() {
  WebResource res;
  res.m_template = QStringLiteral("web/mindmap-editor-template.html");

  // global_styles
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("global_styles");
    r.m_enabled = true;
    r.m_styles = {QStringLiteral("web/css/globalstyles.css")};
    res.m_resources.append(r);
  }

  // built_in
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("built_in");
    r.m_enabled = true;
    r.m_scripts = {
      QStringLiteral("web/js/qwebchannel.js"),
      QStringLiteral("web/js/eventemitter.js"),
      QStringLiteral("web/js/utils.js"),
      QStringLiteral("web/js/vxcore.js")
    };
    res.m_resources.append(r);
  }

  // mindmap_dependencies
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("mindmap_dependencies");
    r.m_enabled = true;
    r.m_scripts = {
      QStringLiteral("web/js/mindmap/lib/mind-elixir/MindElixir.js"),
      QStringLiteral("web/js/mindmap/core/mindmap-core.js"),
      QStringLiteral("web/js/mindmap/features/outline/outline.js"),
      QStringLiteral("web/js/mindmap/features/link-handler/link-handler.js")
    };
    res.m_resources.append(r);
  }

  // mindmap_editor
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("mindmap_editor");
    r.m_enabled = true;
    r.m_scripts = {QStringLiteral("web/js/mindmapeditor.js")};
    res.m_resources.append(r);
  }

  return res;
}
