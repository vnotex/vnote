#include "coreconfig.h"

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
