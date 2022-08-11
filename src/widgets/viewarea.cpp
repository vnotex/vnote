#include "viewarea.h"

#include <QShortcut>
#include <QLabel>
#include <QLayout>
#include <QSize>
#include <QSplitter>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTimer>
#include <QApplication>
#include <QSet>
#include <QHash>
#include <QTabBar>

#include "viewwindow.h"
#include "mainwindow.h"
#include "propertydefs.h"
#include <utils/widgetutils.h>
#include <utils/docsutils.h>
#include <utils/urldragdroputils.h>
#include <core/events.h>
#include <core/vnotex.h>
#include <core/configmgr.h>
#include <core/notebookmgr.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/sessionconfig.h>
#include <core/fileopenparameters.h>
#include <notebook/node.h>
#include <notebook/notebook.h>
#include "editors/plantumlhelper.h"
#include "editors/graphvizhelper.h"
#include <core/historymgr.h>

using namespace vnotex;

ViewArea::ViewArea(QWidget *p_parent)
    : QWidget(p_parent),
      NavigationMode(NavigationMode::Type::DoubleKeys, this)
{
    setupUI();

    setAcceptDrops(true);

    setupShortcuts();

    connect(this, &ViewArea::viewSplitsCountChanged,
            this, &ViewArea::handleViewSplitsCountChange);

    auto mainWindow = VNoteX::getInst().getMainWindow();
    connect(mainWindow, &MainWindow::mainWindowClosed,
            this, [this](const QSharedPointer<Event> &p_event) {
                if (p_event->m_handled) {
                    return;
                }

                if (ConfigMgr::getInst().getCoreConfig().isRecoverLastSessionOnStartEnabled()) {
                    saveSession();
                }

                bool ret = close(false);
                if (!ret) {
                    p_event->m_response = false;
                    p_event->m_handled = true;
                }
            });
    connect(mainWindow, &MainWindow::minimizedToSystemTray,
            this, [this]() {
                if (ConfigMgr::getInst().getCoreConfig().isRecoverLastSessionOnStartEnabled()) {
                    // Save it here, too. Avoid losing session when VNote is closed unexpectedly.
                    saveSession();
                }
            });

    connect(mainWindow, &MainWindow::mainWindowClosedOnQuit,
            this, [this]() {
                close(true);
            });

    connect(mainWindow, &MainWindow::mainWindowStarted,
            this, &ViewArea::loadSession);

    connect(&VNoteX::getInst(), &VNoteX::nodeAboutToMove,
            this, &ViewArea::handleNodeChange);

    connect(&VNoteX::getInst(), &VNoteX::nodeAboutToRemove,
            this, &ViewArea::handleNodeChange);

    connect(&VNoteX::getInst(), &VNoteX::nodeAboutToRename,
            this, &ViewArea::handleNodeChange);

    connect(&VNoteX::getInst(), &VNoteX::nodeAboutToReload,
            this, &ViewArea::handleNodeChange);

    connect(&VNoteX::getInst(), &VNoteX::closeFileRequested,
            this, &ViewArea::closeFile);

    auto &configMgr = ConfigMgr::getInst();
    connect(&configMgr, &ConfigMgr::editorConfigChanged,
            this, [this]() {
                forEachViewWindow([](ViewWindow *p_win) {
                    p_win->handleEditorConfigChange();
                    return true;
                });

                const auto &markdownEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
                PlantUmlHelper::getInst().update(markdownEditorConfig.getPlantUmlJar(),
                                                 markdownEditorConfig.getGraphvizExe(),
                                                 markdownEditorConfig.getPlantUmlCommand());
                GraphvizHelper::getInst().update(markdownEditorConfig.getGraphvizExe());
            });

    m_fileCheckTimer = new QTimer(this);
    m_fileCheckTimer->setSingleShot(false);
    m_fileCheckTimer->setInterval(2000);
    connect(m_fileCheckTimer, &QTimer::timeout,
            this, [this]() {
                auto win = getCurrentViewWindow();
                if (win) {
                    win->checkFileMissingOrChangedOutsidePeriodically();
                }
            });

    connect(qApp, &QApplication::focusChanged,
            this, [this](QWidget *p_old, QWidget *p_now) {
                if (!p_now) {
                    m_fileCheckTimer->stop();
                } else if (!p_old && m_currentSplit) {
                    m_fileCheckTimer->start();
                }
            });
}

ViewArea::~ViewArea()
{
    // All splits/workspaces/windows should be released during close() before destruction.
    Q_ASSERT(m_splits.isEmpty() && m_currentSplit == nullptr);
    Q_ASSERT(m_workspaces.isEmpty());
}

void ViewArea::handleNodeChange(Node *p_node, const QSharedPointer<Event> &p_event)
{
    if (p_event->m_handled) {
        return;
    }

    bool ret = close(p_node, false);
    p_event->m_response = ret;
    p_event->m_handled = !ret;
}

void ViewArea::setupUI()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
}

QSize ViewArea::sizeHint() const
{
    const QSize preferredSize(400, 300);
    auto sz = QWidget::sizeHint();
    if (sz.width() < preferredSize.width()) {
        sz = preferredSize;
    }

    return sz;
}

void ViewArea::openBuffer(Buffer *p_buffer, const QSharedPointer<FileOpenParameters> &p_paras)
{
    // We allow multiple ViewWindows of the same buffer in different workspaces by default.
    QVector<ViewWindow *> wins;
    if (!p_paras->m_alwaysNewWindow) {
        wins = findBufferInViewSplits(p_buffer);
    }
    if (wins.isEmpty()) {
        if (!m_currentSplit) {
            addFirstViewSplit();
        }

        Q_ASSERT(m_currentSplit);

        // Create a ViewWindow from @p_buffer.
        auto window = p_buffer->createViewWindow(p_paras, nullptr);
        m_currentSplit->addViewWindow(window);
        setCurrentViewWindow(window);
    } else {
        auto selectedWin = wins.first();
        for (auto win : wins) {
            // Prefer window in current split.
            if (win->getViewSplit() == m_currentSplit) {
                selectedWin = win;
                break;
            }
        }

        selectedWin->openTwice(p_paras);

        setCurrentViewWindow(selectedWin);
    }

    if (p_paras->m_focus) {
        auto win = getCurrentViewWindow();
        if (win) {
            win->setFocus(Qt::OtherFocusReason);
        }
    }
}

QVector<ViewWindow *> ViewArea::findBufferInViewSplits(const Buffer *p_buffer) const
{
    QVector<ViewWindow *> wins;
    for (auto split : m_splits) {
        auto winsInSplit = split->findBuffer(p_buffer);
        if (!winsInSplit.isEmpty()) {
            wins.append(winsInSplit);
        }
    }

    return wins;
}

ViewSplit *ViewArea::createViewSplit(QWidget *p_parent, ID p_viewSplitId)
{
    auto workspace = createWorkspace();
    m_workspaces.push_back(workspace);

    if (p_viewSplitId == InvalidViewSplitId) {
        p_viewSplitId = m_nextViewSplitId++;
    } else if (p_viewSplitId >= m_nextViewSplitId) {
        m_nextViewSplitId = p_viewSplitId + 1;
    }

    auto split = new ViewSplit(m_workspaces, workspace, p_viewSplitId, p_parent);
    connect(split, &ViewSplit::viewWindowCloseRequested,
            this, [this](ViewWindow *p_win) {
                closeViewWindow(p_win, false, true);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::verticalSplitRequested,
            this, [this](ViewSplit *p_split) {
                splitViewSplit(p_split, SplitType::Vertical);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::horizontalSplitRequested,
            this, [this](ViewSplit *p_split) {
                splitViewSplit(p_split, SplitType::Horizontal);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::maximizeSplitRequested,
            this, &ViewArea::maximizeViewSplit);
    connect(split, &ViewSplit::distributeSplitsRequested,
            this, &ViewArea::distributeViewSplits);
    connect(split, &ViewSplit::removeSplitRequested,
            this, [this](ViewSplit *p_split) {
                removeViewSplit(p_split, false);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::removeSplitAndWorkspaceRequested,
            this, [this](ViewSplit *p_split) {
                removeViewSplit(p_split, true);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::newWorkspaceRequested,
            this, [this](ViewSplit *p_split) {
                newWorkspaceInViewSplit(p_split);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::removeWorkspaceRequested,
            this, [this](ViewSplit *p_split) {
                removeWorkspaceInViewSplit(p_split, true);
                emit windowsChanged();
            });
    connect(split, &ViewSplit::focused,
            this, [this](ViewSplit *p_split) {
                setCurrentViewSplit(p_split, false);
                checkCurrentViewWindowChange();
            });
    connect(split, &ViewSplit::currentViewWindowChanged,
            this, [this](ViewWindow *p_win) {
                checkCurrentViewWindowChange();
                if (shouldUseGlobalStatusWidget()) {
                    if (p_win) {
                        p_win->setStatusWidgetVisible(false);
                    }
                    m_currentStatusWidget = p_win ? p_win->statusWidget() : nullptr;
                    emit statusWidgetChanged(m_currentStatusWidget.get());
                } else {
                    Q_ASSERT(!m_currentStatusWidget);
                    if (p_win) {
                        p_win->setStatusWidgetVisible(true);
                    }
                }
                emit windowsChanged();
            });
    connect(split, &ViewSplit::moveViewWindowOneSplitRequested,
            this, [this](ViewSplit *p_split, ViewWindow *p_win, Direction p_direction) {
                moveViewWindowOneSplit(p_split, p_win, p_direction);
                emit windowsChanged();
            });
    return split;
}

void ViewArea::showSceneWidget()
{
    Q_ASSERT(!m_sceneWidget);
    Q_ASSERT(m_splits.isEmpty());
    auto text = DocsUtils::getDocText(QStringLiteral("get_started.txt"));
    // TODO: a more informative widget, such as adding workspace list and LRU files.
    m_sceneWidget = new QLabel(text, this);
    m_mainLayout->addWidget(m_sceneWidget);
}

void ViewArea::hideSceneWidget()
{
    Q_ASSERT(m_sceneWidget);
    m_mainLayout->removeWidget(m_sceneWidget);
    delete m_sceneWidget;
    m_sceneWidget = nullptr;
}

void ViewArea::addFirstViewSplit()
{
    Q_ASSERT(!m_currentSplit && m_splits.isEmpty());

    auto split = createViewSplit(this);

    m_splits.push_back(split);

    hideSceneWidget();
    m_mainLayout->addWidget(split);

    postFirstViewSplit();
}

void ViewArea::postFirstViewSplit()
{
    Q_ASSERT(!m_splits.isEmpty());
    auto currentSplit = m_splits.first();
    // Check if any split has focus. If there is any, then set it as current split.
    auto focusWidget = QApplication::focusWidget();
    if (focusWidget) {
        for (const auto &split : m_splits) {
            if (split == focusWidget || split->isAncestorOf(focusWidget)) {
                currentSplit = split;
                break;
            }
        }
    }
    setCurrentViewSplit(currentSplit, false);

    emit viewSplitsCountChanged();
    checkCurrentViewWindowChange();

    m_fileCheckTimer->start();
}

static ViewSplit *fetchFirstChildViewSplit(const QSplitter *p_splitter)
{
    if (p_splitter->count() == 0) {
        return nullptr;
    }

    auto child = p_splitter->widget(0);
    auto split = dynamic_cast<ViewSplit *>(child);
    if (split) {
        return split;
    }

    auto childSplitter = dynamic_cast<QSplitter *>(child);
    Q_ASSERT(childSplitter);
    return fetchFirstChildViewSplit(childSplitter);
}

void ViewArea::removeViewSplit(ViewSplit *p_split, bool p_removeWorkspace)
{
    if (p_removeWorkspace) {
        // Remove workspace.
        bool ret = removeWorkspaceInViewSplit(p_split, false);
        if (!ret) {
            return;
        }
    } else {
        // Detach workspace.
        p_split->setWorkspace(nullptr);
    }

    // Remove split.
    disconnect(p_split, 0, this, 0);
    disconnect(this, 0, p_split, 0);
    m_splits.removeAll(p_split);

    // Get new current split.
    ViewSplit *newCurrentSplit = nullptr;
    auto splitter = tryGetParentSplitter(p_split);
    if (splitter) {
        Q_ASSERT(splitter->count() >= 2);
        p_split->hide();
        p_split->setParent(this);

        newCurrentSplit = fetchFirstChildViewSplit(splitter);

        if (splitter->count() == 1) {
            // Remove the splitter if there is only one child in it after the removal.
            unwrapSplitter(splitter);
        }
    } else {
        m_mainLayout->removeWidget(p_split);
        if (!m_splits.isEmpty()) {
            newCurrentSplit = m_splits.first();
        }
    }

    p_split->deleteLater();

    // Show scene widget and update current split.
    if (m_splits.isEmpty()) {
        Q_ASSERT(newCurrentSplit == nullptr);
        setCurrentViewSplit(newCurrentSplit, false);
        showSceneWidget();

        m_fileCheckTimer->stop();
    } else if (m_currentSplit == p_split) {
        setCurrentViewSplit(newCurrentSplit, true);
    }

    emit viewSplitsCountChanged();
    checkCurrentViewWindowChange();
}

ViewWindow *ViewArea::getCurrentViewWindow() const
{
    auto split = getCurrentViewSplit();
    if (split) {
        return split->getCurrentViewWindow();
    }

    return nullptr;
}

void ViewArea::setCurrentViewWindow(ViewWindow *p_win)
{
    auto split = p_win->getViewSplit();
    Q_ASSERT(split);

    split->setCurrentViewWindow(p_win);

    setCurrentViewSplit(split, false);

    checkCurrentViewWindowChange();
}

ViewSplit *ViewArea::getCurrentViewSplit() const
{
    return m_currentSplit;
}

void ViewArea::setCurrentViewSplit(ViewSplit *p_split, bool p_focus)
{
    Q_ASSERT(!p_split || m_splits.contains(p_split));
    if (p_split == m_currentSplit) {
        if (p_split && p_focus) {
            p_split->focus();
        }
        return;
    }

    if (m_currentSplit) {
        m_currentSplit->setActive(false);
    }

    m_currentSplit = p_split;
    if (m_currentSplit) {
        m_currentSplit->setActive(true);
        if (p_focus) {
            m_currentSplit->focus();
        }
    }
}

bool ViewArea::closeViewWindow(ViewWindow *p_win, bool p_force, bool p_removeSplitIfEmpty)
{
    Q_ASSERT(p_win && p_win->getViewSplit());
    // Make it current ViewWindow.
    setCurrentViewWindow(p_win);

    // Get info before close.
    const auto session = p_win->saveSession();
    Notebook *notebook = nullptr;
    if (p_win->getBuffer()) {
        auto node = p_win->getBuffer()->getNode();
        if (node) {
            notebook = node->getNotebook();
        }
    }

    if (!p_win->aboutToClose(p_force)) {
        return false;
    }

    // Update history.
    updateHistory(session, notebook);

    // Remove the status widget.
    if (m_currentStatusWidget && p_win == getCurrentViewWindow()) {
        Q_ASSERT(m_currentStatusWidget == p_win->statusWidget());
        emit statusWidgetChanged(nullptr);
    }

    auto split = p_win->getViewSplit();
    split->takeViewWindow(p_win);

    delete p_win;

    if (p_removeSplitIfEmpty && split->getViewWindowCount() == 0) {
        // Remove this split and workspace.
        removeViewSplit(split, true);
    }

    return true;
}

QSharedPointer<ViewWorkspace> ViewArea::createWorkspace()
{
    // Get the id of the workspace.
    ID id = 1;
    QSet<ID> usedIds;
    for (auto ws : m_workspaces) {
        usedIds.insert(ws->m_id);
    }

    while (true) {
        if (usedIds.contains(id)) {
            ++id;
        } else {
            break;
        }
    }

    return QSharedPointer<ViewWorkspace>::create(id);
}

ViewSplit *ViewArea::splitViewSplit(ViewSplit *p_split, SplitType p_type, bool p_cloneViewWindow)
{
    Q_ASSERT(p_split);
    // Create the new split.
    auto newSplit = createViewSplit(this);

    // Clone a ViewWindow for the same buffer to display in the new split.
    if (p_cloneViewWindow) {
        auto win = p_split->getCurrentViewWindow();
        if (win) {
            auto buffer = win->getBuffer();
            auto newWindow = buffer->createViewWindow(QSharedPointer<FileOpenParameters>::create(), newSplit);
            newSplit->addViewWindow(newWindow);
        }
    }

    // Obey Vim's practice, which is the opposite of Qt.
    auto orientation = p_type == SplitType::Vertical ? Qt::Horizontal : Qt::Vertical;
    auto splitter = tryGetParentSplitter(p_split);
    if (splitter) {
        int idx = splitter->indexOf(p_split);
        if (splitter->orientation() == orientation) {
            // Same orientation.
            splitter->insertWidget(idx + 1, newSplit);
        } else {
            // Split it further.
            auto newSplitter = createSplitter(orientation, this);
            splitter->replaceWidget(idx, newSplitter);

            newSplitter->addWidget(p_split);
            newSplitter->addWidget(newSplit);
        }
    } else {
        Q_ASSERT(p_split->parent() == this);
        m_mainLayout->removeWidget(p_split);

        auto newSplitter = createSplitter(orientation, this);
        newSplitter->addWidget(p_split);
        newSplitter->addWidget(newSplit);

        m_mainLayout->addWidget(newSplitter);
    }

    m_splits.push_back(newSplit);
    setCurrentViewSplit(newSplit, true);

    // Let Qt decide the size of splitter first.
    QCoreApplication::sendPostedEvents();

    distributeViewSplitsOfSplitter(tryGetParentSplitter(newSplit));

    emit viewSplitsCountChanged();
    checkCurrentViewWindowChange();

    return newSplit;
}

QSplitter *ViewArea::createSplitter(Qt::Orientation p_orientation, QWidget *p_parent) const
{
    auto splitter = new QSplitter(p_orientation, p_parent);
    splitter->setChildrenCollapsible(false);
    return splitter;
}

QSplitter *ViewArea::tryGetParentSplitter(const QWidget *p_widget) const
{
    return dynamic_cast<QSplitter *>(p_widget->parent());
}

void ViewArea::distributeViewSplitsOfSplitter(QSplitter *p_splitter)
{
    if (!WidgetUtils::distributeWidgetsOfSplitter(p_splitter)) {
        return;
    }

    // Distribute child splitter.
    for (int i = 0; i < p_splitter->count(); ++i) {
        auto childSplitter = dynamic_cast<QSplitter *>(p_splitter->widget(i));
        if (childSplitter) {
            distributeViewSplitsOfSplitter(childSplitter);
        }
    }

    return;
}

void ViewArea::unwrapSplitter(QSplitter *p_splitter)
{
    Q_ASSERT(p_splitter->count() == 1);
    auto paSplitter = tryGetParentSplitter(p_splitter);
    if (paSplitter) {
        Q_ASSERT(paSplitter->count() >= 2);
        int idx = paSplitter->indexOf(p_splitter);
        auto child = p_splitter->widget(0);
        child->setParent(this);
        paSplitter->replaceWidget(idx, child);
    } else {
        // This is the top child of ViewArea.
        Q_ASSERT(p_splitter->parent() == this);
        m_mainLayout->removeWidget(p_splitter);
        // Maybe another splitter or ViewSplit.
        auto child = p_splitter->widget(0);
        child->setParent(this);
        m_mainLayout->addWidget(child);
    }

    delete p_splitter;
}

void ViewArea::maximizeViewSplit(ViewSplit *p_split)
{
    QWidget *widget = p_split;
    while (widget && widget != this) {
        maximizeWidgetOfSplitter(widget);
        widget = dynamic_cast<QWidget *>(widget->parent());
    }
}

void ViewArea::maximizeWidgetOfSplitter(QWidget *p_widget)
{
    auto splitter = tryGetParentSplitter(p_widget);
    if (!splitter || splitter->count() <= 1) {
        return;
    }

    const int minSplitWidth = 20 * WidgetUtils::calculateScaleFactor();
    auto sizes = splitter->sizes();
    int totalWidth = 0;
    for (auto sz : sizes) {
        totalWidth += sz;
    }

    int newWidth = totalWidth - minSplitWidth * (sizes.size() - 1);
    if (newWidth <= 0) {
        return;
    }

    int idx = splitter->indexOf(p_widget);
    for (int i = 0; i < sizes.size(); ++i) {
        sizes[i] = (i == idx) ? newWidth : minSplitWidth;
    }

    splitter->setSizes(sizes);
}

void ViewArea::distributeViewSplits()
{
    // Get the top splitter if there is any.
    auto splitter = dynamic_cast<QSplitter *>(m_mainLayout->itemAt(0)->widget());
    if (!splitter) {
        return;
    }

    distributeViewSplitsOfSplitter(splitter);
}

void ViewArea::removeWorkspace(QSharedPointer<ViewWorkspace> p_workspace)
{
    if (!p_workspace) {
        return;
    }
    Q_ASSERT(!p_workspace->m_visible && p_workspace->m_viewWindows.isEmpty());

    p_workspace->clear();
    m_workspaces.removeAll(p_workspace);
}

void ViewArea::newWorkspaceInViewSplit(ViewSplit *p_split)
{
    auto workspace = createWorkspace();
    m_workspaces.push_back(workspace);

    p_split->setWorkspace(workspace);
    setCurrentViewSplit(p_split, true);
}

bool ViewArea::removeWorkspaceInViewSplit(ViewSplit *p_split, bool p_insertNew)
{
    // Close all the ViewWindows.
    setCurrentViewSplit(p_split, true);
    auto wins = p_split->getAllViewWindows();
    for (const auto win : wins) {
        if (!closeViewWindow(win, false, false)) {
            return false;
        }
    }

    Q_ASSERT(p_split->getViewWindowCount() == 0);
    auto workspace = p_split->getWorkspace();
    p_split->setWorkspace(nullptr);
    removeWorkspace(workspace);

    if (p_insertNew) {
        // Find an invisible workspace.
        bool found = false;
        for (auto &ws : m_workspaces) {
            if (!ws->m_visible) {
                p_split->setWorkspace(ws);
                found = true;
                break;
            }
        }

        // No invisible workspace. Create a new empty workspace.
        if (!found) {
            newWorkspaceInViewSplit(p_split);
        }
    }

    return true;
}

bool ViewArea::shouldUseGlobalStatusWidget() const
{
    return m_splits.size() <= 1;
}

void ViewArea::handleViewSplitsCountChange()
{
    if (shouldUseGlobalStatusWidget()) {
        // Hide the status widget for all ViewWindows.
        forEachViewWindow([](ViewWindow *p_win) {
            p_win->setStatusWidgetVisible(false);
            return true;
        });

        // Show global status widget for current ViewWindow.
        auto win = getCurrentViewWindow();
        m_currentStatusWidget = win ? win->statusWidget() : nullptr;
        emit statusWidgetChanged(m_currentStatusWidget.get());
    } else {
        // Show standalone status widget for all ViewWindows.
        emit statusWidgetChanged(nullptr);
        m_currentStatusWidget = nullptr;

        forEachViewWindow([](ViewWindow *p_win) {
            p_win->setStatusWidgetVisible(true);
            return true;
        });
    }
}

void ViewArea::forEachViewWindow(const ViewSplit::ViewWindowSelector &p_func)
{
    for (auto split : m_splits) {
        if (!split->forEachViewWindow(p_func)) {
            return;
        }
    }

    for (auto &ws : m_workspaces) {
        if (!ws->m_visible) {
            for (auto win : ws->m_viewWindows) {
                if (!p_func(win)) {
                    return;
                }
            }
        }
    }
}

bool ViewArea::close(bool p_force)
{
    return closeIf(p_force, [](ViewWindow *p_win) {
               Q_UNUSED(p_win);
               return true;
           }, true);
}

void ViewArea::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // CloseTab.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::CloseTab), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            closeViewWindow(win, false, true);
                        }
                    });
        }
    }

    // CloseAllTabs
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::CloseAllTabs), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        getCurrentViewSplit()->closeMultipleTabs(ViewSplit::CloseTabMode::CloseAllTabs);
                    });
        }
    }

    // CloseOtherTabs
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::CloseOtherTabs), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        getCurrentViewSplit()->closeMultipleTabs(ViewSplit::CloseTabMode::CloseOtherTabs);
                    });
        }
    }

    // CloseTabsToTheLeft
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::CloseTabsToTheLeft), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        getCurrentViewSplit()->closeMultipleTabs(ViewSplit::CloseTabMode::CloseTabsToTheLeft);
                    });
        }
    }

    // CloseTabsToTheRight
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::CloseTabsToTheRight), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        getCurrentViewSplit()->closeMultipleTabs(ViewSplit::CloseTabMode::CloseTabsToTheRight);
                    });
        }
    }

    // LocateNode.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::LocateNode), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            auto node = win->getBuffer()->getNode();
                            if (node) {
                                emit VNoteX::getInst().locateNodeRequested(node);
                            }
                        }
                    });
        }
    }

    // FocusContentArea.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::FocusContentArea), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto win = getCurrentViewWindow();
                        if (win) {
                            win->setFocus();
                        } else {
                            setFocus();
                        }
                    });
        }
    }

    // OneSplitLeft.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OneSplitLeft), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        focusSplitByDirection(Direction::Left);
                    });
        }
    }

    // OneSplitDown.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OneSplitDown), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        focusSplitByDirection(Direction::Down);
                    });
        }
    }

    // OneSplitUp.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OneSplitUp), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        focusSplitByDirection(Direction::Up);
                    });
        }
    }

    // OneSplitRight.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OneSplitRight), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        focusSplitByDirection(Direction::Right);
                    });
        }
    }

    // OpenLastClosedFile.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OpenLastClosedFile), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        auto file = HistoryMgr::getInst().popLastClosedFile();
                        if (file.m_path.isEmpty()) {
                            VNoteX::getInst().showStatusMessageShort(tr("No recently closed file"));
                            return;
                        }
                        auto paras = QSharedPointer<FileOpenParameters>::create();
                        paras->m_lineNumber = file.m_lineNumber;
                        paras->m_mode = file.m_mode;
                        paras->m_readOnly = file.m_readOnly;
                        emit VNoteX::getInst().openFileRequested(file.m_path, paras);
                    });
        }
    }
}

bool ViewArea::close(Node *p_node, bool p_force)
{
    return closeIf(p_force, [p_node](ViewWindow *p_win) {
                auto buffer = p_win->getBuffer();
                return buffer->match(p_node) || buffer->isChildOf(p_node);
            }, true);
}

bool ViewArea::close(const Notebook *p_notebook, bool p_force)
{
    return close(p_notebook->getRootNode().data(), p_force);
}

void ViewArea::checkCurrentViewWindowChange()
{
    auto win = getCurrentViewWindow();
    if (win == m_currentWindow) {
        return;
    }

    m_currentWindow = win;
    emit currentViewWindowChanged();
}

bool ViewArea::closeIf(bool p_force, const ViewSplit::ViewWindowSelector &p_func, bool p_closeEmptySplit)
{
    // Go through all hidden workspace. Use current split to show the workspace.
    if (m_workspaces.size() > m_splits.size()) {
        if (!m_currentSplit) {
            // Create at least one split.
            addFirstViewSplit();
        }

        // Need to restore it.
        auto currentWorkspace = m_currentSplit->getWorkspace();

        QVector<QSharedPointer<ViewWorkspace>> hiddenWorkspaces;
        for (auto &ws : m_workspaces) {
            if (!ws->m_visible) {
                Q_ASSERT(ws != currentWorkspace);
                hiddenWorkspaces.push_back(ws);
            }
        }

        Q_ASSERT(!hiddenWorkspaces.isEmpty());
        for (auto &ws : hiddenWorkspaces) {
            m_currentSplit->setWorkspace(ws);

            // Go through this split.
            auto wins = getAllViewWindows(m_currentSplit, p_func);
            for (const auto win : wins) {
                // Do not remove the split even if it is empty.
                bool ret = closeViewWindow(win, p_force, false);
                if (!ret) {
                    // User cancels the close of one ViewWindow. No need to restore the workspace.
                    return false;
                }
            }

            m_currentSplit->setWorkspace(nullptr);

            // Remove this workspace if it is empty.
            if (ws->m_viewWindows.isEmpty()) {
                removeWorkspace(ws);
            }
        }

        // Restore.
        m_currentSplit->setWorkspace(currentWorkspace);
    }

    // Go through all splits.
    // Collect the ViewWindows first. Collect empty splits.
    QVector<ViewWindow *> wins;
    QVector<ViewSplit *> emptySplits;
    for (auto split : m_splits) {
        if (p_closeEmptySplit && split->getViewWindowCount() == 0) {
            emptySplits.push_back(split);
            continue;
        }

        wins.append(getAllViewWindows(split, p_func));
    }

    if (!emptySplits.isEmpty()) {
        // Remove empty splits.
        for (auto split : emptySplits) {
            removeViewSplit(split, true);
        }
    }

    if (wins.isEmpty()) {
        return true;
    }

    // Close the ViewWindow.
    for (auto win : wins) {
        bool ret = closeViewWindow(win, p_force, true);
        if (!ret) {
            return false;
        }
    }

    return true;
}

void ViewArea::focus()
{
    auto split = getCurrentViewSplit();
    if (split) {
        split->focus();
    }
}

QVector<void *> ViewArea::getVisibleNavigationItems()
{
    QVector<void *> items;
    m_navigationItems.clear();

    int idx = 0;
    for (auto split : m_splits) {
        if (split->getViewWindowCount() == 0) {
            continue;
        }
        if (idx >= NavigationMode::c_maxNumOfNavigationItems) {
            break;
        }
        auto info = split->getNavigationModeInfo();
        for (int i = 0; i < info.size() && idx < NavigationMode::c_maxNumOfNavigationItems; ++i, ++idx) {
            items.push_back(info[i].m_viewWindow);
            m_navigationItems.push_back(info[i]);
        }
    }
    return items;
}

void ViewArea::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label)
{
    Q_UNUSED(p_item);
    Q_ASSERT(p_idx > -1);
    p_label->setParent(m_navigationItems[p_idx].m_viewWindow->getViewSplit());
    p_label->move(m_navigationItems[p_idx].m_topLeft);
}

void ViewArea::handleTargetHit(void *p_item)
{
    if (p_item) {
        setCurrentViewWindow(static_cast<ViewWindow *>(p_item));
        focus();
    }
}

void ViewArea::clearNavigation()
{
    NavigationMode::clearNavigation();

    m_navigationItems.clear();
}

void ViewArea::dragEnterEvent(QDragEnterEvent *p_event)
{
    if (UrlDragDropUtils::handleDragEnterEvent(p_event)) {
        return;
    }

    QWidget::dragEnterEvent(p_event);
}

void ViewArea::dropEvent(QDropEvent *p_event)
{
    if (UrlDragDropUtils::handleDropEvent(p_event, [](const QStringList &p_files) {
                for (const auto &file : p_files) {
                    emit VNoteX::getInst().openFileRequested(file, QSharedPointer<FileOpenParameters>::create());
                }
            })) {
        return;
    }

    QWidget::dropEvent(p_event);
}

QVector<ViewWindow *> ViewArea::getAllViewWindows(ViewSplit *p_split, const ViewSplit::ViewWindowSelector &p_func) const
{
    QVector<ViewWindow *> wins;
    p_split->forEachViewWindow([p_func, &wins](ViewWindow *p_win) {
            if (p_func(p_win)) {
                wins.push_back(p_win);
            }
            return true;
        });
    return wins;
}

const QVector<ViewSplit *>& ViewArea::getAllViewSplits() const
{
    return m_splits;
}

QList<Buffer *> ViewArea::getAllBuffersInViewSplits() const
{
    QSet<Buffer *> bufferSet;

    for (auto split : m_splits) {
        auto wins = split->getAllViewWindows();
        for (auto win : wins) {
            bufferSet.insert(win->getBuffer());
        }
    }

    return bufferSet.values();
}

void ViewArea::loadSession()
{
    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    // Clear it if recover is disabled.
    auto sessionData = sessionConfig.getViewAreaSessionAndClear();

    if (!ConfigMgr::getInst().getCoreConfig().isRecoverLastSessionOnStartEnabled()) {
        showSceneWidget();
        return;
    }

    auto session = ViewAreaSession::deserialize(sessionData);

    // Load widgets layout.
    if (session.m_root.isEmpty()) {
        showSceneWidget();
    } else {
        Q_ASSERT(m_splits.isEmpty());
        if (session.m_root.m_type == ViewAreaSession::Node::Type::Splitter) {
            // Splitter.
            auto splitter = createSplitter(session.m_root.m_orientation, this);
            m_mainLayout->addWidget(splitter);

            loadSplitterFromSession(session.m_root, splitter);
        } else {
            // Just only one ViewSplit.
            Q_ASSERT(session.m_root.m_type == ViewAreaSession::Node::Type::ViewSplit);
            auto split = createViewSplit(this, session.m_root.m_viewSplitId);
            m_splits.push_back(split);
            m_mainLayout->addWidget(split);
        }

        QHash<ID, int> viewSplitToWorkspace;

        setCurrentViewSplit(m_splits.first(), false);

        // Load invisible workspace.
        for (int i = 0; i < session.m_workspaces.size(); ++i) {
            const auto &ws = session.m_workspaces[i];
            if (ws.m_viewSplitId != InvalidViewSplitId) {
                viewSplitToWorkspace.insert(ws.m_viewSplitId, i);
                continue;
            }

            for (const auto &winSession : ws.m_viewWindows) {
                openViewWindowFromSession(winSession);
            }

            // Check if there is any window.
            if (m_currentSplit->getViewWindowCount() > 0) {
                m_currentSplit->setCurrentViewWindow(ws.m_currentViewWindowIndex);

                // New another workspace.
                auto newWs = createWorkspace();
                m_workspaces.push_back(newWs);
                m_currentSplit->setWorkspace(newWs);
            }
        }

        // Load visible workspace.
        for (auto split : m_splits) {
            setCurrentViewSplit(split, false);

            auto it = viewSplitToWorkspace.find(split->getId());
            Q_ASSERT(it != viewSplitToWorkspace.end());

            const auto &ws = session.m_workspaces[it.value()];

            for (const auto &winSession : ws.m_viewWindows) {
                openViewWindowFromSession(winSession);
            }

            if (m_currentSplit->getViewWindowCount() > 0) {
                m_currentSplit->setCurrentViewWindow(ws.m_currentViewWindowIndex);
            }
        }

        postFirstViewSplit();

        distributeViewSplits();
    }
}

void ViewArea::saveSession() const
{
    ViewAreaSession session;
    takeSnapshot(session);

    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    sessionConfig.setViewAreaSession(session.serialize());
}

static void takeSnapshotOfWidgetNodes(ViewAreaSession::Node &p_node, const QWidget *p_widget, QHash<ID, ID> &p_workspaceToViewSplit)
{
    p_node.clear();

    // Splitter.
    auto splitter = dynamic_cast<const QSplitter *>(p_widget);
    if (splitter) {
        p_node.m_type = ViewAreaSession::Node::Type::Splitter;
        p_node.m_orientation = splitter->orientation();
        p_node.m_children.resize(splitter->count());

        for (int i = 0; i < p_node.m_children.size(); ++i) {
            takeSnapshotOfWidgetNodes(p_node.m_children[i], splitter->widget(i), p_workspaceToViewSplit);
        }

        return;
    }

    // ViewSplit.
    auto viewSplit = dynamic_cast<const ViewSplit *>(p_widget);
    Q_ASSERT(viewSplit);
    p_node.m_type = ViewAreaSession::Node::Type::ViewSplit;
    p_node.m_viewSplitId = viewSplit->getId();

    auto ws = viewSplit->getWorkspace();
    if (ws) {
        viewSplit->updateStateToWorkspace();
        p_workspaceToViewSplit.insert(ws->m_id, viewSplit->getId());
    }
}


void ViewArea::takeSnapshot(ViewAreaSession &p_session) const
{
    QHash<ID, ID> workspaceToViewSplit;

    // Widget hirarchy.
    p_session.m_root.clear();
    if (!m_splits.isEmpty()) {
        auto topWidget = m_mainLayout->itemAt(0)->widget();
        takeSnapshotOfWidgetNodes(p_session.m_root, topWidget, workspaceToViewSplit);
    }

    // Workspaces.
    p_session.m_workspaces.clear();
    p_session.m_workspaces.reserve(m_workspaces.size());
    for (const auto &ws : m_workspaces) {
        p_session.m_workspaces.push_back(ViewAreaSession::Workspace());
        auto &wsSnap = p_session.m_workspaces.last();
        if (ws->m_visible) {
            auto it = workspaceToViewSplit.find(ws->m_id);
            Q_ASSERT(it != workspaceToViewSplit.end());
            wsSnap.m_viewSplitId = it.value();
        }
        wsSnap.m_currentViewWindowIndex = ws->m_currentViewWindowIndex;
        for (auto win : ws->m_viewWindows) {
            if (win->isSessionEnabled()) {
                wsSnap.m_viewWindows.push_back(win->saveSession());
            }
        }
    }
}

void ViewArea::loadSplitterFromSession(const ViewAreaSession::Node &p_node, QSplitter *p_splitter)
{
    // @p_splitter is the splitter corresponding to @p_node.
    Q_ASSERT(p_node.m_type == ViewAreaSession::Node::Type::Splitter);

    for (const auto &child : p_node.m_children) {
        if (child.m_type == ViewAreaSession::Node::Type::Splitter) {
            auto childSplitter = createSplitter(child.m_orientation, p_splitter);
            p_splitter->addWidget(childSplitter);

            loadSplitterFromSession(child, childSplitter);
        } else {
            Q_ASSERT(child.m_type == ViewAreaSession::Node::Type::ViewSplit);
            auto childSplit = createViewSplit(this, child.m_viewSplitId);
            m_splits.push_back(childSplit);
            p_splitter->addWidget(childSplit);
        }
    }
}

void ViewArea::openViewWindowFromSession(const ViewWindowSession &p_session)
{
    if (p_session.m_bufferPath.isEmpty()) {
        return;
    }

    auto paras = QSharedPointer<FileOpenParameters>::create();
    paras->m_mode = p_session.m_viewWindowMode;
    paras->m_readOnly = p_session.m_readOnly;
    paras->m_lineNumber = p_session.m_lineNumber;
    paras->m_alwaysNewWindow = true;

    emit VNoteX::getInst().openFileRequested(p_session.m_bufferPath, paras);
}

void ViewArea::focusSplitByDirection(Direction p_direction)
{
    auto split = findSplitByDirection(m_currentSplit, p_direction);
    if (!split) {
        qWarning() << "failed to focus split by direction";
        return;
    }

    setCurrentViewSplit(split, true);
    flashViewSplit(split);
}

ViewSplit *ViewArea::findSplitByDirection(ViewSplit *p_split, Direction p_direction)
{
    if (!p_split) {
        return nullptr;
    }

    QWidget *widget = p_split;
    const auto targetSplitType = splitTypeOfDirection(p_direction);
    int splitIdx = 0;

    QSplitter *targetSplitter = nullptr;
    while (true) {
        auto splitter = tryGetParentSplitter(widget);
        if (!splitter) {
            return nullptr;
        }

        if (checkSplitType(splitter) == targetSplitType) {
            targetSplitter = splitter;
            splitIdx = splitter->indexOf(widget);
            break;
        } else {
            widget = splitter;
        }
    }

    Q_ASSERT(targetSplitter);
    switch (p_direction) {
    case Direction::Left:
        --splitIdx;
        break;

    case Direction::Right:
        ++splitIdx;
        break;

    case Direction::Up:
        --splitIdx;
        break;

    case Direction::Down:
        ++splitIdx;
        break;
    }

    if (splitIdx < 0 || splitIdx >= targetSplitter->count()) {
        return nullptr;
    }

    auto targetWidget = targetSplitter->widget(splitIdx);
    // Find first split from targetWidget.
    while (true) {
        auto splitter = dynamic_cast<QSplitter *>(targetWidget);
        if (splitter) {
            if (splitter->count() == 0) {
                // Should not be an empty splitter.
                Q_ASSERT(false);
                return nullptr;
            }
            targetWidget = splitter->widget(0);
        } else {
            auto viewSplit = dynamic_cast<ViewSplit *>(targetWidget);
            Q_ASSERT(viewSplit);
            return viewSplit;
        }
    }
}

ViewArea::SplitType ViewArea::checkSplitType(const QSplitter *p_splitter) const
{
    return p_splitter->orientation() == Qt::Horizontal ? SplitType::Vertical : SplitType::Horizontal;
}

void ViewArea::flashViewSplit(ViewSplit *p_split)
{
    auto tabBar = p_split->tabBar();
    if (!tabBar) {
        return;
    }

    // Directly set the property of ViewSplit won't work.
    WidgetUtils::setPropertyDynamically(tabBar, PropertyDefs::c_viewSplitFlash, true);
    QTimer::singleShot(1000, tabBar, [tabBar]() {
                WidgetUtils::setPropertyDynamically(tabBar, PropertyDefs::c_viewSplitFlash, false);
            });
}

void ViewArea::moveViewWindowOneSplit(ViewSplit *p_split, ViewWindow *p_win, Direction p_direction)
{
    bool splitCountChanged = false;
    auto targetSplit = findSplitByDirection(p_split, p_direction);
    if (!targetSplit) {
        if (p_split->getViewWindowCount() == 1) {
            // Only one ViewWindow left. Skip it.
            return;
        }

        // Create a new split.
        targetSplit = splitViewSplit(p_split, splitTypeOfDirection(p_direction), false);
        if (p_direction == Direction::Left || p_direction == Direction::Up) {
            // Need to swap the position of the two splits.
            auto splitter = tryGetParentSplitter(targetSplit);
            Q_ASSERT(splitter);
            Q_ASSERT(splitter->indexOf(targetSplit) == 1);
            splitter->insertWidget(0, targetSplit);
        }
        splitCountChanged = true;
    }

    // Take ViewWindow out of @p_split.
    p_split->takeViewWindow(p_win);
    if (p_split->getViewWindowCount() == 0) {
        removeViewSplit(p_split, true);
        splitCountChanged = true;
    }

    targetSplit->addViewWindow(p_win);

    setCurrentViewWindow(p_win);

    flashViewSplit(targetSplit);

    if (splitCountChanged) {
        emit viewSplitsCountChanged();
    }
}

ViewArea::SplitType ViewArea::splitTypeOfDirection(Direction p_direction)
{
    if (p_direction == Direction::Up || p_direction == Direction::Down) {
        return SplitType::Horizontal;
    } else {
        return SplitType::Vertical;
    }
}

void ViewArea::updateHistory(const ViewWindowSession &p_session, Notebook *p_notebook) const
{
    HistoryMgr::getInst().add(p_session.m_bufferPath,
                              p_session.m_lineNumber,
                              p_session.m_viewWindowMode,
                              p_session.m_readOnly,
                              p_notebook);
}

void ViewArea::closeFile(const QString &p_filePath, const QSharedPointer<Event> &p_event)
{
    if (p_event->m_handled) {
        return;
    }

    auto node = VNoteX::getInst().getNotebookMgr().loadNodeByPath(p_filePath);
    bool done = false;
    if (node) {
        done = close(node.data(), false);
    } else {
        done = closeIf(false, [p_filePath](ViewWindow *p_win) {
                    auto buffer = p_win->getBuffer();
                    return buffer->match(p_filePath);
                }, true);
    }

    p_event->m_response = done;
    p_event->m_handled = !done;
}

void ViewArea::setCurrentViewWindow(ID p_splitId, int p_windowIndex)
{
    auto split = findSplitById(p_splitId);
    if (split) {
        setCurrentViewSplit(split, true);
        split->setCurrentViewWindow(p_windowIndex);
    }
}

ViewSplit *ViewArea::findSplitById(ID p_splitId) const
{
    for (auto split : m_splits) {
        if (split->getId() == p_splitId) {
            return split;
        }
    }

    return nullptr;
}
