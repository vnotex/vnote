#ifndef VWEBUTILS_H
#define VWEBUTILS_H

#include <QUrl>
#include <QString>


class VWebUtils
{
public:
    // Fix <img src> in @p_html.
    static bool fixImageSrcInHtml(const QUrl &p_baseUrl, QString &p_html);

    // Remove background color style in @p_html.
    static bool removeBackgroundColor(QString &p_html);

    // Translate color styles in @p_html using mappings from VPalette.
    static bool translateColors(QString &p_html);

private:
    VWebUtils();
};

#endif // VWEBUTILS_H
