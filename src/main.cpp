#include "vmainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QDebug>
#include <QLibraryInfo>
#include <QFile>
#include <QTextCodec>
#include "utils/vutils.h"
#include "vsingleinstanceguard.h"
#include "vconfigmanager.h"

VConfigManager vconfig;

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
    vconfig.initialize();

    QString locale = vconfig.getLanguage();
    if (locale == "System" || !VUtils::isValidLanguage(locale)) {
        locale = QLocale::system().name();
    }
    qDebug() << "use locale" << locale;

    // load translation for Qt
    QTranslator qtTranslator;
    if (!qtTranslator.load("qt_" + locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
        qtTranslator.load("qt_" + locale, "translations");
    }
    app.installTranslator(&qtTranslator);

    // load translation for vnote
    QTranslator translator;
    if (translator.load("vnote_" + locale, ":/translations")) {
        qDebug() << "install VNote translator";
        app.installTranslator(&translator);
    }

    QTextCodec *codec = QTextCodec::codecForName("UTF8");
    if (codec) {
        QTextCodec::setCodecForLocale(codec);
    }
    VMainWindow w;
    QString style = VUtils::readFileFromDisk(":/resources/vnote.qss");
    if (!style.isEmpty()) {
        VUtils::processStyle(style, w.getPalette());
        app.setStyleSheet(style);
    }

    w.show();

    return app.exec();
}
