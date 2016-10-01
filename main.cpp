#include "vmainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    VMainWindow w;
    w.show();

    return app.exec();
}
