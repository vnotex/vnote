#include "imageuploadplaceholder.h"

#include <vtextedit/markdownutils.h>

using namespace vnotex;

namespace {
// The span of the placeholder link containing the marker for @p_token, or false.
bool placeholderSpan(const QString &p_content, int p_token, int &p_start, int &p_end) {
  const QString marker = QStringLiteral("vnote-upload-%1").arg(p_token);
  const int idx = p_content.indexOf(marker);
  if (idx < 0) {
    return false;
  }
  const int linkStart = p_content.lastIndexOf(QStringLiteral("!["), idx);
  if (linkStart < 0) {
    return false;
  }
  const int linkEnd = p_content.indexOf(QLatin1Char(')'), idx);
  if (linkEnd < 0) {
    return false;
  }

  p_start = linkStart;
  p_end = linkEnd + 1;
  return true;
}
} // namespace

QString ImageUploadPlaceholder::generate(int p_token, const QString &p_fileName) {
  return QStringLiteral("![Uploading %1...](vnote-upload-%2)").arg(p_fileName).arg(p_token);
}

QString ImageUploadPlaceholder::replace(const QString &p_content, int p_token,
                                        const QString &p_realUrl, const QString &p_title,
                                        int p_width, int p_height) {
  int start = 0;
  int end = 0;
  if (!placeholderSpan(p_content, p_token, start, end)) {
    return p_content;
  }

  QString result = p_content;
  result.replace(
      start, end - start,
      vte::MarkdownUtils::generateImageLink(p_title, p_realUrl, QString(), p_width, p_height));
  return result;
}

QString ImageUploadPlaceholder::remove(const QString &p_content, int p_token) {
  int start = 0;
  int end = 0;
  if (!placeholderSpan(p_content, p_token, start, end)) {
    return p_content;
  }

  QString result = p_content;
  if (end < result.size() && result.at(end) == QLatin1Char('\n')) {
    ++end;
  }
  result.remove(start, end - start);
  return result;
}
