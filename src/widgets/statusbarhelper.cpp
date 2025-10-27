#include "statusbarhelper.h"

#include <QStatusBar>

#include "mainwindow.h"

using namespace vnotex;

void StatusBarHelper::setupStatusBar(MainWindow *p_mainWindow) {
  auto bar = new QStatusBar(p_mainWindow);
  p_mainWindow->setStatusBar(bar);
}
