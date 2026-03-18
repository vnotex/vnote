#include "mainwindow2.h"

#include <QCloseEvent>
#include <QDockWidget>
#include <QJsonDocument>
#include <QToolBar>
#include <QTimer>
#include <QWidget>
#include <QSystemTrayIcon>

#include "constants.h"
#include "titletoolbar.h"
#include "systemtrayhelper.h"
#include "toolbarhelper2.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/sessionconfig.h>

#include <qwebengineview.h>
#include <widgets/notebookexplorer2.h>
#include <widgets/viewarea2.h>
#include <widgets/messageboxhelper.h>
#include <controllers/viewareacontroller.h>

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

  // Setup tool bar.
  setupToolBar();

  // Setup dock widgets.
  setupDocks();

  setupSystemTray();

#if defined(Q_OS_WIN)
  m_dummyWebView = new QWebEngineView(this);
  m_dummyWebView->setAttribute(Qt::WA_DontShowOnScreen);
#endif
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
}

void MainWindow2::setupViewArea() {
  m_viewArea = new ViewArea2(m_serviceLocator, this);
  setCentralWidget(m_viewArea);
}

void MainWindow2::setupDocks() {
  setupNotebookExplorer();

  m_dockWidgetHelper.setupDocks();

  // Wire ViewAreaController's locateNodeRequested to NotebookExplorer2.
  connect(m_viewArea->getController(), &ViewAreaController::locateNodeRequested,
          m_notebookExplorer, &NotebookExplorer2::locateNode);
}

QWidget *MainWindow2::getDockWidget(DockWidgetHelper::DockType p_dockType) const {
  switch (p_dockType) {
    case DockWidgetHelper::DockType::NavigationDock:
      return m_notebookExplorer;
    default:
      return nullptr;
  }
}

void MainWindow2::setupToolBar() {
  const auto &coreConfig = m_serviceLocator.get<ConfigMgr2>()->getCoreConfig();
  const int sz = coreConfig.getToolBarIconSize();

  m_toolBarHelper = new ToolBarHelper2(m_serviceLocator, this);

  if (isFrameless()) {
    auto toolBar = new TitleToolBar(tr("Global"), this);
    toolBar->setIconSize(QSize(sz + 4, sz + 4));
    m_toolBarHelper->setupToolBars(toolBar);
    toolBar->addTitleBarIcons(
        m_toolBarHelper->generateIcon(QStringLiteral("minimize.svg")),
        m_toolBarHelper->generateIcon(QStringLiteral("maximize.svg")),
        m_toolBarHelper->generateIcon(QStringLiteral("maximize_restore.svg")),
        m_toolBarHelper->generateDangerousIcon(QStringLiteral("close.svg")));
    setTitleBar(toolBar);
    connect(this, &FramelessMainWindowImpl::windowStateChanged, toolBar,
            &TitleToolBar::updateMaximizeAct);
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
    // Hide all dock widgets.
    for (auto dock : m_dockWidgetHelper.getDocks()) {
      if (dock && dock->isVisible()) {
        dock->hide();
      }
    }
  } else {
    // Restore dock widgets visibility.
    // For now, just show the navigation dock.
    auto *navDock = m_dockWidgetHelper.getDock(DockWidgetHelper::DockType::NavigationDock);
    if (navDock) {
      navDock->show();
    }
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

