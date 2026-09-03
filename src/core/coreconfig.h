#ifndef CORECONFIG_H
#define CORECONFIG_H

#include "iconfig.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "global.h"

namespace vnotex {
class IConfigMgr;

class CoreConfig : public IConfig {
  Q_GADGET
public:
  enum Shortcut {
    FullScreen,
    StayOnTop,
    ExpandContentArea,
    Settings,
    NewNote,
    NewQuickNote,
    NewFolder,
    CloseFocus,
    CloseAllTabs,
    CloseOtherTabs,
    CloseTabsToTheLeft,
    CloseTabsToTheRight,
    NavigationDock,
    OutlineDock,
    SearchDock,
    SnippetDock,
    LocationListDock,
    HistoryDock,
    TagDock,
    ConsoleDock,
    Search,
    NavigationMode,
    LocateNode,
    VerticalSplit,
    HorizontalSplit,
    MaximizeSplit,
    DistributeSplits,
    RemoveSplitAndWorkspace,
    NewWorkspace,
    Export,
    Quit,
    QuickAccess,
    ActivateTab1,
    ActivateTab2,
    ActivateTab3,
    ActivateTab4,
    ActivateTab5,
    ActivateTab6,
    ActivateTab7,
    ActivateTab8,
    ActivateTab9,
    AlternateTab,
    ActivateNextTab,
    ActivatePreviousTab,
    FocusContentArea,
    OpenWithDefaultProgram,
    OneSplitLeft,
    OneSplitDown,
    OneSplitUp,
    OneSplitRight,
    MoveOneSplitLeft,
    MoveOneSplitDown,
    MoveOneSplitUp,
    MoveOneSplitRight,
    Detach,
    OpenLastClosedFile,
    UnitedEntry,
    Copy,
    Paste,
    Cut,
    Properties,
    Global_WakeUp,
    MaxShortcut
  };
  Q_ENUM(Shortcut)

  CoreConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  const QString &getTheme() const;
  void setTheme(const QString &p_name);

  // Display-only branding. Keep ConfigMgr2::c_appName for stable application identity.
  const QString &getAppName() const;
  void setAppName(const QString &p_name);

  const QString &getLocale() const;
  void setLocale(const QString &p_locale);

  // Should be called after locale is properly set.
  QString getLocaleToUse() const;

  const QString &getShortcut(Shortcut p_shortcut) const;

  int getToolBarIconSize() const;
  void setToolBarIconSize(int p_size);

  int getDocksTabBarIconSize() const;
  void setDocksTabBarIconSize(int p_size);

  const QStringList &getExternalNodeExcludePatterns() const;

  static const QStringList &getAvailableLocales();

  bool isCheckForUpdatesOnStartEnabled() const;
  void setCheckForUpdatesOnStartEnabled(bool p_enabled);

  // Which forge VNote checks for a newer release: "github" or "gitee".
  // Anything that is not an explicit "github" (including an absent key)
  // normalizes to "gitee".
  const QString &getUpdateSource() const;
  void setUpdateSource(const QString &p_source);

  // Trims and lower-cases p_source. ONLY an explicit "github" selects GitHub;
  // every other value -- empty, absent, or unrecognized -- normalizes to
  // "gitee". Keep in step with UpdateService::sourceFromString().
  static QString normalizeUpdateSource(const QString &p_source);

  // Version string the user chose to skip (e.g. "4.3.2"). Empty when nothing is skipped.
  const QString &getSkippedUpdateVersion() const;
  void setSkippedUpdateVersion(const QString &p_version);

  // Epoch milliseconds (UTC) of the last update check that was *started*.
  // 0 means "never checked". Stored as a decimal string because IConfig::readInt
  // is 32-bit and cannot hold an epoch-millisecond value.
  qint64 getLastUpdateCheckTime() const;
  void setLastUpdateCheckTime(qint64 p_msSinceEpoch);

  // True when a new update check should be started now, i.e. the last recorded
  // check is older than p_intervalMs, never happened, or is dated in the future
  // (clock moved backwards / corrupted value -> treat as stale).
  bool isUpdateCheckDue(qint64 p_nowMsSinceEpoch, qint64 p_intervalMs) const;

  // Default throttle between two automatic (startup) update checks.
  static constexpr qint64 c_updateCheckIntervalMs = 24LL * 60 * 60 * 1000;

  int getHistoryMaxCount() const;

  int getSearchMaxResults() const;
  void setSearchMaxResults(int p_count);

  const QString &getShortcutLeaderKey() const;

  LineEndingPolicy getLineEndingPolicy() const;
  void setLineEndingPolicy(LineEndingPolicy p_ending);

  const QJsonArray &getUnitedEntryAlias() const;
  void setUnitedEntryAlias(const QJsonArray &p_alias);

  ViewWindowMode getDefaultOpenMode() const;
  void setDefaultOpenMode(ViewWindowMode p_mode);

private:
  friend class MainConfig;

  void loadShortcuts(const QJsonObject &p_jobj);

  void loadNoteManagement(const QJsonObject &p_jobj);

  QJsonObject saveShortcuts() const;

  void loadUnitedEntry(const QJsonObject &p_jobj);

  QJsonObject saveUnitedEntry() const;

  static ViewWindowMode stringToViewWindowMode(const QString &p_mode);
  static QString viewWindowModeToString(ViewWindowMode p_mode);

  // Backward-compatible parse of the lastUpdateCheckTime JSON value: accepts a
  // decimal string (current format) and a bare JSON number (tolerated so a
  // hand-edited or legacy config is not silently dropped). Anything else -> 0.
  static qint64 parseLastUpdateCheckTime(const QJsonValue &p_value);

  static QString normalizeAppName(const QString &p_name);

  void initDefaults();

  // Theme name.
  QString m_theme{"pure"};

  QString m_appName{QStringLiteral("VNote")};

  // User-specified locale, such as zh_CN, en_US.
  // Empty if not specified.
  QString m_locale;

  QString m_shortcuts[Shortcut::MaxShortcut];

  // Leader key of shortcuts defined in m_shortctus.
  QString m_shortcutLeaderKey;

  // Icon size of MainWindow tool bar.
  int m_toolBarIconSize = 18;

  // Icon size of MainWindow QDockWidgets tab bar.
  int m_docksTabBarIconSize = 20;

  QStringList m_externalNodeExcludePatterns;

  bool m_checkForUpdatesOnStartEnabled = true;

  // Release source used by the update checker. Defaults live in C++; there is
  // no bundled vnotex.json entry for any of the update keys. Gitee is the
  // default because it is reachable for the majority of VNote's users; see
  // normalizeUpdateSource() for why existing configs are not migrated.
  QString m_updateSource{QStringLiteral("gitee")};

  // Version the user explicitly skipped in the update dialog.
  QString m_skippedUpdateVersion;

  // Epoch ms (UTC) of the last started update check. 0 == never.
  qint64 m_lastUpdateCheckTime = 0;

  // Max count of the history items for each notebook and session config.
  int m_historyMaxCount = 100;

  // Max number of results returned by a search.
  int m_searchMaxResults = 1000;

  LineEndingPolicy m_lineEnding = LineEndingPolicy::LF;

  QJsonArray m_unitedEntryAlias;

  ViewWindowMode m_defaultOpenMode = ViewWindowMode::Read;

  static QStringList s_availableLocales;
};
} // namespace vnotex

#endif // CORECONFIG_H
