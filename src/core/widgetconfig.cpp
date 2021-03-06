#include "widgetconfig.h"

using namespace vnotex;

#define READINT(key) readInt(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))

WidgetConfig::WidgetConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig)
{
    m_sessionName = QStringLiteral("widget");
}

void WidgetConfig::init(const QJsonObject &p_app,
                        const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    m_outlineAutoExpandedLevel = READINT(QStringLiteral("outline_auto_expanded_level"));
    if (m_outlineAutoExpandedLevel < 0 || m_outlineAutoExpandedLevel > 6) {
        m_outlineAutoExpandedLevel = 6;
    }

    m_findAndReplaceOptions = static_cast<FindOptions>(READINT(QStringLiteral("find_and_replace_options")));

    m_nodeExplorerViewOrder = READINT(QStringLiteral("node_explorer_view_order"));
    m_nodeExplorerRecycleBinNodeVisible = READBOOL(QStringLiteral("node_explorer_recycle_bin_node_visible"));
    m_nodeExplorerExternalFilesVisible = READBOOL(QStringLiteral("node_explorer_external_files_visible"));
    m_nodeExplorerAutoImportExternalFilesEnabled = READBOOL(QStringLiteral("node_explorer_auto_import_external_files_enabled"));
}

QJsonObject WidgetConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("outline_auto_expanded_level")] = m_outlineAutoExpandedLevel;
    obj[QStringLiteral("find_and_replace_options")] = static_cast<int>(m_findAndReplaceOptions);
    obj[QStringLiteral("node_explorer_view_order")] = m_nodeExplorerViewOrder;
    obj[QStringLiteral("node_explorer_recycle_bin_node_visible")] = m_nodeExplorerRecycleBinNodeVisible;
    obj[QStringLiteral("node_explorer_external_files_visible")] = m_nodeExplorerExternalFilesVisible;
    obj[QStringLiteral("node_explorer_auto_import_external_files_enabled")] = m_nodeExplorerAutoImportExternalFilesEnabled;
    return obj;
}

int WidgetConfig::getOutlineAutoExpandedLevel() const
{
    return m_outlineAutoExpandedLevel;
}

void WidgetConfig::setOutlineAutoExpandedLevel(int p_level)
{
    updateConfig(m_outlineAutoExpandedLevel, p_level, this);
}

FindOptions WidgetConfig::getFindAndReplaceOptions() const
{
    return m_findAndReplaceOptions;
}

void WidgetConfig::setFindAndReplaceOptions(FindOptions p_options)
{
    updateConfig(m_findAndReplaceOptions, p_options, this);
}

int WidgetConfig::getNodeExplorerViewOrder() const
{
    return m_nodeExplorerViewOrder;
}

void WidgetConfig::setNodeExplorerViewOrder(int p_viewOrder)
{
    updateConfig(m_nodeExplorerViewOrder, p_viewOrder, this);
}

bool WidgetConfig::isNodeExplorerRecycleBinNodeVisible() const
{
    return m_nodeExplorerRecycleBinNodeVisible;
}

void WidgetConfig::setNodeExplorerRecycleBinNodeVisible(bool p_visible)
{
    updateConfig(m_nodeExplorerRecycleBinNodeVisible, p_visible, this);
}

bool WidgetConfig::isNodeExplorerExternalFilesVisible() const
{
    return m_nodeExplorerExternalFilesVisible;
}

void WidgetConfig::setNodeExplorerExternalFilesVisible(bool p_visible)
{
    updateConfig(m_nodeExplorerExternalFilesVisible, p_visible, this);
}

bool WidgetConfig::getNodeExplorerAutoImportExternalFilesEnabled() const
{
    return m_nodeExplorerAutoImportExternalFilesEnabled;
}

void WidgetConfig::setNodeExplorerAutoImportExternalFilesEnabled(bool p_enabled)
{
    updateConfig(m_nodeExplorerAutoImportExternalFilesEnabled, p_enabled, this);
}
