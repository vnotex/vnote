// main2.cpp - New entry point for VNote clean architecture
// Uses ServiceLocator for dependency injection instead of singletons.

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QSslSocket>
#include <QTextCodec>
#include <QDir>
#include <QTranslator>

#include <core/configmgr2.h>
#include <core/logger.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/configservice.h>
#include <core/services/notebookservice.h>
#include <core/services/searchservice.h>
#include <gui/services/themeservice.h>
#include <core/sessionconfig.h>
#include <core/singleinstanceguard.h>
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

  // Initialize vxcore context
  VxCoreContextHandle context = nullptr;
  VxCoreError err = vxcore_context_create(nullptr, &context);
  if (err != VXCORE_OK || !context) {
    qCritical() << "Failed to create vxcore context:" << vxcore_error_message(err);
    return -1;
  }
  qInfo() << "VxCore context created";

  // Create ServiceLocator
  ServiceLocator serviceLocator;

  // Create and register services (non-owning pointers stored in ServiceLocator)
  ConfigService configService(context);
  NotebookService notebookService(context);
  SearchService searchService(context);

  serviceLocator.registerService<ConfigService>(&configService);
  serviceLocator.registerService<NotebookService>(&notebookService);
  serviceLocator.registerService<SearchService>(&searchService);
  qInfo() << "Services registered";

  // Create ConfigMgr2 with ConfigService
  ConfigMgr2 configMgr(&configService);
  configMgr.init();

  // Create and register ThemeService (needs config values from ConfigMgr2)
  ThemeServiceConfig themeConfig;
  themeConfig.themeName = configMgr.getCoreConfig().getTheme();
  themeConfig.locale = configMgr.getCoreConfig().getLocaleToUse();
  
  // Add theme search paths (same as legacy ThemeMgr setup)
  auto appDataPath = configService.getDataPath(DataLocation::App);
  auto localDataPath = configService.getDataPath(DataLocation::Local);
  themeConfig.themeSearchPaths << localDataPath + "/themes"
                                << appDataPath + "/themes";
  themeConfig.webStylesSearchPaths << localDataPath + "/web_styles"
                                    << appDataPath + "/web_styles";
  
  ThemeService themeService(themeConfig);
  serviceLocator.registerService<ThemeService>(&themeService);
  qInfo() << "ThemeService registered";

  setOpenGLOption(configMgr);

  disableSandboxIfNeeded();

  // Create Qt application
  Application app(argc, argv);

  configMgr.initAfterQtAppStarted();

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
    return 0;
  }

  case CommandLineOptions::HelpRequested:
    Q_FALLTHROUGH();
  default:
    qInfo() << cmdOptions.m_helpText;
    return 0;
  }

  // Guarding.
  SingleInstanceGuard guard;
  bool canRun = guard.tryRun();
  if (!canRun) {
    guard.requestOpenFiles(cmdOptions.m_pathsToOpen);
    guard.requestShow();
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

  // Create MainWindow2 with ServiceLocator
  MainWindow2 mainWindow(serviceLocator);
  mainWindow.show();
  qInfo() << "MainWindow2 shown";

  // Run event loop
  int ret = app.exec();

  // Cleanup: destroy vxcore context
  vxcore_context_destroy(context);
  qInfo() << "VxCore context destroyed";

  return ret;
}
