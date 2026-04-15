#ifndef WIDGETCONFIG_H
#define WIDGETCONFIG_H

#include "iconfig.h"

#include "global.h"

#include <QString>

namespace vnotex {
class IConfigMgr;

class WidgetConfig : public IConfig {
public:
  WidgetConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

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

  // Legacy: Get/set file type by integer index (for old NewNoteDialog).
  // Deprecated: Use getNewNoteDefaultFileTypeName/setNewNoteDefaultFileTypeName instead.
  int getNewNoteDefaultFileType() const;
  void setNewNoteDefaultFileType(int p_type);

  // Get/set file type by name string (preferred for new code).
  const QString &getNewNoteDefaultFileTypeName() const;
  void setNewNoteDefaultFileTypeName(const QString &p_typeName);

  bool getUnitedEntryExpandAllEnabled() const;
  void setUnitedEntryExpandAllEnabled(bool p_enabled);

  bool isNodeExplorerSingleClickActivation() const;
  void setNodeExplorerSingleClickActivation(bool p_enabled);

  ViewWindowLayoutMode getViewWindowLayoutMode() const;
  void setViewWindowLayoutMode(ViewWindowLayoutMode p_mode);

  int getReadableWidthMaxPx() const;
  void setReadableWidthMaxPx(int p_px);

  int getSearchScope() const;
  void setSearchScope(int p_scope);

  int getSearchMode() const;
  void setSearchMode(int p_mode);

  bool getSearchCaseSensitive() const;
  void setSearchCaseSensitive(bool p_sensitive);

  bool getSearchRegex() const;
  void setSearchRegex(bool p_regex);

  const QString &getSearchFilePattern() const;
  void setSearchFilePattern(const QString &p_pattern);

private:
  int m_outlineAutoExpandedLevel = 6;

  bool m_outlineSectionNumberEnabled = false;

  FindOptions m_findAndReplaceOptions = FindOption::IncrementalSearch;

  int m_notebookSelectorViewOrder = 0;

  int m_nodeExplorerViewOrder = 0;

  int m_nodeExplorerExploreMode = 0;

  bool m_nodeExplorerExternalFilesVisible = true;

  bool m_nodeExplorerAutoImportExternalFilesEnabled = true;

  bool m_nodeExplorerCloseBeforeOpenWithEnabled = true;

  bool m_searchPanelAdvancedSettingsVisible = true;

  // Object name of those docks that should be kept when expanding content area.
  QStringList m_mainWindowKeepDocksExpandingContentArea;

  bool m_snippetPanelBuiltInSnippetsVisible = true;

  // Whether enable two columns for tag explorer.
  bool m_tagExplorerTwoColumnsEnabled = false;

  QString m_newNoteDefaultFileTypeName = QStringLiteral("Markdown");

  bool m_unitedEntryExpandAllEnabled = false;

  // Whether single click activates items in node explorer.
  bool m_nodeExplorerSingleClickActivation = false;

  // Default layout mode for view windows.
  ViewWindowLayoutMode m_viewWindowLayoutMode = ViewWindowLayoutMode::ReadableWidth;

  // Maximum content width in pixels for Readable Width mode.
  int m_readableWidthMaxPx = 900;

  // Search panel: scope index (0=Buffers, 1=CurrentFolder, 2=CurrentNotebook, 3=AllNotebooks).
  int m_searchScope = 0;

  // Search panel: mode index (0=FileNameSearch, 1=ContentSearch, 2=TagSearch).
  int m_searchMode = 0;

  // Search panel: case sensitivity toggle.
  bool m_searchCaseSensitive = false;

  // Search panel: regex toggle.
  bool m_searchRegex = false;

  // Search panel: file pattern filter (empty = no filter).
  QString m_searchFilePattern;

  void initDefaults();
};
} // namespace vnotex

#endif // WIDGETCONFIG_H
