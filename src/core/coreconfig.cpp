#include "coreconfig.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMetaEnum>

#include <utils/utils.h>

using namespace vnotex;

namespace {
const int c_defaultSearchMaxResults = 1000;
const int c_minSearchMaxResults = 1;
const int c_maxSearchMaxResults = 100000;
} // namespace

#define READSTR(key) readString(p_jobj, (key))
#define READINT(key) readInt(p_jobj, (key))
#define READBOOL(key) readBool(p_jobj, (key))
#define READSTRLIST(key) readStringList(p_jobj, (key))

QStringList CoreConfig::s_availableLocales;

// Out-of-line definition required by C++14 when the constant is odr-used
// (e.g. bound to a qint64 reference). Redundant, but harmless, from C++17 on.
constexpr qint64 CoreConfig::c_updateCheckIntervalMs;


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

  m_checkForUpdatesOnStartEnabled = READBOOL(QStringLiteral("checkForUpdatesOnStart"));

  m_skippedUpdateVersion = READSTR(QStringLiteral("skippedUpdateVersion"));

  m_lastUpdateCheckTime =
      parseLastUpdateCheckTime(read(p_jobj, QStringLiteral("lastUpdateCheckTime")));

  m_historyMaxCount = READINT(QStringLiteral("historyMaxCount"));
  if (m_historyMaxCount < 0) {
    m_historyMaxCount = 100;
  }

  m_searchMaxResults = READINT(QStringLiteral("searchMaxResults"));
  if (m_searchMaxResults <= 0) {
    m_searchMaxResults = c_defaultSearchMaxResults;
  } else if (m_searchMaxResults > c_maxSearchMaxResults) {
    m_searchMaxResults = c_maxSearchMaxResults;
  }

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
  obj[QStringLiteral("checkForUpdatesOnStart")] = m_checkForUpdatesOnStartEnabled;
  obj[QStringLiteral("skippedUpdateVersion")] = m_skippedUpdateVersion;
  // Decimal string: IConfig::readInt is 32-bit and QJsonValue::toInt() would
  // truncate an epoch-millisecond value.
  obj[QStringLiteral("lastUpdateCheckTime")] = QString::number(m_lastUpdateCheckTime);
  obj[QStringLiteral("historyMaxCount")] = m_historyMaxCount;
  obj[QStringLiteral("searchMaxResults")] = m_searchMaxResults;
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

  // Back-compat: CloseTab was renamed to CloseFocus. Preserve a user's
  // customized legacy binding. Saving will thereafter emit only "CloseFocus".
  if (!p_jobj.contains(QStringLiteral("CloseFocus")) &&
      p_jobj.contains(QStringLiteral("CloseTab"))) {
    m_shortcuts[Shortcut::CloseFocus] = readString(p_jobj, QStringLiteral("CloseTab"));
  }
}

void CoreConfig::loadNoteManagement(const QJsonObject &p_jobj) {
  // External node.
  const auto externalNodeObj = p_jobj.value(QStringLiteral("externalNode")).toObject();
  m_externalNodeExcludePatterns =
      readStringList(externalNodeObj, QStringLiteral("excludePatterns"));
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

bool CoreConfig::isCheckForUpdatesOnStartEnabled() const { return m_checkForUpdatesOnStartEnabled; }

void CoreConfig::setCheckForUpdatesOnStartEnabled(bool p_enabled) {
  updateConfig(m_checkForUpdatesOnStartEnabled, p_enabled, this);
}

const QString &CoreConfig::getSkippedUpdateVersion() const { return m_skippedUpdateVersion; }

void CoreConfig::setSkippedUpdateVersion(const QString &p_version) {
  updateConfig(m_skippedUpdateVersion, p_version, this);
}

qint64 CoreConfig::getLastUpdateCheckTime() const { return m_lastUpdateCheckTime; }

void CoreConfig::setLastUpdateCheckTime(qint64 p_msSinceEpoch) {
  updateConfig(m_lastUpdateCheckTime, p_msSinceEpoch, this);
}

bool CoreConfig::isUpdateCheckDue(qint64 p_nowMsSinceEpoch, qint64 p_intervalMs) const {
  if (m_lastUpdateCheckTime <= 0) {
    // Never checked.
    return true;
  }

  if (m_lastUpdateCheckTime > p_nowMsSinceEpoch) {
    // Future-dated (clock moved backwards, or a corrupted value): treat as stale
    // so the throttle can never wedge a user out of update checks forever.
    return true;
  }

  return (p_nowMsSinceEpoch - m_lastUpdateCheckTime) >= p_intervalMs;
}

qint64 CoreConfig::parseLastUpdateCheckTime(const QJsonValue &p_value) {
  if (p_value.isString()) {
    bool ok = false;
    const qint64 val = p_value.toString().toLongLong(&ok);
    return (ok && val > 0) ? val : 0;
  }

  if (p_value.isDouble()) {
    // Backward/forward compatibility with a bare JSON number. Values beyond
    // 2^53 are not representable exactly, but epoch ms stays far below that.
    const double val = p_value.toDouble();
    return val > 0 ? static_cast<qint64>(val) : 0;
  }

  return 0;
}

int CoreConfig::getHistoryMaxCount() const { return m_historyMaxCount; }

int CoreConfig::getSearchMaxResults() const { return m_searchMaxResults; }

void CoreConfig::setSearchMaxResults(int p_count) {
  int clamped = qBound(c_minSearchMaxResults, p_count, c_maxSearchMaxResults);
  updateConfig(m_searchMaxResults, clamped, this);
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
  m_externalNodeExcludePatterns = QStringList{QStringLiteral(".gitignore"), QStringLiteral(".git")};

  m_shortcuts[Shortcut::FullScreen] = QStringLiteral("F11");
  m_shortcuts[Shortcut::StayOnTop] = QStringLiteral("F10");
  m_shortcuts[Shortcut::ExpandContentArea] = QStringLiteral("Ctrl+G, E");
  m_shortcuts[Shortcut::Settings] = QStringLiteral("Ctrl+Alt+P");
  m_shortcuts[Shortcut::NewNote] = QStringLiteral("Ctrl+Alt+N");
  m_shortcuts[Shortcut::NewQuickNote] = QStringLiteral("Ctrl+Alt+Q");
  m_shortcuts[Shortcut::NewFolder] = QStringLiteral("Ctrl+Alt+S");
  m_shortcuts[Shortcut::CloseFocus] = QStringLiteral("Ctrl+G, X");
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
  m_shortcuts[Shortcut::ConsoleDock] = QStringLiteral("");
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
  m_shortcuts[Shortcut::Detach] = QStringLiteral("Ctrl+G, Shift+D");
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
  aliases.append(makeAlias(QStringLiteral("n"),
                           QStringLiteral("Search for files by name in current notebook"),
                           QStringLiteral("find --scope notebook --object name")));
  aliases.append(makeAlias(QStringLiteral("g"),
                           QStringLiteral("Search for files by content in current notebook"),
                           QStringLiteral("find --scope notebook --object content")));
  aliases.append(makeAlias(QStringLiteral("b"),
                           QStringLiteral("Search for files by content in open buffers"),
                           QStringLiteral("find --scope buffer --object content")));
  aliases.append(makeAlias(QStringLiteral("f"),
                           QStringLiteral("Search for files by name in current folder"),
                           QStringLiteral("find --scope folder --object name")));
  m_unitedEntryAlias = aliases;
}
