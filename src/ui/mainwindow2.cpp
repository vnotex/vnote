#include "mainwindow2.h"

#include <QWidget>

#include <core/servicelocator.h>

using namespace vnotex;

MainWindow2::MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent)
    : QMainWindow(p_parent), m_serviceLocator(p_serviceLocator) {
  setupUI();
}

MainWindow2::~MainWindow2() {}

ServiceLocator &MainWindow2::getServiceLocator() const { return m_serviceLocator; }

void MainWindow2::setupUI() {
  // Window title
  setWindowTitle(tr("VNote"));

  // Minimum size: 800x600
  setMinimumSize(800, 600);

  // Empty central widget placeholder
  auto *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);
}
