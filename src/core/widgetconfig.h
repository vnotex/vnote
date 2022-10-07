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

        bool getOutlineSectionNumberEnabled() const;
        void setOutlineSectionNumberEnabled(bool p_enabled);

        FindOptions getFindAndReplaceOptions() const;
        void setFindAndReplaceOptions(FindOptions p_options);

        int getNotebookSelectorViewOrder() const;
        void setNotebookSelectorViewOrder(int p_viewOrder);

        int getNodeExplorerViewOrder() const;
        void setNodeExplorerViewOrder(int p_viewOrder);

        int getNodeExplorerExploreMode() const;
        void setNodeExplorerExploreMode(int p_mode);

        bool isNodeExplorerExternalFilesVisible() const;
        void setNodeExplorerExternalFilesVisible(bool p_visible);

        bool getNodeExplorerAutoImportExternalFilesEnabled() const;
        void setNodeExplorerAutoImportExternalFilesEnabled(bool p_enabled);

        bool getNodeExplorerCloseBeforeOpenWithEnabled() const;
        void setNodeExplorerCloseBeforeOpenWithEnabled(bool p_enabled);

        bool isSearchPanelAdvancedSettingsVisible() const;
        void setSearchPanelAdvancedSettingsVisible(bool p_visible);

        const QStringList &getMainWindowKeepDocksExpandingContentArea() const;
        void setMainWindowKeepDocksExpandingContentArea(const QStringList &p_docks);

        bool isSnippetPanelBuiltInSnippetsVisible() const;
        void setSnippetPanelBuiltInSnippetsVisible(bool p_visible);

        bool getTagExplorerTwoColumnsEnabled() const;
        void setTagExplorerTwoColumnsEnabled(bool p_enabled);

        int getNewNoteDefaultFileType() const;
        void setNewNoteDefaultFileType(int p_type);

        bool getUnitedEntryExpandAllEnabled() const;
        void setUnitedEntryExpandAllEnabled(bool p_enabled);

    private:
        int m_outlineAutoExpandedLevel = 6;

        bool m_outlineSectionNumberEnabled = false;

        FindOptions m_findAndReplaceOptions = FindOption::FindNone;

        int m_notebookSelectorViewOrder = 0;

        int m_nodeExplorerViewOrder = 0;

        int m_nodeExplorerExploreMode = 1;

        bool m_nodeExplorerExternalFilesVisible = true;

        bool m_nodeExplorerAutoImportExternalFilesEnabled = true;

        bool m_nodeExplorerCloseBeforeOpenWithEnabled = true;

        bool m_searchPanelAdvancedSettingsVisible = true;

        // Object name of those docks that should be kept when expanding content area.
        QStringList m_mainWindowKeepDocksExpandingContentArea;

        bool m_snippetPanelBuiltInSnippetsVisible = true;

        // Whether enable two columns for tag explorer.
        bool m_tagExplorerTwoColumnsEnabled = false;

        int m_newNoteDefaultFileType = 0;

        bool m_unitedEntryExpandAllEnabled = false;
    };
}

#endif // WIDGETCONFIG_H
