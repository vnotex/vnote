#include "dockwidgethelper.h"

#include <QDockWidget>
#include <QTabBar>
#include <QBitArray>
#include <QHelpEvent>
#include <QToolTip>
#include <QShortcut>
#include <QTextEdit>

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/widgetconfig.h>
#include <utils/iconutils.h>
#include <utils/widgetutils.h>

#include "mainwindow.h"
#include "propertydefs.h"
#include "notebookexplorer.h"
#include "outlineviewer.h"
#include "locationlist.h"
#include "searchpanel.h"
#include "snippetpanel.h"
#include "historypanel.h"
#include "windowspanel.h"
#include "tagexplorer.h"
#include "consoleviewer.h"

using namespace vnotex;

DockWidgetHelper::NavigationItemInfo::NavigationItemInfo(QTabBar *p_tabBar, int p_tabIndex, int p_dockIndex)
    : m_tabBar(p_tabBar),
      m_tabIndex(p_tabIndex),
      m_dockIndex(p_dockIndex)
{
}

DockWidgetHelper::NavigationItemInfo::NavigationItemInfo(int p_dockIndex)
    : m_dockIndex(p_dockIndex)
{
}

DockWidgetHelper::DockWidgetHelper(MainWindow *p_mainWindow)
    : QObject(p_mainWindow),
      NavigationMode(NavigationMode::Type::DoubleKeys, p_mainWindow),
      m_mainWindow(p_mainWindow)
{
}

static int rotationAngle(Qt::DockWidgetArea p_area)
{
    switch (p_area) {
    case Qt::LeftDockWidgetArea:
        return 90;

    case Qt::RightDockWidgetArea:
        return 270;

    default:
        return -1;
    }
}

QString DockWidgetHelper::iconFileName(DockIndex p_dockIndex)
{
    switch (p_dockIndex) {
    case DockIndex::NavigationDock:
        return "navigation_dock.svg";
    case DockIndex::OutlineDock:
        return "outline_dock.svg";
    case DockIndex::HistoryDock:
        return "history_dock.svg";
    case DockIndex::WindowsDock:
        return "windows_dock.svg";
    case DockIndex::TagDock:
        return "tag_dock.svg";
    case DockIndex::SearchDock:
        return "search_dock.svg";
    case DockIndex::SnippetDock:
        return "snippet_dock.svg";
    case DockIndex::LocationListDock:
        return "location_list_dock.svg";
    case DockIndex::ConsoleDock:
        return "console_dock.svg";
    default:
        return QString();
    }
}

void DockWidgetHelper::setupDocks()
{
    m_mainWindow->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::West);
    m_mainWindow->setTabPosition(Qt::RightDockWidgetArea, QTabWidget::East);
    m_mainWindow->setTabPosition(Qt::TopDockWidgetArea, QTabWidget::North);
    m_mainWindow->setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    m_mainWindow->setDockNestingEnabled(true);

    m_dockIcons.resize(DockIndex::MaxDock);

    // The order of m_docks should be identical with enum DockIndex.
    QVector<int> tabifiedDockIndex;

    tabifiedDockIndex.append(m_docks.size());
    setupNavigationDock();

    tabifiedDockIndex.append(m_docks.size());
    setupHistoryDock();

    tabifiedDockIndex.append(m_docks.size());
    setupTagDock();

    tabifiedDockIndex.append(m_docks.size());
    setupSearchDock();

    tabifiedDockIndex.append(m_docks.size());
    setupSnippetDock();

    setupOutlineDock();

    setupWindowsDock();

    setupConsoleDock();

    setupLocationListDock();

    setupShortcuts();

    for (int i = 1; i < tabifiedDockIndex.size(); ++i) {
        m_mainWindow->tabifyDockWidget(m_docks[tabifiedDockIndex[i - 1]], m_docks[tabifiedDockIndex[i]]);
    }
}

static void addWidgetToDock(QDockWidget *p_dock, QWidget *p_widget)
{
    p_dock->setWidget(p_widget);
    p_dock->setFocusProxy(p_widget);
}

void DockWidgetHelper::setupNavigationDock()
{
    auto dock = createDockWidget(DockIndex::NavigationDock, tr("Navigation"), m_mainWindow);

    dock->setObjectName(QStringLiteral("NavigationDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_notebookExplorer);
    m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void DockWidgetHelper::setupOutlineDock()
{
    auto dock = createDockWidget(DockIndex::OutlineDock, tr("Outline"), m_mainWindow);

    dock->setObjectName(QStringLiteral("OutlineDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_outlineViewer);
    m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
}

void DockWidgetHelper::setupWindowsDock()
{
    auto dock = createDockWidget(DockIndex::WindowsDock, tr("Open Windows"), m_mainWindow);

    dock->setObjectName(QStringLiteral("WindowsDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_windowsPanel);
    m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
}

void DockWidgetHelper::setupConsoleDock()
{
    auto dock = createDockWidget(DockIndex::ConsoleDock, tr("Console"), m_mainWindow);

    dock->setObjectName(QStringLiteral("ConsoleDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_consoleViewer);
    m_mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->hide();
}

void DockWidgetHelper::setupSearchDock()
{
    auto dock = createDockWidget(DockIndex::SearchDock, tr("Search"), m_mainWindow);

    dock->setObjectName(QStringLiteral("SearchDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_searchPanel);
    m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void DockWidgetHelper::setupSnippetDock()
{
    auto dock = createDockWidget(DockIndex::SnippetDock, tr("Snippets"), m_mainWindow);

    dock->setObjectName(QStringLiteral("SnippetDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_snippetPanel);
    m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void DockWidgetHelper::setupHistoryDock()
{
    auto dock = createDockWidget(DockIndex::HistoryDock, tr("History"), m_mainWindow);

    dock->setObjectName(QStringLiteral("HistoryDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_historyPanel);
    m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void DockWidgetHelper::setupTagDock()
{
    auto dock = createDockWidget(DockIndex::TagDock, tr("Tags"), m_mainWindow);

    dock->setObjectName(QStringLiteral("TagDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_tagExplorer);
    m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void DockWidgetHelper::setupLocationListDock()
{
    auto dock = createDockWidget(DockIndex::LocationListDock, tr("Location List"), m_mainWindow);

    dock->setObjectName(QStringLiteral("LocationListDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addWidgetToDock(dock, m_mainWindow->m_locationList);
    m_mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->hide();
}

QDockWidget *DockWidgetHelper::createDockWidget(DockIndex p_dockIndex, const QString &p_title, QWidget *p_parent)
{
    auto dock = new QDockWidget(p_title, p_parent);
    dock->setToolTip(p_title);
    dock->setProperty(PropertyDefs::c_dockWidgetIndex, p_dockIndex);
    dock->setProperty(PropertyDefs::c_dockWidgetTitle, p_title);
    m_docks.push_back(dock);
    return dock;
}

void DockWidgetHelper::activateDock(DockIndex p_dockIndex)
{
    Q_ASSERT(p_dockIndex < DockIndex::MaxDock);
    activateDock(getDock(p_dockIndex));
}

void DockWidgetHelper::activateDock(QDockWidget *p_dock)
{
    bool needUpdateTabBar = !p_dock->isVisible();

    p_dock->show();
    Q_FOREACH(QTabBar* tabBar, m_mainWindow->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        bool found = false;
        for (int i = 0; i < tabBar->count(); ++i) {
            if (p_dock == reinterpret_cast<QWidget *>(tabBar->tabData(i).toULongLong())) {
                tabBar->setCurrentIndex(i);
                found = true;
                break;
            }
        }

        if (found) {
            break;
        }
    }

    p_dock->setFocus();

    if (needUpdateTabBar) {
        updateDockWidgetTabBar();
    }
}

const QVector<QDockWidget *> &DockWidgetHelper::getDocks() const
{
    return m_docks;
}

QDockWidget *DockWidgetHelper::getDock(DockIndex p_dockIndex) const
{
    Q_ASSERT(p_dockIndex < DockIndex::MaxDock);
    return m_docks[p_dockIndex];
}

void DockWidgetHelper::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    setupDockActivateShortcut(m_docks[DockIndex::NavigationDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::NavigationDock));

    setupDockActivateShortcut(m_docks[DockIndex::OutlineDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::OutlineDock));

    setupDockActivateShortcut(m_docks[DockIndex::HistoryDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::HistoryDock));

    setupDockActivateShortcut(m_docks[DockIndex::TagDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::TagDock));

    setupDockActivateShortcut(m_docks[DockIndex::SearchDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::SearchDock));
    // Extra shortcut for SearchDock.
    setupDockActivateShortcut(m_docks[DockIndex::SearchDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::Search));

    setupDockActivateShortcut(m_docks[DockIndex::LocationListDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::LocationListDock));

    setupDockActivateShortcut(m_docks[DockIndex::SnippetDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::SnippetDock));

    setupDockActivateShortcut(m_docks[DockIndex::WindowsDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::WindowsDock));

}

void DockWidgetHelper::setupDockActivateShortcut(QDockWidget *p_dock, const QString &p_keys)
{
    auto shortcut = WidgetUtils::createShortcut(p_keys, m_mainWindow);
    if (shortcut) {
        p_dock->setToolTip(QString("%1\t%2").arg(p_dock->windowTitle(),
                                                 QKeySequence(p_keys).toString(QKeySequence::NativeText)));
        connect(shortcut, &QShortcut::activated,
                this, [this, p_dock]() {
                    activateDock(p_dock);
                });
    }
}

void DockWidgetHelper::postSetup()
{
    updateDockWidgetTabBar();

    for (const auto dock : m_docks) {
        connect(dock, &QDockWidget::dockLocationChanged,
                this, &DockWidgetHelper::updateDockWidgetTabBar);
        connect(dock, &QDockWidget::topLevelChanged,
                this, &DockWidgetHelper::updateDockWidgetTabBar);
    }
}

void DockWidgetHelper::updateDockWidgetTabBar()
{
    QBitArray tabifiedDocks(m_docks.size(), false);
    Q_FOREACH(QTabBar* tabBar, m_mainWindow->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!m_tabBarsMonitored.contains(tabBar)) {
            m_tabBarsMonitored.insert(tabBar);
            tabBar->installEventFilter(this);
        }

        tabBar->setDrawBase(false);

        const int sz = ConfigMgr::getInst().getCoreConfig().getDocksTabBarIconSize();
        tabBar->setIconSize(QSize(sz, sz));

        auto tabShape = tabBar->shape();
        bool iconOnly = tabShape == QTabBar::RoundedWest || tabShape == QTabBar::RoundedEast
                        || tabShape == QTabBar::TriangularWest || tabShape == QTabBar::TriangularEast;
        const int cnt = tabBar->count();
        if (cnt == 1) {
            iconOnly = false;
        }

        bool isSideBar = iconOnly && (tabShape == QTabBar::RoundedWest || tabShape == QTabBar::TriangularWest);
        if (tabBar->property(PropertyDefs::c_mainWindowSideBar).toBool() != isSideBar) {
            WidgetUtils::setPropertyDynamically(tabBar, PropertyDefs::c_mainWindowSideBar, isSideBar);
        }

        for (int i = 0; i < cnt; ++i) {
            auto dock = reinterpret_cast<QDockWidget *>(tabBar->tabData(i).toULongLong());
            if (!dock) {
                continue;
            }
            int dockIdx = dock->property(PropertyDefs::c_dockWidgetIndex).toInt();
            tabifiedDocks.setBit(dockIdx);
            if (iconOnly) {
                dock->setWindowTitle(QString());
            } else if (dock->windowTitle().isEmpty()) {
                dock->setWindowTitle(dock->property(PropertyDefs::c_dockWidgetTitle).toString());
            }
            tabBar->setTabIcon(i, getDockIcon(static_cast<DockIndex>(dockIdx), isSideBar));

            if (dock->property(PropertyDefs::c_mainWindowSideBar).toBool() != isSideBar) {
                WidgetUtils::setPropertyDynamically(dock, PropertyDefs::c_mainWindowSideBar, isSideBar);
            }
        }
    }

    // Non-tabified docks.
    for (int i = 0; i < m_docks.size(); ++i) {
        if (!tabifiedDocks[i] && m_docks[i]->windowTitle().isEmpty()) {
            m_docks[i]->setWindowTitle(m_docks[i]->property(PropertyDefs::c_dockWidgetTitle).toString());
        }
    }

    emit m_mainWindow->layoutChanged();
}

bool DockWidgetHelper::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_event->type() == QEvent::ToolTip) {
        // The QTabBar of the tabified dock widgets does not show tooltip due to Qt's internal implementation.
        auto helpEve = static_cast<QHelpEvent *>(p_event);
        auto tabBar = static_cast<QTabBar *>(p_obj);
        int idx = tabBar->tabAt(helpEve->pos());
        bool done = false;
        if (idx > -1) {
            auto dock = reinterpret_cast<QDockWidget *>(tabBar->tabData(idx).toULongLong());
            if (dock) {
                done = true;
                QToolTip::showText(helpEve->globalPos(), dock->property(PropertyDefs::c_dockWidgetTitle).toString());
            }
        }

        if (!done) {
            QToolTip::hideText();
            p_event->ignore();
        }

        return true;
    }

    return QObject::eventFilter(p_obj, p_event);
}

QStringList DockWidgetHelper::getVisibleDocks() const
{
    QStringList visibleDocks;
    for (const auto dock : m_docks) {
        if (dock->isVisible()) {
            visibleDocks.push_back(dock->objectName());
        }
    }
    return visibleDocks;
}

QStringList DockWidgetHelper::hideDocks()
{
    const auto &keepDocks = ConfigMgr::getInst().getWidgetConfig().getMainWindowKeepDocksExpandingContentArea();
    QStringList visibleDocks;
    for (const auto dock : m_docks) {
        const auto objName = dock->objectName();
        if (dock->isVisible()) {
            visibleDocks.push_back(objName);
        }

        if (dock->isFloating() || keepDocks.contains(objName)) {
            continue;
        }

        dock->setVisible(false);
    }

    return visibleDocks;
}

void DockWidgetHelper::restoreDocks(const QStringList &p_visibleDocks)
{
    const auto &keepDocks = ConfigMgr::getInst().getWidgetConfig().getMainWindowKeepDocksExpandingContentArea();
    bool hasVisible = false;
    for (const auto dock : m_docks) {
        const auto objName = dock->objectName();
        if (dock->isFloating() || keepDocks.contains(objName)) {
            continue;
        }

        const bool visible = p_visibleDocks.contains(objName);
        hasVisible = hasVisible || visible;

        dock->setVisible(visible);
    }

    if (!hasVisible) {
        // At least make one visible.
        getDock(DockIndex::NavigationDock)->setVisible(true);
    }

    updateDockWidgetTabBar();
}

QVector<void *> DockWidgetHelper::getVisibleNavigationItems()
{
    m_navigationItems.clear();

    QBitArray tabifiedDocks(m_docks.size(), false);
    Q_FOREACH(QTabBar* tabBar, m_mainWindow->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!tabBar->isVisible()) {
            continue;
        }

        const int cnt = tabBar->count();
        for (int i = 0; i < cnt; ++i) {
            auto dock = reinterpret_cast<QDockWidget *>(tabBar->tabData(i).toULongLong());
            if (!dock) {
                continue;
            }
            int dockIdx = dock->property(PropertyDefs::c_dockWidgetIndex).toInt();
            tabifiedDocks.setBit(dockIdx);

            m_navigationItems.push_back(NavigationItemInfo(tabBar, i, dockIdx));
        }
    }

    // Non-tabified docks.
    for (int i = 0; i < m_docks.size(); ++i) {
        if (!tabifiedDocks[i] && m_docks[i]->isVisible()) {
            m_navigationItems.push_back(NavigationItemInfo(i));
        }
    }

    QVector<void *> items;
    for (auto &item : m_navigationItems) {
        items.push_back(&item);
    }
    return items;
}

const QIcon &DockWidgetHelper::getDockIcon(DockIndex p_dockIndex, bool p_isSideBar)
{
    static const auto fg = VNoteX::getInst().getThemeMgr().paletteColor("widgets#mainwindow#dockwidget_tabbar#icon#fg");
    static const auto selectedFg = VNoteX::getInst().getThemeMgr().paletteColor("widgets#mainwindow#dockwidget_tabbar#icon#selected#fg");
    static auto sideBarFg = VNoteX::getInst().getThemeMgr().paletteColor("widgets#mainwindow#side_bar#icon#fg");
    static auto sideBarSelectedFg = VNoteX::getInst().getThemeMgr().paletteColor("widgets#mainwindow#side_bar#icon#selected#fg");

    if (sideBarFg.isEmpty()) {
        sideBarFg = fg;
    }
    if (sideBarSelectedFg.isEmpty()) {
        sideBarSelectedFg = selectedFg;
    }

    const auto area = m_mainWindow->dockWidgetArea(m_docks[p_dockIndex]);
    const int newAngle = rotationAngle(area);
    if ((m_dockIcons[p_dockIndex].m_rotationAngle != newAngle
         || m_dockIcons[p_dockIndex].m_isSideBar != p_isSideBar)
        && area != Qt::NoDockWidgetArea) {
        QVector<IconUtils::OverriddenColor> colors;
        colors.push_back(IconUtils::OverriddenColor(p_isSideBar ? sideBarFg : fg, QIcon::Normal));
        // FIXME: the Selected Mode is not used by the selected tab of a QTabBar.
        colors.push_back(IconUtils::OverriddenColor(p_isSideBar ? sideBarSelectedFg : selectedFg,
                                                    QIcon::Selected));

        auto iconFile = VNoteX::getInst().getThemeMgr().getIconFile(iconFileName(p_dockIndex));
        m_dockIcons[p_dockIndex].m_icon = IconUtils::fetchIcon(iconFile, colors, newAngle);
        m_dockIcons[p_dockIndex].m_rotationAngle = newAngle;
        m_dockIcons[p_dockIndex].m_isSideBar = p_isSideBar;
    }

    return m_dockIcons[p_dockIndex].m_icon;
}

void DockWidgetHelper::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label)
{
    Q_UNUSED(p_idx);
    auto info = static_cast<NavigationItemInfo *>(p_item);
    if (info->m_tabBar) {
        auto pos = info->m_tabBar->tabRect(info->m_tabIndex).topLeft();
        pos = info->m_tabBar->mapToGlobal(pos);
        p_label->move(m_mainWindow->mapFromGlobal(pos));
    } else {
        p_label->setParent(m_docks[info->m_dockIndex]);
        p_label->move(0, 0);
    }
}

void DockWidgetHelper::handleTargetHit(void *p_item)
{
    auto info = static_cast<NavigationItemInfo *>(p_item);
    activateDock(static_cast<DockIndex>(info->m_dockIndex));
}

void DockWidgetHelper::clearNavigation()
{
    NavigationMode::clearNavigation();

    m_navigationItems.clear();
}
