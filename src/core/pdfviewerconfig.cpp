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

  // Absent key keeps the C++ default, which is what makes this safe to add
  // without a config migration.
  const auto color = p_jobj.value(QStringLiteral("commentColor")).toString();
  if (CommentColor::isValid(color)) {
    m_commentColor = color;
  }
}

QJsonObject PdfViewerConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("viewerResource")] = saveViewerResource();
  obj[QStringLiteral("commentColor")] = m_commentColor;
  return obj;
}

const QString &PdfViewerConfig::getCommentColor() const { return m_commentColor; }

void PdfViewerConfig::setCommentColor(const QString &p_color) {
  if (!CommentColor::isValid(p_color) || m_commentColor == p_color) {
    return;
  }
  updateConfig(m_commentColor, p_color, this);
}

void PdfViewerConfig::loadViewerResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("viewerResource"));
  m_viewerResource.init(p_jobj.value(name).toObject());
}

QJsonObject PdfViewerConfig::saveViewerResource() const { return m_viewerResource.toJson(); }

const WebResource &PdfViewerConfig::getViewerResource() const { return m_viewerResource; }

void PdfViewerConfig::initDefaults() {
  m_viewerResource = defaultViewerResource();
  m_commentColor = CommentColor::defaultToken();
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
