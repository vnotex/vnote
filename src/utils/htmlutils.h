#ifndef HTMLUTILS_H
#define HTMLUTILS_H

#include <QString>

namespace vnotex
{
    class HtmlUtils
    {
    public:
        HtmlUtils() = delete;

        static bool hasOnlyImgTag(const QString &p_html);
    };
}

#endif // HTMLUTILS_H
