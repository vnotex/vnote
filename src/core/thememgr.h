#ifndef THEMEMGR_H
#define THEMEMGR_H

#include <QObject>

#include <QString>
#include <QHash>
#include <QScopedPointer>
#include <QStringList>
#include <QColor>

#include "theme.h"

namespace vnotex
{
    class ThemeMgr : public QObject
    {
        Q_OBJECT
    public:
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

        static void addSearchPath(const QString &p_path);

        static void addSyntaxHighlightingSearchPaths(const QStringList &p_paths);

    private:
        void loadAvailableThemes();

        void loadThemes(const QString &p_path);

        void checkAndAddThemeFolder(const QString &p_folder);

        const Theme &getCurrentTheme() const;

        void loadCurrentTheme(const QString &p_themeName);

        Theme *loadTheme(const QString &p_themeFolder);

        QString findThemeFolder(const QString &p_name) const;

        // Theme name to folder path mapping.
        QHash<QString, QString> m_availableThemes;

        QScopedPointer<Theme> m_currentTheme;

        // Background of the main window.
        // Set at runtime, not from the theme config.
        QColor m_baseBackground;

        // List of path to search for themes.
        static QStringList s_searchPaths;
    };
} // ns vnotex

#endif // THEMEMGR_H
