#include "vmainwindow.h"
#include <QApplication>
#include <QFile>
#include <QTextCodec>
#include "utils/vutils.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QTextCodec *codec = QTextCodec::codecForName("UTF8");
    if (codec) {
        QTextCodec::setCodecForLocale(codec);
    }
    VMainWindow w;
    w.show();

    QString style = VUtils::readFileFromDisk(":/resources/vnote.qss");
    if (!style.isEmpty()) {
        VUtils::processStyle(style);
        app.setStyleSheet(style);
    }

    return app.exec();
}
