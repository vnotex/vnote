#ifndef VWEBUTILS_H
#define VWEBUTILS_H

#include <QUrl>
#include <QString>


class VWebUtils
{
public:
    // Fix <img src> in @p_html.
    static bool fixImageSrcInHtml(const QUrl &p_baseUrl, QString &p_html);

private:
    VWebUtils();
};

#endif // VWEBUTILS_H
