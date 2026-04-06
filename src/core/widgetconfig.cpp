#include "widgetconfig.h"

using namespace vnotex;

#define READINT(key) readInt(p_jobj, (key))
#define READBOOL(key) readBool(p_jobj, (key))
#define READSTRLIST(key) readStringList(p_jobj, (key))

WidgetConfig::WidgetConfig(IConfigMgr *p_mgr, IConfig *p_topConfig) : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("widget");
  initDefaults();
}

void WidgetConfig::fromJson(const QJsonObject &p_jobj) {

  {
    m_outlineAutoExpandedLevel = READINT(QStringLiteral("outlineAutoExpandedLevel"));
    if (m_outlineAutoExpandedLevel < 0 || m_outlineAutoExpandedLevel > 6) {
      m_outlineAutoExpandedLevel = 6;
    }

    m_outlineSectionNumberEnabled = READBOOL(QStringLiteral("outlineSectionNumberEnabled"));
  }

  m_findAndReplaceOptions =
      static_cast<FindOptions>(READINT(QStringLiteral("findAndReplaceOptions")));

  m_notebookSelectorViewOrder = READINT(QStringLiteral("notebookSelectorViewOrder"));

  {
    m_nodeExplorerViewOrder = READINT(QStringLiteral("nodeExplorerViewOrder"));
    m_nodeExplorerExploreMode = READINT(QStringLiteral("nodeExplorerExploreMode"));
    m_nodeExplorerExternalFilesVisible =
        READBOOL(QStringLiteral("nodeExplorerExternalFilesVisible"));
    m_nodeExplorerAutoImportExternalFilesEnabled =
        READBOOL(QStringLiteral("nodeExplorerAutoImportExternalFilesEnabled"));
    m_nodeExplorerCloseBeforeOpenWithEnabled =
        READBOOL(QStringLiteral("nodeExplorerCloseBeforeOpenWithEnabled"));
  }
    m_nodeExplorerSingleClickActivation =
        READBOOL(QStringLiteral("nodeExplorerSingleClickActivation"));

  m_searchPanelAdvancedSettingsVisible =
      READBOOL(QStringLiteral("searchPanelAdvancedSettingsVisible"));

  m_mainWindowKeepDocksExpandingContentArea =
      READSTRLIST(QStringLiteral("mainWindowKeepDocksExpandingContentArea"));

  m_snippetPanelBuiltInSnippetsVisible =
      READBOOL(QStringLiteral("snippetPanelBuiltInSnippetsVisible"));

  m_tagExplorerTwoColumnsEnabled = READBOOL(QStringLiteral("tagExplorerTwoColumnsEnabled"));

  m_newNoteDefaultFileTypeName = readString(p_jobj, QStringLiteral("newNoteDefaultFileTypeName"));
  if (m_newNoteDefaultFileTypeName.isEmpty()) {
    m_newNoteDefaultFileTypeName = QStringLiteral("Markdown");
  }

  m_unitedEntryExpandAllEnabled = READBOOL(QStringLiteral("unitedEntryExpandAll"));

  m_viewWindowLayoutMode = static_cast<ViewWindowLayoutMode>(
      READINT(QStringLiteral("viewWindowLayoutMode")));
  m_readableWidthMaxPx = READINT(QStringLiteral("readableWidthMaxPx"));
  if (m_readableWidthMaxPx <= 0) {
    m_readableWidthMaxPx = 720;
  }

  m_searchScope = READINT(QStringLiteral("searchScope"));
  if (m_searchScope < 0 || m_searchScope > 3) {
    m_searchScope = 0;
  }

  m_searchMode = READINT(QStringLiteral("searchMode"));
  if (m_searchMode < 0 || m_searchMode > 2) {
    m_searchMode = 0;
  }

  m_searchCaseSensitive = READBOOL(QStringLiteral("searchCaseSensitive"));
  m_searchRegex = READBOOL(QStringLiteral("searchRegex"));
  m_searchFilePattern = readString(p_jobj, QStringLiteral("searchFilePattern"));
}

QJsonObject WidgetConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("outlineAutoExpandedLevel")] = m_outlineAutoExpandedLevel;
  obj[QStringLiteral("outlineSectionNumberEnabled")] = m_outlineSectionNumberEnabled;

  obj[QStringLiteral("findAndReplaceOptions")] = static_cast<int>(m_findAndReplaceOptions);

  obj[QStringLiteral("notebookSelectorViewOrder")] = m_notebookSelectorViewOrder;

  obj[QStringLiteral("nodeExplorerViewOrder")] = m_nodeExplorerViewOrder;
  obj[QStringLiteral("nodeExplorerExploreMode")] = m_nodeExplorerExploreMode;
  obj[QStringLiteral("nodeExplorerExternalFilesVisible")] = m_nodeExplorerExternalFilesVisible;
  obj[QStringLiteral("nodeExplorerAutoImportExternalFilesEnabled")] =
      m_nodeExplorerAutoImportExternalFilesEnabled;
  obj[QStringLiteral("nodeExplorerCloseBeforeOpenWithEnabled")] =
      m_nodeExplorerCloseBeforeOpenWithEnabled;
  obj[QStringLiteral("nodeExplorerSingleClickActivation")] =
      m_nodeExplorerSingleClickActivation;

  obj[QStringLiteral("searchPanelAdvancedSettingsVisible")] =
      m_searchPanelAdvancedSettingsVisible;
  obj[QStringLiteral("snippetPanelBuiltInSnippetsVisible")] =
      m_snippetPanelBuiltInSnippetsVisible;
  obj[QStringLiteral("tagExplorerTwoColumnsEnabled")] = m_tagExplorerTwoColumnsEnabled;
  writeStringList(obj, QStringLiteral("mainWindowKeepDocksExpandingContentArea"),
                  m_mainWindowKeepDocksExpandingContentArea);
  obj[QStringLiteral("newNoteDefaultFileTypeName")] = m_newNoteDefaultFileTypeName;
  obj[QStringLiteral("unitedEntryExpandAll")] = m_unitedEntryExpandAllEnabled;
  obj[QStringLiteral("viewWindowLayoutMode")] =
      static_cast<int>(m_viewWindowLayoutMode);
  obj[QStringLiteral("readableWidthMaxPx")] = m_readableWidthMaxPx;
  obj[QStringLiteral("searchScope")] = m_searchScope;
  obj[QStringLiteral("searchMode")] = m_searchMode;
  obj[QStringLiteral("searchCaseSensitive")] = m_searchCaseSensitive;
  obj[QStringLiteral("searchRegex")] = m_searchRegex;
  obj[QStringLiteral("searchFilePattern")] = m_searchFilePattern;
  return obj;
}

int WidgetConfig::getOutlineAutoExpandedLevel() const { return m_outlineAutoExpandedLevel; }

void WidgetConfig::setOutlineAutoExpandedLevel(int p_level) {
  updateConfig(m_outlineAutoExpandedLevel, p_level, this);
}

bool WidgetConfig::getOutlineSectionNumberEnabled() const { return m_outlineSectionNumberEnabled; }

void WidgetConfig::setOutlineSectionNumberEnabled(bool p_enabled) {
  updateConfig(m_outlineSectionNumberEnabled, p_enabled, this);
}

FindOptions WidgetConfig::getFindAndReplaceOptions() const { return m_findAndReplaceOptions; }

void WidgetConfig::setFindAndReplaceOptions(FindOptions p_options) {
  updateConfig(m_findAndReplaceOptions, p_options, this);
}

int WidgetConfig::getNodeExplorerViewOrder() const { return m_nodeExplorerViewOrder; }

void WidgetConfig::setNodeExplorerViewOrder(int p_viewOrder) {
  updateConfig(m_nodeExplorerViewOrder, p_viewOrder, this);
}

int WidgetConfig::getNotebookSelectorViewOrder() const { return m_notebookSelectorViewOrder; }

void WidgetConfig::setNotebookSelectorViewOrder(int p_viewOrder) {
  updateConfig(m_notebookSelectorViewOrder, p_viewOrder, this);
}

int WidgetConfig::getNodeExplorerExploreMode() const { return m_nodeExplorerExploreMode; }

void WidgetConfig::setNodeExplorerExploreMode(int p_mode) {
  updateConfig(m_nodeExplorerExploreMode, p_mode, this);
}

bool WidgetConfig::isNodeExplorerExternalFilesVisible() const {
  return m_nodeExplorerExternalFilesVisible;
}

void WidgetConfig::setNodeExplorerExternalFilesVisible(bool p_visible) {
  updateConfig(m_nodeExplorerExternalFilesVisible, p_visible, this);
}

bool WidgetConfig::getNodeExplorerAutoImportExternalFilesEnabled() const {
  return m_nodeExplorerAutoImportExternalFilesEnabled;
}

void WidgetConfig::setNodeExplorerAutoImportExternalFilesEnabled(bool p_enabled) {
  updateConfig(m_nodeExplorerAutoImportExternalFilesEnabled, p_enabled, this);
}

bool WidgetConfig::getNodeExplorerCloseBeforeOpenWithEnabled() const {
  return m_nodeExplorerCloseBeforeOpenWithEnabled;
}

void WidgetConfig::setNodeExplorerCloseBeforeOpenWithEnabled(bool p_enabled) {
  updateConfig(m_nodeExplorerCloseBeforeOpenWithEnabled, p_enabled, this);
}

bool WidgetConfig::isSearchPanelAdvancedSettingsVisible() const {
  return m_searchPanelAdvancedSettingsVisible;
}

void WidgetConfig::setSearchPanelAdvancedSettingsVisible(bool p_visible) {
  updateConfig(m_searchPanelAdvancedSettingsVisible, p_visible, this);
}

const QStringList &WidgetConfig::getMainWindowKeepDocksExpandingContentArea() const {
  return m_mainWindowKeepDocksExpandingContentArea;
}

void WidgetConfig::setMainWindowKeepDocksExpandingContentArea(const QStringList &p_docks) {
  updateConfig(m_mainWindowKeepDocksExpandingContentArea, p_docks, this);
}

bool WidgetConfig::isSnippetPanelBuiltInSnippetsVisible() const {
  return m_snippetPanelBuiltInSnippetsVisible;
}

void WidgetConfig::setSnippetPanelBuiltInSnippetsVisible(bool p_visible) {
  updateConfig(m_snippetPanelBuiltInSnippetsVisible, p_visible, this);
}

bool WidgetConfig::getTagExplorerTwoColumnsEnabled() const {
  return m_tagExplorerTwoColumnsEnabled;
}

void WidgetConfig::setTagExplorerTwoColumnsEnabled(bool p_enabled) {
  updateConfig(m_tagExplorerTwoColumnsEnabled, p_enabled, this);
}

const QString &WidgetConfig::getNewNoteDefaultFileTypeName() const {
  return m_newNoteDefaultFileTypeName;
}

void WidgetConfig::setNewNoteDefaultFileTypeName(const QString &p_typeName) {
  updateConfig(m_newNoteDefaultFileTypeName, p_typeName, this);
}

// Legacy methods for backward compatibility with old NewNoteDialog.
// Maps between integer type index and type name string.
int WidgetConfig::getNewNoteDefaultFileType() const {
  // Map type name to legacy integer index.
  // 0=Markdown, 1=Text, 2=PDF, 3=MindMap, 4=Others
  if (m_newNoteDefaultFileTypeName == QStringLiteral("Markdown")) {
    return 0;
  } else if (m_newNoteDefaultFileTypeName == QStringLiteral("Text")) {
    return 1;
  } else if (m_newNoteDefaultFileTypeName == QStringLiteral("PDF")) {
    return 2;
  } else if (m_newNoteDefaultFileTypeName == QStringLiteral("MindMap")) {
    return 3;
  } else {
    return 4; // Others
  }
}

void WidgetConfig::setNewNoteDefaultFileType(int p_type) {
  // Map legacy integer index to type name.
  QString typeName;
  switch (p_type) {
  case 0:
    typeName = QStringLiteral("Markdown");
    break;
  case 1:
    typeName = QStringLiteral("Text");
    break;
  case 2:
    typeName = QStringLiteral("PDF");
    break;
  case 3:
    typeName = QStringLiteral("MindMap");
    break;
  default:
    typeName = QStringLiteral("Others");
    break;
  }
  updateConfig(m_newNoteDefaultFileTypeName, typeName, this);
}

bool WidgetConfig::getUnitedEntryExpandAllEnabled() const { return m_unitedEntryExpandAllEnabled; }

void WidgetConfig::setUnitedEntryExpandAllEnabled(bool p_enabled) {
  updateConfig(m_unitedEntryExpandAllEnabled, p_enabled, this);
}

bool WidgetConfig::isNodeExplorerSingleClickActivation() const {
  return m_nodeExplorerSingleClickActivation;
}

void WidgetConfig::setNodeExplorerSingleClickActivation(bool p_enabled) {
  updateConfig(m_nodeExplorerSingleClickActivation, p_enabled, this);
}

ViewWindowLayoutMode WidgetConfig::getViewWindowLayoutMode() const {
  return m_viewWindowLayoutMode;
}

void WidgetConfig::setViewWindowLayoutMode(ViewWindowLayoutMode p_mode) {
  updateConfig(m_viewWindowLayoutMode, p_mode, this);
}

int WidgetConfig::getReadableWidthMaxPx() const { return m_readableWidthMaxPx; }

void WidgetConfig::setReadableWidthMaxPx(int p_px) {
  updateConfig(m_readableWidthMaxPx, p_px, this);
}

void WidgetConfig::initDefaults() {
  m_mainWindowKeepDocksExpandingContentArea = {QStringLiteral("OutlineDock.vnotex"), QStringLiteral("WindowsDock.vnotex")};
}

int WidgetConfig::getSearchScope() const { return m_searchScope; }

void WidgetConfig::setSearchScope(int p_scope) {
  updateConfig(m_searchScope, p_scope, this);
}

int WidgetConfig::getSearchMode() const { return m_searchMode; }

void WidgetConfig::setSearchMode(int p_mode) {
  updateConfig(m_searchMode, p_mode, this);
}

bool WidgetConfig::getSearchCaseSensitive() const { return m_searchCaseSensitive; }

void WidgetConfig::setSearchCaseSensitive(bool p_sensitive) {
  updateConfig(m_searchCaseSensitive, p_sensitive, this);
}

bool WidgetConfig::getSearchRegex() const { return m_searchRegex; }

void WidgetConfig::setSearchRegex(bool p_regex) {
  updateConfig(m_searchRegex, p_regex, this);
}

const QString &WidgetConfig::getSearchFilePattern() const { return m_searchFilePattern; }

void WidgetConfig::setSearchFilePattern(const QString &p_pattern) {
  updateConfig(m_searchFilePattern, p_pattern, this);
}
