// main2.cpp - New entry point for VNote clean architecture
// Uses ServiceLocator for dependency injection instead of singletons.

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QProcess>
#include <QSslSocket>
#include <QTextCodec>
#include <QTranslator>

#include <core/configmgr2.h>
#include <core/constants.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/logger.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/configservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <core/services/templateservice.h>
#include <core/services/vnote3migrationservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <core/singleinstanceguard.h>
#include <gui/services/navigationmodeservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/viewwindowfactory.h>
#include <gui/utils/widgetutils.h>
#include <qwindow.h>
#include <vtextedit/spellchecker.h>
#include <vtextedit/vtexteditor.h>
#include <vxcore/vxcore.h>
#include <widgets/mainwindow2.h>
#include <widgets/messageboxhelper.h>

#include "application.h"
#include "commandlineoptions.h"
#include "fakeaccessible.h"

using namespace vnotex;

void loadTranslators(QApplication &p_app, const ConfigMgr2 &configMgr) {
  auto localeName = configMgr.getCoreConfig().getLocale();
  if (!localeName.isEmpty()) {
    QLocale::setDefault(QLocale(localeName));
  }

  QLocale locale;
  qInfo() << "locale:" << locale.name();

  const auto translationsPath = QDir("app:translations").absolutePath();
  qInfo() << "translations dir: " << translationsPath;
  if (translationsPath.isEmpty()) {
    qWarning() << "failed to locate translations directory";
    return;
  }

  // For QTextEdit/QTextBrowser and other basic widgets.
  std::unique_ptr<QTranslator> qtbaseTranslator(new QTranslator(&p_app));
  if (qtbaseTranslator->load(locale, "qtbase", "_", translationsPath)) {
    p_app.installTranslator(qtbaseTranslator.release());
  }

  // qt_zh_CN.ts does not cover the real QDialogButtonBox which uses QPlatformTheme.
  std::unique_ptr<QTranslator> dialogButtonBoxTranslator(new QTranslator(&p_app));
  if (dialogButtonBoxTranslator->load(locale, "qdialogbuttonbox", "_", translationsPath)) {
    p_app.installTranslator(dialogButtonBoxTranslator.release());
  }

  std::unique_ptr<QTranslator> webengineTranslator(new QTranslator(&p_app));
  if (webengineTranslator->load(locale, "qwebengine", "_", translationsPath)) {
    p_app.installTranslator(webengineTranslator.release());
  }

  std::unique_ptr<QTranslator> qtTranslator(new QTranslator(&p_app));
  if (qtTranslator->load(locale, "qtv", "_", translationsPath)) {
    p_app.installTranslator(qtTranslator.release());
  }

  std::unique_ptr<QTranslator> qtEnvTranslator(new QTranslator(&p_app));
  if (qtEnvTranslator->load(locale, "qt", "_", translationsPath)) {
    p_app.installTranslator(qtEnvTranslator.release());
  }

  std::unique_ptr<QTranslator> vnoteTranslator(new QTranslator(&p_app));
  if (vnoteTranslator->load(locale, "vnote", "_", translationsPath)) {
    p_app.installTranslator(vnoteTranslator.release());
  }

  std::unique_ptr<QTranslator> vtexteditTranslator(new QTranslator(&p_app));
  if (vtexteditTranslator->load(locale, "vtextedit", "_", translationsPath)) {
    p_app.installTranslator(vtexteditTranslator.release());
  }
}

void setOpenGLOption(const ConfigMgr2 &configMgr) {
  // Set OpenGL option on Windows.
  // Set environment QT_OPENGL to "angle/desktop/software".
#if defined(Q_OS_WIN)
  {
    auto option = configMgr.getSessionConfig().getOpenGL();
    qDebug() << "OpenGL option" << SessionConfig::openGLToString(option);
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
}

void disableSandboxIfNeeded() {
#if defined(Q_OS_LINUX)
  // Disable sandbox on Linux.
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox");
#endif
}

int main(int argc, char *argv[]) {
  // Set UTF-8 codec for locale
  QTextCodec *codec = QTextCodec::codecForName("UTF8");
  if (codec) {
    QTextCodec::setCodecForLocale(codec);
  }

  vxcore_set_app_info(ConfigMgr2::c_orgName.toUtf8().constData(),
                      ConfigMgr2::c_appName.toUtf8().constData());

  // Initialize vxcore context
  VxCoreContextHandle context = nullptr;
  VxCoreError err = vxcore_context_create(nullptr, &context);
  if (err != VXCORE_OK || !context) {
    qCritical() << "Failed to create vxcore context:" << vxcore_error_message(err);
    return -1;
  }
  qInfo() << "VxCore context created";

  int ret = 0;

  // Scoped block to ensure proper destruction order:
  // All services and UI must be destroyed BEFORE vxcore_context_destroy()
  {
    // Create ServiceLocator
    ServiceLocator serviceLocator;

    // Create and register services (non-owning pointers stored in ServiceLocator)
    HookManager hookManager;
    ConfigService configService(context, &hookManager);
    NotebookCoreService notebookService(context);
    SearchCoreService searchService(context);
    SearchService searchAsyncService(&searchService);
    WorkspaceCoreService workspaceService(context);
    BufferService bufferService(context, &hookManager);
    TagCoreService tagCoreService(context);
    TagService tagService(context, &hookManager);
    SnippetCoreService snippetCoreService(context);

    serviceLocator.registerService<ConfigService>(&configService);
    serviceLocator.registerService<ConfigCoreService>(configService.coreService());
    serviceLocator.registerService<NotebookCoreService>(&notebookService);
    VNote3MigrationService migrationService(&notebookService, &tagCoreService);
    serviceLocator.registerService<VNote3MigrationService>(&migrationService);
    serviceLocator.registerService<BufferService>(&bufferService);
    serviceLocator.registerService<SearchCoreService>(&searchService);
    serviceLocator.registerService<SearchService>(&searchAsyncService);
    serviceLocator.registerService<WorkspaceCoreService>(&workspaceService);
    serviceLocator.registerService<HookManager>(&hookManager);
    serviceLocator.registerService<TagCoreService>(&tagCoreService);
    serviceLocator.registerService<TagService>(&tagService);
    serviceLocator.registerService<SnippetCoreService>(&snippetCoreService);
    qInfo() << "Services registered (including HookManager)";

    // Wire HookManager to NotebookCoreService for firing node operation hooks.
    notebookService.setHookManager(&hookManager);

    // Wire HookManager to WorkspaceCoreService for firing view area hooks.
    workspaceService.setHookManager(&hookManager);

    // Create ConfigMgr2 with ConfigCoreService (from ConfigService wrapper)
    ConfigMgr2 configMgr(configService.coreService());
    configMgr.init();
    serviceLocator.registerService<ConfigMgr2>(&configMgr);
    qInfo() << "ConfigMgr2 registered";

    // Set initial auto-save policy from config.
    bufferService.syncAutoSavePolicy(
        static_cast<int>(configMgr.getEditorConfig().getAutoSavePolicy()));

    // Create FileTypeService with VxCoreContextHandle and locale
    FileTypeCoreService fileTypeService(context, configMgr.getCoreConfig().getLocaleToUse());
    serviceLocator.registerService<FileTypeCoreService>(&fileTypeService);
    qInfo() << "FileTypeService registered";

    // Create TemplateService with ConfigMgr2
    TemplateService templateService(&configMgr);
    serviceLocator.registerService<TemplateService>(&templateService);
    qInfo() << "TemplateService registered";

    // Create HtmlTemplateService with ConfigMgr2
    HtmlTemplateService htmlTemplateService(&configMgr);
    serviceLocator.registerService<HtmlTemplateService>(&htmlTemplateService);
    qInfo() << "HtmlTemplateService registered";

    // Create ViewWindowFactory and register built-in file type creators
    ViewWindowFactory viewWindowFactory;
    viewWindowFactory.registerBuiltInCreators();
    serviceLocator.registerService<ViewWindowFactory>(&viewWindowFactory);
    qInfo() << "ViewWindowFactory registered";

    setOpenGLOption(configMgr);

    disableSandboxIfNeeded();

    // Create Qt application
    Application app(argc, argv);

    configMgr.initAfterQtAppStarted();

    // Create ThemeService after Qt app is started
    ThemeService themeService({configMgr.getCoreConfig().getTheme(),
                               configMgr.getCoreConfig().getLocaleToUse(),
                               configService.getDataPath(DataLocation::App)});
    serviceLocator.registerService<ThemeService>(&themeService);
    app.setThemeService(&themeService);
    qInfo() << "ThemeService registered";

    // Initialize syntax highlighting repository (must happen before any TextEditor is created).
    // Legacy equivalent: VNoteX::initThemeMgr() -> ThemeMgr::addSyntaxHighlightingSearchPaths().
    vte::VTextEditor::addSyntaxCustomSearchPaths(
        QStringList() << configMgr.getConfigDataFolder(ConfigMgr2::SyntaxHighlighting));

    // Initialize spell check dictionary search paths.
    // Legacy equivalent: MainWindow::setupSpellCheck().
    vte::SpellChecker::addDictionaryCustomSearchPaths(
        QStringList() << configMgr.getConfigDataFolder(ConfigMgr2::Dicts));

    QAccessible::installFactory(&FakeAccessible::accessibleFactory);

    {
      const QString iconPath = ":/vnotex/data/core/icons/vnote.ico";
      // Make sense only on Windows.
      app.setWindowIcon(QIcon(iconPath));

      app.setApplicationName(ConfigMgr2::c_appName);
      app.setOrganizationName(ConfigMgr2::c_orgName);

      app.setApplicationVersion(ConfigMgr2::getApplicationVersion());
    }

    CommandLineOptions cmdOptions;
    switch (cmdOptions.parse(app.arguments())) {
    case CommandLineOptions::Ok:
      break;

    case CommandLineOptions::Error:
      fprintf(stderr, "%s\n", qPrintable(cmdOptions.m_errorMsg));
      // Arguments to WebEngineView will be unknown ones. So just let it go.
      break;

    case CommandLineOptions::VersionRequested: {
      auto versionStr =
          QStringLiteral("%1 %2").arg(app.applicationName(), app.applicationVersion());
      qInfo() << versionStr;
      vxcore_context_destroy(context);
      return 0;
    }

    case CommandLineOptions::HelpRequested:
      Q_FALLTHROUGH();
    default:
      qInfo() << cmdOptions.m_helpText;
      vxcore_context_destroy(context);
      return 0;
    }

    // Guarding.
    SingleInstanceGuard guard;
    bool canRun = guard.tryRun();
    if (!canRun) {
      guard.requestOpenFiles(cmdOptions.m_pathsToOpen);
      guard.requestShow();
      vxcore_context_destroy(context);
      return 0;
    }

    // Init logger after app info is set.
    Logger::init(cmdOptions.m_verbose, cmdOptions.m_logToStderr);

    qInfo() << QStringLiteral("%1 (v%2) started at %3 (%4)")
                   .arg(ConfigMgr2::c_appName, app.applicationVersion(),
                        QDateTime::currentDateTime().toString(), QSysInfo::productType());

    qInfo() << "OpenSSL build version:" << QSslSocket::sslLibraryBuildVersionString()
            << "link version:" << QSslSocket::sslLibraryVersionString();

    if (QSslSocket::sslLibraryBuildVersionNumber() != QSslSocket::sslLibraryVersionNumber()) {
      qWarning() << "versions of the built and linked OpenSSL mismatch, network may not work";
    }

    loadTranslators(app, configMgr);

    if (app.styleSheet().isEmpty()) {
      auto style = themeService.fetchQtStyleSheet();
      if (!style.isEmpty()) {
        app.setStyleSheet(style);
        // Set up hot-reload for the theme folder if enabled via command line
        if (cmdOptions.m_watchThemes) {
          const auto themeFolderPath = themeService.getCurrentTheme().getThemeFolder();
          app.watchThemeFolder(themeFolderPath);
        }
      }
    }

    // Create MainWindow2 with ServiceLocator
    MainWindow2 mainWindow(serviceLocator);

    // Create NavigationModeService AFTER MainWindow2 (needs top-level widget)
    NavigationModeService navigationModeService(configMgr.getCoreConfig(), &mainWindow);
    serviceLocator.registerService<NavigationModeService>(&navigationModeService);
    qInfo() << "NavigationModeService registered";

    // Register all navigation targets after NavigationModeService is available.
    mainWindow.setupNavigationMode();

    mainWindow.show();
    qInfo() << "MainWindow2 shown";

    // Let MainWindow show first to decide the screen on which app is running.
    WidgetUtils::calculateScaleFactor(mainWindow.windowHandle()->screen());
    themeService.setBaseBackground(mainWindow.palette().color(QPalette::Base));

    mainWindow.kickOffPostInit(cmdOptions.m_pathsToOpen);

    // Run event loop
    ret = app.exec();
    if (ret == kExitToRestart) {
      // Asked to restart VNote.
      guard.exit();
      QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
      // Services and configMgr will be destroyed when leaving this scope,
      // then vxcore_context_destroy() will be called below.
    }
    // All services destroyed here before vxcore context
  }

  // Cleanup: destroy vxcore context (after all services are destroyed)
  vxcore_context_destroy(context);
  qInfo() << "VxCore context destroyed";

  if (ret == kExitToRestart) {
    // Must use exit() in Linux to quit the parent process in Qt 5.12.
    // Thanks to @ygcaicn.
    exit(0);
  }

  return ret;
}
