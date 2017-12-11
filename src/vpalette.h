#ifndef VPALETTE_H
#define VPALETTE_H

#include <QString>
#include <QHash>
#include <QMap>

class QSettings;

struct VPaletteMetaData
{
    // These are all file PATH, not name.
    QString m_qssFile;
    QString m_mdhlFile;
    QString m_cssFile;
    QString m_codeBlockCssFile;

    QString toString() const
    {
        return QString("palette metadata qss=%1 mdhl=%2 css=%3 codeBlockCss=%4")
                      .arg(m_qssFile)
                      .arg(m_mdhlFile)
                      .arg(m_cssFile)
                      .arg(m_codeBlockCssFile);
    }
};


class VPalette
{
public:
    explicit VPalette(const QString &p_file);

    QString color(const QString &p_name) const;

    // Read QSS file.
    QString fetchQtStyleSheet() const;

    // Fill "@xxx" in @p_text with corresponding style.
    void fillStyle(QString &p_text) const;

    // Read themes and return the mappings of editor styles.
    static QMap<QString, QString> editorStylesFromThemes(const QList<QString> &p_themeFiles);

    // Read themes and return the mappings of css styles.
    static QMap<QString, QString> cssStylesFromThemes(const QList<QString> &p_themeFiles);

    // Read themes and return the mappings of css styles.
    static QMap<QString, QString> codeBlockCssStylesFromThemes(const QList<QString> &p_themeFiles);

    static VPaletteMetaData getPaletteMetaData(const QString &p_paletteFile);

    // Return the name of the theme.
    static QString themeName(const QString &p_paletteFile);

    // Return the name of the editor style of the theme.
    static QString themeEditorStyle(const QString &p_paletteFile);

    // Return the name of the css style of the theme.
    static QString themeCssStyle(const QString &p_paletteFile);

    // Return the name of the css style of the theme.
    static QString themeCodeBlockCssStyle(const QString &p_paletteFile);

private:
    void init(const QString &p_file);

    void initPaleteFromSettings(QSettings *p_settings, const QString &p_group);

    void fillAbsoluteUrl(QString &p_style) const;

    // File path of the palette file.
    QString m_file;

    QHash<QString, QString> m_palette;

    VPaletteMetaData m_data;
};

#endif // VPALETTE_H
