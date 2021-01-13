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

#include "toolbox.h"
#include "notebookexplorer.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "buffermgr.h"
#include "viewarea.h"
#include <core/configmgr.h>
#include <core/sessionconfig.h>
#include <core/coreconfig.h>
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

void MainWindow::kickOffOnStart()
{
    VNoteX::getInst().initLoad();

    emit mainWindowStarted();

    emit layoutChanged();

    demoWidget();
}

void MainWindow::setupUI()
{
    setupCentralWidget();
    setupDocks();
    setupToolBar();
    setupStatusBar();
    setupSystemTray();

    activateDock(m_docks[DockIndex::NavigationDock]);
}

void MainWindow::setupStatusBar()
{
    m_statusBarHelper.setupStatusBar(this);
    connect(&VNoteX::getInst(), &VNoteX::statusMessageRequested,
            statusBar(), &QStatusBar::showMessage);
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

    for (int i = 1; i < m_docks.size(); ++i) {
        tabifyDockWidget(m_docks[i - 1], m_docks[i]);
    }

    for (auto dock : m_docks) {
        connect(dock, &QDockWidget::visibilityChanged,
                this, [this]() {
                    updateTabBarStyle();
                    emit layoutChanged();
                });
    }

    // Activate the first dock.
    activateDock(m_docks[0]);
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

    setupNavigationToolBox();
    dock->setWidget(m_navigationToolBox);
    dock->setFocusProxy(m_navigationToolBox);
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

void MainWindow::setupNavigationToolBox()
{
    m_navigationToolBox = new ToolBox(this);
    m_navigationToolBox->setObjectName("NavigationToolBox.vnotex");

    NavigationModeMgr::getInst().registerNavigationTarget(m_navigationToolBox);

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();

    // Notebook explorer.
    setupNotebookExplorer(m_navigationToolBox);
    m_navigationToolBox->addItem(m_notebookExplorer,
                                 themeMgr.getIconFile("notebook_explorer.svg"),
                                 tr("Notebooks"),
                                 nullptr);

    /*
    // History explorer.
    auto historyExplorer = new QWidget(this);
    m_navigationToolBox->addItem(historyExplorer,
                                 themeMgr.getIconFile("history_explorer.svg"),
                                 tr("History"),
                                 nullptr);

    // Tag explorer.
    auto tagExplorer = new QWidget(this);
    m_navigationToolBox->addItem(tagExplorer,
                                 themeMgr.getIconFile("tag_explorer.svg"),
                                 tr("Tags"),
                                 nullptr);
     */
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

    auto& sessionConfig = ConfigMgr::getInst().getSessionConfig();
    sessionConfig.setMainWindowStateGeometry(sg);
}

void MainWindow::loadStateAndGeometry()
{
    const auto& sessionConfig = ConfigMgr::getInst().getSessionConfig();
    const auto sg = sessionConfig.getMainWindowStateGeometry();

    if (!sg.m_mainGeometry.isEmpty()) {
        restoreGeometry(sg.m_mainGeometry);
    }

    if (!sg.m_mainState.isEmpty()) {
        // Will also restore the state of dock widgets.
        restoreState(sg.m_mainState);
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
    for (auto dock : m_docks) {
        if (!dock->isFloating()) {
            dock->setVisible(!p_expanded);
        }
    }
}

bool MainWindow::isContentAreaExpanded() const
{
    for (auto dock : m_docks) {
        if (!dock->isFloating() && dock->isVisible()) {
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
    // Focus Navigation dock.
    {
        auto keys = coreConfig.getShortcut(CoreConfig::Shortcut::NavigationDock);
        auto shortcut = WidgetUtils::createShortcut(keys, this);
        if (shortcut) {
            auto dock = m_docks[DockIndex::NavigationDock];
            dock->setToolTip(QString("%1\t%2").arg(dock->windowTitle(),
                                                   QKeySequence(keys).toString(QKeySequence::NativeText)));
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        activateDock(m_docks[DockIndex::NavigationDock]);
                    });
        }
    }

    // Focus Outline dock.
    {
        auto keys = coreConfig.getShortcut(CoreConfig::Shortcut::OutlineDock);
        auto shortcut = WidgetUtils::createShortcut(keys, this);
        if (shortcut) {
            auto dock = m_docks[DockIndex::OutlineDock];
            dock->setToolTip(QString("%1\t%2").arg(dock->windowTitle(),
                                                   QKeySequence(keys).toString(QKeySequence::NativeText)));
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        activateDock(m_docks[DockIndex::OutlineDock]);
                    });
        }
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
    ExportDialog dialog(currentNotebook,
                        folderNode,
                        viewWindow ? viewWindow->getBuffer() : nullptr,
                        this);
    dialog.exec();
}
