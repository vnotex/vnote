#include "mainwindow2.h"

#include <QCloseEvent>
#include <QDockWidget>
#include <QTimer>
#include <QWidget>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <qwebengineview.h>
#include <widgets/notebookexplorer2.h>

using namespace vnotex;

MainWindow2::MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent)
    : QMainWindow(p_parent), m_serviceLocator(p_serviceLocator) {
  setupUI();

  loadStateAndGeometry();
}

MainWindow2::~MainWindow2() {}

ServiceLocator &MainWindow2::getServiceLocator() { return m_serviceLocator; }

NotebookExplorer2 *MainWindow2::getNotebookExplorer() const { return m_notebookExplorer; }

void MainWindow2::setupUI() {
  // Window title
  setWindowTitle(tr("VNote"));

  // Minimum size: 800x600
  setMinimumSize(800, 600);

  // Empty central widget placeholder
  auto *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  // Setup dock widgets
  setupDocks();

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
  });
}

void MainWindow2::loadStateAndGeometry() {
  const auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();
  const auto sg = sessionConfig.getMainWindowStateGeometry();

  if (!sg.m_mainGeometry.isEmpty()) {
    restoreGeometry(sg.m_mainGeometry);
  }

  if (!sg.m_mainState.isEmpty()) {
    // Will also restore the state of dock widgets.
    restoreState(sg.m_mainState);
  }

  QByteArray explorerState = sessionConfig.getNotebookExplorerSession();
  if (!explorerState.isEmpty()) {
    m_notebookExplorer->restoreState(explorerState);
  }
}

void MainWindow2::closeEvent(QCloseEvent *p_event) {
  if (isVisible()) {
    saveStateAndGeometry();
  }

  QMainWindow::closeEvent(p_event);
}

void MainWindow2::saveStateAndGeometry() {
  SessionConfig::MainWindowStateGeometry sg;
  sg.m_mainState = saveState();
  sg.m_mainGeometry = saveGeometry();

  auto &sessionConfig = m_serviceLocator.get<ConfigMgr2>()->getSessionConfig();
  sessionConfig.setMainWindowStateGeometry(sg);

  sessionConfig.setNotebookExplorerSession(m_notebookExplorer->saveState());
}

void MainWindow2::setupNotebookExplorer() {
  m_notebookExplorer = new NotebookExplorer2(m_serviceLocator, this);
  m_notebookExplorer->setObjectName("NotebookExplorer2.vnotex");
}

void MainWindow2::setupDocks() {
  setupNotebookExplorer();

  m_dockWidgetHelper.setupDocks();
}

QWidget *MainWindow2::getDockWidget(DockWidgetHelper::DockType p_dockType) const {
  switch (p_dockType) {
    case DockWidgetHelper::DockType::NavigationDock:
      return m_notebookExplorer;
    default:
      return nullptr;
  }
}
