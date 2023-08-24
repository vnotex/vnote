#include "mainwindow.h"

#include <QFileInfo>
#include <QDebug>
#include <QResizeEvent>
#include <QSplitter>
#include <QDockWidget>
#include <QVariant>
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
#include <QProgressDialog>
#include <QHotkey>

#include "notebookexplorer.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "buffermgr.h"
#include "viewarea.h"
#include <core/configmgr.h>
#include <core/sessionconfig.h>
#include <core/coreconfig.h>
#include <core/mainconfig.h>
#include <core/widgetconfig.h>
#include <core/events.h>
#include <core/exception.h>
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
#include "historypanel.h"
#include "windowspanel.h"
#include "windowsprovider.h"
#include <notebook/notebook.h>
#include "searchinfoprovider.h"
#include <vtextedit/spellchecker.h>
#include <utils/docsutils.h>
#include <utils/iconutils.h>
#include <core/thememgr.h>
#include "dialogs/updater.h"
#include "tagexplorer.h"
#include "toolbarhelper.h"
#include "statusbarhelper.h"
#include "consoleviewer.h"

using namespace vnotex;

MainWindow::MainWindow(QWidget *p_parent)
    : FramelessMainWindowImpl(!ConfigMgr::getInst().getSessionConfig().getSystemTitleBarEnabled(), p_parent),
      m_dockWidgetHelper(this)
{
    VNoteX::getInst().setMainWindow(this);

    NavigationModeMgr::init(this);

    setupUI();

    setupShortcuts();

    loadStateAndGeometry();

    m_dockWidgetHelper.postSetup();

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

        {
            QProgressDialog proDlg(tr("Initializing core components..."),
                                   QString(),
                                   0,
                                   0,
                                   this);
            proDlg.setWindowFlags(proDlg.windowFlags() & ~Qt::WindowCloseButtonHint);
            proDlg.setWindowModality(Qt::WindowModal);
            proDlg.setValue(0);

            VNoteX::getInst().initLoad();

            setupSpellCheck();
        }

        // Do necessary stuffs before emitting this signal.
        emit mainWindowStarted();

        emit layoutChanged();

        checkNotebooksFailedToLoad();

        loadWidgetsData();

        demoWidget();

        openFiles(p_paths);

        if (MainConfig::isVersionChanged()) {
            QString tips;
            try {
                tips = DocsUtils::getDocText("features_tips.txt");
            } catch (Exception &p_e) {
                // Just ignore it.
                Q_UNUSED(p_e);
            }
            if (!tips.isEmpty()) {
                MessageBoxHelper::notify(MessageBoxHelper::Information,
                                         tips,
                                         this);
            }

            const auto file = DocsUtils::getDocFile(QStringLiteral("welcome.md"));
            if (!file.isEmpty()) {
                auto paras = QSharedPointer<FileOpenParameters>::create();
                paras->m_readOnly = true;
                paras->m_sessionEnabled = false;
                emit VNoteX::getInst().openFileRequested(file, paras);
            }
        }

        if (ConfigMgr::getInst().getCoreConfig().isCheckForUpdatesOnStartEnabled()) {
            QTimer::singleShot(5 * 60 * 1000, this, &MainWindow::checkForUpdates);
        }
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

    m_dockWidgetHelper.activateDock(DockWidgetHelper::NavigationDock);
}

void MainWindow::setupStatusBar()
{
    StatusBarHelper::setupStatusBar(this);
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
        auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
        connect(&notebookMgr, &NotebookMgr::notebookAboutToClose,
                this, [this](const Notebook *p_notebook) {
                    m_viewArea->close(p_notebook, true);
                });
        connect(&notebookMgr, &NotebookMgr::notebookAboutToRemove,
                this, [this](const Notebook *p_notebook) {
                    m_viewArea->close(p_notebook, true);
                });
    }

    setCentralWidget(m_viewArea);
}

void MainWindow::setupDocks()
{
    setupNotebookExplorer();

    setupTagExplorer();

    setupOutlineViewer();

    setupConsoleViewer();

    setupHistoryPanel();

    setupWindowsPanel();

    setupSearchPanel();

    setupSnippetPanel();

    setupLocationList();

    m_dockWidgetHelper.setupDocks();

    NavigationModeMgr::getInst().registerNavigationTarget(&m_dockWidgetHelper);
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

void MainWindow::setupHistoryPanel()
{
    m_historyPanel = new HistoryPanel(this);
    m_historyPanel->setObjectName("HistoryPanel.vnotex");
}

void MainWindow::setupWindowsPanel()
{
    m_windowsPanel = new WindowsPanel(QSharedPointer<WindowsProvider>::create(m_viewArea), this);
    m_windowsPanel->setObjectName("WindowsPanel.vnotex");
}

void MainWindow::setupLocationList()
{
    m_locationList = new LocationList(this);
    m_locationList->setObjectName("LocationList.vnotex");
}

void MainWindow::setupNotebookExplorer()
{
    m_notebookExplorer = new NotebookExplorer(this);
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
    connect(&VNoteX::getInst(), &VNoteX::newQuickNoteRequested,
            m_notebookExplorer, &NotebookExplorer::newQuickNote);
    connect(&VNoteX::getInst(), &VNoteX::importFileRequested,
            m_notebookExplorer, &NotebookExplorer::importFile);
    connect(&VNoteX::getInst(), &VNoteX::importFolderRequested,
            m_notebookExplorer, &NotebookExplorer::importFolder);
    connect(&VNoteX::getInst(), &VNoteX::importLegacyNotebookRequested,
            m_notebookExplorer, &NotebookExplorer::importLegacyNotebook);
    connect(&VNoteX::getInst(), &VNoteX::manageNotebooksRequested,
            m_notebookExplorer, &NotebookExplorer::manageNotebooks);
    connect(&VNoteX::getInst(), &VNoteX::locateNodeRequested,
            this, [this](Node *p_node) {
                m_dockWidgetHelper.activateDock(DockWidgetHelper::NavigationDock);
                m_notebookExplorer->locateNode(p_node);
            });

    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    connect(&notebookMgr, &NotebookMgr::notebooksUpdated,
            m_notebookExplorer, &NotebookExplorer::loadNotebooks);
    connect(&notebookMgr, &NotebookMgr::notebookUpdated,
            m_notebookExplorer, &NotebookExplorer::reloadNotebook);
    connect(&notebookMgr, &NotebookMgr::currentNotebookChanged,
            m_notebookExplorer, &NotebookExplorer::setCurrentNotebook);
    connect(m_notebookExplorer, &NotebookExplorer::notebookActivated,
            &notebookMgr, &NotebookMgr::setCurrentNotebook);
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
        // Avoid geometry corruption caused by fullscreen or minimized window.
        const auto state = windowState();
        if (state & (Qt::WindowMinimized | Qt::WindowFullScreen)) {
            if (m_windowOldState & Qt::WindowMaximized) {
                showMaximized();
            } else {
                showNormal();
            }
        }

        // Do not expand the content area.
        setContentAreaExpanded(false);

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

        m_trayIcon->hide();

        FramelessMainWindowImpl::closeEvent(p_event);
        qApp->exit(exitCode > -1 ? exitCode : 0);
    } else {
        emit minimizedToSystemTray();

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
    sg.m_tagExplorerState = m_tagExplorer->saveState();
    sg.m_notebookExplorerState = m_notebookExplorer->saveState();
    sg.m_locationListState = m_locationList->saveState();

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
            m_visibleDocksBeforeExpand = m_dockWidgetHelper.getVisibleDocks();
        }
    }

    if (!sg.m_tagExplorerState.isEmpty()) {
        m_tagExplorer->restoreState(sg.m_tagExplorerState);
    }

    if (!sg.m_notebookExplorerState.isEmpty()) {
        m_notebookExplorer->restoreState(sg.m_notebookExplorerState);
    }

    if (!sg.m_locationListState.isEmpty()) {
        m_locationList->restoreState(sg.m_locationListState);
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
    if (m_contentAreaExpanded == p_expanded) {
        return;
    }

    m_contentAreaExpanded = p_expanded;
    if (p_expanded) {
        // Store the state and hide.
        m_visibleDocksBeforeExpand = m_dockWidgetHelper.hideDocks();
    } else {
        // Restore the state.
        m_dockWidgetHelper.restoreDocks(m_visibleDocksBeforeExpand);
    }
}

bool MainWindow::isContentAreaExpanded() const
{
    return m_contentAreaExpanded;
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

    // There are OutlineViewers in each ViewWindow. We only need to register navigation mode for the outline panel.
    NavigationModeMgr::getInst().registerNavigationTarget(m_outlineViewer->getNavigationModeWrapper());

    connect(m_viewArea, &ViewArea::currentViewWindowChanged,
            this, [this]() {
                auto win = m_viewArea->getCurrentViewWindow();
                m_outlineViewer->setOutlineProvider(win ? win->getOutlineProvider() : nullptr);
            });
    connect(m_outlineViewer, &OutlineViewer::focusViewArea,
            this, &MainWindow::focusViewArea);
}

void MainWindow::setupConsoleViewer()
{
    m_consoleViewer = new ConsoleViewer(this);
    m_consoleViewer->setObjectName("ConsoleViewer.vnotex");

    connect(&VNoteX::getInst(), &VNoteX::showOutputRequested,
            this, [this](const QString &p_text) {
        m_consoleViewer->append(p_text);
        m_dockWidgetHelper.activateDock(DockWidgetHelper::ConsoleDock);
    });
}

const QVector<QDockWidget *> &MainWindow::getDocks() const
{
    return m_dockWidgetHelper.getDocks();
}

ViewArea *MainWindow::getViewArea() const
{
    return m_viewArea;
}

void MainWindow::focusViewArea()
{
    m_viewArea->focus();
}

NotebookExplorer *MainWindow::getNotebookExplorer() const
{
    Q_ASSERT(m_notebookExplorer);
    return m_notebookExplorer;
}

void MainWindow::setupToolBar()
{
    const int sz = ConfigMgr::getInst().getCoreConfig().getToolBarIconSize();

    if (isFrameless()) {
        auto toolBar = new TitleToolBar(tr("Global"), this);
        toolBar->setIconSize(QSize(sz + 4, sz + 4));
        ToolBarHelper::setupToolBars(this, toolBar);
        toolBar->addTitleBarIcons(ToolBarHelper::generateIcon(QStringLiteral("minimize.svg")),
                                  ToolBarHelper::generateIcon(QStringLiteral("maximize.svg")),
                                  ToolBarHelper::generateIcon(QStringLiteral("maximize_restore.svg")),
                                  ToolBarHelper::generateDangerousIcon(QStringLiteral("close.svg")));
        setTitleBar(toolBar);
        connect(this, &FramelessMainWindowImpl::windowStateChanged,
                toolBar, &TitleToolBar::updateMaximizeAct);
    } else {
        auto toolBar = new QToolBar(tr("Global"), this);
        toolBar->setIconSize(QSize(sz, sz));
        ToolBarHelper::setupToolBars(this, toolBar);
    }

    // Disable the context menu above tool bar.
    setContextMenuPolicy(Qt::NoContextMenu);
}

void MainWindow::closeOnQuit()
{
    // No user interaction is available.
    emit mainWindowClosedOnQuit();

    VNoteX::getInst().getNotebookMgr().close();
}

void MainWindow::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // For cross-platform global shortcuts, the external library QHotkey is used.
    QKeySequence wakeUp(coreConfig.getShortcut(CoreConfig::Global_WakeUp));
    if (!wakeUp.isEmpty()) {
        auto qHotkey = new QHotkey(wakeUp, true, this);
        connect(qHotkey , &QHotkey::activated, this, &MainWindow::showMainWindow);
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

    setWindowFlagsOnUpdate();

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

    FramelessMainWindowImpl::changeEvent(p_event);
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

void MainWindow::updateDockWidgetTabBar()
{
    m_dockWidgetHelper.updateDockWidgetTabBar();
}

void MainWindow::exportNotes()
{
    if (m_exportDialog) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("There is one export dialog running. Please close it first."),
                                 this);
        m_exportDialog->activateWindow();
        m_exportDialog->show();
        return;
    }

    auto currentNotebook = m_notebookExplorer->currentNotebook().data();
    auto viewWindow = m_viewArea->getCurrentViewWindow();
    auto folderNode = m_notebookExplorer->currentExploredFolderNode();
    if (folderNode && (folderNode->isRoot())) {
        folderNode = nullptr;
    }
    auto noteNode = m_notebookExplorer->currentExploredNode();
    if (noteNode && !noteNode->hasContent()) {
        noteNode = nullptr;
    }

    m_exportDialog = new ExportDialog(currentNotebook,
                                      folderNode,
                                      noteNode,
                                      viewWindow ? viewWindow->getBuffer() : nullptr,
                                      nullptr);
    connect(m_exportDialog, &QDialog::finished,
            this, [this]() {
                m_exportDialog->deleteLater();
                m_exportDialog = nullptr;
            });

    // Let it be able to run at background.
    m_exportDialog->show();
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
        int y = (height() - labelH) / 3;
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
        m_dockWidgetHelper.activateDock(DockWidgetHelper::LocationListDock);
    } else {
        m_dockWidgetHelper.getDock(DockWidgetHelper::LocationListDock)->hide();
    }
}

void MainWindow::toggleLocationListVisible()
{
    bool visible = m_dockWidgetHelper.getDock(DockWidgetHelper::LocationListDock)->isVisible();
    setLocationListVisible(!visible);
}

void MainWindow::setupSpellCheck()
{
    const auto &configMgr = ConfigMgr::getInst();
    vte::SpellChecker::addDictionaryCustomSearchPaths(
        QStringList() << configMgr.getUserDictsFolder() << configMgr.getAppDictsFolder());
}

void MainWindow::checkForUpdates()
{
    Updater::checkForUpdates(this, [this](bool p_hasUpdate, const QString &p_version, const QString &p_errMsg) {
                if (p_version.isEmpty()) {
                    statusBar()->showMessage(tr("Failed to check for updates (%1)").arg(p_errMsg), 3000);
                } else if (p_hasUpdate) {
                    statusBar()->showMessage(tr("Updates available: %1").arg(p_version));
                }
            });
}

void MainWindow::checkNotebooksFailedToLoad()
{
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    const auto &notebooks = notebookMgr.getNotebooksFailedToLoad();
    if (notebooks.isEmpty()) {
        return;
    }

    int ret = MessageBoxHelper::questionYesNo(MessageBoxHelper::Warning,
                                              tr("Failed to load %n notebook(s).", "", notebooks.size()),
                                              tr("These notebooks may be moved or deleted. It is recommended to remove "
                                                 "them from configuration and open them with the correct root folder path later.\n"
                                                 "Remove them from the configuration?"),
                                              notebooks.join(QLatin1Char('\n')),
                                              this);
    if (ret == QMessageBox::Yes) {
        notebookMgr.clearNotebooksFailedToLoad();
    }
}

void MainWindow::setupTagExplorer()
{
    m_tagExplorer = new TagExplorer(this);
    connect(&VNoteX::getInst().getNotebookMgr(), &NotebookMgr::currentNotebookChanged,
            m_tagExplorer, &TagExplorer::setNotebook);
}

void MainWindow::loadWidgetsData()
{
    m_historyPanel->initialize();

    m_snippetPanel->initialize();
}
