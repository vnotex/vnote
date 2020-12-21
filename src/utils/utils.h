#ifndef UTILS_H
#define UTILS_H

#include <QString>
#include <QDateTime>
#include <QCoreApplication>
#include <QPixmap>

#if !defined(V_ASSERT)
    #define V_ASSERT(cond) ((!(cond)) ? qt_assert(#cond, __FILE__, __LINE__) : qt_noop())
#endif

// Thanks to CGAL/cgal.
#ifndef __has_attribute
    #define __has_attribute(x) 0  // Compatibility with non-clang compilers.
#endif

#ifndef __has_cpp_attribute
  #define __has_cpp_attribute(x) 0  // Compatibility with non-supporting compilers.
#endif

class QWidget;

namespace vnotex
{
    class Utils
    {
    public:
        Utils() = delete;

        static void sleepWait(int p_milliseconds);

        // Append @p_new to @p_msg as a new line.
        static void appendMsg(QString &p_msg, const QString &p_new);

        static QString dateTimeString(const QDateTime &p_dateTime);

        static QString dateTimeStringUniform(const QDateTime &p_dateTime);

        static QDateTime dateTimeFromStringUniform(const QString &p_str);

        static QChar keyToChar(int p_key, bool p_lowerCase);

        static QString pickAvailableFontFamily(const QStringList &p_families);

        static QPixmap svgToPixmap(const QByteArray &p_content,
                                   QRgb p_background,
                                   qreal p_scaleFactor);

        static bool fuzzyEqual(qreal p_a, qreal p_b);

        static QString boolToString(bool p_val);
    };
} // ns vnotex

#endif // UTILS_H
