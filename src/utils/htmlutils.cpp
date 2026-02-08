#include "htmlutils.h"

#include <QRegularExpression>

using namespace vnotex;

bool HtmlUtils::hasOnlyImgTag(const QString &p_html) {
  // Tricky.
  QRegularExpression reg(QStringLiteral("<(?:p|span|div) "));
  return !p_html.contains(reg);
}

QString HtmlUtils::escapeHtml(QString p_text) {
  // IMPORTANT: Replace & first to avoid double-escaping &lt; and &gt;
  p_text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
  return p_text;
}

QString HtmlUtils::unicodeEncode(const QString &p_text) {
  QString encodedStr;
  for (const auto ch : p_text) {
    if (ch.unicode() > 255) {
      encodedStr += QStringLiteral("&#%1;").arg(static_cast<int>(ch.unicode()));
    } else {
      encodedStr += ch;
    }
  }

  return encodedStr;
}
