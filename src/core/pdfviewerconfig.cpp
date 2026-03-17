#include "pdfviewerconfig.h"

#include "mainconfig.h"

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
}

QJsonObject PdfViewerConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("viewerResource")] = saveViewerResource();
  return obj;
}

void PdfViewerConfig::loadViewerResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("viewerResource"));
  m_viewerResource.init(p_jobj.value(name).toObject());
}

QJsonObject PdfViewerConfig::saveViewerResource() const { return m_viewerResource.toJson(); }

const WebResource &PdfViewerConfig::getViewerResource() const { return m_viewerResource; }

void PdfViewerConfig::initDefaults() {
  m_viewerResource = defaultViewerResource();
}

WebResource PdfViewerConfig::defaultViewerResource() {
  WebResource res;
  res.m_template = QStringLiteral("web/pdf.js/web/pdf-viewer-template.html");

  // built_in
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("built_in");
    r.m_enabled = true;
    r.m_scripts = {
      QStringLiteral("web/js/qwebchannel.js"),
      QStringLiteral("web/js/eventemitter.js"),
      QStringLiteral("web/js/utils.js"),
      QStringLiteral("web/js/vxcore.js"),
      QStringLiteral("web/pdf.js/pdfviewercore.js")
    };
    res.m_resources.append(r);
  }

  // pdf.js
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("pdf.js");
    r.m_enabled = true;
    r.m_scripts = {
      QStringLiteral("web/pdf.js/build/pdf.js"),
      QStringLiteral("web/pdf.js/web/viewer.js")
    };
    r.m_styles = {QStringLiteral("web/pdf.js/web/viewer.css")};
    res.m_resources.append(r);
  }

  // pdf_viewer
  {
    WebResource::Resource r;
    r.m_name = QStringLiteral("pdf_viewer");
    r.m_enabled = true;
    r.m_scripts = {QStringLiteral("web/pdf.js/pdfviewer.js")};
    r.m_styles = {QStringLiteral("web/pdf.js/pdfviewer.css")};
    res.m_resources.append(r);
  }

  return res;
}
