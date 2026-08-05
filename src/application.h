#ifndef APPLICATION_H
#define APPLICATION_H
#include <QApplication>
#include <QCoreApplication>
#include <QString>

#include <functional>

class QFileSystemWatcher;
class QTimer;

namespace vnotex {

class ThemeService;

class Application : public QApplication {
  Q_OBJECT
public:
  Application(int &p_argc, char **p_argv);

  ~Application() override;

  // Set ThemeService for hot-reload support.
  void setThemeService(ThemeService *p_themeService);

  // Set up theme folder watcher for hot-reload
  void watchThemeFolder(const QString &p_themeFolderPath);

  // Reload the theme resources (stylesheet, icons, etc)
  void reloadThemeResources();

  // Register the native macOS Services provider ("Create Note in VNote").
  // No-op on other platforms. MUST be called only after captureNoteRequested
  // has a connected receiver: Apple may dispatch a request to the provider
  // immediately after registration.
  void registerServiceProvider();

  // Platform-independent validation/dispatch seam shared with the native macOS
  // Service provider, which supplies the already-converted (possibly null)
  // selection text.
  //
  // Missing/null or whitespace-only input returns false, writes a non-empty
  // @p_error and never invokes @p_callback. Accepted input clears @p_error,
  // invokes @p_callback exactly once with the ORIGINAL unmodified text (never
  // trimmed) and returns true. An absent callback is a programming error and is
  // reported as a failure rather than a silent success, so a true return always
  // means the request was actually dispatched.
  static bool dispatchCaptureText(const QString &p_text, QString &p_error,
                                  const std::function<void(const QString &)> &p_callback) {
    if (p_text.isNull() || p_text.trimmed().isEmpty()) {
      p_error = QCoreApplication::translate("vnotex::Application",
                                            "Select some text to create a note in VNote.");
      return false;
    }

    if (!p_callback) {
      p_error = QCoreApplication::translate("vnotex::Application",
                                            "VNote is not ready to capture a note yet.");
      return false;
    }

    p_error.clear();
    p_callback(p_text);
    return true;
  }

signals:
  void openFileRequested(const QString &p_filePath);

  // Emitted when the macOS "Create Note in VNote" Service is invoked on a
  // non-empty text selection. The text is delivered verbatim.
  void captureNoteRequested(const QString &p_text);

protected:
  bool event(QEvent *p_event) Q_DECL_OVERRIDE;

private:
  ThemeService *m_themeService = nullptr;
  QFileSystemWatcher *m_styleWatcher = nullptr;
  QTimer *m_reloadTimer = nullptr;

  // Retained Objective-C service-provider instance (macOS only). Held as void*
  // so the header stays free of AppKit types.
  void *m_serviceProvider = nullptr;
};
} // namespace vnotex

#endif // APPLICATION_H
