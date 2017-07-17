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

VConfigManager vconfig;

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
    QStringList filePaths;
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (QFileInfo::exists(args[i])) {
            QString filePath = args[i];
            QFileInfo fi(filePath);
            if (fi.isFile()) {
                // Need to use absolute path here since VNote may be launched
                // in different working directory.
                filePath = QDir::cleanPath(fi.absoluteFilePath());
            }

            filePaths.append(filePath);
        }
    }

    qDebug() << "command line arguments" << args;

    if (!canRun) {
        // Ask another instance to open files passed in.
        if (!filePaths.isEmpty()) {
            guard.openExternalFiles(filePaths);
        }

        return 0;
    }

    vconfig.initialize();

    QString locale = VUtils::getLocale();
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

    VMainWindow w(&guard);
    QString style = VUtils::readFileFromDisk(":/resources/vnote.qss");
    if (!style.isEmpty()) {
        VUtils::processStyle(style, w.getPalette());
        app.setStyleSheet(style);
    }

    w.show();

    w.openExternalFiles(filePaths);

    return app.exec();
}
