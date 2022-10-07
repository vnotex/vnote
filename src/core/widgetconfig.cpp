#include "widgetconfig.h"

using namespace vnotex;

#define READINT(key) readInt(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READSTRLIST(key) readStringList(appObj, userObj, (key))

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

    {
        m_outlineAutoExpandedLevel = READINT(QStringLiteral("outline_auto_expanded_level"));
        if (m_outlineAutoExpandedLevel < 0 || m_outlineAutoExpandedLevel > 6) {
            m_outlineAutoExpandedLevel = 6;
        }

        m_outlineSectionNumberEnabled = READBOOL(QStringLiteral("outline_section_number_enabled"));
    }

    m_findAndReplaceOptions = static_cast<FindOptions>(READINT(QStringLiteral("find_and_replace_options")));

    m_notebookSelectorViewOrder = READINT(QStringLiteral("notebook_selector_view_order"));

    {
        m_nodeExplorerViewOrder = READINT(QStringLiteral("node_explorer_view_order"));
        m_nodeExplorerExploreMode = READINT(QStringLiteral("node_explorer_explore_mode"));
        m_nodeExplorerExternalFilesVisible = READBOOL(QStringLiteral("node_explorer_external_files_visible"));
        m_nodeExplorerAutoImportExternalFilesEnabled = READBOOL(QStringLiteral("node_explorer_auto_import_external_files_enabled"));
        m_nodeExplorerCloseBeforeOpenWithEnabled = READBOOL(QStringLiteral("node_explorer_close_before_open_with_enabled"));
    }

    m_searchPanelAdvancedSettingsVisible = READBOOL(QStringLiteral("search_panel_advanced_settings_visible"));

    m_mainWindowKeepDocksExpandingContentArea = READSTRLIST(QStringLiteral("main_window_keep_docks_expanding_content_area"));

    m_snippetPanelBuiltInSnippetsVisible = READBOOL(QStringLiteral("snippet_panel_builtin_snippets_visible"));

    m_tagExplorerTwoColumnsEnabled = READBOOL(QStringLiteral("tag_explorer_two_columns_enabled"));

    m_newNoteDefaultFileType = READINT(QStringLiteral("new_note_default_file_type"));

    m_unitedEntryExpandAllEnabled = READBOOL(QStringLiteral("united_entry_expand_all"));
}

QJsonObject WidgetConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("outline_auto_expanded_level")] = m_outlineAutoExpandedLevel;
    obj[QStringLiteral("outline_section_number_enabled")] = m_outlineSectionNumberEnabled;

    obj[QStringLiteral("find_and_replace_options")] = static_cast<int>(m_findAndReplaceOptions);

    obj[QStringLiteral("notebook_selector_view_order")] = m_notebookSelectorViewOrder;

    obj[QStringLiteral("node_explorer_view_order")] = m_nodeExplorerViewOrder;
    obj[QStringLiteral("node_explorer_explore_mode")] = m_nodeExplorerExploreMode;
    obj[QStringLiteral("node_explorer_external_files_visible")] = m_nodeExplorerExternalFilesVisible;
    obj[QStringLiteral("node_explorer_auto_import_external_files_enabled")] = m_nodeExplorerAutoImportExternalFilesEnabled;
    obj[QStringLiteral("node_explorer_close_before_open_with_enabled")] = m_nodeExplorerCloseBeforeOpenWithEnabled;

    obj[QStringLiteral("search_panel_advanced_settings_visible")] = m_searchPanelAdvancedSettingsVisible;
    obj[QStringLiteral("tag_explorer_two_columns_enabled")] = m_tagExplorerTwoColumnsEnabled;
    writeStringList(obj,
                    QStringLiteral("main_window_keep_docks_expanding_content_area"),
                    m_mainWindowKeepDocksExpandingContentArea);
    obj[QStringLiteral("new_note_default_file_type")] = m_newNoteDefaultFileType;
    obj[QStringLiteral("united_entry_expand_all")] = m_unitedEntryExpandAllEnabled;
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

bool WidgetConfig::getOutlineSectionNumberEnabled() const
{
    return m_outlineSectionNumberEnabled;
}

void WidgetConfig::setOutlineSectionNumberEnabled(bool p_enabled)
{
    updateConfig(m_outlineSectionNumberEnabled, p_enabled, this);
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

int WidgetConfig::getNotebookSelectorViewOrder() const
{
    return m_notebookSelectorViewOrder;
}

void WidgetConfig::setNotebookSelectorViewOrder(int p_viewOrder)
{
    updateConfig(m_notebookSelectorViewOrder, p_viewOrder, this);
}

int WidgetConfig::getNodeExplorerExploreMode() const
{
    return m_nodeExplorerExploreMode;
}

void WidgetConfig::setNodeExplorerExploreMode(int p_mode)
{
    updateConfig(m_nodeExplorerExploreMode, p_mode, this);
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

bool WidgetConfig::getNodeExplorerCloseBeforeOpenWithEnabled() const
{
    return m_nodeExplorerCloseBeforeOpenWithEnabled;
}

void WidgetConfig::setNodeExplorerCloseBeforeOpenWithEnabled(bool p_enabled)
{
    updateConfig(m_nodeExplorerCloseBeforeOpenWithEnabled, p_enabled, this);
}

bool WidgetConfig::isSearchPanelAdvancedSettingsVisible() const
{
    return m_searchPanelAdvancedSettingsVisible;
}

void WidgetConfig::setSearchPanelAdvancedSettingsVisible(bool p_visible)
{
    updateConfig(m_searchPanelAdvancedSettingsVisible, p_visible, this);
}

const QStringList &WidgetConfig::getMainWindowKeepDocksExpandingContentArea() const
{
    return m_mainWindowKeepDocksExpandingContentArea;
}

void WidgetConfig::setMainWindowKeepDocksExpandingContentArea(const QStringList &p_docks)
{
    updateConfig(m_mainWindowKeepDocksExpandingContentArea, p_docks, this);
}

bool WidgetConfig::isSnippetPanelBuiltInSnippetsVisible() const
{
    return m_snippetPanelBuiltInSnippetsVisible;
}

void WidgetConfig::setSnippetPanelBuiltInSnippetsVisible(bool p_visible)
{
    updateConfig(m_snippetPanelBuiltInSnippetsVisible, p_visible, this);
}

bool WidgetConfig::getTagExplorerTwoColumnsEnabled() const
{
    return m_tagExplorerTwoColumnsEnabled;
}

void WidgetConfig::setTagExplorerTwoColumnsEnabled(bool p_enabled)
{
    updateConfig(m_tagExplorerTwoColumnsEnabled, p_enabled, this);
}

int WidgetConfig::getNewNoteDefaultFileType() const
{
    return m_newNoteDefaultFileType;
}

void WidgetConfig::setNewNoteDefaultFileType(int p_type)
{
    updateConfig(m_newNoteDefaultFileType, p_type, this);
}

bool WidgetConfig::getUnitedEntryExpandAllEnabled() const
{
    return m_unitedEntryExpandAllEnabled;
}

void WidgetConfig::setUnitedEntryExpandAllEnabled(bool p_enabled)
{
    updateConfig(m_unitedEntryExpandAllEnabled, p_enabled, this);
}
