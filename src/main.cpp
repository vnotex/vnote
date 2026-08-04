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

#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <controllers/imagehostcontroller.h>
#include <core/configmgr2.h>
#include <core/constants.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/logger.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/activityservice.h>
#include <core/services/activitystatsservice.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/configservice.h>
#include <core/services/eventbridge.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/imagehostservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/historyservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/notificationservice.h>
#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncstateclassifier.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <core/services/taskservice.h>
#include <core/services/templateservice.h>
#include <core/services/updateservice.h>
#include <core/services/vnote3migrationservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <core/singleinstanceguard.h>
#include <core/updateinstaller.h>
#include <core/updatelease.h>
#include <core/vxcorelogbridge.h>
#include <gui/services/navigationmodeservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/stickerfactory.h>
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
  // =========================================================================
  // Incremental-update interlock. These are the FIRST executable statements of
  // main(), before Logger::installEarly() and everything else below, and well
  // before vxcore_set_app_info / vxcore_context_create.
  //
  // What this DOES guarantee: no process reaches VNote's own initialization --
  // vxcore context creation, service construction, config load, window setup --
  // while another process is applying an update.
  //
  // What it does NOT guarantee (accepted residual risk): the Windows loader
  // resolves and maps this executable's static imports (Qt, VTextEdit, vxcore)
  // BEFORE main() runs, so a launch that races an in-progress apply can already
  // have mapped a mixed old/new DLL set, or fail in the loader outright. No
  // in-process lease can close that window; only the external-helper /
  // bootstrap-executable follow-up can. The apply is therefore kept as short as
  // possible and journaled so the next launch recovers.
  //
  // installDir / exePath come from GetModuleFileNameW, so no QCoreApplication
  // is required (and none exists yet).
  const QString installDir = vnotex::UpdateInstaller::installDirFromModulePath();
  const QString exePath = vnotex::UpdateInstaller::exePathFromModulePath();

  // Outer-scope so it survives destruction of every service, ConfigMgr2,
  // Application and the guard, all of which happen before the apply runs.
  vnotex::UpdateLease lease;

  {
    vnotex::UpdateLease::AcquireError leaseError = vnotex::UpdateLease::AcquireError::None;
    vnotex::UpdateLease startupLease =
        vnotex::UpdateLease::acquire(installDir, vnotex::UpdateLease::c_defaultTimeoutMs,
                                     &leaseError);
    if (!startupLease) {
      // FAIL CLOSED. Never fall through to initialization: another process may
      // be swapping binaries this one is about to use. No Qt exists yet, so the
      // message has to be a raw Win32 one.
#ifdef Q_OS_WIN
      ::MessageBoxW(nullptr, L"VNote is finishing an update. Please try again in a moment.",
                    L"VNote", MB_OK | MB_ICONINFORMATION);
#else
      fprintf(stderr, "VNote is finishing an update. Please try again in a moment.\n");
#endif
      Q_UNUSED(leaseError);
      return 1;
    }

    // Complete or reverse an interrupted apply before VNote creates its vxcore
    // context, services, configuration or windows, then reclaim the backups of
    // a transaction that already reached a terminal state (this is the "first
    // successful post-update launch").
    //
    // NOT "before anything maps a module": the loader mapped this process's
    // static imports before main() was entered. See the guarantee note above.
    vnotex::UpdateInstaller::recoverInterrupted(installDir);
    vnotex::UpdateInstaller::cleanupOldBackups(installDir);

    // Hand the lease to the outer scope; it is released per the tri-state rule
    // below (immediately on Primary, at the very end of main() otherwise).
    lease = std::move(startupLease);
  }

  // Install the log message handler at the very beginning so early startup logs
  // (vxcore context creation, service registration, etc.) are captured into an
  // in-memory buffer instead of leaking to the console via Qt's default handler.
  // Logger::configure() below resolves the final state and flushes or drops them.
  vnotex::Logger::installEarly();

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
  // VNote's unified log pipeline (installed by Logger::installEarly() above).
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

  // Enable QtWebEngine remote debugging (DevTools) when --remote-debugging-port
  // is passed. On Qt 6 the bare argv switch is NOT honored by QtWebEngine; the
  // sanctioned mechanism is the QTWEBENGINE_REMOTE_DEBUGGING environment
  // variable, which must be set BEFORE QtWebEngine initializes (before the first
  // QWebEngineView). CommandLineOptions::parse() runs far too late (after
  // QApplication), so scan argv literally here, mirroring the --verbose early
  // scan above. Only inject if the user hasn't already exported the env var.
  if (qgetenv("QTWEBENGINE_REMOTE_DEBUGGING").isEmpty()) {
    const QLatin1String prefix("--remote-debugging-port=");
    for (int i = 1; i < argc; ++i) {
      const QString arg = QString::fromLocal8Bit(argv[i]);
      if (arg.startsWith(prefix)) {
        const QString port = arg.mid(prefix.size());
        if (!port.isEmpty()) {
          qputenv("QTWEBENGINE_REMOTE_DEBUGGING", port.toLocal8Bit());
        }
        break;
      }
    }
  }

  vxcore_set_app_info(ConfigMgr2::c_orgName.toUtf8().constData(),
                      ConfigMgr2::c_appName.toUtf8().constData());

  // Initialize vxcore context
  VxCoreContextHandle context = nullptr;
  VxCoreError err = vxcore_context_create(nullptr, &context);
  if (err != VXCORE_OK || !context) {
    // Logger is not configured yet at this point (its buffer would be dropped on
    // this early return), so surface the fatal startup diagnostic to stderr
    // directly.
    fprintf(stderr, "Failed to create vxcore context: %s\n", vxcore_error_message(err));
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
    HistoryService historyService(&notebookService);
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
    NotificationService notificationService;

    serviceLocator.registerService<ConfigService>(&configService);
    serviceLocator.registerService<ConfigCoreService>(configService.coreService());
    serviceLocator.registerService<NotebookCoreService>(&notebookService);
    serviceLocator.registerService<HistoryService>(&historyService);
    serviceLocator.registerService<NotificationService>(&notificationService);
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

    // Incremental updater. Mechanism only: eligibility, network, planning and
    // staging. All policy that needs config (skipped version, check throttle,
    // "check on start") lives in UpdateController, because core_configs links
    // core_services and the reverse dependency would be circular.
    UpdateService updateService(installDir, ConfigMgr2::getApplicationVersion());
    serviceLocator.registerService<UpdateService>(&updateService);
    qInfo() << "UpdateService registered";

    // TaskService: new-arch replacement for the legacy TaskMgr. Constructed
    // after ConfigMgr2 (needs initialized config), NotebookCoreService, and
    // SnippetCoreService. No production ITaskContext implementer exists yet, so
    // a null context is injected (all context-derived variables resolve empty).
    TaskService taskService(&configMgr, &notebookService, &snippetCoreService,
                            /*ITaskContext*/ nullptr);
    serviceLocator.registerService<TaskService>(&taskService);
    taskService.init();
    qInfo() << "TaskService registered";

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

    // Create StickerFactory and register built-in dashboard sticker creators
    StickerFactory stickerFactory;
    stickerFactory.registerBuiltInCreators();
    serviceLocator.registerService<StickerFactory>(&stickerFactory);
    qInfo() << "StickerFactory registered";

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
          // Flush any pending focused-time delta into vxcore before teardown.
          auto *activitySvc = serviceLocator.get<vnotex::ActivityService>();
          if (activitySvc) {
            activitySvc->flush();
          }
        },
        Qt::DirectConnection);

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
      // Print to stdout directly. qInfo() would be captured by the buffered log
      // handler (installEarly) and, on this early-exit path, never flushed.
      fprintf(stdout, "%s\n", qPrintable(versionStr));
      ret = 0;
      earlyExit = true;
      break;
    }

    case CommandLineOptions::HelpRequested:
      Q_FALLTHROUGH();
    default:
      // Print help to stdout verbatim. Using qInfo() would escape the newlines
      // in the help text and render it as a single garbled line (issue #2710).
      fprintf(stdout, "%s", qPrintable(cmdOptions.m_helpText));
      ret = 0;
      earlyExit = true;
      break;
    }
    if (earlyExit) {
      break;
    }

    // Guarding.
    SingleInstanceGuard guard;
    const auto guardResult = guard.tryRun();
    if (guardResult != SingleInstanceGuard::TryRunResult::Primary) {
      if (guardResult == SingleInstanceGuard::TryRunResult::Secondary) {
        if (cmdOptions.m_detachedView) {
          // Forward as a detached-view open. Do NOT raise/show the running main
          // window; only the new detached window should appear.
          guard.requestOpenFilesDetached(cmdOptions.m_pathsToOpen);
        } else {
          guard.requestOpenFiles(cmdOptions.m_pathsToOpen);
          guard.requestShow();
        }
        ret = 0;
      } else {
        // BusyUnreachable: the lock is held but the holder cannot be reached.
        // This used to fail OPEN and become a second primary. Exit instead.
        qWarning() << "another VNote instance holds the lock but is unreachable; exiting";
        ret = 1;
      }
      // The update lease is deliberately NOT released here. A rejected starter
      // has already mapped Qt, VTextEdit and vxcore; releasing now would let an
      // applier start swapping files that are still mapped into this process.
      // It is released as the very last action of main(), after teardown.
      break;
    }

    // Primary: this process owns the instance, so an applier can no longer
    // start without going through the guard. Release the startup lease; the
    // apply path re-acquires it after app.exec() returns.
    lease.release();

    // The guard is already listening, while MainWindow2 does not exist yet.
    // ensureExtraData() processes events to keep startup responsive, so retain
    // any requests arriving in that window and replay them once the final
    // handlers are connected below.
    struct PendingOpenRequest {
      QStringList m_files;
      bool m_detached = false;
    };
    QVector<PendingOpenRequest> pendingOpenRequests;
    bool pendingShow = false;
    const auto pendingOpenConnection =
        QObject::connect(&guard, &SingleInstanceGuard::openFilesRequested, &app,
                         [&pendingOpenRequests](const QStringList &p_files) {
                           pendingOpenRequests.append(PendingOpenRequest{p_files, false});
                         });
    const auto pendingDetachedConnection =
        QObject::connect(&guard, &SingleInstanceGuard::openFilesDetachedRequested, &app,
                         [&pendingOpenRequests](const QStringList &p_files) {
                           pendingOpenRequests.append(PendingOpenRequest{p_files, true});
                         });
    const auto pendingShowConnection =
        QObject::connect(&guard, &SingleInstanceGuard::showRequested, &app,
                         [&pendingShow]() { pendingShow = true; });

    // Only the primary may install bundled data. Besides avoiding duplicate
    // work, this prevents a rejected secondary launch from overwriting themes
    // or web resources while the running primary is using them.
    configMgr.initAfterQtAppStarted();

    // Create ThemeService after bundled themes have been installed.
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

    // Resolve the final logger state and flush the buffered startup logs
    // (dropping below-threshold ones, e.g. when --quiet is set). Reached only on
    // the normal run path; early-exit paths (version/help/second-instance) leave
    // the buffered startup logs unflushed, which is the desired quiet behavior.
    Logger::configure(configMgr.getLogFile(), cmdOptions.m_verbose, cmdOptions.m_logToStderr,
                      cmdOptions.m_quiet);

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

    // Activity tracking (Qt side): must be constructed BEFORE MainWindow2 so
    // it subscribes to FileAfterOpen / MainWindowBeforeClose before those hooks
    // can fire during window startup. Focus-time is computed here and pushed
    // into vxcore's app-global activity.db; note created/edited are captured
    // natively inside vxcore.
    ActivityService activityService(context, &hookManager);
    serviceLocator.registerService<ActivityService>(&activityService);
    qInfo() << "ActivityService registered";

    // Read-only companion to ActivityService: exposes activity.db metrics to
    // the dashboard Activity sticker. Registered before MainWindow2 so the
    // lazily-built home dashboard can resolve it.
    ActivityStatsService activityStatsService(context);
    serviceLocator.registerService<ActivityStatsService>(&activityStatsService);
    qInfo() << "ActivityStatsService registered";

    // Create MainWindow2 with ServiceLocator
    MainWindow2 mainWindow(serviceLocator);
    // Create NavigationModeService AFTER MainWindow2 (needs top-level widget)
    NavigationModeService navigationModeService(configMgr.getCoreConfig(), &mainWindow);
    serviceLocator.registerService<NavigationModeService>(&navigationModeService);
    qInfo() << "NavigationModeService registered";

    // Register all navigation targets after NavigationModeService is available.
    mainWindow.setupNavigationMode();

    // Handle requests forwarded from a second instance (files passed to
    // "Open with VNote" while VNote is already running, plus raise/show).
    QObject::connect(&guard, &SingleInstanceGuard::openFilesRequested, &mainWindow,
                     [&mainWindow](const QStringList &p_files) { mainWindow.openFiles(p_files); });
    QObject::connect(&guard, &SingleInstanceGuard::openFilesDetachedRequested, &mainWindow,
                     [&mainWindow](const QStringList &p_files) {
                       mainWindow.openFiles(p_files, true);
                     });
    QObject::connect(&guard, &SingleInstanceGuard::showRequested, &mainWindow,
                     &MainWindow2::showMainWindow);

    QObject::disconnect(pendingOpenConnection);
    QObject::disconnect(pendingDetachedConnection);
    QObject::disconnect(pendingShowConnection);

    if (cmdOptions.m_detachedView) {
      mainWindow.showMinimized();
    } else {
      mainWindow.show();
    }
    qInfo() << "MainWindow2 shown";

    for (const auto &request : pendingOpenRequests) {
      mainWindow.openFiles(request.m_files, request.m_detached);
    }
    if (pendingShow) {
      mainWindow.showMainWindow();
    }

    // Let MainWindow show first to decide the screen on which app is running.
    WidgetUtils::calculateScaleFactor(mainWindow.windowHandle()->screen());
    themeService.setBaseBackground(mainWindow.palette().color(QPalette::Base));

    mainWindow.kickOffPostInit(cmdOptions.m_pathsToOpen, cmdOptions.m_detachedView);

    // Run event loop
    ret = app.exec();

    if (ret == kExitToRestart || ret == kExitToApplyUpdate) {
      // Re-acquire the update lease WHILE the SingleInstanceGuard is still
      // held, so no other process can slip in between the guard's release (at
      // scope exit, below) and the swap. If acquisition fails we degrade to a
      // plain restart -- never to an unserialized apply.
      vnotex::UpdateLease::AcquireError leaseError = vnotex::UpdateLease::AcquireError::None;
      lease = vnotex::UpdateLease::acquire(installDir, vnotex::UpdateLease::c_defaultTimeoutMs,
                                           &leaseError);
      if (!lease && ret == kExitToApplyUpdate) {
        qWarning() << "could not acquire the update lease; deferring the update to the next quit";
        vnotex::UpdateInstaller::writeRetryableResult(
            installDir, QStringLiteral("another process held the update lease"));
        ret = kExitToRestart;
      }
    }

    // The explicit guard.exit() that used to live here is redundant: the guard
    // destructor calls it at scope exit, which is also when the QLocalServer it
    // owns must die (it must not outlive QApplication).
    //
    // All services destroyed here before vxcore context.
  } while (false);

  // Cleanup: destroy vxcore context (after all services are destroyed)
  vxcore_context_destroy(context);
  qInfo() << "VxCore context destroyed";

  // =========================================================================
  // Post-scope: services, ConfigMgr2, Application and the guard are all gone,
  // and the vxcore context is destroyed. From here on there is NO qApp, no
  // widgets, no translations, no queued signals, no QtConcurrent, no network,
  // and no pointer into ServiceLocator / ConfigMgr2 / UpdateService remains
  // valid. UpdateInstaller re-reads its plan from pending.json and uses only
  // synchronous QtCore plus Win32.
  // =========================================================================
  if (ret == kExitToApplyUpdate && lease) {
    // Re-probe: a pending update can outlive an OS, driver or filesystem change.
    if (!vnotex::UpdateInstaller::probeAtomicRenameSupport(installDir)) {
      vnotex::UpdateInstaller::writeRetryableResult(
          installDir, QStringLiteral("this system can no longer replace a running program file"));
    } else {
      // QtWebEngine descendants must be gone before anything under the install
      // tree is touched. TimedOut / Error abort BEFORE the first journaled
      // operation, so nothing changes and the update is retried next time.
      const auto waitResult = vnotex::UpdateInstaller::waitForWebEngineChildren(installDir, 30000);
      if (waitResult == vnotex::UpdateInstaller::WaitResult::NoChildren ||
          waitResult == vnotex::UpdateInstaller::WaitResult::Exited) {
        vnotex::UpdateInstaller::applyPending(installDir);
      } else {
        vnotex::UpdateInstaller::writeRetryableResult(
            installDir, vnotex::UpdateInstaller::waitResultToString(waitResult));
      }
    }
  }

  if (ret == kExitToRestart || ret == kExitToApplyUpdate) {
    // Spawn WHILE still holding the lease: the child blocks at its own
    // top-of-main() acquisition, so no third launcher can win both the lease
    // and the already-released guard before the intended replacement exists.
    const bool spawned = QProcess::startDetached(exePath, QStringList());
    if (!spawned) {
      qCritical() << "failed to start the replacement process at" << exePath;
      vnotex::UpdateInstaller::writeSpawnFailure(installDir);
    }

    // exit() does NOT unwind C++ objects, so the lease must be released by hand.
    lease.release();

    // Must use exit() in Linux to quit the parent process in Qt 5.12.
    // Thanks to @ygcaicn.
    exit(0);
  }

  // Rejected starters (Secondary / BusyUnreachable) held the lease through the
  // whole teardown; this is the last statement before returning.
  lease.release();

  return ret;
}
