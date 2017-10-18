#include "vmainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QDebug>
#include <QLibraryInfo>
#include <QFile>
#include <QTextCodec>
#include <QFileInfo>
#include <QStringList>
#include <QDir>
#include "utils/vutils.h"
#include "vsingleinstanceguard.h"
#include "vconfigmanager.h"

VConfigManager *g_config;

#if defined(QT_NO_DEBUG)
QFile g_logFile;
#endif

void VLogger(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toUtf8();
    QString header;

    switch (type) {
    case QtDebugMsg:
        header = "Debug:";
        break;

    case QtInfoMsg:
        header = "Info:";
        break;

    case QtWarningMsg:
        header = "Warning:";
        break;

    case QtCriticalMsg:
        header = "Critical:";
        break;

    case QtFatalMsg:
        header = "Fatal:";
    }

#if defined(QT_NO_DEBUG)
    Q_UNUSED(context);

    QTextStream stream(&g_logFile);

#if defined(Q_OS_WIN)
    stream << header << localMsg << "\r\n";
#else
    stream << header << localMsg << "\n";
#endif

    if (type == QtFatalMsg) {
        g_logFile.close();
        abort();
    }

#else
    std::string fileStr = QFileInfo(context.file).fileName().toStdString();
    const char *file = fileStr.c_str();

    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "%s(%s:%u) %s\n",
                header.toStdString().c_str(), file, context.line, localMsg.constData());
        break;
    case QtInfoMsg:
        fprintf(stderr, "%s(%s:%u) %s\n",
                header.toStdString().c_str(), file, context.line, localMsg.constData());
        break;
    case QtWarningMsg:
        fprintf(stderr, "%s(%s:%u) %s\n",
                header.toStdString().c_str(), file, context.line, localMsg.constData());
        break;
    case QtCriticalMsg:
        fprintf(stderr, "%s(%s:%u) %s\n",
                header.toStdString().c_str(), file, context.line, localMsg.constData());
        break;
    case QtFatalMsg:
        fprintf(stderr, "%s(%s:%u) %s\n",
                header.toStdString().c_str(), file, context.line, localMsg.constData());
        abort();
    }

    fflush(stderr);
#endif
}

int main(int argc, char *argv[])
{
    VSingleInstanceGuard guard;
    bool canRun = guard.tryRun();

#if defined(QT_NO_DEBUG)
    if (canRun) {
        g_logFile.setFileName(VConfigManager::getLogFilePath());
        g_logFile.open(QIODevice::WriteOnly);
    }
#endif

    if (canRun) {
        qInstallMessageHandler(VLogger);
    }

    QTextCodec *codec = QTextCodec::codecForName("UTF8");
    if (codec) {
        QTextCodec::setCodecForLocale(codec);
    }

    QApplication app(argc, argv);

    // The file path passed via command line arguments.
    QStringList filePaths = VUtils::filterFilePathsToOpen(app.arguments().mid(1));

    qDebug() << "command line arguments" << app.arguments();
    qDebug() << "files to open from arguments" << filePaths;

    if (!canRun) {
        // Ask another instance to open files passed in.
        if (!filePaths.isEmpty()) {
            guard.openExternalFiles(filePaths);
        } else {
            guard.showInstance();
        }

        return 0;
    }

    VConfigManager vconfig;
    vconfig.initialize();
    g_config = &vconfig;

    QString locale = VUtils::getLocale();
    // Set default locale.
    if (locale == "zh_CN") {
        QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    }

    // load translation for Qt
    QTranslator qtTranslator;
    if (!qtTranslator.load("qt_" + locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
        qtTranslator.load("qt_" + locale, "translations");
    }

    app.installTranslator(&qtTranslator);

    // load translation for vnote
    QTranslator translator;
    if (translator.load("vnote_" + locale, ":/translations")) {
        app.installTranslator(&translator);
    }

    VMainWindow w(&guard);
    QString style = VUtils::readFileFromDisk(":/resources/vnote.qss");
    if (!style.isEmpty()) {
        VUtils::processStyle(style, w.getPalette());
        app.setStyleSheet(style);
    }

    w.show();

    w.openFiles(filePaths);

    w.openStartupPages();

    return app.exec();
}
