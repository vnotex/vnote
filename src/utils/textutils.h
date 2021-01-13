#ifndef TEXTUTILS_H
#define TEXTUTILS_H

#include <QString>

namespace vnotex
{
    class TextUtils
    {
    public:
        TextUtils() = delete;

        static int firstNonSpace(const QString &p_text);

        static QString removeCodeBlockFence(const QString &p_text);

        static int fetchIndentation(const QString &p_text);

        static QString unindentText(const QString &p_text, int p_spaces);

        // Unindent multi-lines text according to the indentation of the first line.
        static QString unindentTextMultiLines(const QString &p_text);
    };
}

#endif // TEXTUTILS_H
