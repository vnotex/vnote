#ifndef THEMESERVICE_H
#define THEMESERVICE_H

#include <QColor>
#include <QObject>
#include <QPair>
#include <QScopedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/noncopyable.h"
#include "core/theme.h"

namespace vnotex {

class HookManager;

// Configuration for ThemeService initialization via DI.
struct ThemeServiceConfig {
  // Current theme to load.
  QString themeName;

  // Locale for display names (e.g., "en_US").
  QString locale;

  QString appDataPath;
};

// GUI-aware service for theme management.
// Provides full Qt GUI API (QColor, etc.).
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

  QString fetchWebStyleSheet() const;
  QString fetchTextEditorStyle() const;

  // Get palette color by name from current theme.
  QString paletteColor(const QString &p_name) const;

  // Probe for an OPTIONAL palette color: returns an empty string without
  // warning when the current theme does not define the token. Use this (and
  // mark the literal `// palette-token-optional: <reason>`) when the call site
  // has its own fallback.
  QString optionalPaletteColor(const QString &p_name) const;

  // Resolve one semantic comment-highlight token (see CommentColor) to a real
  // CSS color.
  //
  // Highlights are painted ON the rendered PDF page, which pdf.js draws from
  // the document itself and is therefore white (or whatever the PDF says)
  // regardless of the application theme. The defaults are consequently anchored
  // to the page, not to the palette — a "yellow" highlight has to stay readable
  // on paper in all 12 themes. A theme MAY still override any token by defining
  // `widgets.pdfcomment.<token>` in its palette.json; when it does not, the
  // built-in translucent default is used.
  //
  // Returns a fully RESOLVED color, never an unresolved `@palette#`/`@base#`
  // token: an unresolved token would be dropped by the CSS parser and the
  // highlight would render invisible.
  QString commentHighlightColor(const QString &p_token) const;

  // A `:root { --vx-comment-<token>: <color>; ... }` block covering EVERY token
  // in CommentColor::all(), for injection into the PDF viewer template.
  // pdfviewer.css references only these custom properties, so it needs no
  // literal color of its own and follows a theme switch for free (the template
  // is force-regenerated and the page reloaded on themeChanged).
  QString commentHighlightCssVariables() const;

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

  // Find theme info by name. Returns nullptr if not found.
  const ThemeInfo *findTheme(const QString &p_name) const;

  // Refresh themes list and reload current theme.
  void refresh();

  // Reload current theme.
  void refreshCurrentTheme();

  // Switch to a different theme by name.
  void switchTheme(const QString &p_name);

  // Get all rendering (web) stylesheets <DisplayName, FilePath>: each theme's web.css plus any
  // loose css found under the web_styles search paths.
  QVector<QPair<QString, QString>> getWebStyles() const;

  // Get all syntax-highlight stylesheets <DisplayName, FilePath>: ONLY each theme's highlight.css
  // (holds the Prism .token colors). Loose web_styles/*.css are intentionally excluded so every
  // listed option is a real highlight.css (see ExportStyleResolver's basename contract).
  QVector<QPair<QString, QString>> getSyntaxStyles() const;

  // Set the HookManager for firing theme hooks.
  void setHookManager(HookManager *p_hookMgr);

signals:
  // Emitted before a theme load begins (switch or refresh).
  void themeAboutToChange(const QString &p_themeName);

  // Emitted after a theme is loaded (switched or refreshed).
  void themeChanged(const QString &p_themeName);

private:
  void loadAvailableThemes();

  void loadThemes(const QString &p_path);

  void checkAndAddThemeFolder(const QString &p_folder);

  void loadCurrentTheme(const QString &p_themeName);

  Theme *loadTheme(const QString &p_themeFolder);

  QString findThemeFolder(const QString &p_name) const;

  // Collect one entry per theme for the given theme file type; when p_includeSearchPathCss is
  // true, also append any loose *.css found under the web_styles search paths.
  QVector<QPair<QString, QString>> collectThemeStyles(Theme::File p_themeFileType,
                                                      bool p_includeSearchPathCss) const;

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

  // Hook manager for firing theme hooks (not owned).
  HookManager *m_hookMgr = nullptr;
};

} // namespace vnotex

#endif // THEMESERVICE_H
