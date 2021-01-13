#ifndef WEBUTILS_H
#define WEBUTILS_H

#include <QString>

class QUrl;

namespace vnotex
{
    class WebUtils
    {
    public:
        WebUtils() = delete;

        // Remove query in the url (?xxx).
        static QString purifyUrl(const QString &p_url);

        static QString toDataUri(const QUrl &p_url, bool p_keepTitle);

        static QString copyResource(const QUrl &p_url, const QString &p_folder);
    };
}

#endif // WEBUTILS_H
