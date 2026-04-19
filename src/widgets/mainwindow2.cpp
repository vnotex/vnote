#include "mainwindow2.h"

#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include "constants.h"
#include "systemtrayhelper.h"
#include "titletoolbar.h"
#include "toolbarhelper2.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/exportcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/sessionconfig.h>
#include <gui/services/navigationmodeservice.h>
#include <gui/services/themeservice.h>

#include <controllers/searchcontroller.h>
#include <controllers/viewareacontroller.h>
#include <qwebengineview.h>
#include <unitedentry/unitedentrymgr.h>
#include <views/inodeexplorer.h>
#include <widgets/dialogs/exportdialog2.h>
#include <widgets/locationlist2.h>
#include <widgets/messageboxhelper.h>
#include <widgets/notebookexplorer2.h>
#include <widgets/notebookselector2.h>
#include <widgets/outlineviewer.h>
#include <widgets/searchpanel2.h>
#include <widgets/snippetpanel2.h>
#include <widgets/tagexplorer2.h>
#include <widgets/viewarea2.h>
#include <widgets/viewwindow2.h>

using namespace vnotex;

MainWindow2::MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent)
    : FramelessMainWindowImpl(
          !p_serviceLocator.get<ConfigMgr2>()->getSessionConfig().getSystemTitleBarEnabled(),
          p_parent),
      m_serviceLocator(p_serviceLocator) {
  setupUI();

  // Restore window geometry/state early so the window appears at the right
  // size and position.  View-area layout is deferred to kickOffPostInit()
  // to avoid creating splits before the event loop is running.
  restoreWindowGeometry();
}

MainWindow2::~MainWindow2() {}

ServiceLocator &MainWindow2::getServiceLocator() { return m_serviceLocator; }

NotebookExplorer2 *MainWindow2::getNotebookExplorer() const { return m_notebookExplorer; }

ViewArea2 *MainWindow2::getViewArea() const { return m_viewArea; }

void MainWindow2::setupUI() {
  // Window title
  setWindowTitle(tr("VNote"));

  // Minimum size: 800x600
  setMinimumSize(800, 600);

  // Setup ViewArea2 as central widget.
  setupViewArea();

  // Setup dock widgets (before toolbar, so the Windows submenu can enumerate them).
  setupDocks();

  // Setup tool bar.
  setupToolBar();

  // Wire notebook changes to UnitedEntryMgr (after setupToolBar creates m_toolBarHelper).
  connect(m_notebookExplorer, &NotebookExplorer2::currentNotebookChanged,
          m_toolBarHelper->unitedEntryMgr(), &UnitedEntryMgr::setCurrentNotebookId);

  // Wire folder context to UnitedEntryMgr.
  connect(m_notebookExplorer, &NotebookExplorer2::currentExploredFolderChanged,
          m_toolBarHelper->unitedEntryMgr(), &UnitedEntryMgr::setCurrentFolderId);

  setupNavigationMode();

  setupSystemTray();

#if defined(Q_OS_WIN)
  m_dummyWebView = new QWebEngineView(this);
  m_dummyWebView->setAttribute(Qt::WA_DontShowOnScreen);
#endif
}

void MainWindow2::setupNavigationMode() {
  auto *navService = m_serviceLocator.get<NavigationModeService>();
  if (!navService) {
    return;
  }

  // Register in exact order matching legacy key assignment:
  // 1. ViewArea2 (was first in legacy → gets key 'a')
  navService->registerNavigationTarget(m_viewArea);

  // 2. DockWidgetHelper
  navService->registerNavigationTarget(&m_dockWidgetHelper);

  // 3. NotebookSelector2
  navService->registerNavigationTarget(m_notebookExplorer->getNotebookSelector());

  // 4. CombinedNodeExplorer wrapper (if available)
  if (auto *wrapper = m_notebookExplorer->getNodeExplorer()->getNavigationModeWrapper()) {
    navService->registerNavigationTarget(wrapper);
  }

  // 5. TagExplorer2 tag wrapper
  if (auto *tagWrapper = m_tagExplorer->getTagNavigationWrapper()) {
    navService->registerNavigationTarget(tagWrapper);
  }

  // 6. TagExplorer2 file wrapper
  if (auto *fileWrapper = m_tagExplorer->getFileNavigationWrapper()) {
    navService->registerNavigationTarget(fileWrapper);
  }

  // 7. OutlineViewer wrapper
  if (auto *outlineWrapper = m_outlineViewer->getOutlineNavigationWrapper()) {
    navService->registerNavigationTarget(outlineWrapper);
  }
}

void MainWindow2::kickOffPostInit(const QStringList &p_pathsToOpen) {
  // Restore notebook explorer state from session config.
  auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();

  QTimer::singleShot(300, [this, p_pathsToOpen]() {
#if defined(Q_OS_WIN)
    if (m_dummyWebView) {
      delete m_dummyWebView;
      m_dummyWebView = nullptr;
    }
#endif

    loadStateAndGeometry();

    emit layoutChanged();

    m_notebookExplorer->loadNotebooks();
    m_notebookExplorer->applyRestoredSessionState();

    // Initialize snippet panel (load snippets from config).
    m_snippetPanel->initialize();

    // Fire after-start hook so components can restore session state.
    auto *hookMgr = m_serviceLocator.get<HookManager>();
    if (hookMgr) {
      QVariantMap args;
      hookMgr->doAction(HookNames::MainWindowAfterStart, args);
    }
  });
}

void MainWindow2::restoreWindowGeometry() {
  const auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();
  const auto sg = sessionConfig.getMainWindowStateGeometry();

  if (!sg.m_mainGeometry.isEmpty()) {
    restoreGeometry(sg.m_mainGeometry);
  }

  if (!sg.m_mainState.isEmpty()) {
    // Will also restore the state of dock widgets.
    restoreState(sg.m_mainState);
  }
}

void MainWindow2::loadStateAndGeometry() {
  const auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();

  QByteArray explorerState = sessionConfig.getNotebookExplorerSession();
  if (!explorerState.isEmpty()) {
    m_notebookExplorer->restoreState(explorerState);
  }

  if (m_tagExplorer) {
    QByteArray tagExplorerState = sessionConfig.getTagExplorerSession();
    if (!tagExplorerState.isEmpty()) {
      m_tagExplorer->restoreState(tagExplorerState);
    }
  }

  // Load view area layout (new architecture).
  if (m_viewArea) {
    QJsonObject layout = sessionConfig.getViewAreaLayout();
    qInfo() << "MainWindow2::loadStateAndGeometry: loading view area layout"
            << QJsonDocument(layout).toJson(QJsonDocument::Compact);
    m_viewArea->loadLayoutFromSession(layout);
  }
}

void MainWindow2::closeEvent(QCloseEvent *p_event) {
  auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();
  const int toTray = sessionConfig.getMinimizeToSystemTray();
  bool isExit = m_requestQuit > -1 || toTray == 0;
  const int exitCode = m_requestQuit;
  m_requestQuit = -1;

#if defined(Q_OS_MACOS)
  // Do not support minimized to tray on macOS.
  isExit = true;
#endif

  bool needShowMessage = false;
  if (!isExit && toTray == -1) {
    int ret = MessageBoxHelper::questionYesNo(MessageBoxHelper::Question,
                                              tr("Do you want to minimize %1 to system tray "
                                                 "instead of quitting when closed?")
                                                  .arg(qApp->applicationName()),
                                              tr("You could change the option in Settings later."),
                                              QString(), this);
    if (ret == QMessageBox::Yes) {
      sessionConfig.setMinimizeToSystemTray(true);
      needShowMessage = true;
    } else if (ret == QMessageBox::No) {
      sessionConfig.setMinimizeToSystemTray(false);
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
    // Fire before-close hook. Subscribers (ViewArea2, ConfigService) handle:
    // - Tab order sync and buffer close with save prompts (ViewArea2, priority 10)
    // - Session snapshot to disk (ConfigService, priority 100)
    // Hook is cancelled if user cancels save prompts.
    auto *hookMgr = m_serviceLocator.get<HookManager>();
    if (hookMgr) {
      if (hookMgr->doAction(HookNames::MainWindowBeforeClose)) {
        // User cancelled — undo shutdown preparation.
        hookMgr->doAction(HookNames::MainWindowShutdownCancelled);
        p_event->ignore();
        return;
      }
    }

    m_trayIcon->hide();

    FramelessMainWindowImpl::closeEvent(p_event);
    qApp->exit(exitCode > -1 ? exitCode : 0);
  } else {
    emit minimizedToSystemTray();

    hide();
    p_event->ignore();
    if (needShowMessage) {
      m_trayIcon->showMessage(ConfigMgr2::c_appName,
                              tr("%1 is still running here.").arg(ConfigMgr2::c_appName));
    }
  }
}

void MainWindow2::saveStateAndGeometry() {
  if (m_layoutReset) {
    return;
  }

  SessionConfig::MainWindowStateGeometry sg;
  sg.m_mainState = saveState();
  sg.m_mainGeometry = saveGeometry();

  auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();
  sessionConfig.setMainWindowStateGeometry(sg);

  sessionConfig.setNotebookExplorerSession(m_notebookExplorer->saveState());

  if (m_tagExplorer) {
    sessionConfig.setTagExplorerSession(m_tagExplorer->saveState());
  }

  // Save view area layout (new architecture).
  if (m_viewArea) {
    QJsonObject layout = m_viewArea->saveLayout();
    qInfo() << "MainWindow2::saveStateAndGeometry: saving view area layout"
            << QJsonDocument(layout).toJson(QJsonDocument::Compact);
    sessionConfig.setViewAreaLayout(layout);
  }
}

void MainWindow2::setupNotebookExplorer() {
  m_notebookExplorer = new NotebookExplorer2(m_serviceLocator, this);
  m_notebookExplorer->setObjectName("NotebookExplorer2.vnotex");

  // Connect MainWindow2 signals to NotebookExplorer2 slots
  connect(this, &MainWindow2::newNoteRequested, m_notebookExplorer, &NotebookExplorer2::newNote);
  connect(this, &MainWindow2::newQuickNoteRequested, m_notebookExplorer,
          &NotebookExplorer2::newQuickNote);
  connect(this, &MainWindow2::newFolderRequested, m_notebookExplorer,
          &NotebookExplorer2::newFolder);
  connect(this, &MainWindow2::importFileRequested, m_notebookExplorer,
          &NotebookExplorer2::importFile);
  connect(this, &MainWindow2::importFolderRequested, m_notebookExplorer,
          &NotebookExplorer2::importFolder);
  connect(this, &MainWindow2::exportRequested, this, &MainWindow2::exportNotes);
}

void MainWindow2::exportNotes() {
  // Single-instance enforcement.
  if (m_exportDialog) {
    m_exportDialog->activateWindow();
    m_exportDialog->raise();
    return;
  }

  // Build ExportContext from current state.
  ExportContext context;

  // Get current ViewWindow2 (may be null if no file is open).
  auto *viewWin = m_viewArea->getCurrentViewWindow();
  if (viewWin) {
    context.bufferContent = viewWin->getLatestContent();
    context.bufferName = viewWin->getName();
    const auto &buffer = viewWin->getBuffer();
    context.currentNodeId = buffer.nodeId();
  }

  // Get notebook/folder context from NotebookExplorer2.
  context.notebookId = m_notebookExplorer->currentNotebookId();
  context.currentFolderId = m_notebookExplorer->currentExploredFolderId();

  // If no explicit current node from ViewWindow2, use explorer's selected node.
  if (!context.currentNodeId.isValid()) {
    context.currentNodeId = m_notebookExplorer->currentExploredNodeId();
  }

  // Default source for toolbar is CurrentBuffer (if available).
  context.presetSource = viewWin ? ExportSource::CurrentBuffer : ExportSource::CurrentNote;

  // Create non-modal ExportDialog2.
  m_exportDialog = new ExportDialog2(m_serviceLocator, context, this);

  // Cleanup on close.
  connect(m_exportDialog, &QDialog::finished, this, [this]() {
    m_exportDialog->deleteLater();
    m_exportDialog = nullptr;
  });

  m_exportDialog->show();
}

void MainWindow2::setupViewArea() {
  m_viewArea = new ViewArea2(m_serviceLocator, this);
  setCentralWidget(m_viewArea);
}

void MainWindow2::setupOutlineViewer() {
  m_outlineViewer = new OutlineViewer(m_serviceLocator, QString(), this);
}

void MainWindow2::setupTagExplorer() {
  m_tagExplorer = new TagExplorer2(m_serviceLocator, this);
  m_tagExplorer->setObjectName("TagExplorer2.vnotex");
}

void MainWindow2::setupSnippetExplorer() {
  m_snippetPanel = new SnippetPanel2(m_serviceLocator, this);
  m_snippetPanel->setObjectName("SnippetPanel2.vnotex");
}

void MainWindow2::setupSearchPanel() { m_searchPanel = new SearchPanel2(m_serviceLocator, this); }

void MainWindow2::setupLocationList() {
  m_locationList = new LocationList2(m_serviceLocator, this);
}

void MainWindow2::setupDocks() {
  setupNotebookExplorer();

  setupOutlineViewer();

  setupTagExplorer();

  setupSnippetExplorer();

  setupSearchPanel();

  setupLocationList();

  m_dockWidgetHelper.setupDocks();
  m_dockWidgetHelper.postSetup();

  // Refresh dock tab icons when theme changes.
  auto *themeService = m_serviceLocator.get<ThemeService>();
  connect(themeService, &ThemeService::themeChanged, &m_dockWidgetHelper,
          &DockWidgetHelper::refreshIcons);

  // Wire SearchController to LocationList2's model.
  m_searchPanel->getController()->setModel(m_locationList->getModel());

  // Wire SearchController node activation to buffer opening.
  connect(m_searchPanel->getController(), &SearchController::nodeActivated, this,
          [this](const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings) {
            auto *bufferSvc = m_serviceLocator.get<BufferService>();
            if (bufferSvc) {
              bufferSvc->openBuffer(p_nodeId, p_settings);
            }
          });

  // Wire notebook changes to SearchPanel2.
  connect(m_notebookExplorer, &NotebookExplorer2::currentNotebookChanged, m_searchPanel,
          &SearchPanel2::setCurrentNotebookId);

  // Wire folder context to SearchPanel2.
  connect(m_notebookExplorer, &NotebookExplorer2::currentExploredFolderChanged, m_searchPanel,
          &SearchPanel2::setCurrentFolderId);

  // Wire context menu export to ExportDialog2.
  connect(m_notebookExplorer, &NotebookExplorer2::exportNodeRequested, this,
          [this](const NodeIdentifier &p_nodeId) {
            if (m_exportDialog) {
              m_exportDialog->activateWindow();
              m_exportDialog->raise();
              return;
            }

            bool isFolder = p_nodeId.relativePath.isEmpty() ||
                            QFileInfo(p_nodeId.relativePath).suffix().isEmpty();

            ExportContext context;
            context.currentNodeId = p_nodeId;
            context.notebookId = p_nodeId.notebookId;

            if (isFolder) {
              context.currentFolderId = p_nodeId;
              context.presetSource = ExportSource::CurrentFolder;
            } else {
              const auto parentPath = QFileInfo(p_nodeId.relativePath).path();
              context.currentFolderId = NodeIdentifier{
                  p_nodeId.notebookId,
                  parentPath == QStringLiteral(".") ? QString() : parentPath,
              };
              context.presetSource = ExportSource::CurrentNote;
            }

            auto *viewWin = m_viewArea->getCurrentViewWindow();
            if (viewWin) {
              const auto &buffer = viewWin->getBuffer();
              if (buffer.nodeId() == p_nodeId) {
                context.bufferContent = viewWin->getLatestContent();
                context.bufferName = viewWin->getName();
              }
            }

            m_exportDialog = new ExportDialog2(m_serviceLocator, context, this);
            connect(m_exportDialog, &QDialog::finished, this, [this]() {
              m_exportDialog->deleteLater();
              m_exportDialog = nullptr;
            });
            m_exportDialog->show();
          });
  // Wire LocationList2 result activation to SearchController.
  connect(m_locationList, &LocationList2::resultActivated, m_searchPanel->getController(),
          &SearchController::activateResult);

  // Auto-show the Location List dock when a search starts.
  connect(m_searchPanel->getController(), &SearchController::searchStarted, this,
          [this]() { m_dockWidgetHelper.activateDock(DockWidgetHelper::LocationListDock); });

  // Wire ViewAreaController's locateNodeRequested to NotebookExplorer2.
  // Activate the navigation dock first so the notebook explorer is visible.
  connect(m_viewArea->getController(), &ViewAreaController::locateNodeRequested, this,
          [this](const NodeIdentifier &p_nodeId) {
            m_dockWidgetHelper.activateDock(DockWidgetHelper::NavigationDock);
            m_notebookExplorer->locateNode(p_nodeId);
          });

  // Wire current-window changes to the outline viewer.
  connect(m_viewArea->getController(), &ViewAreaController::currentViewWindowChanged, this,
          [this]() {
            auto *win = m_viewArea->getCurrentViewWindow();
            m_outlineViewer->setOutlineProvider(win ? win->getOutlineProvider() : nullptr);
          });

  // Wire notebook changes to TagExplorer2.
  connect(m_notebookExplorer, &NotebookExplorer2::currentNotebookChanged, m_tagExplorer,
          &TagExplorer2::setNotebookId);

  // Wire TagExplorer2 node activation to buffer opening.
  connect(m_tagExplorer, &TagExplorer2::openNodeRequested, this,
          [this](const NodeIdentifier &p_nodeId) {
            auto *bufferSvc = m_serviceLocator.get<BufferService>();
            if (bufferSvc) {
              bufferSvc->openBuffer(p_nodeId);
            }
          });

  // Wire snippet apply from SnippetPanel2 to active ViewWindow2.
  connect(m_snippetPanel, &SnippetPanel2::applySnippetRequested, this,
          [this](const QString &p_name) {
            auto *viewWin = m_viewArea->getCurrentViewWindow();
            if (viewWin) {
              viewWin->applySnippet(p_name);
            } else {
              qWarning() << "No active view window for snippet apply";
            }
          });
}

QWidget *MainWindow2::getDockWidget(DockWidgetHelper::DockType p_dockType) const {
  switch (p_dockType) {
  case DockWidgetHelper::DockType::NavigationDock:
    return m_notebookExplorer;
  case DockWidgetHelper::DockType::OutlineDock:
    return m_outlineViewer;
  case DockWidgetHelper::DockType::TagDock:
    return m_tagExplorer;
  case DockWidgetHelper::DockType::SnippetDock:
    return m_snippetPanel;
  case DockWidgetHelper::DockType::SearchDock:
    return m_searchPanel;
  case DockWidgetHelper::DockType::LocationListDock:
    return m_locationList;
  default:
    return nullptr;
  }
}

void MainWindow2::setupToolBar() {
  const auto &coreConfig = m_serviceLocator.get<ConfigMgr2>()->getCoreConfig();
  const int sz = coreConfig.getToolBarIconSize();

  m_toolBarHelper = new ToolBarHelper2(m_serviceLocator, this);

  auto *themeService = m_serviceLocator.get<ThemeService>();
  connect(themeService, &ThemeService::themeChanged, m_toolBarHelper,
          [this]() { m_toolBarHelper->refreshIcons(); });

  if (isFrameless()) {
    auto toolBar = new TitleToolBar(tr("Global"), this);
    toolBar->setIconSize(QSize(sz + 4, sz + 4));
    m_toolBarHelper->setupToolBars(toolBar);
    toolBar->addTitleBarIcons(m_toolBarHelper->generateIcon(QStringLiteral("minimize.svg")),
                              m_toolBarHelper->generateIcon(QStringLiteral("maximize.svg")),
                              m_toolBarHelper->generateIcon(QStringLiteral("maximize_restore.svg")),
                              m_toolBarHelper->generateDangerousIcon(QStringLiteral("close.svg")));
    setTitleBar(toolBar);
    connect(this, &FramelessMainWindowImpl::windowStateChanged, toolBar,
            &TitleToolBar::updateMaximizeAct);
    connect(themeService, &ThemeService::themeChanged, toolBar, [this, toolBar]() {
      toolBar->refreshIcons(
          m_toolBarHelper->generateIcon(QStringLiteral("minimize.svg")),
          m_toolBarHelper->generateIcon(QStringLiteral("maximize.svg")),
          m_toolBarHelper->generateIcon(QStringLiteral("maximize_restore.svg")),
          m_toolBarHelper->generateDangerousIcon(QStringLiteral("close.svg")));
    });
  } else {
    auto toolBar = new QToolBar(tr("Global"), this);
    toolBar->setIconSize(QSize(sz, sz));
    m_toolBarHelper->setupToolBars(toolBar);
  }

  // Disable the context menu above tool bar.
  setContextMenuPolicy(Qt::NoContextMenu);
}

bool MainWindow2::isContentAreaExpanded() const { return m_contentAreaExpanded; }

void MainWindow2::setContentAreaExpanded(bool p_expanded) {
  if (m_contentAreaExpanded == p_expanded) {
    return;
  }

  m_contentAreaExpanded = p_expanded;

  if (m_contentAreaExpanded) {
    // Save visible docks and hide non-floating/non-keep docks.
    m_visibleDocksBeforeExpand = m_dockWidgetHelper.hideDocks();
  } else {
    // Restore dock widgets to their previous visibility.
    m_dockWidgetHelper.restoreDocks(m_visibleDocksBeforeExpand);
    m_visibleDocksBeforeExpand.clear();
  }

  emit layoutChanged();
}

void MainWindow2::setStayOnTop(bool p_enabled) {
  bool visible = isVisible();
  Qt::WindowFlags flags = windowFlags();

  if (p_enabled) {
    setWindowFlags(flags | Qt::WindowStaysOnTopHint);
  } else {
    setWindowFlags(flags & ~Qt::WindowStaysOnTopHint);
  }

  if (visible) {
    show();
  }
}

const QVector<QDockWidget *> &MainWindow2::getDocks() const {
  return m_dockWidgetHelper.getDocks();
}

void MainWindow2::resetStateAndGeometry() {
  if (m_layoutReset) {
    return;
  }

  m_layoutReset = true;

  // Clear saved state.
  auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();
  SessionConfig::MainWindowStateGeometry sg;
  sessionConfig.setMainWindowStateGeometry(sg);

  // Reset to default size.
  resize(1200, 800);

  emit layoutChanged();
}

void MainWindow2::restart() {
  m_requestQuit = kExitToRestart;
  close();
}

void MainWindow2::changeEvent(QEvent *p_event) {
  if (p_event->type() == QEvent::WindowStateChange) {
    QWindowStateChangeEvent *eve = static_cast<QWindowStateChangeEvent *>(p_event);
    m_windowOldState = eve->oldState();
  }

  FramelessMainWindowImpl::changeEvent(p_event);
}

void MainWindow2::showMainWindow() {
  // Fire hook before showing main window (WordPress-style plugin architecture)
  auto *hookMgr = m_serviceLocator.get<HookManager>();
  if (hookMgr) {
    QVariantMap args;
    if (hookMgr->doAction(HookNames::MainWindowBeforeShow, args)) {
      return; // Cancelled by plugin
    }
  }

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

  // Fire hook after showing main window
  if (hookMgr) {
    QVariantMap args;
    hookMgr->doAction(HookNames::MainWindowAfterShow, args);
  }
}

void MainWindow2::setupSystemTray() {
  m_trayIcon = SystemTrayHelper::setupSystemTray(this, m_serviceLocator.get<ConfigMgr2>());
  m_trayIcon->show();
}

void MainWindow2::quitApp() {
  m_requestQuit = 0;
  close();
}
