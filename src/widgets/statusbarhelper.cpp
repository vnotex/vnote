#include "statusbarhelper.h"

#include <QStatusBar>
#include <QtWidgets>

#include "mainwindow.h"

using namespace vnotex;

StatusBarHelper::StatusBarHelper(MainWindow *p_mainWindow)
    : m_mainWindow(p_mainWindow)
{
}

void StatusBarHelper::setupStatusBar()
{
    m_statusBar = new QStatusBar(m_mainWindow);
    m_mainWindow->setStatusBar(m_statusBar);
}
