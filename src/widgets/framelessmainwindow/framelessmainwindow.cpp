#include "framelessmainwindow.h"

#include <QEvent>

using namespace vnotex;

FramelessMainWindow::FramelessMainWindow(bool p_frameless, QWidget *p_parent)
    : QMainWindow(p_parent),
      m_frameless(p_frameless),
      m_defaultFlags(windowFlags())
{
    if (m_frameless) {
        setWindowFlags(m_defaultFlags | Qt::FramelessWindowHint);
    }
}


bool FramelessMainWindow::isFrameless() const
{
    return m_frameless;
}

void FramelessMainWindow::setTitleBar(QWidget *p_titleBar)
{
    Q_ASSERT(!m_titleBar && m_frameless);

    m_titleBar = p_titleBar;
}

void FramelessMainWindow::changeEvent(QEvent *p_event)
{
    QMainWindow::changeEvent(p_event);

    if (p_event->type() == QEvent::WindowStateChange) {
        m_windowStates = windowState();
        m_resizable = m_windowStates == Qt::WindowNoState;
        m_movable = m_windowStates == Qt::WindowNoState;
        emit windowStateChanged(m_windowStates);
    }
}

bool FramelessMainWindow::isMaximized() const
{
    return (m_windowStates & Qt::WindowMaximized) && !(m_windowStates & Qt::WindowFullScreen);
}

void FramelessMainWindow::setWindowFlagsOnUpdate()
{
    // Do nothing by default.
}
