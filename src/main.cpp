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
#include <QSslSocket>
#include <QOpenGLContext>
#include <QProcess>

#include "utils/vutils.h"
#include "vsingleinstanceguard.h"
#include "vconfigmanager.h"
#include "vpalette.h"
#include "vapplication.h"

VConfigManager *g_config;

VPalette *g_palette;

#if defined(QT_NO_DEBUG)
// 5MB log size.
#define MAX_LOG_SIZE 5 * 1024 * 1024

// Whether print debug log in RELEASE mode.
bool g_debugLog = false;

QFile g_logFile;

static void initLogFile(const QString &p_file)
{
    g_logFile.setFileName(p_file);
    if (g_logFile.size() >= MAX_LOG_SIZE) {
        g_logFile.open(QIODevice::WriteOnly | QIODevice::Text);
    } else {
        g_logFile.open(QIODevice::Append | QIODevice::Text);
    }
}
#endif

void VLogger(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
#if defined(QT_NO_DEBUG)
    if (!g_debugLog && type == QtDebugMsg) {
        return;
    }
#endif

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

    default:
        break;
    }

    QString fileName = QFileInfo(context.file).fileName();
#if defined(QT_NO_DEBUG)
    QTextStream stream(&g_logFile);
    stream << header << (QString("(%1:%2) ").arg(fileName).arg(context.line))
           << localMsg << "\n";

    if (type == QtFatalMsg) {
        g_logFile.close();
        abort();
    }
#else
    std::string fileStr = fileName.toStdString();
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
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    bool allowMultiInstances = true;
#else
    bool allowMultiInstances = false;
#endif
    for (int i = 1; i < argc; ++i) {
        if (!qstrcmp(argv[i], "-m")) {
            allowMultiInstances = true;
            break;
        }
    }

    VSingleInstanceGuard guard;
    bool canRun = true;
    if (!allowMultiInstances) {
        canRun = guard.tryRun();
    }

    QTextCodec *codec = QTextCodec::codecForName("UTF8");
    if (codec) {
        QTextCodec::setCodecForLocale(codec);
    }

    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // This only takes effect on Win, X11 and Android.
    // It will disturb original scaling. Just disable it for now.
    // QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // Set openGL version.
    // Or set environment QT_OPENGL to "angle/desktop/software".
    // QCoreApplication::setAttribute(Qt::AA_UseOpenGLES, true);
#if defined(Q_OS_WIN)
    int winOpenGL = VConfigManager::getWindowsOpenGL();
    qInfo() << "OpenGL option" << winOpenGL;
    switch (winOpenGL) {
    case 1:
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
        break;

    case 2:
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
        break;

    case 3:
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
        break;

    case 0:
        V_FALLTHROUGH;
    default:
        break;
    }
#endif

    VApplication app(argc, argv);

    // The file path passed via command line arguments.
    QStringList filePaths = VUtils::filterFilePathsToOpen(app.arguments().mid(1));

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

#if defined(QT_NO_DEBUG)
    for (int i = 1; i < argc; ++i) {
        if (!qstrcmp(argv[i], "-d")) {
            g_debugLog = true;
            break;
        }
    }

    initLogFile(vconfig.getLogFilePath());
#endif

    qInstallMessageHandler(VLogger);

    qInfo() << "VNote started" << g_config->c_version << QDateTime::currentDateTime().toString();

    QString locale = VUtils::getLocale();
    // Set default locale.
    if (locale == "zh_CN") {
        QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    }

    qDebug() << "command line arguments" << app.arguments();
    qDebug() << "files to open from arguments" << filePaths;

    // Check the openSSL.
    qInfo() << "openGL" << QOpenGLContext::openGLModuleType();
    qInfo() << "openSSL"
             << QSslSocket::sslLibraryBuildVersionString()
             << QSslSocket::sslLibraryVersionNumber();

    // Load missing translation for Qt (QTextEdit/QPlainTextEdit/QTextBrowser).
    QTranslator qtTranslator1;
    if (qtTranslator1.load("widgets_" + locale, ":/translations")) {
        app.installTranslator(&qtTranslator1);
    }

    QTranslator qtTranslator2;
    if (qtTranslator2.load("qdialogbuttonbox_" + locale, ":/translations")) {
        app.installTranslator(&qtTranslator2);
    }

    QTranslator qtTranslator3;
    if (qtTranslator3.load("qwebengine_" + locale, ":/translations")) {
        app.installTranslator(&qtTranslator3);
    }

    // Load translation for Qt from resource.
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + locale, ":/translations")) {
        app.installTranslator(&qtTranslator);
    }

    // Load translation for Qt from env.
    QTranslator qtTranslatorEnv;
    if (qtTranslatorEnv.load("qt_" + locale, "translations")) {
        app.installTranslator(&qtTranslatorEnv);
    }

    // Load translation for vnote.
    QTranslator translator;
    if (translator.load("vnote_" + locale, ":/translations")) {
        app.installTranslator(&translator);
    }

    VPalette palette(g_config->getThemeFile());
    g_palette = &palette;

    VMainWindow w(&guard);
    app.setWindow(&w);
    QString style = palette.fetchQtStyleSheet();
    if (!style.isEmpty()) {
        app.setStyleSheet(style);
    }

    w.show();

    g_config->setBaseBackground(w.palette().color(QPalette::Window));

    w.kickOffStartUpTimer(filePaths);

    int ret = app.exec();
    app.setWindow(nullptr);
    if (ret == RESTART_EXIT_CODE) {
        // Ask to restart VNote.
        guard.exit();
        QProcess::startDetached(qApp->applicationFilePath(), QStringList());
        return 0;
    }

    return ret;
}
