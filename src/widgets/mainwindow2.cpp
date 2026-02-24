#include "mainwindow2.h"

#include <QDockWidget>
#include <QWidget>

#include <core/servicelocator.h>
#include <widgets/notebookexplorer2.h>

using namespace vnotex;

MainWindow2::MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent)
    : QMainWindow(p_parent), m_serviceLocator(p_serviceLocator) {
  setupUI();
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
