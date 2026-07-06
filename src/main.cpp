// main2.cpp - New entry point for VNote clean architecture
// Uses ServiceLocator for dependency injection instead of singletons.

#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QSslSocket>
#include <QStyle>
#include <QTextCodec>
#include <QTranslator>

#include <controllers/imagehostcontroller.h>
#include <core/configmgr2.h>
#include <core/constants.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/logger.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/configservice.h>
#include <core/services/eventbridge.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/imagehostservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncstateclassifier.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <core/services/templateservice.h>
#include <core/services/vnote3migrationservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <core/singleinstanceguard.h>
#include <core/vxcorelogbridge.h>
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

// Build the QTWEBENGINE_CHROMIUM_FLAGS value by MERGING VNote's default flags
// with whatever the user already exported, WITHOUT clobbering theirs. Each
// default flag is appended only if it is not already present (whole-token
// match), so the user keeps full control.
//
// Why this matters: VNote used to set this variable via two separate qputenv()
// calls, the second of which overwrote the first (so --disable-logging was lost
// on Linux) AND both unconditionally erased any user-supplied value. That
// removed the only way for a user to pass Chromium workaround flags (e.g.
// --single-process / --disable-gpu / --in-process-gpu) needed to survive
// platform-specific QtWebEngine crashes such as the "Failed to parse extension
// manifest" abort reported on Ubuntu 26.04 (issue #2705).
QByteArray buildChromiumFlags(const QByteArray &p_existing) {
  QByteArray flags = p_existing;

  auto hasFlag = [&flags](const char *p_flag) {
    // Match both the bare switch ("--foo") and Chromium's valued form
    // ("--foo=value") as a whole token, so e.g. --enable-logging=stderr still
    // counts as the user opting into logging.
    const QByteArray needle(p_flag);
    const QByteArray valued = needle + '=';
    const auto tokens = flags.split(' ');
    for (const auto &token : tokens) {
      if (token == needle || token.startsWith(valued)) {
        return true;
      }
    }
    return false;
  };
  auto appendFlag = [&flags](const char *p_flag) {
    if (!flags.isEmpty()) {
      flags.append(' ');
    }
    flags.append(p_flag);
  };

  // Quiet Chromium's logging by default, unless the user explicitly opted into
  // logs via --enable-logging. This hides VizNullHypothesis and other verbose
  // Chromium output (and, as a side effect, some genuine ERROR/WARNING lines).
  if (!hasFlag("--disable-logging") && !hasFlag("--enable-logging")) {
    appendFlag("--disable-logging");
  }

#if defined(Q_OS_LINUX)
  // Chromium's sandbox can abort the process on some Linux kernels/distros;
  // disabling it keeps QtWebEngine usable there.
  if (!hasFlag("--no-sandbox")) {
    appendFlag("--no-sandbox");
  }
#endif

  return flags;
}

int main(int argc, char *argv[]) {
  // Set UTF-8 codec for locale
  QTextCodec *codec = QTextCodec::codecForName("UTF8");
  if (codec) {
    QTextCodec::setCodecForLocale(codec);
  }

  // Early parse: scan argv for --verbose flag BEFORE vxcore context creation.
  // This allows us to inject env vars that vxcore's Logger constructor reads.
  // We do a simple literal scan instead of full QCommandLineParser (which requires
  // QCoreApplication) since we only care about the --verbose flag for now.
  bool verboseEarlyFlag = false;
  {
    QStringList args;
    for (int i = 0; i < argc; ++i) {
      args << QString::fromLocal8Bit(argv[i]);
    }
    if (args.contains("--verbose") || args.contains("-verbose")) {
      verboseEarlyFlag = true;
    }
  }

  // If --verbose was passed, unmute VNote- and vxcore-owned QLoggingCategory
  // categories only (NOT Qt's built-in qt.* categories). Each category is listed
  // explicitly rather than via a wildcard, because a bare `*.debug=true` rule
  // also turns on noisy Qt internals like qt.accessibility / qt.qpa.* that
  // produce thousands of QAccessibleInterface log lines on every render and
  // have been observed to trigger a use-after-free crash deep inside
  // Qt6WebEngineCore.dll while editing PlantUML / Mermaid fenced blocks (the
  // accessibility debug formatter dereferences interfaces freed by the
  // WebEngine accessibility tree turnover). See
  // .sisyphus/evidence/puml-edit-crash/diagnosis.md for the full analysis.
  //
  // All vxcore logs are routed through the VxCoreLogBridge under the
  // vnote.vxbridge category, so enabling that one entry covers every vxcore
  // log line as well.
  //
  // When adding a new Q_LOGGING_CATEGORY under the vnote.* namespace, also
  // append it to the list below so it participates in --verbose.
  // Only inject if the user hasn't already set these env vars.
  if (verboseEarlyFlag) {
    if (qgetenv("VNOTE_LOG_RULES").isEmpty()) {
      qputenv("VNOTE_LOG_RULES", "vnote.sync.debug=true\n"
                                 "vnote.sync.workqueue.debug=true\n"
                                 "vnote.buffer.debug=true\n"
                                 "vnote.buffer.savequeue.debug=true\n"
                                 "vnote.workspace.debug=true\n"
                                 "vnote.web.js.debug=true\n"
                                 "vnote.vim.debug=true\n"
                                 "vnote.vxbridge.debug=true\n"
                                 "vnote.ui.debug=true\n"
                                 "vnote.config.debug=true\n"
                                 "vnote.perf.save.debug=true");
    }
    if (qgetenv("VXCORE_LOG_LEVEL").isEmpty()) {
      qputenv("VXCORE_LOG_LEVEL", "debug");
    }
  }

  // Route all vxcore logs through Qt's logging system so they reach
  // VNote's unified log pipeline (installed by Logger::init() later).
  vnotex::installVxCoreLogBridge();

  // Install default logging rules before QApplication creation
  // so the first qDebug message respects them
  vnotex::installDefaultLoggingRules();

  // Set QTWEBENGINE_CHROMIUM_FLAGS by MERGING VNote's defaults (--disable-logging
  // everywhere, plus --no-sandbox on Linux) with any value the user already
  // exported, instead of overwriting it. See buildChromiumFlags() above. To see
  // Chromium logs, run with QTWEBENGINE_CHROMIUM_FLAGS=--enable-logging; to work
  // around a platform QtWebEngine crash, add flags like --single-process and
  // VNote will preserve them (issue #2705).
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
          buildChromiumFlags(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS")));

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
  // All services and UI must be destroyed BEFORE vxcore_context_destroy().
  // Wrapped in do/while(false) so early-exit paths (Version/Help/single-instance)
  // can `break;` out and let scope unwinding tear down services BEFORE the
  // single post-scope vxcore_context_destroy() call.
  do {
    // Create ServiceLocator
    ServiceLocator serviceLocator;

    // Create and register services (non-owning pointers stored in ServiceLocator)
    HookManager hookManager;
    ConfigService configService(context, &hookManager);
    NotebookCoreService notebookService(context);
    SearchCoreService searchService(context);
    SearchService searchAsyncService(&searchService);
    WorkspaceCoreService workspaceService(context);
    // T7: NotebookIoGate serializes save/sync I/O per notebook. Must be
    // constructed BEFORE BufferService (which captures it by pointer) and
    // remain alive until after BufferService shutdown.
    NotebookIoGate notebookIoGate;
    BufferService bufferService(context, &hookManager, &notebookIoGate);
    TagCoreService tagCoreService(context);
    TagService tagService(context, &hookManager);
    SnippetCoreService snippetCoreService(context);

    serviceLocator.registerService<ConfigService>(&configService);
    serviceLocator.registerService<ConfigCoreService>(configService.coreService());
    serviceLocator.registerService<NotebookCoreService>(&notebookService);
    VNote3MigrationService migrationService(&notebookService, &tagCoreService);
    serviceLocator.registerService<VNote3MigrationService>(&migrationService);
    serviceLocator.registerService<BufferService>(&bufferService);
    serviceLocator.registerService<NotebookIoGate>(&notebookIoGate);
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

    // T6 (notebook-explorer-drag-reorder): wire the SAME NotebookIoGate that
    // BufferSaveQueue / SyncOps already use, so reorderFolderChildren
    // serializes against in-flight saves and sync stage-phase work for the
    // same notebook.
    notebookService.setNotebookIoGate(&notebookIoGate);

    // Wire HookManager to WorkspaceCoreService for firing view area hooks.
    workspaceService.setHookManager(&hookManager);

    // Sync stack: EventBridge + SyncCredentialsStore + SyncService.
    // EventBridge must be registered before SyncService (it looks it up in
    // its constructor).
    EventBridge eventBridge(context);
    serviceLocator.registerService<EventBridge>(&eventBridge);

    SyncCredentialsStore syncCredentialsStore(serviceLocator);
    serviceLocator.registerService<SyncCredentialsStore>(&syncCredentialsStore);

    // W14.2 (F5.1): SyncWorkQueueManager is the per-notebook serialized
    // executor used by SyncService for all async sync dispatch. Registered
    // BEFORE SyncService so the latter picks it up via ServiceLocator.
    // Shutdown is driven from QCoreApplication::aboutToQuit below.
    SyncWorkQueueManager syncWorkQueueManager;
    serviceLocator.registerService<SyncWorkQueueManager>(&syncWorkQueueManager);

    SyncService syncService(serviceLocator);
    serviceLocator.registerService<SyncService>(&syncService);

    SyncStateClassifier syncStateClassifier(serviceLocator);
    serviceLocator.registerService<SyncStateClassifier>(&syncStateClassifier);

    qInfo() << "Sync stack registered (EventBridge + SyncService)";

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

    // Create ImageHostService with HookManager for upload hooks.
    ImageHostService imageHostService(&hookManager);
    imageHostService.loadFromConfig(configMgr.getEditorConfig().getImageHosts(),
                                    configMgr.getEditorConfig().getDefaultImageHost());
    serviceLocator.registerService<ImageHostService>(&imageHostService);
    qInfo() << "ImageHostService registered";

    // Create ImageHostController for shared access across widgets.
    ImageHostController imageHostController(serviceLocator);
    serviceLocator.registerService<ImageHostController>(&imageHostController);
    qInfo() << "ImageHostController registered";

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

    // Create Qt application
    QGuiApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    Application app(argc, argv);

    // T17: bounded SyncService shutdown on QApplication aboutToQuit. Use
    // Qt::DirectConnection because the GUI event loop is shutting down at
    // this point; queued slots may never run. Idempotent: SyncService::shutdown()
    // observes its own m_shutDown flag, so the dtor of syncService is a no-op
    // after this call fires.
    QObject::connect(
        &app, &QCoreApplication::aboutToQuit, &app,
        [&serviceLocator]() {
          auto *svc = serviceLocator.get<vnotex::SyncService>();
          if (svc) {
            svc->shutdown();
          }
          // W14.2: drain the per-notebook serialized executor with bounded
          // 5s timeout, AFTER SyncService::shutdown() so no new work is
          // enqueued during the drain window. Idempotent (calling twice is
          // safe — the destructor also calls shutdown(5000)).
          auto *queueMgr = serviceLocator.get<vnotex::SyncWorkQueueManager>();
          if (queueMgr) {
            queueMgr->shutdown(5000);
          }
          // T7: drain async BufferSaveQueue so in-flight auto-saves complete
          // before BufferService is destroyed. Idempotent — dtor also calls
          // shutdown(5000).
          auto *bufSvc = serviceLocator.get<vnotex::BufferService>();
          if (bufSvc) {
            bufSvc->shutdown(5000);
          }
          // Crash-fix: unregister EventBridge from vxcore EventManager BEFORE
          // SyncService/vxcore teardown to avoid AV in ~EventBridge ->
          // vxcore_off_event (mutex on already-freed EventManager).
          auto *bridge = serviceLocator.get<vnotex::EventBridge>();
          if (bridge) {
            bridge->shutdown();
          }
        },
        Qt::DirectConnection);

    configMgr.initAfterQtAppStarted();

    // Create ThemeService after Qt app is started
    ThemeService themeService({configMgr.getCoreConfig().getTheme(),
                               configMgr.getCoreConfig().getLocaleToUse(),
                               configService.getDataPath(DataLocation::App)});
    serviceLocator.registerService<ThemeService>(&themeService);
    app.setThemeService(&themeService);
    themeService.setHookManager(&hookManager);
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
    bool earlyExit = false;
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
      ret = 0;
      earlyExit = true;
      break;
    }

    case CommandLineOptions::HelpRequested:
      Q_FALLTHROUGH();
    default:
      qInfo() << cmdOptions.m_helpText;
      ret = 0;
      earlyExit = true;
      break;
    }
    if (earlyExit) {
      break;
    }

    // Guarding.
    SingleInstanceGuard guard;
    bool canRun = guard.tryRun();
    if (!canRun) {
      guard.requestOpenFiles(cmdOptions.m_pathsToOpen);
      guard.requestShow();
      ret = 0;
      break;
    }

    // Init logger after app info is set.
    Logger::init(configMgr.getLogFile(), cmdOptions.m_verbose, cmdOptions.m_logToStderr);

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
  } while (false);

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
