#include "application.h"

#include <QFileOpenEvent>
#include <QDebug>

using namespace vnotex;

Application::Application(int &p_argc, char **p_argv)
    : QApplication(p_argc, p_argv)
{
}

bool Application::event(QEvent *p_event)
{
    // On macOS, we need this to open file from Finder.
    if (p_event->type() == QEvent::FileOpen) {
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(p_event);
        qDebug() << "request to open file" << openEvent->file();
        emit openFileRequested(openEvent->file());
    }

    return QApplication::event(p_event);
}
