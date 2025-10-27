#ifndef UTILS_H
#define UTILS_H

#include <QCoreApplication>
#include <QDateTime>
#include <QPixmap>
#include <QString>

#if !defined(V_ASSERT)
#define V_ASSERT(cond) ((!(cond)) ? qt_assert(#cond, __FILE__, __LINE__) : qt_noop())
#endif

// Thanks to CGAL/cgal.
#ifndef __has_attribute
#define __has_attribute(x) 0 // Compatibility with non-clang compilers.
#endif

#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0 // Compatibility with non-supporting compilers.
#endif

class QWidget;
class QJsonObject;

namespace vnotex {
class Utils {
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

  static QPixmap svgToPixmap(const QByteArray &p_content, QRgb p_background, qreal p_scaleFactor);

  static bool fuzzyEqual(qreal p_a, qreal p_b);

  static QString boolToString(bool p_val);

  static QString intToString(int p_val, int p_width = 0);

  static QByteArray toJsonString(const QJsonObject &p_obj);

  static QJsonObject fromJsonString(const QByteArray &p_data);

  // Parse @p_exp into tokens and read the target value from @p_obj.
  // Format: obj1.obj2.arr[2].obj3.
  static QJsonValue parseAndReadJson(const QJsonObject &p_obj, const QString &p_exp);

  static QColor toColor(const QString &p_color);

  static QStringList toLower(const QStringList &p_list);
};
} // namespace vnotex

#endif // UTILS_H
