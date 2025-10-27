#ifndef HTMLUTILS_H
#define HTMLUTILS_H

#include <QString>

namespace vnotex {
class HtmlUtils {
public:
  HtmlUtils() = delete;

  static bool hasOnlyImgTag(const QString &p_html);

  static QString escapeHtml(QString p_text);

  static QString unicodeEncode(const QString &p_text);
};
} // namespace vnotex

#endif // HTMLUTILS_H
