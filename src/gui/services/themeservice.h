#ifndef THEMESERVICE_H
#define THEMESERVICE_H

#include <QColor>
#include <QObject>
#include <QPixmap>
#include <QPair>
#include <QScopedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/noncopyable.h"
#include "core/theme.h"

namespace vnotex {

// Configuration for ThemeService initialization via DI.
struct ThemeServiceConfig {
  // Current theme to load.
  QString themeName;

  // Locale for display names (e.g., "en_US").
  QString locale;

  QString appDataPath;
};

// GUI-aware service for theme management.
// Provides full Qt GUI API (QColor, QPixmap).
// Uses constructor DI instead of singletons.
class ThemeService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  struct ThemeInfo {
    // Theme identifier.
    QString m_name;

    // Localized display name.
    QString m_displayName;

    // Path to theme folder.
    QString m_folderPath;
  };

  // Constructor receives config via DI.
  explicit ThemeService(const ThemeServiceConfig &p_config, QObject *p_parent = nullptr);

  // Get icon file path. If @p_icon is a path, returns it directly.
  // Otherwise searches current theme, then falls back to resource.
  QString getIconFile(const QString &p_icon) const;

  // Fetch Qt stylesheet from current theme.
  QString fetchQtStyleSheet() const;

  // Get palette color by name from current theme.
  QString paletteColor(const QString &p_name) const;

  // Get file path of specified file type from current theme.
  QString getFile(Theme::File p_fileType) const;

  // Get editor highlight theme (file path or theme name).
  QString getEditorHighlightTheme() const;

  // Get Markdown editor highlight theme (file path or theme name).
  QString getMarkdownEditorHighlightTheme() const;

  // Get base background color (GUI type).
  const QColor &getBaseBackground() const;

  // Set base background color (GUI type).
  void setBaseBackground(const QColor &p_bg);

  // Get all available themes.
  const QVector<ThemeInfo> &getAllThemes() const;

  // Get current theme.
  const Theme &getCurrentTheme() const;

  // Get theme preview image (GUI type).
  QPixmap getThemePreview(const QString &p_name) const;

  // Find theme info by name. Returns nullptr if not found.
  const ThemeInfo *findTheme(const QString &p_name) const;

  // Refresh themes list and reload current theme.
  void refresh();

  // Reload current theme.
  void refreshCurrentTheme();

  // Get all web stylesheets <DisplayName, FilePath>.
  QVector<QPair<QString, QString>> getWebStyles() const;

private:
  void loadAvailableThemes();

  void loadThemes(const QString &p_path);

  void checkAndAddThemeFolder(const QString &p_folder);

  void loadCurrentTheme(const QString &p_themeName);

  Theme *loadTheme(const QString &p_themeFolder);

  QString findThemeFolder(const QString &p_name) const;

  // Locale for display names.
  QString m_locale;

  // Paths to search for themes (instance member, not static).
  QStringList m_themeSearchPaths;

  // Paths to search for web styles (instance member, not static).
  QStringList m_webStylesSearchPaths;

  // Available themes.
  QVector<ThemeInfo> m_themes;

  // Current theme.
  QScopedPointer<Theme> m_currentTheme;

  // Base background color (GUI type).
  QColor m_baseBackground;
};

} // namespace vnotex

#endif // THEMESERVICE_H
