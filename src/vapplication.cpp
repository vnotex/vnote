#include "vapplication.h"

VApplication::VApplication(int &argc, char **argv)
    : QApplication(argc, argv)
{
    connect(this, &QApplication::applicationStateChanged, this, &VApplication::onApplicationStateChanged);
}

void VApplication::onApplicationStateChanged(Qt::ApplicationState state)
{
    qWarning() << "VApplication::onApplicationStateChanged state =" << int(state);
    if(state == Qt::ApplicationActive) {
        this->window->show();
        // Need to call raise() in macOS.
        this->window->raise();
    }
}
