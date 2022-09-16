#ifndef DOCKWIDGETHELPER_H
#define DOCKWIDGETHELPER_H

#include <QVector>
#include <QIcon>
#include <QSet>
#include <QPair>

#include "navigationmode.h"

class QDockWidget;
class QTabBar;

namespace vnotex
{
    class MainWindow;

    // Dock widget helper for MainWindow.
    class DockWidgetHelper : public QObject, public NavigationMode
    {
        Q_OBJECT
    public:
        // Index in m_docks.
        enum DockIndex
        {
            NavigationDock = 0,
            HistoryDock,
            TagDock,
            SearchDock,
            SnippetDock,
            OutlineDock,
            WindowsDock,
            ConsoleDock,
            LocationListDock,
            MaxDock
        };
        Q_ENUM(DockIndex)

        explicit DockWidgetHelper(MainWindow *p_mainWindow);

        void setupDocks();

        void postSetup();

        void activateDock(DockIndex p_dockIndex);

        QDockWidget *getDock(DockIndex p_dockIndex) const;

        const QVector<QDockWidget *> &getDocks() const;

        void updateDockWidgetTabBar();

        QStringList getVisibleDocks() const;

        QStringList hideDocks();

        void restoreDocks(const QStringList &p_visibleDocks);

    // NavigationMode.
    protected:
        QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

        void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

        void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

        void clearNavigation() Q_DECL_OVERRIDE;

    protected:
        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

    private:
        struct NavigationItemInfo
        {
            NavigationItemInfo() = default;

            NavigationItemInfo(QTabBar *p_tabBar, int p_tabIndex, int p_dockIndex);

            NavigationItemInfo(int p_dockIndex);

            QTabBar *m_tabBar = nullptr;

            int m_tabIndex = -1;

            int m_dockIndex = -1;
        };

        struct IconInfo
        {
            QIcon m_icon;

            int m_rotationAngle = INT_MIN;

            bool m_isSideBar = false;
        };

        void setupNavigationDock();

        void setupOutlineDock();

        void setupWindowsDock();

        void setupConsoleDock();

        void setupSearchDock();

        void setupSnippetDock();

        void setupHistoryDock();

        void setupTagDock();

        void setupLocationListDock();

        QDockWidget *createDockWidget(DockIndex p_dockIndex, const QString &p_title, QWidget *p_parent);

        void setupShortcuts();

        void activateDock(QDockWidget *p_dock);

        void setupDockActivateShortcut(QDockWidget *p_dock, const QString &p_keys);

        const QIcon &getDockIcon(DockIndex p_dockIndex, bool p_isSideBar);

        static QString iconFileName(DockIndex p_dockIndex);

        MainWindow *m_mainWindow = nullptr;

        QVector<IconInfo> m_dockIcons;

        QVector<QDockWidget *> m_docks;

        // We need to install event filter to the tabbar of tabified dock widgets.
        QSet<QTabBar *> m_tabBarsMonitored;

        QVector<NavigationItemInfo> m_navigationItems;
    };
}

#endif // DOCKWIDGETHELPER_H
