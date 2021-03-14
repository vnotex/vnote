#ifndef WIDGETCONFIG_H
#define WIDGETCONFIG_H

#include "iconfig.h"

#include "global.h"

#include <QString>

namespace vnotex
{
    class WidgetConfig : public IConfig
    {
    public:
        WidgetConfig(ConfigMgr *p_mgr, IConfig *p_topConfig);

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        int getOutlineAutoExpandedLevel() const;
        void setOutlineAutoExpandedLevel(int p_level);

        FindOptions getFindAndReplaceOptions() const;
        void setFindAndReplaceOptions(FindOptions p_options);

        int getNodeExplorerViewOrder() const;
        void setNodeExplorerViewOrder(int p_viewOrder);

        bool isNodeExplorerRecycleBinNodeVisible() const;
        void setNodeExplorerRecycleBinNodeVisible(bool p_visible);

        bool isNodeExplorerExternalFilesVisible() const;
        void setNodeExplorerExternalFilesVisible(bool p_visible);

        bool getNodeExplorerAutoImportExternalFilesEnabled() const;
        void setNodeExplorerAutoImportExternalFilesEnabled(bool p_enabled);

    private:
        int m_outlineAutoExpandedLevel = 6;

        FindOptions m_findAndReplaceOptions = FindOption::None;

        int m_nodeExplorerViewOrder = 0;

        bool m_nodeExplorerRecycleBinNodeVisible = false;

        bool m_nodeExplorerExternalFilesVisible = true;

        bool m_nodeExplorerAutoImportExternalFilesEnabled = true;
    };
}

#endif // WIDGETCONFIG_H
