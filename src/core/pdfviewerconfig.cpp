#include "pdfviewerconfig.h"

#include "mainconfig.h"

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

using namespace vnotex;

PdfViewerConfig::PdfViewerConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig)
{
    m_sessionName = QStringLiteral("pdf_viewer");
}

void PdfViewerConfig::init(const QJsonObject &p_app,
                           const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    loadViewerResource(appObj, userObj);
}

QJsonObject PdfViewerConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("viewer_resource")] = saveViewerResource();
    return obj;
}

void PdfViewerConfig::loadViewerResource(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const QString name(QStringLiteral("viewer_resource"));

    if (MainConfig::isVersionChanged()) {
        bool needOverride = p_app[QStringLiteral("override_viewer_resource")].toBool();
        if (needOverride) {
            qInfo() << "override \"viewer_resource\" in user configuration due to version change";
            m_viewerResource.init(p_app[name].toObject());
            return;
        }
    }

    if (p_user.contains(name)) {
        m_viewerResource.init(p_user[name].toObject());
    } else {
        m_viewerResource.init(p_app[name].toObject());
    }
}

QJsonObject PdfViewerConfig::saveViewerResource() const
{
    return m_viewerResource.toJson();
}

const WebResource &PdfViewerConfig::getViewerResource() const
{
    return m_viewerResource;
}
