#include "vmainwindow.h"
#include <QApplication>
#include <QFile>
#include <QTextCodec>
#include "utils/vutils.h"
#include "vsingleinstanceguard.h"

int main(int argc, char *argv[])
{
    VSingleInstanceGuard guard;
    if (!guard.tryRun()) {
        return 0;
    }

    QApplication app(argc, argv);

    QTextCodec *codec = QTextCodec::codecForName("UTF8");
    if (codec) {
        QTextCodec::setCodecForLocale(codec);
    }
    VMainWindow w;
    w.show();

    QString style = VUtils::readFileFromDisk(":/resources/vnote.qss");
    if (!style.isEmpty()) {
        VUtils::processStyle(style, w.getPalette());
        app.setStyleSheet(style);
    }

    return app.exec();
}
