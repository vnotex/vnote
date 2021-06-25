#include "mainwindow.h"

#include <QFileInfo>
#include <QDebug>
#include <QResizeEvent>
#include <QSplitter>
#include <QVariant>
#include <QDockWidget>
#include <QTextEdit>
#include <QStatusBar>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QTabBar>
#include <QVariant>
#include <QCoreApplication>
#include <QApplication>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QWindowStateChangeEvent>
#include <QTimer>

#include "toolbox.h"
#include "notebookexplorer.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "buffermgr.h"
#include "viewarea.h"
#include <core/configmgr.h>
#include <core/sessionconfig.h>
#include <core/coreconfig.h>
#include <core/widgetconfig.h>
#include <core/events.h>
#include <core/fileopenparameters.h>
#include <widgets/dialogs/exportdialog.h>
#include "viewwindow.h"
#include "outlineviewer.h"
#include <utils/widgetutils.h>
#include "navigationmodemgr.h"
#include "messageboxhelper.h"
#include "systemtrayhelper.h"
#include "titletoolbar.h"
#include "locationlist.h"
#include "searchpanel.h"
#include "snippetpanel.h"
#include <notebook/notebook.h>
#include "searchinfoprovider.h"
#include <vtextedit/spellchecker.h>

using namespace vnotex;

MainWindow::MainWindow(QWidget *p_parent)
    : QMainWindow(p_parent)
{
    VNoteX::getInst().setMainWindow(this);

    NavigationModeMgr::init(this);

    setupUI();

    setupShortcuts();

    loadStateAndGeometry();

    // The signal is particularly useful if your application has to do some last-second cleanup.
    // Note that no user interaction is possible in this state.
    connect(qApp, &QCoreApplication::aboutToQuit,
            this, &MainWindow::closeOnQuit);

    connect(&VNoteX::getInst(), &VNoteX::exportRequested,
            this, &MainWindow::exportNotes);
}

MainWindow::~MainWindow()
{
    // Should be desturcted before status bar.
    delete m_viewArea;
    m_viewArea = nullptr;
}

void MainWindow::kickOffOnStart(const QStringList &p_paths)
{
    QTimer::singleShot(300, [this, p_paths]() {
        // Need to load the state of dock widgets again after the main window is shown.
        loadStateAndGeometry(true);

        VNoteX::getInst().initLoad();

        setupSpellCheck();

        // Do necessary stuffs before emitting this signal.
        emit mainWindowStarted();

        emit layoutChanged();

        demoWidget();

        openFiles(p_paths);
    });
}

void MainWindow::openFiles(const QStringList &p_files)
{
    for (const auto &file : p_files) {
        emit VNoteX::getInst().openFileRequested(file, QSharedPointer<FileOpenParameters>::create());
    }
}

void MainWindow::setupUI()
{
    setupCentralWidget();
    setupDocks();
    setupToolBar();
    setupStatusBar();
    setupTipsArea();
    setupSystemTray();

    activateDock(m_docks[DockIndex::NavigationDock]);
}

void MainWindow::setupStatusBar()
{
    m_statusBarHelper.setupStatusBar(this);
    connect(&VNoteX::getInst(), &VNoteX::statusMessageRequested,
            statusBar(), &QStatusBar::showMessage);
}

void MainWindow::setupTipsArea()
{
    connect(&VNoteX::getInst(), &VNoteX::tipsRequested,
            this, &MainWindow::showTips);
}

void MainWindow::createTipsArea()
{
    if (m_tipsLabel) {
        return;
    }

    m_tipsLabel = new QLabel(this);
    m_tipsLabel->setObjectName("MainWindowTipsLabel");
    m_tipsLabel->hide();

    m_tipsTimer = new QTimer(this);
    m_tipsTimer->setSingleShot(true);
    m_tipsTimer->setInterval(3000);
    connect(m_tipsTimer, &QTimer::timeout,
            this, [this]() {
                setTipsAreaVisible(false);
            });
}

void MainWindow::setupCentralWidget()
{
    m_viewArea = new ViewArea(this);
    NavigationModeMgr::getInst().registerNavigationTarget(m_viewArea);
    connect(&VNoteX::getInst().getBufferMgr(), &BufferMgr::bufferRequested,
            m_viewArea, &ViewArea::openBuffer);

    connect(m_viewArea, &ViewArea::statusWidgetChanged,
            this, [this](QWidget *p_widget) {
                if (m_viewAreaStatusWidget) {
                    // Will hide it.
                    statusBar()->removeWidget(m_viewAreaStatusWidget);
                }

                m_viewAreaStatusWidget = p_widget;
                if (m_viewAreaStatusWidget) {
                    statusBar()->addPermanentWidget(m_viewAreaStatusWidget);
                    m_viewAreaStatusWidget->show();
                }
            });
    connect(m_viewArea, &ViewArea::currentViewWindowChanged,
            this, [this]() {
                setWindowTitle(getViewAreaTitle());
            });
    connect(m_viewArea, &ViewArea::currentViewWindowUpdated,
            this, [this]() {
                setWindowTitle(getViewAreaTitle());
            });

    {
        auto notebookMgr = &VNoteX::getInst().getNotebookMgr();
        connect(notebookMgr, &NotebookMgr::notebookAboutToClose,
                this, [this](const Notebook *p_notebook) {
                    m_viewArea->close(p_notebook, true);
                });
        connect(notebookMgr, &NotebookMgr::notebookAboutToRemove,
                this, [this](const Notebook *p_notebook) {
                    m_viewArea->close(p_notebook, true);
                });
    }

    setCentralWidget(m_viewArea);
}

void MainWindow::setupDocks()
{
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::West);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::East);
    setTabPosition(Qt::TopDockWidgetArea, QTabWidget::North);
    setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    setDockNestingEnabled(true);

    // The order of m_docks should be identical with enum DockIndex.
    setupNavigationDock();

    setupOutlineDock();

    setupSearchDock();

    setupSnippetDock();

    for (int i = 1; i < m_docks.size(); ++i) {
        tabifyDockWidget(m_docks[i - 1], m_docks[i]);
    }

    setupLocationListDock();

    for (auto dock : m_docks) {
        connect(dock, &QDockWidget::visibilityChanged,
                this, [this]() {
                    updateTabBarStyle();
                    emit layoutChanged();
                });
    }

    activateDock(m_docks[DockIndex::NavigationDock]);
}

void MainWindow::activateDock(QDockWidget *p_dock)
{
    p_dock->show();
    Q_FOREACH(QTabBar* tabBar, this->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
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
}

void MainWindow::setupNavigationDock()
{
    auto dock = new QDockWidget(tr("Navigation"), this);
    m_docks.push_back(dock);

    dock->setObjectName(QStringLiteral("NavigationDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    setupNotebookExplorer(this);
    dock->setWidget(m_notebookExplorer);
    dock->setFocusProxy(m_notebookExplorer);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::setupOutlineDock()
{
    auto dock = new QDockWidget(tr("Outline"), this);
    m_docks.push_back(dock);

    dock->setObjectName(QStringLiteral("OutlineDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    setupOutlineViewer();
    dock->setWidget(m_outlineViewer);
    dock->setFocusProxy(m_outlineViewer);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::setupSearchDock()
{
    auto dock = new QDockWidget(tr("Search"), this);
    m_docks.push_back(dock);

    dock->setObjectName(QStringLiteral("SearchDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    setupSearchPanel();
    dock->setWidget(m_searchPanel);
    dock->setFocusProxy(m_searchPanel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::setupSearchPanel()
{
    m_searchPanel = new SearchPanel(
        QSharedPointer<SearchInfoProvider>::create(m_viewArea,
                                                   m_notebookExplorer,
                                                   &VNoteX::getInst().getNotebookMgr()),
        this);
    m_searchPanel->setObjectName("SearchPanel.vnotex");
}

void MainWindow::setupSnippetDock()
{
    auto dock = new QDockWidget(tr("Snippets"), this);
    m_docks.push_back(dock);

    dock->setObjectName(QStringLiteral("SnippetDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    setupSnippetPanel();
    dock->setWidget(m_snippetPanel);
    dock->setFocusProxy(m_snippetPanel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::setupSnippetPanel()
{
    m_snippetPanel = new SnippetPanel(this);
    m_snippetPanel->setObjectName("SnippetPanel.vnotex");
    connect(m_snippetPanel, &SnippetPanel::applySnippetRequested,
            this, [this](const QString &p_name) {
                auto viewWindow = m_viewArea->getCurrentViewWindow();
                if (viewWindow) {
                    viewWindow->applySnippet(p_name);
                    viewWindow->setFocus();
                }
            });
}

void MainWindow::setupLocationListDock()
{
    auto dock = new QDockWidget(tr("Location List"), this);
    m_docks.push_back(dock);

    dock->setObjectName(QStringLiteral("LocationListDock.vnotex"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    setupLocationList();
    dock->setWidget(m_locationList);
    dock->setFocusProxy(m_locationList);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->hide();
}

void MainWindow::setupLocationList()
{
    m_locationList = new LocationList(this);
    m_locationList->setObjectName("LocationList.vnotex");

    NavigationModeMgr::getInst().registerNavigationTarget(m_locationList->getNavigationModeWrapper());
}

void MainWindow::setupNotebookExplorer(QWidget *p_parent)
{
    m_notebookExplorer = new NotebookExplorer(p_parent);
    connect(&VNoteX::getInst(), &VNoteX::newNotebookRequested,
            m_notebookExplorer, &NotebookExplorer::newNotebook);
    connect(&VNoteX::getInst(), &VNoteX::newNotebookFromFolderRequested,
            m_notebookExplorer, &NotebookExplorer::newNotebookFromFolder);
    connect(&VNoteX::getInst(), &VNoteX::importNotebookRequested,
            m_notebookExplorer, &NotebookExplorer::importNotebook);
    connect(&VNoteX::getInst(), &VNoteX::newFolderRequested,
            m_notebookExplorer, &NotebookExplorer::newFolder);
    connect(&VNoteX::getInst(), &VNoteX::newNoteRequested,
            m_notebookExplorer, &NotebookExplorer::newNote);
    connect(&VNoteX::getInst(), &VNoteX::importFileRequested,
            m_notebookExplorer, &NotebookExplorer::importFile);
    connect(&VNoteX::getInst(), &VNoteX::importFolderRequested,
            m_notebookExplorer, &NotebookExplorer::importFolder);
    connect(&VNoteX::getInst(), &VNoteX::importLegacyNotebookRequested,
            m_notebookExplorer, &NotebookExplorer::importLegacyNotebook);
    connect(&VNoteX::getInst(), &VNoteX::locateNodeRequested,
            m_notebookExplorer, &NotebookExplorer::locateNode);

    auto notebookMgr = &VNoteX::getInst().getNotebookMgr();
    connect(notebookMgr, &NotebookMgr::notebooksUpdated,
            m_notebookExplorer, &NotebookExplorer::loadNotebooks);
    connect(notebookMgr, &NotebookMgr::notebookUpdated,
            m_notebookExplorer, &NotebookExplorer::reloadNotebook);
    connect(notebookMgr, &NotebookMgr::currentNotebookChanged,
            m_notebookExplorer, &NotebookExplorer::setCurrentNotebook);
    connect(m_notebookExplorer, &NotebookExplorer::notebookActivated,
            notebookMgr, &NotebookMgr::setCurrentNotebook);
}

void MainWindow::closeEvent(QCloseEvent *p_event)
{
    const int toTray = ConfigMgr::getInst().getSessionConfig().getMinimizeToSystemTray();
    bool isExit = m_requestQuit > -1 || toTray == 0;
    const int exitCode = m_requestQuit;
    m_requestQuit = -1;

#if defined(Q_OS_MACOS)
    // Do not support minimized to tray on macOS.
    isExit = true;
#endif

    bool needShowMessage = false;
    if(!isExit && toTray == -1){
        int ret =  MessageBoxHelper::questionYesNo(MessageBoxHelper::Question,
                                                   tr("Do you want to minimize %1 to system tray "
                                                      "instead of quitting when closed?").arg(qApp->applicationName()),
                                                   tr("You could change the option in Settings later."),
                                                   QString(),
                                                   this);
        if (ret == QMessageBox::Yes) {
            ConfigMgr::getInst().getSessionConfig().setMinimizeToSystemTray(true);
            needShowMessage = true;
        } else if (ret == QMessageBox::No) {
            ConfigMgr::getInst().getSessionConfig().setMinimizeToSystemTray(false);
            isExit = true;
        } else {
            p_event->ignore();
            return;
        }
    }

    if (isVisible()) {
        saveStateAndGeometry();
    }

    if (isExit || !m_trayIcon->isVisible()) {
        // Signal out the close event.
        auto event = QSharedPointer<Event>::create();
        event->m_response = true;
        emit mainWindowClosed(event);
        if (!event->m_response.toBool()) {
            // Stop the close.
            p_event->ignore();
            return;
        }

        QMainWindow::closeEvent(p_event);
        qApp->exit(exitCode > -1 ? exitCode : 0);
    } else {
        hide();
        p_event->ignore();
        if (needShowMessage) {
            m_trayIcon->showMessage(ConfigMgr::c_appName, tr("%1 is still running here.").arg(ConfigMgr::c_appName));
        }
    }
}

void MainWindow::saveStateAndGeometry()
{
    if (m_layoutReset) {
        return;
    }

    SessionConfig::MainWindowStateGeometry sg;
    sg.m_mainState = saveState();
    sg.m_mainGeometry = saveGeometry();
    sg.m_visibleDocksBeforeExpand = m_visibleDocksBeforeExpand;

    auto& sessionConfig = ConfigMgr::getInst().getSessionConfig();
    sessionConfig.setMainWindowStateGeometry(sg);
}

void MainWindow::loadStateAndGeometry(bool p_stateOnly)
{
    const auto& sessionConfig = ConfigMgr::getInst().getSessionConfig();
    const auto sg = sessionConfig.getMainWindowStateGeometry();

    if (!p_stateOnly && !sg.m_mainGeometry.isEmpty()) {
        restoreGeometry(sg.m_mainGeometry);
    }

    if (!sg.m_mainState.isEmpty()) {
        // Will also restore the state of dock widgets.
        restoreState(sg.m_mainState);
    }

    if (!p_stateOnly) {
        m_visibleDocksBeforeExpand = sg.m_visibleDocksBeforeExpand;
        if (m_visibleDocksBeforeExpand.isEmpty()) {
            // Init (or init again if there is no visible dock).
            for (int i = 0; i < m_docks.size(); ++i) {
                if (m_docks[i]->isVisible()) {
                    m_visibleDocksBeforeExpand.push_back(m_docks[i]->objectName());
                }
            }
        }
    }
}

void MainWindow::resetStateAndGeometry()
{
    if (m_layoutReset) {
        return;
    }

    m_layoutReset = true;
    SessionConfig::MainWindowStateGeometry sg;
    auto& sessionConfig = ConfigMgr::getInst().getSessionConfig();
    sessionConfig.setMainWindowStateGeometry(sg);
}

void MainWindow::setContentAreaExpanded(bool p_expanded)
{
    const auto &keepDocks = ConfigMgr::getInst().getWidgetConfig().getMainWindowKeepDocksExpandingContentArea();

    if (p_expanded) {
        // Store the state and hide.
        m_visibleDocksBeforeExpand.clear();
        for (int i = 0; i < m_docks.size(); ++i) {
            const auto objName = m_docks[i]->objectName();
            if (m_docks[i]->isVisible()) {
                m_visibleDocksBeforeExpand.push_back(objName);
            }

            if (m_docks[i]->isFloating() || keepDocks.contains(objName)) {
                continue;
            }

            m_docks[i]->setVisible(false);
        }
    } else {
        // Restore the state.
        bool hasVisible = false;
        for (int i = 0; i < m_docks.size(); ++i) {
            const auto objName = m_docks[i]->objectName();
            if (m_docks[i]->isFloating() || keepDocks.contains(objName)) {
                continue;
            }

            const bool visible = m_visibleDocksBeforeExpand.contains(objName);
            hasVisible = hasVisible || visible;

            m_docks[i]->setVisible(visible);
        }

        if (!hasVisible) {
            // At least make one visible.
            m_docks[DockIndex::NavigationDock]->setVisible(true);
        }
    }
}

bool MainWindow::isContentAreaExpanded() const
{
    const auto &keepDocks = ConfigMgr::getInst().getWidgetConfig().getMainWindowKeepDocksExpandingContentArea();
    for (auto dock : m_docks) {
        if (!dock->isFloating() && dock->isVisible() && !keepDocks.contains(dock->objectName())) {
            return false;
        }
    }
    return true;
}

void MainWindow::demoWidget()
{
}

QString MainWindow::getViewAreaTitle() const
{
    QString title;
    const auto win = m_viewArea->getCurrentViewWindow();
    if (win) {
        title = win->getName();
    }
    return title.isEmpty() ? QString() : QString("%1 - %2").arg(title, ConfigMgr::c_appName);
}

void MainWindow::setupOutlineViewer()
{
    // Do not provide title here since there is one in the dock title.
    m_outlineViewer = new OutlineViewer(QString(), this);
    m_outlineViewer->setObjectName("OutlineViewer.vnotex");

    NavigationModeMgr::getInst().registerNavigationTarget(m_outlineViewer->getNavigationModeWrapper());

    connect(m_viewArea, &ViewArea::currentViewWindowChanged,
            this, [this]() {
                auto win = m_viewArea->getCurrentViewWindow();
                m_outlineViewer->setOutlineProvider(win ? win->getOutlineProvider() : nullptr);
            });
    connect(m_outlineViewer, &OutlineViewer::focusViewArea,
            this, &MainWindow::focusViewArea);
}

const QVector<QDockWidget *> &MainWindow::getDocks() const
{
    return m_docks;
}

void MainWindow::focusViewArea()
{
    m_viewArea->focus();
}

void MainWindow::setupToolBar()
{
    const int sz = ConfigMgr::getInst().getCoreConfig().getToolBarIconSize();
    const QSize iconSize(sz, sz);
    if (!ConfigMgr::getInst().getSessionConfig().getSystemTitleBarEnabled()) {
        // Use unified tool bar as title bar.
        auto framelessFlags = Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint
                              | Qt::WindowCloseButtonHint | Qt::WindowFullscreenButtonHint;

        auto winFlags = windowFlags();
        winFlags |= Qt::CustomizeWindowHint;
        winFlags &= ~framelessFlags;
        setWindowFlags(winFlags);

        auto toolBar = new TitleToolBar(tr("Global"), this);
        toolBar->setIconSize(iconSize);
        m_toolBarHelper.setupToolBars(this, toolBar);
        toolBar->addTitleBarIcons(ToolBarHelper::generateIcon(QStringLiteral("minimize.svg")),
                                  ToolBarHelper::generateIcon(QStringLiteral("maximize.svg")),
                                  ToolBarHelper::generateIcon(QStringLiteral("maximize_restore.svg")),
                                  ToolBarHelper::generateDangerousIcon(QStringLiteral("close.svg")));
    } else {
        auto toolBar = new QToolBar(tr("Global"), this);
        toolBar->setIconSize(iconSize);
        m_toolBarHelper.setupToolBars(this, toolBar);
    }

    // Disable the context menu above tool bar.
    setContextMenuPolicy(Qt::NoContextMenu);
}

void MainWindow::closeOnQuit()
{
    // No user interaction is available.
    emit mainWindowClosedOnQuit();
}

void MainWindow::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    setupDockActivateShortcut(m_docks[DockIndex::NavigationDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::NavigationDock));

    setupDockActivateShortcut(m_docks[DockIndex::OutlineDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::OutlineDock));

    setupDockActivateShortcut(m_docks[DockIndex::SearchDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::SearchDock));
    // Extra shortcut for SearchDock.
    setupDockActivateShortcut(m_docks[DockIndex::SearchDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::Search));

    setupDockActivateShortcut(m_docks[DockIndex::LocationListDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::LocationListDock));

    setupDockActivateShortcut(m_docks[DockIndex::SnippetDock],
                              coreConfig.getShortcut(CoreConfig::Shortcut::SnippetDock));
}

void MainWindow::setupDockActivateShortcut(QDockWidget *p_dock, const QString &p_keys)
{
    auto shortcut = WidgetUtils::createShortcut(p_keys, this);
    if (shortcut) {
        p_dock->setToolTip(QString("%1\t%2").arg(p_dock->windowTitle(),
                                                 QKeySequence(p_keys).toString(QKeySequence::NativeText)));
        connect(shortcut, &QShortcut::activated,
                this, [this, p_dock]() {
                    activateDock(p_dock);
                });
    }
}

void MainWindow::setStayOnTop(bool p_enabled)
{
    bool shown = isVisible();
    Qt::WindowFlags flags = windowFlags();

    const Qt::WindowFlags magicFlag = Qt::WindowStaysOnTopHint;
    if (p_enabled) {
        setWindowFlags(flags | magicFlag);
    } else {
        setWindowFlags(flags ^ magicFlag);
    }

    if (shown) {
        show();
    }
}

void MainWindow::setupSystemTray()
{
    m_trayIcon = SystemTrayHelper::setupSystemTray(this);
    m_trayIcon->show();
}

void MainWindow::restart()
{
    m_requestQuit = RESTART_EXIT_CODE;
    close();
}

void MainWindow::changeEvent(QEvent *p_event)
{
    if (p_event->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *eve = static_cast<QWindowStateChangeEvent *>(p_event);
        m_windowOldState = eve->oldState();
    }

    QMainWindow::changeEvent(p_event);
}

void MainWindow::showMainWindow()
{
    if (isMinimized()) {
        if (m_windowOldState & Qt::WindowMaximized) {
            showMaximized();
        } else if (m_windowOldState & Qt::WindowFullScreen) {
            showFullScreen();
        } else {
            showNormal();
        }
    } else {
        show();
        // Need to call raise() in macOS.
        raise();
    }

    activateWindow();
}

void MainWindow::quitApp()
{
    m_requestQuit = 0;
    close();
}

void MainWindow::updateTabBarStyle()
{
    Q_FOREACH(QTabBar* tabBar, this->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        tabBar->setDrawBase(false);
    }
}

void MainWindow::exportNotes()
{
    auto currentNotebook = m_notebookExplorer->currentNotebook().data();
    auto viewWindow = m_viewArea->getCurrentViewWindow();
    auto folderNode = m_notebookExplorer->currentExploredFolderNode();
    if (folderNode && (folderNode->isRoot() || currentNotebook->isRecycleBinNode(folderNode))) {
        folderNode = nullptr;
    }
    auto noteNode = m_notebookExplorer->currentExploredNode();
    if (noteNode && !noteNode->hasContent()) {
        noteNode = nullptr;
    }
    ExportDialog dialog(currentNotebook,
                        folderNode,
                        noteNode,
                        viewWindow ? viewWindow->getBuffer() : nullptr,
                        this);
    dialog.exec();
}

void MainWindow::showTips(const QString &p_message, int p_timeoutMilliseconds)
{
    createTipsArea();

    m_tipsTimer->stop();

    setTipsAreaVisible(false);

    if (p_message.isEmpty()) {
        return;
    }

    m_tipsLabel->setText(p_message);
    setTipsAreaVisible(true);

    m_tipsTimer->start(p_timeoutMilliseconds);
}

void MainWindow::setTipsAreaVisible(bool p_visible)
{
    Q_ASSERT(m_tipsLabel);
    if (p_visible) {
        m_tipsLabel->adjustSize();
        int labelW = m_tipsLabel->width();
        int labelH = m_tipsLabel->height();
        int x = (width() - labelW) / 2;
        int y = (height() - labelH) / 2;
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }

        m_tipsLabel->move(x, y);
        m_tipsLabel->show();
    } else {
        m_tipsLabel->hide();
    }
}

LocationList *MainWindow::getLocationList() const
{
    return m_locationList;
}

void MainWindow::setLocationListVisible(bool p_visible)
{
    if (p_visible) {
        activateDock(m_docks[DockIndex::LocationListDock]);
    } else {
        m_docks[DockIndex::LocationListDock]->hide();
    }
}

void MainWindow::toggleLocationListVisible()
{
    bool visible = m_docks[DockIndex::LocationListDock]->isVisible();
    setLocationListVisible(!visible);
}

void MainWindow::setupSpellCheck()
{
    const auto &configMgr = ConfigMgr::getInst();
    vte::SpellChecker::addDictionaryCustomSearchPaths(
        QStringList() << configMgr.getUserDictsFolder() << configMgr.getAppDictsFolder());
}
