#include "mainwindow2.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QWindowStateChangeEvent>

#include <QHotkey>

#include <QWKWidgets/widgetwindowagent.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "constants.h"
#include "systemtrayhelper.h"
#include "titletoolbar2.h"
#include "toolbarhelper2.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/exportcontext.h>
#include <core/fileopensettings.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/syncservice.h>
#include <core/sessionconfig.h>
#include <gui/services/navigationmodeservice.h>
#include <gui/services/themeservice.h>

#include <controllers/firstruncontroller.h>
#include <controllers/searchcontroller.h>
#include <controllers/syncconflictcontroller.h>
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
#include <widgets/taskpanel2.h>
#include <widgets/consoleviewer.h>
#include <widgets/viewarea2.h>
#include <widgets/viewwindow2.h>

#include <core/services/mainwindowtaskcontext.h>
#include <core/services/taskservice.h>

using namespace vnotex;

MainWindow2::MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent)
    : QMainWindow(p_parent), m_serviceLocator(p_serviceLocator) {
  m_frameless = !p_serviceLocator.get<ConfigMgr2>()->getSessionConfig().getSystemTitleBarEnabled();
  if (m_frameless) {
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setupWindowAgent();
  }
  setupUI();

  setAcceptDrops(true);

  // Restore window geometry/state early so the window appears at the right
  // size and position.  View-area layout is deferred to kickOffPostInit()
  // to avoid creating splits before the event loop is running.
  restoreWindowGeometry();
}

MainWindow2::~MainWindow2() {
  // Detach the injected task context before it is destroyed, since TaskService
  // (owned in main.cpp) outlives this window.
  if (m_taskContext) {
    auto *taskService = m_serviceLocator.get<TaskService>();
    if (taskService) {
      taskService->setTaskContext(nullptr);
    }
    delete m_taskContext;
    m_taskContext = nullptr;
  }
}

void MainWindow2::setupWindowAgent() {
  m_windowAgent = new QWK::WidgetWindowAgent(this);
  m_windowAgent->setup(this);
}

bool MainWindow2::isFrameless() const { return m_frameless; }

void MainWindow2::updateWindowTitle() {
  // Only drive the title when the OS draws the title bar; the custom TitleToolBar2
  // does not render the window title.
  if (isFrameless()) {
    return;
  }

  QString title = ConfigMgr2::c_appName; // "VNote"
  auto *win = m_viewArea ? m_viewArea->getCurrentViewWindow() : nullptr;
  if (win) {
    const QString name = win->getName();
    if (!name.isEmpty()) {
      title = QStringLiteral("%1 - %2").arg(name, ConfigMgr2::c_appName);
    }
  }
  setWindowTitle(title);
}

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

  // Connect theme switch orchestration (BEFORE setupDocks/setupToolBar so this
  // handler fires before DockWidgetHelper/ToolBarHelper2 handlers).
  auto *themeService = m_serviceLocator.get<ThemeService>();
  if (themeService) {
    connect(themeService, &ThemeService::themeAboutToChange, this, [this]() {
      if (!m_progressDialog) {
        m_progressDialog = new QProgressDialog(this);
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setCancelButton(nullptr);
        m_progressDialog->setMinimumDuration(0);
      }
      m_progressDialog->setRange(0, 3);
      m_progressDialog->setValue(0);
      m_progressDialog->setLabelText(tr("Loading theme..."));
    });
    connect(themeService, &ThemeService::themeChanged, this, &MainWindow2::onThemeChanged);
  }

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

  // Inject the view window navigator (ViewAreaController) into UnitedEntryMgr so
  // the "windows" united entry can enumerate and focus open view windows.
  if (m_toolBarHelper->unitedEntryMgr() && m_viewArea && m_viewArea->getController()) {
    m_toolBarHelper->unitedEntryMgr()->setViewWindowNavigator(m_viewArea->getController());
  }

  // Read-only affordance: disable the File-toolbar mutation actions when the
  // current notebook is read-only (Export stays enabled). Loose coupling — the
  // explorer only emits; MainWindow2 drives the toolbar helper.
  connect(m_notebookExplorer, &NotebookExplorer2::readOnlyStateChanged, this,
          [this](bool p_readOnly) { m_toolBarHelper->setMutationActionsEnabled(!p_readOnly); });

  setupNavigationMode();

  setupSystemTray();

  // T16: wire SyncService::conflictsDetected → SyncConflictController. The
  // controller is long-lived (owned by MainWindow2) so successive conflict
  // batches reuse the same orchestrator.
  //
  // Retry policy: m_syncRetryCount tracks how many times conflictsDetected has
  // fired for a given notebookId without an intervening clean syncFinished.
  // After 3 attempts we abort the cycle and surface a QMessageBox::warning so
  // the user is not trapped in an infinite resolve/re-conflict loop.
  if (auto *syncSvc = m_serviceLocator.get<SyncService>()) {
    m_syncConflictController = new SyncConflictController(m_serviceLocator, this);

    connect(syncSvc, &SyncService::conflictsDetected, this,
            [this](const QString &p_notebookId, const QStringList &p_files) {
              int &count = m_syncRetryCount[p_notebookId];
              ++count;
              if (count > 3) {
                // Reset so the next user-initiated Sync starts clean.
                m_syncRetryCount.remove(p_notebookId);
                QMessageBox::warning(this, tr("Sync conflict"),
                                     tr("Sync conflict could not be resolved after 3 attempts. "
                                        "Please resolve manually or contact support."));
                return;
              }
              if (m_syncConflictController) {
                m_syncConflictController->presentConflicts(p_notebookId, p_files, this);
              }
            });

    // A clean sync (no conflict) for this notebook resets the retry counter.
    connect(syncSvc, &SyncService::syncFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              if (p_result == VXCORE_OK) {
                m_syncRetryCount.remove(p_notebookId);
              }
            });

    // User-cancelled the conflict dialog: reset counter (sync stays blocked
    // until the user manually retries via the title-bar Sync button).
    if (m_syncConflictController) {
      connect(m_syncConflictController, &SyncConflictController::conflictsAbandoned, this,
              [this](const QString &p_notebookId) { m_syncRetryCount.remove(p_notebookId); });
    }
  }

  // First-run experience: construct BEFORE kickOffPostInit() so the controller's
  // MainWindowAfterStart subscription (priority 5) is registered before the hook
  // fires. Surface the created notebook via a queued connection so the explorer
  // refresh runs after the synchronous hook-dispatch chain completes.
  m_firstRunController = new FirstRunController(m_serviceLocator, this);
  connect(
      m_firstRunController, &FirstRunController::defaultNotebookCreated, this,
      [this](const QString &p_id) {
        if (m_notebookExplorer && !p_id.isEmpty()) {
          m_notebookExplorer->loadNotebooks();
          m_notebookExplorer->setCurrentNotebook(p_id);
        }
      },
      Qt::QueuedConnection);

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
    validateDockProportions();

    emit layoutChanged();

    m_notebookExplorer->loadNotebooks();
    m_notebookExplorer->applyRestoredSessionState();

    // Initialize snippet panel (load snippets from config).
    m_snippetPanel->initialize();

    // Fire after-start hook so components can restore session state. The
    // view area's MainWindowAfterStart handler runs session restore and, once
    // core propagation is re-enabled, emits ViewArea2::corePropagationReady,
    // which drains any files queued below (see setupViewArea()).
    auto *hookMgr = m_serviceLocator.get<HookManager>();
    if (hookMgr) {
      QVariantMap args;
      hookMgr->doAction(HookNames::MainWindowAfterStart, args);
    }
  });

  // Queue the initial command-line paths so they are opened by the
  // corePropagationReady handler once the view area is ready. Queuing here
  // (before the deferred lambda above) guarantees the paths are present when
  // the ready signal fires.
  m_pendingOpenPaths.append(p_pathsToOpen);
}

void MainWindow2::openFiles(const QStringList &p_paths) {
  if (p_paths.isEmpty()) {
    return;
  }

  // If startup is still in progress, queue the paths and let the post-init
  // drain open them once the view area is ready.
  if (!m_postInitComplete) {
    m_pendingOpenPaths.append(p_paths);
    return;
  }

  doOpenFiles(p_paths);
}

void MainWindow2::doOpenFiles(const QStringList &p_paths) {
  if (p_paths.isEmpty()) {
    return;
  }

  auto *bufferSvc = m_serviceLocator.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "MainWindow2::doOpenFiles: BufferService unavailable";
    return;
  }

  for (const auto &path : p_paths) {
    if (path.isEmpty()) {
      continue;
    }

    // Resolve to an absolute path relative to the current working directory so
    // relative command-line arguments (e.g. "./note.md") open correctly.
    const QFileInfo finfo(path);
    const QString absolutePath = finfo.absoluteFilePath();

    if (!finfo.exists()) {
      qWarning() << "MainWindow2::doOpenFiles: path does not exist:" << absolutePath;
      continue;
    }

    if (finfo.isDir()) {
      // Folders are not opened as buffers; skip for now.
      qInfo() << "MainWindow2::doOpenFiles: skipping directory:" << absolutePath;
      continue;
    }

    // Open as an external file: empty notebookId, absolute path. The
    // FileAfterOpen hook drives ViewAreaController to display the buffer.
    NodeIdentifier nodeId;
    nodeId.relativePath = absolutePath;

    FileOpenSettings settings;
    bufferSvc->openBuffer(nodeId, settings);
  }
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
    qCDebug(lcWorkspace) << "MainWindow2::loadStateAndGeometry: loading view area layout"
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

    QMainWindow::closeEvent(p_event);
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

  // Once the view area has finished session restore and re-enabled core
  // propagation, mark post-init complete and open any files that were queued
  // during startup (command-line paths or requests forwarded from a second
  // instance). This is an explicit handoff rather than relying on timer
  // ordering, so buffers are always registered in their vxcore workspace.
  connect(m_viewArea, &ViewArea2::corePropagationReady, this, [this]() {
    m_postInitComplete = true;
    const auto pending = m_pendingOpenPaths;
    m_pendingOpenPaths.clear();
    doOpenFiles(pending);
  });
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

void MainWindow2::setupTaskPanel() {
  m_taskPanel = new TaskPanel2(m_serviceLocator, this);
  m_taskPanel->setObjectName("TaskPanel2.vnotex");
}

void MainWindow2::setupConsoleViewer() {
  m_consoleViewer = new ConsoleViewer(m_serviceLocator, this);
  m_consoleViewer->setObjectName("ConsoleViewer.vnotex");
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

  setupTaskPanel();

  setupConsoleViewer();

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

            // Keep the OS window title in sync with the current note, and follow renames
            // of the currently-active window. Drop the previous window's connection first.
            disconnect(m_currentWindowNameConn);
            if (win) {
              m_currentWindowNameConn =
                  connect(win, &ViewWindow2::nameChanged, this,
                          [this]() { updateWindowTitle(); });
            }
            updateWindowTitle();
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

  // Tasks + Console wiring.
  auto *taskService = m_serviceLocator.get<TaskService>();
  if (taskService) {
    // Inject the production task context now that the notebook explorer and
    // view area exist. TaskVariableMgr resolves context lazily, so this takes
    // effect immediately for subsequent task runs without re-init.
    MainWindowTaskContext::Providers providers;
    providers.currentNotebookId = [this]() -> QString {
      return m_notebookExplorer ? m_notebookExplorer->currentNotebookId() : QString();
    };
    providers.currentBufferPath = [this]() -> QString {
      auto *win = m_viewArea ? m_viewArea->getCurrentViewWindow() : nullptr;
      return win ? win->getBuffer().resolvedPath() : QString();
    };
    providers.currentBufferNotebookId = [this]() -> QString {
      auto *win = m_viewArea ? m_viewArea->getCurrentViewWindow() : nullptr;
      return win ? win->getBuffer().nodeId().notebookId : QString();
    };
    providers.currentBufferRelativePath = [this]() -> QString {
      auto *win = m_viewArea ? m_viewArea->getCurrentViewWindow() : nullptr;
      return win ? win->getBuffer().nodeId().relativePath : QString();
    };
    providers.selectedText = [this]() -> QString {
      auto *win = m_viewArea ? m_viewArea->getCurrentViewWindow() : nullptr;
      return win ? win->getSelectedText() : QString();
    };
    m_taskContext = new MainWindowTaskContext(std::move(providers), this);
    taskService->setTaskContext(m_taskContext);

    // Stream task output to the Console viewer, auto-showing the Console dock
    // whenever output arrives (i.e. when a task runs).
    connect(taskService, &TaskService::taskOutputRequested, this,
            [this](const QString &p_text) {
              // Auto-show the Console dock only when it is hidden, so streaming
              // output does not repeatedly steal focus.
              auto *consoleDock = m_dockWidgetHelper.getDock(DockWidgetHelper::ConsoleDock);
              if (consoleDock && !consoleDock->isVisible()) {
                m_dockWidgetHelper.activateDock(DockWidgetHelper::ConsoleDock);
              }
              if (m_consoleViewer) {
                m_consoleViewer->appendOutput(p_text);
              }
            });

    // Reload notebook-scoped tasks when the current notebook changes.
    connect(m_notebookExplorer, &NotebookExplorer2::currentNotebookChanged, taskService,
            [taskService](const QString &) { taskService->reloadNotebookTasks(); });
  }

  // Initialize the tasks panel now that context + wiring are in place.
  if (m_taskPanel) {
    // Route task-file editing through the generic external-file open flow so
    // the .json opens in the built-in editor rather than the OS handler.
    connect(m_taskPanel, &TaskPanel2::editTaskFileRequested, this,
            [this](const QString &p_filePath) { openFiles({p_filePath}); });

    m_taskPanel->initialize();
  }
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
  case DockWidgetHelper::DockType::TaskDock:
    return m_taskPanel;
  case DockWidgetHelper::DockType::ConsoleDock:
    return m_consoleViewer;
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
    auto *toolBar = new TitleToolBar2(tr("Global"), this);
    toolBar->setIconSize(QSize(sz, sz));
    m_toolBarHelper->setupToolBars(toolBar);
    auto *titleBtns = toolBar->createTitleBarButtons(
        m_toolBarHelper->generateIcon(QStringLiteral("minimize.svg")),
        m_toolBarHelper->generateIcon(QStringLiteral("maximize.svg")),
        m_toolBarHelper->generateIcon(QStringLiteral("maximize_restore.svg")),
        m_toolBarHelper->generateDangerousIcon(QStringLiteral("close.svg")));

    // Wrap toolbar in a container with top padding for frameless title bar.
    // QToolBar's internal layout ignores setContentsMargins, so we use a wrapper.
    auto *titleBarWrapper = new QWidget(this);
    auto *wrapperLayout = new QVBoxLayout(titleBarWrapper);
    wrapperLayout->setContentsMargins(0, 2, 0, 0);
    wrapperLayout->setSpacing(0);

    auto *innerLayout = new QHBoxLayout();
    innerLayout->setContentsMargins(0, 0, 0, 0);
    innerLayout->setSpacing(0);
    innerLayout->addWidget(toolBar, 1);
    innerLayout->addWidget(titleBtns);
    wrapperLayout->addLayout(innerLayout);
    setMenuWidget(titleBarWrapper);

    // Register title bar and system buttons for qwindowkit.
    m_windowAgent->setTitleBar(titleBarWrapper);
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, toolBar->minimizeButton());
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, toolBar->maximizeButton());
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::Close, toolBar->closeButton());

    // Mark interactive toolbar widgets as hit-test visible so they receive mouse events.
    for (auto *act : toolBar->actions()) {
      auto *w = toolBar->widgetForAction(act);
      if (w) {
        m_windowAgent->setHitTestVisible(w, true);
      }
    }

    connect(this, &MainWindow2::windowStateChanged, toolBar, &TitleToolBar2::updateMaximizeButton);
    connect(themeService, &ThemeService::themeChanged, toolBar, [this, toolBar]() {
      toolBar->refreshIcons(m_toolBarHelper->generateIcon(QStringLiteral("minimize.svg")),
                            m_toolBarHelper->generateIcon(QStringLiteral("maximize.svg")),
                            m_toolBarHelper->generateIcon(QStringLiteral("maximize_restore.svg")),
                            m_toolBarHelper->generateDangerousIcon(QStringLiteral("close.svg")));
    });
  } else {
    auto *toolBar = new QToolBar(tr("Global"), this);
    toolBar->setIconSize(QSize(sz, sz));
    m_toolBarHelper->setupToolBars(toolBar);
    addToolBar(toolBar);
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
  if (m_windowAgent) {
    // qwindowkit is active — setWindowFlags() would destroy the native handle.
#ifdef Q_OS_WIN
    auto hwnd = reinterpret_cast<HWND>(winId());
    SetWindowPos(hwnd, p_enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    if (auto *win = windowHandle()) {
      win->setFlag(Qt::WindowStaysOnTopHint, p_enabled);
    }
#endif
  } else {
    // System title bar mode — safe to use setWindowFlags().
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
  QTimer::singleShot(0, this, [this]() { validateDockProportions(); });

  emit layoutChanged();
}

void MainWindow2::validateDockProportions() {
  const int windowW = width();
  const int kMinDockWidth = 150;
  const int kMaxDockWidthPercent = 40;
  const int kDefaultDockWidthPercent = 25;
  const int kMinDockHeight = 80;

  const auto &docks = m_dockWidgetHelper.getDocks();

  QList<QDockWidget *> hDockList;
  QList<int> hSizeList;

  // Left docks: only NavigationDock (index 0) as representative for tabified group.
  if (docks.size() > static_cast<int>(DockWidgetHelper::NavigationDock)) {
    auto *dock = docks[DockWidgetHelper::NavigationDock];
    if (dock && !dock->isFloating() && dock->isVisible()) {
      const int dw = dock->width();
      const int maxW = windowW * kMaxDockWidthPercent / 100;
      if (dw < kMinDockWidth) {
        hDockList.append(dock);
        hSizeList.append(kMinDockWidth);
      } else if (dw > maxW) {
        hDockList.append(dock);
        hSizeList.append(windowW * kDefaultDockWidthPercent / 100);
      }
    }
  }

  // Right dock: OutlineDock (index 5).
  if (docks.size() > static_cast<int>(DockWidgetHelper::OutlineDock)) {
    auto *dock = docks[DockWidgetHelper::OutlineDock];
    if (dock && !dock->isFloating() && dock->isVisible()) {
      const int dw = dock->width();
      const int maxW = windowW * kMaxDockWidthPercent / 100;
      if (dw < kMinDockWidth) {
        hDockList.append(dock);
        hSizeList.append(kMinDockWidth);
      } else if (dw > maxW) {
        hDockList.append(dock);
        hSizeList.append(windowW * kDefaultDockWidthPercent / 100);
      }
    }
  }

  if (!hDockList.isEmpty()) {
    resizeDocks(hDockList, hSizeList, Qt::Horizontal);
  }

  // Bottom docks: ConsoleDock (index 6) and LocationListDock (index 7).
  QList<QDockWidget *> vDockList;
  QList<int> vSizeList;

  for (auto dockType : {DockWidgetHelper::ConsoleDock, DockWidgetHelper::LocationListDock}) {
    if (docks.size() > static_cast<int>(dockType)) {
      auto *dock = docks[dockType];
      if (dock && !dock->isFloating() && dock->isVisible()) {
        if (dock->height() < kMinDockHeight) {
          vDockList.append(dock);
          vSizeList.append(kMinDockHeight);
        }
      }
    }
  }

  if (!vDockList.isEmpty()) {
    resizeDocks(vDockList, vSizeList, Qt::Vertical);
  }

  qCDebug(lcUi) << "MainWindow2::validateDockProportions: windowWidth=" << windowW << ", adjusted"
                << hDockList.size() << "horizontal," << vDockList.size() << "vertical docks";
}

void MainWindow2::restart() {
  m_requestQuit = kExitToRestart;
  close();
}

void MainWindow2::changeEvent(QEvent *p_event) {
  if (p_event->type() == QEvent::WindowStateChange) {
    QWindowStateChangeEvent *eve = static_cast<QWindowStateChangeEvent *>(p_event);
    m_windowOldState = eve->oldState();
    emit windowStateChanged(windowState());
  }

  QMainWindow::changeEvent(p_event);
}

void MainWindow2::dragEnterEvent(QDragEnterEvent *p_event) {
  const auto *mimeData = p_event->mimeData();
  if (mimeData && mimeData->hasUrls()) {
    const auto urls = mimeData->urls();
    for (const auto &url : urls) {
      if (url.isLocalFile()) {
        p_event->acceptProposedAction();
        return;
      }
    }
  }

  p_event->ignore();
}

void MainWindow2::dropEvent(QDropEvent *p_event) {
  const auto *mimeData = p_event->mimeData();
  if (mimeData && mimeData->hasUrls()) {
    QStringList paths;
    const auto urls = mimeData->urls();
    for (const auto &url : urls) {
      if (url.isLocalFile()) {
        const auto path = url.toLocalFile();
        if (!path.isEmpty()) {
          paths.append(path);
        }
      }
    }

    if (!paths.isEmpty()) {
      openFiles(paths);
      p_event->acceptProposedAction();
      return;
    }
  }

  p_event->ignore();
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

  setupGlobalHotkey();
}

void MainWindow2::setupGlobalHotkey() {
  const auto &coreConfig = m_serviceLocator.get<ConfigMgr2>()->getCoreConfig();
  const auto keySeq = QKeySequence(coreConfig.getShortcut(CoreConfig::Global_WakeUp));
  if (keySeq.isEmpty()) {
    return;
  }

  // Register a global (system-wide) hotkey to wake up / show the main window.
  // Owned by this MainWindow2 (auto-unregistered on destruction).
  auto hotkey = new QHotkey(keySeq, true, this);
  if (!hotkey->isRegistered()) {
    qWarning() << "failed to register global wake-up hotkey" << keySeq.toString();
    return;
  }

  connect(hotkey, &QHotkey::activated, this, [this]() { showMainWindow(); });
}

void MainWindow2::quitApp() {
  m_requestQuit = 0;
  close();
}

void MainWindow2::onThemeChanged() {
  auto *themeService = m_serviceLocator.get<ThemeService>();
  if (!themeService)
    return;

  // Step 1: Apply stylesheet
  if (m_progressDialog) {
    m_progressDialog->setValue(1);
    m_progressDialog->setLabelText(tr("Applying stylesheet..."));
  }
  QCoreApplication::processEvents();

  auto stylesheet = themeService->fetchQtStyleSheet();
  if (!stylesheet.isEmpty()) {
    qApp->setStyleSheet(stylesheet);
    qApp->style()->unpolish(qApp);
    qApp->style()->polish(qApp);
  }

  // Step 2: Refresh UI
  if (m_progressDialog) {
    m_progressDialog->setValue(2);
    m_progressDialog->setLabelText(tr("Refreshing UI..."));
  }
  QCoreApplication::processEvents();

  themeService->setBaseBackground(palette().color(QPalette::Base));

  // Step 3: Done
  if (m_progressDialog) {
    m_progressDialog->setValue(3);
  }
}
