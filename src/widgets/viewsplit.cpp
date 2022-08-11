#include "viewsplit.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QTabBar>
#include <QMimeData>
#include <QFileInfo>
#include <QShortcut>

#include "viewwindow.h"
#include "viewarea.h"
#include <core/vnotex.h>
#include <core/thememgr.h>
#include <utils/iconutils.h>
#include <utils/pathutils.h>
#include "widgetsfactory.h"
#include <utils/widgetutils.h>
#include <utils/urldragdroputils.h>
#include <utils/clipboardutils.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include "propertydefs.h"
#include "fileopenparameters.h"

using namespace vnotex;

QIcon ViewSplit::s_windowListIcon;

QIcon ViewSplit::s_windowListActiveIcon;

QIcon ViewSplit::s_menuIcon;

QIcon ViewSplit::s_menuActiveIcon;

const QString ViewSplit::c_activeActionButtonForegroundName = "widgets#viewsplit#action_button#active#fg";

const QString ViewSplit::c_actionButtonForegroundName = "widgets#viewsplit#action_button#fg";

ViewSplit::ViewSplit(const QVector<QSharedPointer<ViewWorkspace>> &p_allWorkspaces,
                     const QSharedPointer<ViewWorkspace> &p_workspace,
                     ID p_id,
                     QWidget *p_parent)
    : QTabWidget(p_parent),
      m_id(p_id),
      m_allWorkspaces(p_allWorkspaces)
{
    setAcceptDrops(true);

    setupUI();

    setupShortcuts();

    setWorkspace(p_workspace);
}

ViewSplit::~ViewSplit()
{
    Q_ASSERT(!m_workspace);
}

void ViewSplit::setupUI()
{
    // Property of QTabWidget.
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideNone);
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    setupCornerWidget();
    setupTabBar();

    connect(this, &QTabWidget::tabCloseRequested,
            this, [this](int p_idx) {
                emit focused(this);
                closeTab(p_idx);
            });
    connect(this, &QTabWidget::tabBarDoubleClicked,
            this, &ViewSplit::closeTab);
    connect(this, &QTabWidget::tabBarClicked,
            this, [this](int p_idx) {
                Q_UNUSED(p_idx);
                focusCurrentViewWindow();
                emit focused(this);
            });
    connect(this, &QTabWidget::currentChanged,
            this, [this](int p_idx) {
                Q_UNUSED(p_idx);
                focusCurrentViewWindow();
                emit currentViewWindowChanged(getCurrentViewWindow());
            });
}

void ViewSplit::focusCurrentViewWindow()
{
    auto win = getCurrentViewWindow();
    if (win) {
        win->setFocus();
    } else {
        setFocus();
    }

    m_lastViewWindow = m_currentViewWindow;
    m_currentViewWindow = win;
}

void ViewSplit::setupCornerWidget()
{
    initIcons();

    // Container.
    auto widget = new QWidget(this);
    widget->setProperty(PropertyDefs::c_viewSplitCornerWidget, true);
    auto layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    // Widnow list button.
    {
        m_windowListButton = new QToolButton(this);
        m_windowListButton->setPopupMode(QToolButton::InstantPopup);
        m_windowListButton->setProperty(PropertyDefs::c_actionToolButton, true);

        auto act = new QAction(s_windowListIcon, tr("Open Windows"), m_windowListButton);
        m_windowListButton->setDefaultAction(act);

        auto menu = WidgetsFactory::createMenu(m_windowListButton);
        connect(menu, &QMenu::aboutToShow,
                this, [this, menu]() {
                    updateWindowList(menu);
                });
        connect(menu, &QMenu::triggered,
                this, [this](QAction *p_act) {
                    int idx = p_act->data().toInt();
                    setCurrentViewWindow(getViewWindow(idx));
                });
        m_windowListButton->setMenu(menu);

        layout->addWidget(m_windowListButton);
    }

    // Menu button.
    {
        m_menuButton = new QToolButton(this);
        m_menuButton->setPopupMode(QToolButton::InstantPopup);
        m_menuButton->setProperty(PropertyDefs::c_actionToolButton, true);

        auto act = new QAction(s_menuIcon, tr("Menu"), m_menuButton);
        m_menuButton->setDefaultAction(act);

        auto menu = WidgetsFactory::createMenu(m_menuButton);
        connect(menu, &QMenu::aboutToShow,
                this, [this, menu]() {
                    updateMenu(menu);
                });
        m_menuButton->setMenu(menu);

        layout->addWidget(m_menuButton);
    }

    widget->installEventFilter(this);

    setCornerWidget(widget, Qt::TopRightCorner);
}

bool ViewSplit::eventFilter(QObject *p_object, QEvent *p_event)
{
    if (p_object == cornerWidget()) {
        if (p_event->type() == QEvent::Resize) {
            auto resizeEve = static_cast<QResizeEvent *>(p_event);
            int height = resizeEve->size().height();
            if (height > 0) {
                // Make corner widget visible even when there is no tab.
                cornerWidget()->setMinimumHeight(height);
            }
        }
    } else if (p_object == tabBar()) {
        if (p_event->type() == QEvent::MouseButtonRelease) {
            auto mouseEve = static_cast<QMouseEvent *>(p_event);
            if (mouseEve->button() == Qt::MiddleButton) {
                int idx = tabBar()->tabAt(mouseEve->pos());
                closeTab(idx);
            }
        }
    }

    return QTabWidget::eventFilter(p_object, p_event);
}

void ViewSplit::setupTabBar()
{
    auto bar = tabBar();
    // If DrawBase is true, there is a border that we could not control the style.
    bar->setDrawBase(false);
    bar->setContextMenuPolicy(Qt::CustomContextMenu);

    // Middle click to close tab.
    bar->installEventFilter(this);

    connect(bar, &QTabBar::customContextMenuRequested,
            this, [this](const QPoint &p_pos) {
                int idx = tabBar()->tabAt(p_pos);
                if (idx == -1) {
                    return;
                }

                QScopedPointer<QMenu> menu(WidgetsFactory::createMenu());
                createContextMenuOnTabBar(menu.data(), idx);

                if (!menu->isEmpty()) {
                    menu->exec(tabBar()->mapToGlobal(p_pos));
                }
            });
}

void ViewSplit::initIcons()
{
    if (!s_windowListIcon.isNull()) {
        return;
    }

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const QString windowListIconName("split_window_list.svg");
    const QString menuIconName("split_menu.svg");
    const QString fg = themeMgr.paletteColor(c_actionButtonForegroundName);
    const QString activeFg = themeMgr.paletteColor(c_activeActionButtonForegroundName);

    s_windowListIcon = IconUtils::fetchIcon(themeMgr.getIconFile(windowListIconName), fg);
    s_windowListActiveIcon = IconUtils::fetchIcon(themeMgr.getIconFile(windowListIconName), activeFg);

    s_menuIcon = IconUtils::fetchIcon(themeMgr.getIconFile(menuIconName), fg);
    s_menuActiveIcon = IconUtils::fetchIcon(themeMgr.getIconFile(menuIconName), activeFg);
}

void ViewSplit::setWorkspace(const QSharedPointer<ViewWorkspace> &p_workspace)
{
    if (m_workspace == p_workspace) {
        return;
    }

    updateAndTakeCurrentWorkspace();

    m_workspace = p_workspace;
    if (m_workspace) {
        Q_ASSERT(!m_workspace->m_visible);

        for (auto win : m_workspace->m_viewWindows) {
            addViewWindow(win);
        }

        int idx = m_workspace->m_currentViewWindowIndex;
        if (idx >= 0 && idx < m_workspace->m_viewWindows.size()) {
            setCurrentViewWindow(m_workspace->m_viewWindows[idx]);
        }

        m_workspace->m_visible = true;
    }
}

void ViewSplit::updateAndTakeCurrentWorkspace()
{
    if (m_workspace) {
        updateStateToWorkspace();

        // Take all the view windows out.
        int cnt = getViewWindowCount();
        for (int i = cnt - 1; i >= 0; --i) {
            takeViewWindow(getViewWindow(i));
        }

        m_workspace->m_visible = false;

        m_workspace = nullptr;
    } else {
        Q_ASSERT(getViewWindowCount() == 0);
    }
}

void ViewSplit::updateStateToWorkspace() const
{
    if (!m_workspace) {
        return;
    }

    Q_ASSERT(m_workspace->m_visible);

    m_workspace->m_currentViewWindowIndex = currentIndex();

    int cnt = getViewWindowCount();
    m_workspace->m_viewWindows.resize(cnt);
    for (int i = cnt - 1; i >= 0; --i) {
        m_workspace->m_viewWindows[i] = getViewWindow(i);
    }
}

QVector<ViewWindow *> ViewSplit::findBuffer(const Buffer *p_buffer) const
{
    QVector<ViewWindow *> wins;
    int cnt = getViewWindowCount();
    for (int i = 0; i < cnt; ++i) {
        auto win = getViewWindow(i);
        if (win->getBuffer() == p_buffer) {
            wins.push_back(win);
        }
    }

    return wins;
}

ViewWindow *ViewSplit::getViewWindow(int p_idx) const
{
    return dynamic_cast<ViewWindow *>(widget(p_idx));
}

QVector<ViewWindow *> ViewSplit::getAllViewWindows() const
{
    QVector<ViewWindow *> wins;
    int cnt = getViewWindowCount();
    for (int i = 0; i < cnt; ++i) {
        auto win = getViewWindow(i);
        wins.push_back(win);
    }

    return wins;
}

int ViewSplit::getViewWindowCount() const
{
    return count();
}

void ViewSplit::addViewWindow(ViewWindow *p_win)
{
    int idx = addTab(p_win, p_win->getIcon(), p_win->getName());
    setTabToolTip(idx, p_win->getTitle());

    p_win->setViewSplit(this);
    p_win->setVisible(true);

    connect(p_win, &ViewWindow::focused,
            this, [this]() {
                emit focused(this);
            });

    connect(p_win, &ViewWindow::statusChanged,
            this, [this]() {
                auto win = dynamic_cast<ViewWindow *>(sender());
                int idx = indexOf(win);
                Q_ASSERT(idx != -1);
                setTabIcon(idx, win->getIcon());
            });

    connect(p_win, &ViewWindow::nameChanged,
            this, [this]() {
                auto win = dynamic_cast<ViewWindow *>(sender());
                int idx = indexOf(win);
                Q_ASSERT(idx != -1);
                setTabText(idx, win->getName());
            });
}

ViewWindow *ViewSplit::getCurrentViewWindow() const
{
    return dynamic_cast<ViewWindow *>(currentWidget());
}

void ViewSplit::setCurrentViewWindow(ViewWindow *p_win)
{
    if (!p_win) {
        return;
    }

    Q_ASSERT(p_win->getViewSplit() == this);
    setCurrentWidget(p_win);
}

void ViewSplit::takeViewWindow(ViewWindow *p_win)
{
    Q_ASSERT(p_win->getViewSplit() == this);
    p_win->setViewSplit(nullptr);

    int idx = indexOf(p_win);
    Q_ASSERT(idx != -1);
    removeTab(idx);

    disconnect(p_win, 0, this, 0);

    p_win->setVisible(false);
    p_win->setParent(nullptr);
}

QSharedPointer<ViewWorkspace> ViewSplit::getWorkspace() const
{
    return m_workspace;
}

void ViewSplit::setActive(bool p_active)
{
    if (p_active) {
        m_windowListButton->defaultAction()->setIcon(s_windowListActiveIcon);
        m_menuButton->defaultAction()->setIcon(s_menuActiveIcon);
    } else {
        m_windowListButton->defaultAction()->setIcon(s_windowListIcon);
        m_menuButton->defaultAction()->setIcon(s_menuIcon);
    }
}

void ViewSplit::updateWindowList(QMenu *p_menu)
{
    p_menu->clear();

    if (!m_windowListActionGroup) {
        m_windowListActionGroup = new QActionGroup(p_menu);
    } else {
        WidgetUtils::clearActionGroup(m_windowListActionGroup);
    }

    auto currentViewWindow = getCurrentViewWindow();
    int cnt = getViewWindowCount();
    if (cnt == 0) {
        // Add a dummy entry.
        auto act = p_menu->addAction(tr("No Window To Show"));
        act->setEnabled(false);
        return;
    }

    for (int i = 0; i < cnt; ++i) {
        auto window = getViewWindow(i);

        auto act = new QAction(window->getName(),
                               m_windowListActionGroup);
        act->setToolTip(window->getTitle());
        act->setData(i);
        act->setCheckable(true);
        p_menu->addAction(act);

        if (currentViewWindow == window) {
            act->setChecked(true);
        }
    }
}

void ViewSplit::updateMenu(QMenu *p_menu)
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    p_menu->clear();

    // Workspaces.
    {
        p_menu->addSection(tr("Workspaces"));
        if (!m_workspaceActionGroup) {
            m_workspaceActionGroup = new QActionGroup(p_menu);
            connect(m_workspaceActionGroup, &QActionGroup::triggered,
                    this, [this](QAction *p_act) {
                        int idx = p_act->data().toInt();
                        const auto &newWorkspace = m_allWorkspaces[idx];
                        if (newWorkspace != m_workspace) {
                            setWorkspace(newWorkspace);
                        }
                    });
        } else {
            WidgetUtils::clearActionGroup(m_workspaceActionGroup);
        }

        for (int i = 0; i < m_allWorkspaces.size(); ++i) {
            auto act = new QAction(tr("Workspace %1").arg(m_allWorkspaces[i]->m_id),
                                   m_workspaceActionGroup);
            act->setData(i);
            act->setCheckable(true);
            act->setEnabled(!m_allWorkspaces[i]->m_visible);
            p_menu->addAction(act);

            if (m_allWorkspaces[i] == m_workspace) {
                act->setEnabled(true);
                act->setChecked(true);
            }
        }

        p_menu->addSeparator();

        {
            auto act = p_menu->addAction(tr("New Workspace"),
                                         [this]() {
                                             emit newWorkspaceRequested(this);
                                         });
            WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::NewWorkspace));
        }

        p_menu->addAction(tr("Remove Workspace"),
                          [this]() {
                              emit removeWorkspaceRequested(this);
                          });
    }

    // Splits.
    {
        // Do not add icon here since it will consume too much space.
        p_menu->addSection(tr("Split"));
        auto act = p_menu->addAction(tr("Vertical Split"),
                                     [this]() {
                                         emit verticalSplitRequested(this);
                                     });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::VerticalSplit));

        act = p_menu->addAction(tr("Horizontal Split"),
                                [this]() {
                                    emit horizontalSplitRequested(this);
                                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::HorizontalSplit));

        act = p_menu->addAction(tr("Maximize Split"),
                                [this]() {
                                    emit maximizeSplitRequested(this);
                                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::MaximizeSplit));

        act = p_menu->addAction(tr("Distribute Splits"),
                                [this]() {
                                    emit distributeSplitsRequested();
                                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::DistributeSplits));

        act = p_menu->addAction(tr("Remove Split"),
                                [this]() {
                                    emit removeSplitRequested(this);
                                });

        act = p_menu->addAction(tr("Remove Split And Workspace"),
                                [this]() {
                                    emit removeSplitAndWorkspaceRequested(this);
                                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::RemoveSplitAndWorkspace));
    }
}

void ViewSplit::createContextMenuOnTabBar(QMenu *p_menu, int p_tabIdx)
{
    Q_ASSERT(p_tabIdx > -1);

    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // Close Tab.
    {
        auto closeTabAct = p_menu->addAction(tr("Close Tab"),
                                             [this, p_tabIdx]() {
                                                 closeTab(p_tabIdx);
                                             });
        WidgetUtils::addActionShortcutText(closeTabAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::CloseTab));
    }

    // Close All Tabs.
    {
        auto closeTabAct = p_menu->addAction(tr("Close All Tabs"),
                                             [this, p_tabIdx]() {
                                                 closeMultipleTabs(p_tabIdx, CloseTabMode::CloseAllTabs);
                                             });
        WidgetUtils::addActionShortcutText(closeTabAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::CloseAllTabs));
    }

    // Close Other Tabs.
    {
        auto closeTabAct = p_menu->addAction(tr("Close Other Tabs"),
                                             [this, p_tabIdx]() {
                                                 closeMultipleTabs(p_tabIdx, CloseTabMode::CloseOtherTabs);
                                             });
        WidgetUtils::addActionShortcutText(closeTabAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::CloseOtherTabs));
    }

    // Close Tabs To The Left
    {
        auto closeTabAct = p_menu->addAction(tr("Close Tabs To The Left"),
                                             [this, p_tabIdx]() {
                                                 closeMultipleTabs(p_tabIdx, CloseTabMode::CloseTabsToTheLeft);
                                             });
        WidgetUtils::addActionShortcutText(closeTabAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::CloseTabsToTheLeft));
    }

    // Close Tabs To The Right.
    {
        auto closeTabAct = p_menu->addAction(tr("Close Tabs To The Right"),
                                             [this, p_tabIdx]() {
                                                 closeMultipleTabs(p_tabIdx, CloseTabMode::CloseTabsToTheRight);
                                             });
        WidgetUtils::addActionShortcutText(closeTabAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::CloseTabsToTheRight));
    }

    p_menu->addSeparator();

    {
        auto act = p_menu->addAction(tr("Auto Reload"));
        act->setToolTip(tr("Reload file from disk automatically if it is changed outside"));
        act->setCheckable(true);
        auto win = getViewWindow(p_tabIdx);
        Q_ASSERT(win);
        act->setChecked(win->getWindowFlags() & ViewWindow::AutoReload);
        connect(act, &QAction::triggered,
                this, [win](bool p_checked) {
                    if (p_checked) {
                        win->setWindowFlags(win->getWindowFlags() | ViewWindow::AutoReload);
                    } else {
                        win->setWindowFlags(win->getWindowFlags() & ~ViewWindow::AutoReload);
                    }
                });
    }

    p_menu->addSeparator();

    // Copy Path.
    p_menu->addAction(tr("Copy Path"),
                      [this, p_tabIdx]() {
                          auto win = getViewWindow(p_tabIdx);
                          if (win) {
                              const auto filePath = win->getBuffer()->getPath();
                              ClipboardUtils::setTextToClipboard(filePath);
                              VNoteX::getInst().showStatusMessageShort(tr("Copied path: %1").arg(filePath));
                          }
                      });

    // Open Location.
    p_menu->addAction(tr("Open Location"),
                      [this, p_tabIdx]() {
                          auto win = getViewWindow(p_tabIdx);
                          if (win) {
                              const auto location = PathUtils::parentDirPath(win->getBuffer()->getPath());
                              WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(location));
                          }
                      });

    // Locate Node.
    auto win = getViewWindow(p_tabIdx);
    if (win && win->getBuffer()->getNode()) {
        auto locateNodeAct = p_menu->addAction(tr("Locate Node"),
                                               [this, p_tabIdx]() {
                                                   auto win = getViewWindow(p_tabIdx);
                                                   auto node = win->getBuffer()->getNode();
                                                   if (node) {
                                                       emit VNoteX::getInst().locateNodeRequested(node);
                                                   }
                                               });
        WidgetUtils::addActionShortcutText(locateNodeAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::LocateNode));
    }

    // Pin To Quick Access.
    p_menu->addAction(tr("Pin To Quick Access"),
                      [this, p_tabIdx]() {
                          auto win = getViewWindow(p_tabIdx);
                          if (win) {
                              const QStringList files(win->getBuffer()->getPath());
                              emit VNoteX::getInst().pinToQuickAccessRequested(files);
                          }
                      });

    p_menu->addSeparator();

    {
        auto splitAct = p_menu->addAction(tr("Move One Split Left"),
                [this, p_tabIdx]() {
                    auto win = getViewWindow(p_tabIdx);
                    if (win) {
                        emit moveViewWindowOneSplitRequested(this, win, Direction::Left);
                    }
                });
        WidgetUtils::addActionShortcutText(splitAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitLeft));
    }

    {
        auto splitAct = p_menu->addAction(tr("Move One Split Right"),
                [this, p_tabIdx]() {
                    auto win = getViewWindow(p_tabIdx);
                    if (win) {
                        emit moveViewWindowOneSplitRequested(this, win, Direction::Right);
                    }
                });
        WidgetUtils::addActionShortcutText(splitAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitRight));
    }

    {
        auto splitAct = p_menu->addAction(tr("Move One Split Up"),
                [this, p_tabIdx]() {
                    auto win = getViewWindow(p_tabIdx);
                    if (win) {
                        emit moveViewWindowOneSplitRequested(this, win, Direction::Up);
                    }
                });
        WidgetUtils::addActionShortcutText(splitAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitUp));
    }

    {
        auto splitAct = p_menu->addAction(tr("Move One Split Down"),
                [this, p_tabIdx]() {
                    auto win = getViewWindow(p_tabIdx);
                    if (win) {
                        emit moveViewWindowOneSplitRequested(this, win, Direction::Down);
                    }
                });
        WidgetUtils::addActionShortcutText(splitAct,
                                           coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitDown));
    }
}

void ViewSplit::closeTab(int p_idx)
{
    auto win = getViewWindow(p_idx);
    if (win) {
        emit viewWindowCloseRequested(win);
    }
}

void ViewSplit::closeMultipleTabs(CloseTabMode p_ctm)
{
    closeMultipleTabs(currentIndex(), p_ctm);
}

void ViewSplit::closeMultipleTabs(int p_idx, CloseTabMode p_ctm)
{
    QVector<ViewWindow *> windowsNeedToClose;
    int cnt = getViewWindowCount();

    switch (p_ctm) {
    case CloseTabMode::CloseAllTabs:
        for (int i = 0; i < cnt; i++) {
            windowsNeedToClose.push_back(getViewWindow(i));
        }
        break;
    case CloseTabMode::CloseOtherTabs:
        for (int i = 0; i < cnt; i++) {
            if (i != p_idx) {
                windowsNeedToClose.push_back(getViewWindow(i));
            }
        }
        break;
    case CloseTabMode::CloseTabsToTheLeft:
        for (int i = 0; i < p_idx; i++) {
             windowsNeedToClose.push_back(getViewWindow(i));
        }
        break;
    case CloseTabMode::CloseTabsToTheRight:
        for (int i = cnt - 1; i > p_idx; i--) {
             windowsNeedToClose.push_back(getViewWindow(i));
        }
        break;
    }

    for (auto win : windowsNeedToClose) {
        emit viewWindowCloseRequested(win);
    }
}

void ViewSplit::mousePressEvent(QMouseEvent *p_event)
{
    QTabWidget::mousePressEvent(p_event);

    // For an empty QTabWidget, we need to focus this split when user press
    // the space of widget.
    emit focused(this);
}

bool ViewSplit::forEachViewWindow(const ViewWindowSelector &p_func) const
{
    int cnt = getViewWindowCount();
    for (int i = 0; i < cnt; ++i) {
        auto win = getViewWindow(i);
        if (!p_func(win)) {
            return false;
        }
    }
    return true;
}

QVector<ViewSplit::ViewWindowNavigationModeInfo> ViewSplit::getNavigationModeInfo() const
{
    QVector<ViewWindowNavigationModeInfo> infos;
    auto bar = tabBar();
    for (int i = 0; i < bar->count(); ++i) {
        QPoint tl = bar->tabRect(i).topLeft();
        if (tl.x() < 0 || tl.x() >= bar->width()) {
            continue;
        }

        ViewWindowNavigationModeInfo info;
        info.m_topLeft = bar->mapToParent(tl);
        info.m_viewWindow = getViewWindow(i);
        infos.append(info);
    }

    return infos;
}

void ViewSplit::dragEnterEvent(QDragEnterEvent *p_event)
{
    if (UrlDragDropUtils::handleDragEnterEvent(p_event)) {
        return;
    }

    QTabWidget::dragEnterEvent(p_event);
}

void ViewSplit::dropEvent(QDropEvent *p_event)
{
    if (UrlDragDropUtils::handleDropEvent(p_event, [](const QStringList &p_files) {
                for (const auto &file : p_files) {
                    emit VNoteX::getInst().openFileRequested(file, QSharedPointer<FileOpenParameters>::create());
                }
            })) {
        return;
    }

    QTabWidget::dropEvent(p_event);
}

void ViewSplit::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // NewWorkspace.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::NewWorkspace), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        emit newWorkspaceRequested(this);
                    });
        }
    }

    // VerticalSplit.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::VerticalSplit), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        emit verticalSplitRequested(this);
                    });
        }
    }

    // HorizontalSplit.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::HorizontalSplit), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        emit horizontalSplitRequested(this);
                    });
        }
    }

    // MaximizeSplit.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::MaximizeSplit), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        emit maximizeSplitRequested(this);
                    });
        }
    }

    // DistributeSplits.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::DistributeSplits), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        emit distributeSplitsRequested();
                    });
        }
    }

    // RemoveSplitAndWorkspace.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::RemoveSplitAndWorkspace), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        emit removeSplitAndWorkspaceRequested(this);
                    });
        }
    }

    // ActivateTab1.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab1), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(0);
                    });
        }
    }

    // ActivateTab2.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab2), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(1);
                    });
        }
    }

    // ActivateTab3.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab3), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(2);
                    });
        }
    }

    // ActivateTab4.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab4), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(3);
                    });
        }
    }

    // ActivateTab5.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab5), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(4);
                    });
        }
    }

    // ActivateTab6.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab6), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(5);
                    });
        }
    }

    // ActivateTab7.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab7), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(6);
                    });
        }
    }

    // ActivateTab8.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab8), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(7);
                    });
        }
    }

    // ActivateTab9.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateTab9), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        setCurrentViewWindow(8);
                    });
        }
    }

    // AlternateTab.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::AlternateTab), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, &ViewSplit::alternateTab);
        }
    }

    // ActivateNextTab.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivateNextTab), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        activateNextTab(false);
                    });
        }
    }

    // ActivatePreviousTab.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::ActivatePreviousTab), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        activateNextTab(true);
                    });
        }
    }

    // MoveOneSplitLeft.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::MoveOneSplitLeft), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            emit moveViewWindowOneSplitRequested(this, win, Direction::Left);
                        }
                    });
        }
    }

    // MoveOneSplitDown.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::MoveOneSplitDown), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            emit moveViewWindowOneSplitRequested(this, win, Direction::Down);
                        }
                    });
        }
    }

    // MoveOneSplitUp.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::MoveOneSplitUp), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            emit moveViewWindowOneSplitRequested(this, win, Direction::Up);
                        }
                    });
        }
    }

    // MoveOneSplitRight.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::MoveOneSplitRight), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            emit moveViewWindowOneSplitRequested(this, win, Direction::Right);
                        }
                    });
        }
    }
}

void ViewSplit::focus()
{
    focusCurrentViewWindow();
}

ID ViewSplit::getId() const
{
    return m_id;
}

void ViewSplit::setCurrentViewWindow(int p_idx)
{
    auto win = getViewWindow(p_idx);
    setCurrentViewWindow(win);
}

void ViewSplit::alternateTab()
{
    if (!m_lastViewWindow) {
        return;
    }

    // It is fine even when m_lastViewWindow is a wild pointer. The implementation will just
    // compare its value without dereferencing it.
    if (-1 != indexOf(m_lastViewWindow)) {
        setCurrentViewWindow(m_lastViewWindow);
    } else {
        m_lastViewWindow = nullptr;
    }
}

void ViewSplit::activateNextTab(bool p_backward)
{
    int idx = currentIndex();
    if (idx == -1 || count() == 1) {
        return;
    }

    if (p_backward) {
        --idx;
        if (idx < 0) {
            idx = count() - 1;
        }
    } else {
        ++idx;
        if (idx >= count()) {
            idx = 0;
        }
    }

    setCurrentViewWindow(idx);
}
