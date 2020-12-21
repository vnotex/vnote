#include <QApplication>
#include <QDebug>
#include <QTextCodec>
#include <QSslSocket>
#include <QLocale>
#include <QTranslator>
#include <QScopedPointer>
#include <QOpenGLContext>
#include <QDateTime>
#include <QSysInfo>
#include <QProcess>

#include <core/configmgr.h>
#include <core/mainconfig.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/singleinstanceguard.h>
#include <core/vnotex.h>
#include <core/logger.h>
#include <core/global.h>
#include <widgets/mainwindow.h>
#include <QWebEngineSettings>
#include <core/exception.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

void loadTranslators(QApplication &p_app);

void initWebEngineSettings();

int main(int argc, char *argv[])
{
    SingleInstanceGuard guard;
    bool canRun = guard.tryRun();
    if (!canRun) {
        return 0;
    }

    QTextCodec *codec = QTextCodec::codecForName("UTF8");
    if (codec) {
        QTextCodec::setCodecForLocale(codec);
    }

    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // This only takes effect on Win, X11 and Android.
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // Set OpenGL option on Windows.
    // Set environment QT_OPENGL to "angle/desktop/software".
#if defined(Q_OS_WIN)
    {
        auto option = SessionConfig::getOpenGLAtBootstrap();
        qInfo() << "OpenGL option" << SessionConfig::openGLToString(option);
        switch (option) {
        case SessionConfig::OpenGL::Desktop:
            QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
            break;

        case SessionConfig::OpenGL::Angle:
            QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
            break;

        case SessionConfig::OpenGL::Software:
            QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
            break;

        default:
            break;
        }
    }
#endif

    QApplication app(argc, argv);

    initWebEngineSettings();

    const QString iconPath = ":/vnotex/data/core/icons/vnote.ico";
    // Make sense only on Windows.
    app.setWindowIcon(QIcon(iconPath));

    app.setApplicationName(ConfigMgr::c_appName);
    app.setOrganizationName(ConfigMgr::c_orgName);

    try {
        app.setApplicationVersion(ConfigMgr::getInst().getConfig().getVersion());
    } catch (Exception &e) {
        MessageBoxHelper::notify(MessageBoxHelper::Critical,
                                 QApplication::tr("%1 failed to start.").arg(ConfigMgr::c_appName),
                                 QApplication::tr("Failed to initialize configuration manager. "
                                                  "Please check if all the files are intact or reinstall the application."),
                                 e.what());
        return -1;
    }

    // Init logger after app info is set.
    Logger::init(false);

    qInfo() << QString("%1 (v%2) started at %3 (%4)").arg(ConfigMgr::c_appName,
                                                          app.applicationVersion(),
                                                          QDateTime::currentDateTime().toString(),
                                                          QSysInfo::productType());

    qInfo() << "OpenSSL build version:" << QSslSocket::sslLibraryBuildVersionString()
            << "link version:" << QSslSocket::sslLibraryVersionString();

    if (QSslSocket::sslLibraryBuildVersionNumber() != QSslSocket::sslLibraryVersionNumber()) {
        qWarning() << "versions of the built and linked OpenSSL mismatch, network may not work";
    }

    // TODO: parse command line options.

    if (app.styleSheet().isEmpty()) {
        auto style = VNoteX::getInst().getThemeMgr().fetchQtStyleSheet();
        if (!style.isEmpty()) {
            app.setStyleSheet(style);
        }
    }

    loadTranslators(app);

    MainWindow window;

    window.show();
    VNoteX::getInst().getThemeMgr().setBaseBackground(window.palette().color(QPalette::Base));

    window.kickOffOnStart();

    int ret = app.exec();
    if (ret == RESTART_EXIT_CODE) {
        // Asked to restart VNote.
        guard.exit();
        QProcess::startDetached(ConfigMgr::getApplicationFilePath(), QStringList());
        qInfo() << "Exit.";
        exit(0);
    }

    return ret;
}

void loadTranslators(QApplication &p_app)
{
    auto localeName = ConfigMgr::getInst().getCoreConfig().getLocale();
    if (!localeName.isEmpty()) {
        QLocale::setDefault(QLocale(localeName));
    }

    QLocale locale;
    qInfo() << "locale:" << locale.name();

    const QString resourceTranslationFolder(QStringLiteral(":/translations"));
    const QString envTranslationFolder(QStringLiteral("translations"));

    // Load missing translation for Qt (QTextEdit/QPlainTextEdit/QTextBrowser).
    QScopedPointer<QTranslator> widgetsTranslator(new QTranslator(&p_app));
    if (widgetsTranslator->load(locale, "widgets", "_", resourceTranslationFolder)) {
        p_app.installTranslator(widgetsTranslator.take());
    }

    QScopedPointer<QTranslator> dialogButtonBoxTranslator(new QTranslator(&p_app));
    if (dialogButtonBoxTranslator->load(locale, "qdialogbuttonbox", "_", resourceTranslationFolder)) {
        p_app.installTranslator(dialogButtonBoxTranslator.take());
    }

    QScopedPointer<QTranslator> webengineTranslator(new QTranslator(&p_app));
    if (webengineTranslator->load(locale, "qwebengine", "_", resourceTranslationFolder)) {
        p_app.installTranslator(webengineTranslator.take());
    }

    // Load translation for Qt from resource.
    QScopedPointer<QTranslator> qtTranslator(new QTranslator(&p_app));
    if (qtTranslator->load(locale, "qt", "_", resourceTranslationFolder)) {
        p_app.installTranslator(qtTranslator.take());
    }

    // Load translation for Qt from env.
    QScopedPointer<QTranslator> qtEnvTranslator(new QTranslator(&p_app));
    if (qtEnvTranslator->load(locale, "qt", "_", envTranslationFolder)) {
        p_app.installTranslator(qtEnvTranslator.take());
    }
}

void initWebEngineSettings()
{
    auto settings = QWebEngineSettings::defaultSettings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
}
