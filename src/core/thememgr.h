#ifndef THEMEMGR_H
#define THEMEMGR_H

#include <QObject>

#include <QString>
#include <QScopedPointer>
#include <QStringList>
#include <QVector>
#include <QColor>
#include <QPixmap>

#include "theme.h"

namespace vnotex
{
    class ThemeMgr : public QObject
    {
        Q_OBJECT
    public:
        struct ThemeInfo
        {
            // Id.
            QString m_name;

            // Locale supported.
            QString m_displayName;

            QString m_folderPath;
        };

        ThemeMgr(const QString &p_currentThemeName, QObject *p_parent = nullptr);

        // @p_icon: file path or file name of the icon.
        // If @p_icon is a path, just return it.
        // Otherwise, first try to find it in current theme, and if not found,
        // find it in resource file.
        // Return the path of the icon to use.
        QString getIconFile(const QString &p_icon) const;

        QString fetchQtStyleSheet() const;

        QString paletteColor(const QString &p_name) const;

        QString getFile(Theme::File p_fileType) const;

        // Return the file path of the theme or just the theme name.
        QString getEditorHighlightTheme() const;

        QString getMarkdownEditorHighlightTheme() const;

        const QColor &getBaseBackground() const;
        void setBaseBackground(const QColor &p_bg);

        const QVector<ThemeInfo> &getAllThemes() const;

        const Theme &getCurrentTheme() const;

        QPixmap getThemePreview(const QString &p_name) const;

        const ThemeInfo *findTheme(const QString &p_name) const;

        // Refresh the themes list.
        // Won't affect current theme since we do not support changing theme real time for now.
        void refresh();

        // Return all web stylesheets available, including those from themes and web styles search paths.
        // <DisplayName, FilePath>.
        QVector<QPair<QString, QString>> getWebStyles() const;

        static void addSearchPath(const QString &p_path);

        static void addSyntaxHighlightingSearchPaths(const QStringList &p_paths);

        static void addWebStylesSearchPath(const QString &p_path);

    private:
        void loadAvailableThemes();

        void loadThemes(const QString &p_path);

        void checkAndAddThemeFolder(const QString &p_folder, const QString &p_locale);

        void loadCurrentTheme(const QString &p_themeName);

        Theme *loadTheme(const QString &p_themeFolder);

        QString findThemeFolder(const QString &p_name) const;

        QVector<ThemeInfo> m_themes;

        QScopedPointer<Theme> m_currentTheme;

        // Background of the main window.
        // Set at runtime, not from the theme config.
        QColor m_baseBackground;

        // List of paths to search for themes.
        static QStringList s_searchPaths;

        // List of paths to search for CSS styles, including CSS syntax highlighting styles.
        static QStringList s_webStylesSearchPaths;
    };
} // ns vnotex

#endif // THEMEMGR_H
