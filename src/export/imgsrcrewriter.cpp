#include "imgsrcrewriter.h"

#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

using namespace vnotex;

namespace {
// The `([^>]*)` groups around `src` are exactly what preserves every other
// attribute -- including the `width` / `height` of a sized image -- across a
// rewrite.
const char *kImgRegExp =
    "<img ([^>]*)src=\"(?!data:)([^\"]+)\"([^>]*)>"; // image-parser-allow: rendered html only
} // namespace

bool vnotex::rewriteRenderedImgSrc(QString &p_html, QChar p_quote,
                                   const std::function<QString(const QString &)> &p_resolve) {
  bool altered = false;
  QRegularExpression reg(QString::fromLatin1(kImgRegExp));

  int pos = 0;
  while (pos < p_html.size()) {
    QRegularExpressionMatch match;
    const int idx = p_html.indexOf(reg, pos, &match);
    if (idx == -1) {
      break;
    }

    if (match.captured(2).isEmpty()) {
      pos = idx + match.capturedLength();
      continue;
    }

    const QString replacement = p_resolve(match.captured(2));
    if (replacement.isEmpty()) {
      pos = idx + match.capturedLength();
      continue;
    }

    // The replacement is interpolated into markup, so it must not be able to
    // terminate the attribute or the tag. It is not free of user influence:
    // WebUtils::copyResource() derives the copied file's name from the source
    // URL's base name, so an asset literally named `a"><script>…</script>.png`
    // would otherwise inject markup into the exported document -- which is then
    // opened in a browser, or handed to wkhtmltopdf.
    //
    // Refusing is right rather than escaping: every legitimate replacement here
    // is a data URI or a `./resources/<name>` path, neither of which contains a
    // quote or `>`. Escaping would instead produce a valid attribute pointing
    // at a file that does not exist.
    if (replacement.contains(p_quote) || replacement.contains(QLatin1Char('>')) ||
        replacement.contains(QLatin1Char('<'))) {
      qWarning() << "refused to rewrite an <img> src that would break out of the tag"
                 << replacement.left(80);
      pos = idx + match.capturedLength();
      continue;
    }

    const QString newTag =
        QStringLiteral("<img %1src=%2%3%2%4>")
            .arg(match.captured(1), QString(p_quote), replacement, match.captured(3));
    p_html.replace(idx, match.capturedLength(), newTag);
    pos = idx + newTag.size();
    altered = true;
  }

  return altered;
}
