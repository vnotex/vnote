#ifndef APPLICATION_H
#define APPLICATION_H
#include <QApplication>

class QFileSystemWatcher;
class QTimer;

namespace vnotex {

class ThemeService;

class Application : public QApplication {
  Q_OBJECT
public:
  Application(int &p_argc, char **p_argv);

  // Set ThemeService for hot-reload support.
  void setThemeService(ThemeService *p_themeService);

  // Set up theme folder watcher for hot-reload
  void watchThemeFolder(const QString &p_themeFolderPath);

  // Reload the theme resources (stylesheet, icons, etc)
  void reloadThemeResources();

signals:
  void openFileRequested(const QString &p_filePath);

protected:
  bool event(QEvent *p_event) Q_DECL_OVERRIDE;

private:
  ThemeService *m_themeService = nullptr;
  QFileSystemWatcher *m_styleWatcher = nullptr;
  QTimer *m_reloadTimer = nullptr;
};
} // namespace vnotex

#endif // APPLICATION_H
