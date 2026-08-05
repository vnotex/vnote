#include "application.h"

#include <QDebug>
#include <QDir>
#include <QFileOpenEvent>
#include <QFileSystemWatcher>
#include <QStyle>
#include <QTimer>

#include <gui/services/themeservice.h>

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>
#endif

using namespace vnotex;

#ifdef Q_OS_MACOS
// Native provider for the "Create Note in VNote" system Service declared under
// NSServices in the bundle Info.plist. It performs NO validation of its own:
// everything but the AppKit pasteboard extraction goes through
// Application::dispatchCaptureText, so the native path cannot drift from the
// tested contract.
@interface VNoteServiceProvider : NSObject
@property(nonatomic, assign) vnotex::Application *app;
- (void)createNoteFromSelection:(NSPasteboard *)p_pboard
                       userData:(NSString *)p_userData
                          error:(NSString **)p_error;
@end

@implementation VNoteServiceProvider

- (void)createNoteFromSelection:(NSPasteboard *)p_pboard
                       userData:(NSString *)p_userData
                          error:(NSString **)p_error {
  Q_UNUSED(p_userData);

  // The Service declares NSSendTypes public.utf8-plain-text; read the matching
  // modern pasteboard type.
  NSString *selection = [p_pboard stringForType:NSPasteboardTypeString];

  // A null QString for a missing selection: dispatchCaptureText rejects it.
  QString text;
  if (selection) {
    text = QString::fromNSString(selection);
  }

  vnotex::Application *app = self.app;
  QString errorMessage;
  const bool accepted =
      vnotex::Application::dispatchCaptureText(text, errorMessage, [app](const QString &p_text) {
        // Qt window activation alone does not reliably foreground an app that
        // was invoked from another process.
        [NSApp activateIgnoringOtherApps:YES];
        emit app->captureNoteRequested(p_text);
      });

  if (!accepted && p_error) {
    *p_error = errorMessage.toNSString();
  }
}

@end
#endif // Q_OS_MACOS

Application::Application(int &p_argc, char **p_argv) : QApplication(p_argc, p_argv) {}

Application::~Application() {
#ifdef Q_OS_MACOS
  if (m_serviceProvider) {
    [NSApp setServicesProvider:nil];
    VNoteServiceProvider *provider = static_cast<VNoteServiceProvider *>(m_serviceProvider);
    m_serviceProvider = nullptr;
#if !__has_feature(objc_arc)
    [provider release];
#else
    (void)provider;
#endif
  }
#endif
}

void Application::registerServiceProvider() {
#ifdef Q_OS_MACOS
  if (m_serviceProvider) {
    return;
  }

  VNoteServiceProvider *provider = [[VNoteServiceProvider alloc] init];
  provider.app = this;

  // Apple documents that requests may be delivered as soon as the provider is
  // registered, so callers must have connected captureNoteRequested first.
  [NSApp setServicesProvider:provider];
  NSUpdateDynamicServices();

  // Manual retain/release: the project does not compile with ARC. The provider
  // stays alive for the whole Application lifetime.
  m_serviceProvider = static_cast<void *>(provider);
#endif
}

void Application::setThemeService(ThemeService *p_themeService) { m_themeService = p_themeService; }

void Application::watchThemeFolder(const QString &p_themeFolderPath) {
  if (p_themeFolderPath.isEmpty()) {
    return;
  }

  // Initialize watchers only when needed
  if (!m_styleWatcher) {
    m_styleWatcher = new QFileSystemWatcher(this);
  }
  if (!m_reloadTimer) {
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(500); // 500ms debounce delay
    connect(m_reloadTimer, &QTimer::timeout, this, &Application::reloadThemeResources);

    // Connect file watcher to timer
    connect(m_styleWatcher, &QFileSystemWatcher::directoryChanged, m_reloadTimer,
            qOverload<>(&QTimer::start));
    connect(m_styleWatcher, &QFileSystemWatcher::fileChanged, m_reloadTimer,
            qOverload<>(&QTimer::start));
  }

  // Watch the theme folder and its files
  m_styleWatcher->addPath(p_themeFolderPath);

  // Also watch individual files in the theme folder
  QDir themeDir(p_themeFolderPath);
  QStringList files = themeDir.entryList(QDir::Files);
  for (const QString &file : files) {
    m_styleWatcher->addPath(themeDir.filePath(file));
  }
}

void Application::reloadThemeResources() {
  if (!m_themeService) {
    qWarning() << "ThemeService not set, cannot reload theme resources";
    return;
  }

  m_themeService->refreshCurrentTheme();
}

bool Application::event(QEvent *p_event) {
  // On macOS, we need this to open file from Finder.
  if (p_event->type() == QEvent::FileOpen) {
    QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(p_event);
    qDebug() << "request to open file" << openEvent->file();
    emit openFileRequested(openEvent->file());
  }

  return QApplication::event(p_event);
}
