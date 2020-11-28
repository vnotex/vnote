#include "statusbarhelper.h"

#include <QStatusBar>
#include <QtWidgets>

#include "mainwindow.h"

using namespace vnotex;

void StatusBarHelper::setupStatusBar(MainWindow *p_win)
{
    m_statusBar = new QStatusBar(p_win);
    p_win->setStatusBar(m_statusBar);
}
