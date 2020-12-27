#ifndef CORECONFIG_H
#define CORECONFIG_H

#include "iconfig.h"

#include <QtGlobal>
#include <QString>
#include <QStringList>

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
            CloseTab,
            NavigationDock,
            OutlineDock,
            NavigationMode,
            LocateNode,
            VerticalSplit,
            HorizontalSplit,
            MaximizeSplit,
            DistributeSplits,
            RemoveSplitAndWorkspace,
            NewWorkspace,
            MaxShortcut
        };
        Q_ENUM(Shortcut)

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

        static const QStringList &getAvailableLocales();

    private:
        void loadShortcuts(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonObject saveShortcuts() const;

        // Theme name.
        QString m_theme;

        // User-specified locale, such as zh_CN, en_US.
        // Empty if not specified.
        QString m_locale;

        QString m_shortcuts[Shortcut::MaxShortcut];

        // Icon size of MainWindow tool bar.
        int m_toolBarIconSize = 16;

        static QStringList s_availableLocales;
    };
} // ns vnotex

#endif // CORECONFIG_H
