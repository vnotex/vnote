#include "pdfviewerconfig.h"

#include "mainconfig.h"

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

using namespace vnotex;

PdfViewerConfig::PdfViewerConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("pdf_viewer");
}

void PdfViewerConfig::fromJson(const QJsonObject &p_jobj) {
  loadViewerResource(p_jobj);
}

QJsonObject PdfViewerConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("viewer_resource")] = saveViewerResource();
  return obj;
}

void PdfViewerConfig::loadViewerResource(const QJsonObject &p_jobj) {
  const QString name(QStringLiteral("viewer_resource"));
  m_viewerResource.init(p_jobj.value(name).toObject());
}

QJsonObject PdfViewerConfig::saveViewerResource() const { return m_viewerResource.toJson(); }

const WebResource &PdfViewerConfig::getViewerResource() const { return m_viewerResource; }
