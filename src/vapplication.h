#ifndef VAPPLICATION_H
#define VAPPLICATION_H

#include <QApplication>
#include <QDebug>
#include "vmainwindow.h"

class VApplication : public QApplication
{
    Q_OBJECT
public:
    explicit VApplication(int &argc, char **argv);

    void setWindow(VMainWindow * window)
    {
        this->window = window;
    }


public slots:
    void onApplicationStateChanged(Qt::ApplicationState state);

private:
    VMainWindow *window;
};

#endif // VAPPLICATION_H