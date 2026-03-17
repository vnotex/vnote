#include "coreconfig.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMetaEnum>

#include <utils/utils.h>

using namespace vnotex;

#define READSTR(key) readString(p_jobj, (key))
#define READINT(key) readInt(p_jobj, (key))
#define READBOOL(key) readBool(p_jobj, (key))
#define READSTRLIST(key) readStringList(p_jobj, (key))

QStringList CoreConfig::s_availableLocales;

CoreConfig::CoreConfig(IConfigMgr *p_mgr, IConfig *p_topConfig) : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("core");
  initDefaults();
}

const QString &CoreConfig::getTheme() const { return m_theme; }

void CoreConfig::setTheme(const QString &p_name) { updateConfig(m_theme, p_name, this); }

void CoreConfig::fromJson(const QJsonObject &p_jobj) {

  m_theme = READSTR(QStringLiteral("theme"));

  m_locale = READSTR(QStringLiteral("locale"));
  if (!m_locale.isEmpty() && !getAvailableLocales().contains(m_locale)) {
    m_locale = QStringLiteral("en_US");
  }

  loadShortcuts(p_jobj.value(QStringLiteral("shortcuts")).toObject());

  m_shortcutLeaderKey = READSTR(QStringLiteral("shortcutLeaderKey"));

  m_toolBarIconSize = READINT(QStringLiteral("toolbarIconSize"));
  if (m_toolBarIconSize <= 0) {
    m_toolBarIconSize = 18;
  }

  m_docksTabBarIconSize = READINT(QStringLiteral("docksTabbarIconSize"));
  if (m_docksTabBarIconSize <= 0) {
    m_docksTabBarIconSize = 18;
  }

  loadNoteManagement(p_jobj.value(QStringLiteral("noteManagement")).toObject());

  m_recoverLastSessionOnStartEnabled = READBOOL(QStringLiteral("recoverLastSessionOnStart"));

  m_checkForUpdatesOnStartEnabled = READBOOL(QStringLiteral("checkForUpdatesOnStart"));

  m_historyMaxCount = READINT(QStringLiteral("historyMaxCount"));
  if (m_historyMaxCount < 0) {
    m_historyMaxCount = 100;
  }

  m_perNotebookHistoryEnabled = READBOOL(QStringLiteral("perNotebookHistory"));

  {
    auto lineEnding = READSTR(QStringLiteral("lineEnding"));
    m_lineEnding = stringToLineEndingPolicy(lineEnding);
  }

  {
    auto mode = READSTR(QStringLiteral("defaultOpenMode"));
    m_defaultOpenMode = stringToViewWindowMode(mode);
  }

  loadUnitedEntry(p_jobj);
}

QJsonObject CoreConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("theme")] = m_theme;
  obj[QStringLiteral("locale")] = m_locale;
  obj[QStringLiteral("shortcuts")] = saveShortcuts();
  obj[QStringLiteral("shortcutLeaderKey")] = m_shortcutLeaderKey;
  obj[QStringLiteral("toolbarIconSize")] = m_toolBarIconSize;
  obj[QStringLiteral("docksTabbarIconSize")] = m_docksTabBarIconSize;
  obj[QStringLiteral("recoverLastSessionOnStart")] = m_recoverLastSessionOnStartEnabled;
  obj[QStringLiteral("checkForUpdatesOnStart")] = m_checkForUpdatesOnStartEnabled;
  obj[QStringLiteral("historyMaxCount")] = m_historyMaxCount;
  obj[QStringLiteral("perNotebookHistory")] = m_perNotebookHistoryEnabled;
  obj[QStringLiteral("lineEnding")] = lineEndingPolicyToString(m_lineEnding);
  obj[QStringLiteral("unitedEntry")] = saveUnitedEntry();
  obj[QStringLiteral("defaultOpenMode")] = viewWindowModeToString(m_defaultOpenMode);
  return obj;
}

const QString &CoreConfig::getLocale() const { return m_locale; }

void CoreConfig::setLocale(const QString &p_locale) { updateConfig(m_locale, p_locale, this); }

QString CoreConfig::getLocaleToUse() const { return QLocale().name(); }

const QStringList &CoreConfig::getAvailableLocales() {
  if (s_availableLocales.isEmpty()) {
    s_availableLocales << QStringLiteral("en_US");
    s_availableLocales << QStringLiteral("zh_CN");
    s_availableLocales << QStringLiteral("ja_JP");
  }

  return s_availableLocales;
}

void CoreConfig::loadShortcuts(const QJsonObject &p_jobj) {
  static const auto indexOfShortcutEnum =
      CoreConfig::staticMetaObject.indexOfEnumerator("Shortcut");
  Q_ASSERT(indexOfShortcutEnum >= 0);
  const auto metaEnum = CoreConfig::staticMetaObject.enumerator(indexOfShortcutEnum);
  // Skip the Max flag.
  for (int i = 0; i < metaEnum.keyCount() - 1; ++i) {
    m_shortcuts[i] = readString(p_jobj, metaEnum.key(i));
  }
}

void CoreConfig::loadNoteManagement(const QJsonObject &p_jobj) {
  // External node.
  const auto externalNodeObj = p_jobj.value(QStringLiteral("externalNode")).toObject();
  m_externalNodeExcludePatterns = readStringList(externalNodeObj, QStringLiteral("excludePatterns"));
}

QJsonObject CoreConfig::saveShortcuts() const {
  QJsonObject obj;
  static const auto indexOfShortcutEnum =
      CoreConfig::staticMetaObject.indexOfEnumerator("Shortcut");
  Q_ASSERT(indexOfShortcutEnum >= 0);
  const auto metaEnum = CoreConfig::staticMetaObject.enumerator(indexOfShortcutEnum);
  // Skip the Max flag.
  for (int i = 0; i < metaEnum.keyCount() - 1; ++i) {
    obj[metaEnum.key(i)] = m_shortcuts[i];
  }
  return obj;
}

const QString &CoreConfig::getShortcut(Shortcut p_shortcut) const {
  Q_ASSERT(p_shortcut < Shortcut::MaxShortcut);
  return m_shortcuts[p_shortcut];
}

int CoreConfig::getToolBarIconSize() const { return m_toolBarIconSize; }

void CoreConfig::setToolBarIconSize(int p_size) {
  Q_ASSERT(p_size > 0);
  updateConfig(m_toolBarIconSize, p_size, this);
}

int CoreConfig::getDocksTabBarIconSize() const { return m_docksTabBarIconSize; }

void CoreConfig::setDocksTabBarIconSize(int p_size) {
  Q_ASSERT(p_size > 0);
  updateConfig(m_docksTabBarIconSize, p_size, this);
}

const QStringList &CoreConfig::getExternalNodeExcludePatterns() const {
  return m_externalNodeExcludePatterns;
}

bool CoreConfig::isRecoverLastSessionOnStartEnabled() const {
  return m_recoverLastSessionOnStartEnabled;
}

void CoreConfig::setRecoverLastSessionOnStartEnabled(bool p_enabled) {
  updateConfig(m_recoverLastSessionOnStartEnabled, p_enabled, this);
}

bool CoreConfig::isCheckForUpdatesOnStartEnabled() const { return m_checkForUpdatesOnStartEnabled; }

void CoreConfig::setCheckForUpdatesOnStartEnabled(bool p_enabled) {
  updateConfig(m_checkForUpdatesOnStartEnabled, p_enabled, this);
}

int CoreConfig::getHistoryMaxCount() const { return m_historyMaxCount; }

bool CoreConfig::isPerNotebookHistoryEnabled() const { return m_perNotebookHistoryEnabled; }

void CoreConfig::setPerNotebookHistoryEnabled(bool p_enabled) {
  updateConfig(m_perNotebookHistoryEnabled, p_enabled, this);
}

const QString &CoreConfig::getShortcutLeaderKey() const { return m_shortcutLeaderKey; }

LineEndingPolicy CoreConfig::getLineEndingPolicy() const { return m_lineEnding; }

void CoreConfig::setLineEndingPolicy(LineEndingPolicy p_ending) {
  updateConfig(m_lineEnding, p_ending, this);
}

void CoreConfig::loadUnitedEntry(const QJsonObject &p_jobj) {
  const auto unitedObj = p_jobj.value(QStringLiteral("unitedEntry")).toObject();
  m_unitedEntryAlias = unitedObj.value(QStringLiteral("alias")).toArray();
}

QJsonObject CoreConfig::saveUnitedEntry() const {
  QJsonObject unitedObj;
  unitedObj[QStringLiteral("alias")] = m_unitedEntryAlias;
  return unitedObj;
}

const QJsonArray &CoreConfig::getUnitedEntryAlias() const { return m_unitedEntryAlias; }

void CoreConfig::setUnitedEntryAlias(const QJsonArray &p_alias) {
  updateConfig(m_unitedEntryAlias, p_alias, this);
}

ViewWindowMode CoreConfig::getDefaultOpenMode() const { return m_defaultOpenMode; }

void CoreConfig::setDefaultOpenMode(ViewWindowMode p_mode) {
  updateConfig(m_defaultOpenMode, p_mode, this);
}

ViewWindowMode CoreConfig::stringToViewWindowMode(const QString &p_mode) {
  if (p_mode == "edit") {
    return ViewWindowMode::Edit;
  }

  return ViewWindowMode::Read;
}

QString CoreConfig::viewWindowModeToString(ViewWindowMode p_mode) {
  switch (p_mode) {
  case ViewWindowMode::Edit:
    return "edit";

  default:
    return "read";
  }
}

void CoreConfig::initDefaults() {
  m_shortcutLeaderKey = QStringLiteral("Ctrl+G");
  m_externalNodeExcludePatterns = {QStringLiteral(".gitignore"), QStringLiteral(".git")};

  m_shortcuts[Shortcut::FullScreen] = QStringLiteral("F11");
  m_shortcuts[Shortcut::StayOnTop] = QStringLiteral("F10");
  m_shortcuts[Shortcut::ExpandContentArea] = QStringLiteral("Ctrl+G, E");
  m_shortcuts[Shortcut::Settings] = QStringLiteral("Ctrl+Alt+P");
  m_shortcuts[Shortcut::NewNote] = QStringLiteral("Ctrl+Alt+N");
  m_shortcuts[Shortcut::NewQuickNote] = QStringLiteral("Ctrl+Alt+Q");
  m_shortcuts[Shortcut::NewFolder] = QStringLiteral("Ctrl+Alt+S");
  m_shortcuts[Shortcut::CloseTab] = QStringLiteral("Ctrl+G, X");
  m_shortcuts[Shortcut::CloseAllTabs] = QStringLiteral("");
  m_shortcuts[Shortcut::CloseOtherTabs] = QStringLiteral("");
  m_shortcuts[Shortcut::CloseTabsToTheLeft] = QStringLiteral("");
  m_shortcuts[Shortcut::CloseTabsToTheRight] = QStringLiteral("");
  m_shortcuts[Shortcut::NavigationDock] = QStringLiteral("Ctrl+G, A");
  m_shortcuts[Shortcut::OutlineDock] = QStringLiteral("Ctrl+G, U");
  m_shortcuts[Shortcut::SearchDock] = QStringLiteral("");
  m_shortcuts[Shortcut::SnippetDock] = QStringLiteral("Ctrl+G, S");
  m_shortcuts[Shortcut::LocationListDock] = QStringLiteral("Ctrl+G, C");
  m_shortcuts[Shortcut::HistoryDock] = QStringLiteral("");
  m_shortcuts[Shortcut::TagDock] = QStringLiteral("");
  m_shortcuts[Shortcut::Search] = QStringLiteral("Ctrl+Alt+F");
  m_shortcuts[Shortcut::NavigationMode] = QStringLiteral("Ctrl+G, W");
  m_shortcuts[Shortcut::LocateNode] = QStringLiteral("Ctrl+G, D");
  m_shortcuts[Shortcut::VerticalSplit] = QStringLiteral("Ctrl+G, \\");
  m_shortcuts[Shortcut::HorizontalSplit] = QStringLiteral("Ctrl+G, -");
  m_shortcuts[Shortcut::MaximizeSplit] = QStringLiteral("Ctrl+G, Shift+\\");
  m_shortcuts[Shortcut::DistributeSplits] = QStringLiteral("Ctrl+G, =");
  m_shortcuts[Shortcut::RemoveSplitAndWorkspace] = QStringLiteral("Ctrl+G, R");
  m_shortcuts[Shortcut::NewWorkspace] = QStringLiteral("");
  m_shortcuts[Shortcut::Export] = QStringLiteral("Ctrl+G, T");
  m_shortcuts[Shortcut::Quit] = QStringLiteral("Ctrl+Q");
  m_shortcuts[Shortcut::FlashPage] = QStringLiteral("Ctrl+Alt+L");
  m_shortcuts[Shortcut::QuickAccess] = QStringLiteral("Ctrl+Alt+I");
  m_shortcuts[Shortcut::ActivateTab1] = QStringLiteral("Ctrl+G, 1");
  m_shortcuts[Shortcut::ActivateTab2] = QStringLiteral("Ctrl+G, 2");
  m_shortcuts[Shortcut::ActivateTab3] = QStringLiteral("Ctrl+G, 3");
  m_shortcuts[Shortcut::ActivateTab4] = QStringLiteral("Ctrl+G, 4");
  m_shortcuts[Shortcut::ActivateTab5] = QStringLiteral("Ctrl+G, 5");
  m_shortcuts[Shortcut::ActivateTab6] = QStringLiteral("Ctrl+G, 6");
  m_shortcuts[Shortcut::ActivateTab7] = QStringLiteral("Ctrl+G, 7");
  m_shortcuts[Shortcut::ActivateTab8] = QStringLiteral("Ctrl+G, 8");
  m_shortcuts[Shortcut::ActivateTab9] = QStringLiteral("Ctrl+G, 9");
  m_shortcuts[Shortcut::AlternateTab] = QStringLiteral("Ctrl+G, 0");
  m_shortcuts[Shortcut::ActivateNextTab] = QStringLiteral("Ctrl+G, N");
  m_shortcuts[Shortcut::ActivatePreviousTab] = QStringLiteral("Ctrl+G, P");
  m_shortcuts[Shortcut::FocusContentArea] = QStringLiteral("Ctrl+G, Y");
  m_shortcuts[Shortcut::OpenWithDefaultProgram] = QStringLiteral("F9");
  m_shortcuts[Shortcut::OneSplitLeft] = QStringLiteral("Ctrl+G, H");
  m_shortcuts[Shortcut::OneSplitDown] = QStringLiteral("Ctrl+G, J");
  m_shortcuts[Shortcut::OneSplitUp] = QStringLiteral("Ctrl+G, K");
  m_shortcuts[Shortcut::OneSplitRight] = QStringLiteral("Ctrl+G, L");
  m_shortcuts[Shortcut::MoveOneSplitLeft] = QStringLiteral("Ctrl+G, Shift+H");
  m_shortcuts[Shortcut::MoveOneSplitDown] = QStringLiteral("Ctrl+G, Shift+J");
  m_shortcuts[Shortcut::MoveOneSplitUp] = QStringLiteral("Ctrl+G, Shift+K");
  m_shortcuts[Shortcut::MoveOneSplitRight] = QStringLiteral("Ctrl+G, Shift+L");
  m_shortcuts[Shortcut::OpenLastClosedFile] = QStringLiteral("Ctrl+Shift+T");
  m_shortcuts[Shortcut::UnitedEntry] = QStringLiteral("Ctrl+G, G");
  m_shortcuts[Shortcut::Copy] = QStringLiteral("Ctrl+C");
  m_shortcuts[Shortcut::Cut] = QStringLiteral("Ctrl+X");
  m_shortcuts[Shortcut::Paste] = QStringLiteral("Ctrl+V");
  m_shortcuts[Shortcut::Properties] = QStringLiteral("F2");
  m_shortcuts[Shortcut::Global_WakeUp] = QStringLiteral("Ctrl+Alt+U");

  auto makeAlias = [](const QString &p_name, const QString &p_desc, const QString &p_value) {
    QJsonObject obj;
    obj[QStringLiteral("name")] = p_name;
    obj[QStringLiteral("description")] = p_desc;
    obj[QStringLiteral("value")] = p_value;
    return obj;
  };
  QJsonArray aliases;
  aliases.append(makeAlias(QStringLiteral("q"), QStringLiteral("Search for folders/files by name in all notebooks"), QStringLiteral("find --scope all_notebook --object name --target file --target folder")));
  aliases.append(makeAlias(QStringLiteral("a"), QStringLiteral("Search for files by content in all notebooks"), QStringLiteral("find --scope all_notebook --object content --target file")));
  aliases.append(makeAlias(QStringLiteral("z"), QStringLiteral("Search for files by tag in all notebooks"), QStringLiteral("find --scope all_notebook --object tag --target file")));
  aliases.append(makeAlias(QStringLiteral("w"), QStringLiteral("Search for notebooks by name in all notebooks"), QStringLiteral("find --scope all_notebook --object name --target notebook")));
  aliases.append(makeAlias(QStringLiteral("e"), QStringLiteral("Search for folders/files by name in current notebook"), QStringLiteral("find --scope notebook --object name --target file --target folder")));
  aliases.append(makeAlias(QStringLiteral("d"), QStringLiteral("Search for files by content in current notebook"), QStringLiteral("find --scope notebook --object content --target file")));
  aliases.append(makeAlias(QStringLiteral("c"), QStringLiteral("Search for files by tag in current notebook"), QStringLiteral("find --scope notebook --object tag --target file")));
  aliases.append(makeAlias(QStringLiteral("r"), QStringLiteral("Search for folders/files by name in current folder"), QStringLiteral("find --scope folder --object name --target file --target folder")));
  aliases.append(makeAlias(QStringLiteral("f"), QStringLiteral("Search for files by content in current folder"), QStringLiteral("find --scope folder --object content --target file")));
  aliases.append(makeAlias(QStringLiteral("v"), QStringLiteral("Search for files by tag in current folder"), QStringLiteral("find --scope folder --object tag --target file")));
  aliases.append(makeAlias(QStringLiteral("t"), QStringLiteral("Search for files by name in buffers"), QStringLiteral("find --scope buffer --object name --target file")));
  aliases.append(makeAlias(QStringLiteral("g"), QStringLiteral("Search for files by content in buffers"), QStringLiteral("find --scope buffer --object content --target file")));
  m_unitedEntryAlias = aliases;
}
