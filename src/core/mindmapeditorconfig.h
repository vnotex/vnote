#ifndef MINDMAPEDITORCONFIG_H
#define MINDMAPEDITORCONFIG_H

#include "iconfig.h"

#include "webresource.h"

namespace vnotex
{
    class MindMapEditorConfig : public IConfig
    {
    public:
        MindMapEditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig);

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        const WebResource &getEditorResource() const;

    private:
        friend class MainConfig;

        void loadEditorResource(const QJsonObject &p_app, const QJsonObject &p_user);
        QJsonObject saveEditorResource() const;

        WebResource m_editorResource;
    };
}

#endif // MINDMAPEDITORCONFIG_H
