#ifndef PDFVIEWERCONFIG_H
#define PDFVIEWERCONFIG_H

#include "iconfig.h"

#include "webresource.h"

namespace vnotex
{
    class PdfViewerConfig : public IConfig
    {
    public:
        PdfViewerConfig(ConfigMgr *p_mgr, IConfig *p_topConfig);

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        const WebResource &getViewerResource() const;

    private:
        friend class MainConfig;

        void loadViewerResource(const QJsonObject &p_app, const QJsonObject &p_user);
        QJsonObject saveViewerResource() const;

        WebResource m_viewerResource;
    };
}

#endif // PDFVIEWERCONFIG_H
