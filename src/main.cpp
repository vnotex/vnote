#include "vmainwindow.h"
#include <QApplication>
#include <QFile>
#include <QTextCodec>
#include "utils/vutils.h"
#include "vsingleinstanceguard.h"

void VLogger(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toUtf8();
    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "Debug:%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info:%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning:%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical:%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal:%s (%s:%u)\n", localMsg.constData(), context.file, context.line);
        abort();
    }
}

int main(int argc, char *argv[])
{
    //qInstallMessageHandler(VLogger);

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
