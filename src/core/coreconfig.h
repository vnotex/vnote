#ifndef CORECONFIG_H
#define CORECONFIG_H

#include "iconfig.h"

#include <QtGlobal>
#include <QString>
#include <QStringList>

#include "global.h"

namespace vnotex
{
    class CoreConfig : public IConfig
    {
        Q_GADGET
    public:
        enum Shortcut
        {
            FullScreen,
            StayOnTop,
            ExpandContentArea,
            Settings,
            NewNote,
            NewQuickNote,
            NewFolder,
            CloseTab,
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
            WindowsDock,
            TagDock,
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
            FlashPage,
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

        struct FileTypeSuffix
        {
            FileTypeSuffix() = default;

            FileTypeSuffix(const QString &p_name, const QStringList &p_suffixes);

            bool operator==(const FileTypeSuffix &p_other) const;

            QString m_name;

            QStringList m_suffixes;
        };

        CoreConfig(ConfigMgr *p_mgr, IConfig *p_topConfig);

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        const QString &getTheme() const;
        void setTheme(const QString &p_name);

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

        bool isRecoverLastSessionOnStartEnabled() const;
        void setRecoverLastSessionOnStartEnabled(bool p_enabled);

        bool isCheckForUpdatesOnStartEnabled() const;
        void setCheckForUpdatesOnStartEnabled(bool p_enabled);

        int getHistoryMaxCount() const;

        bool isPerNotebookHistoryEnabled() const;
        void setPerNotebookHistoryEnabled(bool p_enabled);

        const QString &getShortcutLeaderKey() const;

        LineEndingPolicy getLineEndingPolicy() const;
        void setLineEndingPolicy(LineEndingPolicy p_ending);

        const QVector<FileTypeSuffix> &getFileTypeSuffixes() const;
        void setFileTypeSuffixes(const QVector<FileTypeSuffix> &p_fileTypeSuffixes);

        const QStringList *findFileTypeSuffix(const QString &p_name) const;

        const QJsonArray &getUnitedEntryAlias() const;
        void setUnitedEntryAlias(const QJsonArray &p_alias);

        ViewWindowMode getDefaultOpenMode() const;
        void setDefaultOpenMode(ViewWindowMode p_mode);

    private:
        friend class MainConfig;

        void loadShortcuts(const QJsonObject &p_app, const QJsonObject &p_user);

        void loadNoteManagement(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonObject saveShortcuts() const;

        void loadFileTypeSuffixes(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonArray saveFileTypeSuffixes() const;

        void loadUnitedEntry(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonObject saveUnitedEntry() const;

        static ViewWindowMode stringToViewWindowMode(const QString &p_mode);
        static QString viewWindowModeToString(ViewWindowMode p_mode);

        // Theme name.
        QString m_theme;

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

        // Whether recover last session on start.
        bool m_recoverLastSessionOnStartEnabled = true;

        bool m_checkForUpdatesOnStartEnabled = true;

        // Max count of the history items for each notebook and session config.
        int m_historyMaxCount = 100;

        // Whether store history in each notebook.
        bool m_perNotebookHistoryEnabled = false;

        LineEndingPolicy m_lineEnding = LineEndingPolicy::LF;

        QVector<FileTypeSuffix> m_fileTypeSuffixes;

        QJsonArray m_unitedEntryAlias;

        ViewWindowMode m_defaultOpenMode = ViewWindowMode::Read;

        static QStringList s_availableLocales;
    };
} // ns vnotex

#endif // CORECONFIG_H
