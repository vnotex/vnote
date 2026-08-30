#include "pdfviewerconfig.h"

#include "mainconfig.h"

#include "services/commenttypes.h"

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

using namespace vnotex;

PdfViewerConfig::PdfViewerConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("pdf_viewer");
  initDefaults();
}

void PdfViewerConfig::fromJson(const QJsonObject &p_jobj) {
  loadViewerResource(p_jobj);

  // RESET before overlaying, so calling fromJson() twice with different objects
  // cannot retain stale state from the first call.
  m_toolOptions.clear();
  for (const auto &tool : toolNames()) {
    m_toolOptions.insert(tool, ToolOptions());
  }

  // Absent key keeps the C++ default. In practice ConfigMgr2::init() merges the
  // user's document over the defaults document, so `tools` is always present by
  // the time this runs; the fallback matters only for a direct fromJson() call.
  const auto toolsObj = p_jobj.value(QStringLiteral("tools")).toObject();
  for (const auto &tool : toolNames()) {
    m_toolOptions.insert(tool, toolOptionsFromJson(tool, toolsObj.value(tool).toObject()));
  }
}

QJsonObject PdfViewerConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("viewerResource")] = saveViewerResource();

  QJsonObject tools;
  for (const auto &tool : toolNames()) {
    tools[tool] = toolOptionsToJson(tool, getToolOptions(tool));
  }
  obj[QStringLiteral("tools")] = tools;
  return obj;
}

QStringList PdfViewerConfig::toolNames() { return PdfToolOptions::toolNames(); }

QJsonObject PdfViewerConfig::toolOptionsToJson(const QString &p_tool,
                                               const ToolOptions &p_options) {
  QJsonObject raw;
  raw.insert(PdfToolOptions::colorKey(), p_options.m_color);
  if (PdfToolOptions::hasWidth(p_tool)) {
    raw.insert(PdfToolOptions::widthKey(), p_options.m_width);
  }
  if (PdfToolOptions::hasFontSize(p_tool)) {
    raw.insert(PdfToolOptions::fontSizeKey(), p_options.m_fontSize);
  }
  if (PdfToolOptions::hasOpacity(p_tool)) {
    raw.insert(PdfToolOptions::opacityKey(), p_options.m_opacity);
  }
  return PdfToolOptions::normalize(p_tool, raw);
}

PdfViewerConfig::ToolOptions PdfViewerConfig::toolOptionsFromJson(const QString &p_tool,
                                                                  const QJsonObject &p_obj) {
  const auto normalized = PdfToolOptions::normalize(p_tool, p_obj);

  ToolOptions options;
  options.m_color = normalized.value(PdfToolOptions::colorKey()).toString();
  // A tool that does not carry the scalar keeps the struct default, so the
  // value stays meaningful if the tool ever grows one.
  if (PdfToolOptions::hasWidth(p_tool)) {
    options.m_width = normalized.value(PdfToolOptions::widthKey()).toDouble();
  }
  if (PdfToolOptions::hasFontSize(p_tool)) {
    options.m_fontSize = normalized.value(PdfToolOptions::fontSizeKey()).toDouble();
  }
  if (PdfToolOptions::hasOpacity(p_tool)) {
    options.m_opacity = normalized.value(PdfToolOptions::opacityKey()).toDouble();
  }
  return options;
}

PdfViewerConfig::ToolOptions PdfViewerConfig::getToolOptions(const QString &p_tool) const {
  return m_toolOptions.value(p_tool, ToolOptions());
}

void PdfViewerConfig::setToolOptions(const QString &p_tool, const ToolOptions &p_options) {
  if (!PdfToolOptions::isValidTool(p_tool)) {
    return;
  }
  // Same normalization as the JSON path, so the getter can never hand back a
  // value the serializer would have rewritten.
  const auto normalized = toolOptionsFromJson(p_tool, toolOptionsToJson(p_tool, p_options));
  updateConfig(m_toolOptions[p_tool], normalized, this);
}

void PdfViewerConfig::loadViewerResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("viewerResource"));
  m_viewerResource.init(p_jobj.value(name).toObject());
}

QJsonObject PdfViewerConfig::saveViewerResource() const { return m_viewerResource.toJson(); }

const WebResource &PdfViewerConfig::getViewerResource() const { return m_viewerResource; }

void PdfViewerConfig::initDefaults() {
  m_viewerResource = defaultViewerResource();
  m_toolOptions.clear();
  for (const auto &tool : toolNames()) {
    m_toolOptions.insert(tool, ToolOptions());
  }
}

WebResource PdfViewerConfig::defaultViewerResource() {
  WebResource res;
  res.m_template = QStringLiteral("web/pdf.js/web/pdf-viewer-template.html");

  // built_in
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("built_in");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/js/qwebchannel.js"),
                              QStringLiteral("web/js/eventemitter.js"),
                              QStringLiteral("web/js/utils.js"), QStringLiteral("web/js/vxcore.js"),
                              QStringLiteral("web/pdf.js/pdfviewercore.js")};
    res.m_resources.append(r);
  }

  // pdf.js
  //
  // ESM since v4: both entry points are `.mjs` and are emitted with
  // type="module" by HtmlTemplateService::fillPdfResources(), which makes them
  // DEFERRED. `pdfviewer.mjs` below must therefore also be a module, or it would
  // run before pdf.mjs/viewer.mjs have executed.
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("pdf.js");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/pdf.js/build/pdf.mjs"),
                              QStringLiteral("web/pdf.js/web/viewer.mjs")};
    r.m_styles = QStringList{QStringLiteral("web/pdf.js/web/viewer.css")};
    res.m_resources.append(r);
  }

  // pdf_viewer
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("pdf_viewer");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/pdf.js/pdfviewer.mjs")};
    r.m_styles = QStringList{QStringLiteral("web/pdf.js/pdfviewer.css")};
    res.m_resources.append(r);
  }

  return res;
}
