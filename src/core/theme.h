#ifndef THEME_H
#define THEME_H

#include <QString>
#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QPixmap>

namespace tests
{
    class TestTheme;
}

namespace vnotex
{
    class Theme
    {
    public:
        enum class File
        {
            Palette = 0,
            QtStyleSheet,
            WebStyleSheet,
            HighlightStyleSheet,
            TextEditorStyle,
            MarkdownEditorStyle,
            EditorHighlightStyle,
            MarkdownEditorHighlightStyle,
            Cover,
            Max
        };

        QString fetchQtStyleSheet() const;

        QString paletteColor(const QString &p_name) const;

        // Get the file path of @p_fileType if exists.
        QString getFile(File p_fileType) const;

        // Return the file path of the theme or just the theme name.
        QString getEditorHighlightTheme() const;

        // Return the file path of the theme or just the theme name.
        QString getMarkdownEditorHighlightTheme() const;

        QString name() const;

        static bool isValidThemeFolder(const QString &p_folder);

        static Theme *fromFolder(const QString &p_folder);

        static QString getDisplayName(const QString &p_folder, const QString &p_locale);

        static QPixmap getCover(const QString &p_folder);

    private:
        struct Metadata
        {
            int m_revision = 0;

            // Name of the theme for editor syntax highlight.
            // Will be ignored if EditorHighlightStyle file exists within the theme.
            QString m_editorHighlightTheme;

            // Use for MarkdownEditor code block highlight.
            // If not specified, will use m_editorHighlightTheme.
            QString m_markdownEditorHighlightTheme;
        };

        typedef QJsonObject Palette;

        Theme(const QString &p_themeFolderPath,
              const Metadata &p_metadata,
              const QJsonObject &p_palette);

        QString m_themeFolderPath;

        Theme::Metadata m_metadata;

        Palette m_palette;

        static Metadata readMetadata(const QJsonObject &p_obj);

        static Theme::Palette translatePalette(const QJsonObject &p_obj);

        static void translatePaletteObject(const Palette &p_palette,
                                           QJsonObject &p_obj,
                                           const QString &p_key);

        // Translate p_obj[p_key] by looking up @p_palette.
        // Return <changed, unresolvedRefs>.
        static QPair<bool, int> translatePaletteObjectOnce(const Palette &p_palette,
                                                           QJsonObject &p_obj,
                                                           const QString &p_key);

        static QJsonValue findValueByKeyPath(const Palette &p_palette, const QString &p_keyPath);

        static void translateStyleByPalette(const Palette &p_palette, QString &p_style);

        static void translateUrlToAbsolute(const QString &p_basePath, QString &p_style);

        // Font-family in QSS only supports specifying one font, not a list.
        // Thus we need to choose one available font from the list.
        static void translateFontFamilyList(QString &p_style);

        static void translateScaledSize(qreal p_factor, QString &p_style);

        static QJsonObject readJsonFile(const QString &p_filePath);

        static QJsonObject readPaletteFile(const QString &p_folder);

        // Whether @p_str is a reference definition like "@xxxx".
        static bool isRef(const QString &p_str);

        static QString getFileName(File p_fileType);

        friend class tests::TestTheme;
    };
} // ns vnotex

#endif // THEME_H
