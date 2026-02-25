#include "widgetconfig.h"

using namespace vnotex;

#define READINT(key) readInt(p_jobj, (key))
#define READBOOL(key) readBool(p_jobj, (key))
#define READSTRLIST(key) readStringList(p_jobj, (key))

WidgetConfig::WidgetConfig(IConfigMgr *p_mgr, IConfig *p_topConfig) : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("widget");
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

  m_searchPanelAdvancedSettingsVisible =
      READBOOL(QStringLiteral("searchPanelAdvancedSettingsVisible"));

  m_mainWindowKeepDocksExpandingContentArea =
      READSTRLIST(QStringLiteral("mainWindowKeepDocksExpandingContentArea"));

  m_snippetPanelBuiltInSnippetsVisible =
      READBOOL(QStringLiteral("snippetPanelBuiltInSnippetsVisible"));

  m_tagExplorerTwoColumnsEnabled = READBOOL(QStringLiteral("tagExplorerTwoColumnsEnabled"));

  m_newNoteDefaultFileType = READINT(QStringLiteral("newNoteDefaultFileType"));

  m_unitedEntryExpandAllEnabled = READBOOL(QStringLiteral("unitedEntryExpandAll"));
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

  obj[QStringLiteral("searchPanelAdvancedSettingsVisible")] =
      m_searchPanelAdvancedSettingsVisible;
  obj[QStringLiteral("snippetPanelBuiltInSnippetsVisible")] =
      m_snippetPanelBuiltInSnippetsVisible;
  obj[QStringLiteral("tagExplorerTwoColumnsEnabled")] = m_tagExplorerTwoColumnsEnabled;
  writeStringList(obj, QStringLiteral("mainWindowKeepDocksExpandingContentArea"),
                  m_mainWindowKeepDocksExpandingContentArea);
  obj[QStringLiteral("newNoteDefaultFileType")] = m_newNoteDefaultFileType;
  obj[QStringLiteral("unitedEntryExpandAll")] = m_unitedEntryExpandAllEnabled;
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

int WidgetConfig::getNewNoteDefaultFileType() const { return m_newNoteDefaultFileType; }

void WidgetConfig::setNewNoteDefaultFileType(int p_type) {
  updateConfig(m_newNoteDefaultFileType, p_type, this);
}

bool WidgetConfig::getUnitedEntryExpandAllEnabled() const { return m_unitedEntryExpandAllEnabled; }

void WidgetConfig::setUnitedEntryExpandAllEnabled(bool p_enabled) {
  updateConfig(m_unitedEntryExpandAllEnabled, p_enabled, this);
}
