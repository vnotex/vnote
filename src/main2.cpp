// main2.cpp - New entry point for VNote clean architecture
// Uses ServiceLocator for dependency injection instead of singletons.

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QTextCodec>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configservice.h>
#include <core/services/notebookservice.h>
#include <core/services/searchservice.h>
#include <ui/mainwindow2.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

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

  // Create Qt application
  QApplication app(argc, argv);
  app.setApplicationName("VNote");
  app.setOrganizationName("VNoteX");
  app.setApplicationVersion("4.0.0-alpha");

  // Set application icon
  const QString iconPath = ":/vnotex/data/core/icons/vnote.ico";
  app.setWindowIcon(QIcon(iconPath));

  // Create ServiceLocator
  ServiceLocator serviceLocator;

  // Create and register services (non-owning pointers stored in ServiceLocator)
  ConfigService configService(context);
  NotebookService notebookService(context);
  SearchService searchService(context);

  serviceLocator.registerService<ConfigService>(&configService);
  serviceLocator.registerService<NotebookService>(&notebookService);
  serviceLocator.registerService<SearchService>(&searchService);
  qInfo() << "Services registered: ConfigService, NotebookService, SearchService";

  // Create ConfigMgr2 with ConfigService
  ConfigMgr2 configMgr(&configService);
  configMgr.init();
  qInfo() << "ConfigMgr2 initialized";

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
