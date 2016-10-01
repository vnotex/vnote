#include "vmainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    VMainWindow w;
    w.show();

    return a.exec();
}
