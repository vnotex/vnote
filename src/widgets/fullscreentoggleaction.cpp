#include "fullscreentoggleaction.h"

#include <QEvent>

using namespace vnotex;

FullScreenToggleAction::FullScreenToggleAction(QWidget *p_window,
                                               const QIcon &p_icon,
                                               QObject *p_parent)
    : BiAction(p_icon,
               tr("F&ull Screen"),
               QIcon(),
               tr("Exit F&ull Screen"),
               p_parent),
      m_window(p_window)
{
    setCheckable(true);

    if (m_window) {
        m_window->installEventFilter(this);
    }

    connect(this, &QAction::triggered,
            this, [this](bool p_checked) {
                if ((p_checked && !m_window->isFullScreen())
                    || (!p_checked && m_window->isFullScreen())) {
                    setWindowFullScreen(m_window, p_checked);
                }
            });
}

bool FullScreenToggleAction::eventFilter(QObject *p_object, QEvent *p_event)
{
    if (p_object == m_window) {
        if (p_event->type() == QEvent::WindowStateChange) {
            if (m_window->isFullScreen() != isChecked()) {
                trigger();
            }
        }
    }

    return false;
}

void FullScreenToggleAction::setWindowFullScreen(QWidget *p_window, bool p_set)
{
    Q_ASSERT(p_window);
    if (p_set) {
        p_window->setWindowState(p_window->windowState() | Qt::WindowFullScreen);
    } else {
        p_window->setWindowState(p_window->windowState() & ~Qt::WindowFullScreen);
    }
}
