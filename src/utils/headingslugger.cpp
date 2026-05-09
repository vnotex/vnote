#include "headingslugger.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringLiteral>

using namespace vnotex;

HeadingSlugger::HeadingSlugger() {
}

QString HeadingSlugger::slug(const QString &p_heading) {
  QString base = slugify(p_heading);
  int &count = m_occurrences[base];
  QString result = count == 0 ? base : base + QStringLiteral("-") + QString::number(count);
  ++count;
  return result;
}

void HeadingSlugger::reset() {
  m_occurrences.clear();
}

static bool isInKeepSet(int p_category) {
  switch (p_category) {
  case QChar::Letter_Uppercase:      // 0
  case QChar::Letter_Lowercase:      // 1
  case QChar::Letter_Titlecase:      // 2
  case QChar::Letter_Modifier:       // 3
  case QChar::Letter_Other:          // 4
  case QChar::Mark_NonSpacing:       // 5
  case QChar::Mark_SpacingCombining: // 6
  case QChar::Mark_Enclosing:        // 7
  case QChar::Number_DecimalDigit:   // 8
  case QChar::Number_Letter:         // 10
  case QChar::Number_Other:          // 11
  case QChar::Punctuation_Connector: // 22
    return true;
  default:
    return false;
  }
}

QString HeadingSlugger::slugify(const QString &p_text) {
  QString text = p_text;

  // Step 1: Strip VNote heading sequence numbers.
  static const QRegularExpression headingNumRe(
      QStringLiteral("^\\d{1,3}(?:\\.\\d+)*\\. "));
  auto match = headingNumRe.match(text);
  if (match.hasMatch()) {
    text = text.mid(match.capturedLength());
  }

  // Step 2: Unicode-aware lowercase.
  text = text.toLower();

  // Step 3: Remove characters not in keep-set.
  QString result;
  result.reserve(text.size());
  for (int i = 0; i < text.size(); ++i) {
    QChar ch = text[i];

    // Keep space and hyphen explicitly.
    if (ch == QLatin1Char(' ') || ch == QLatin1Char('-')) {
      result.append(ch);
      continue;
    }

    // Handle surrogate pairs.
    if (ch.isHighSurrogate() && i + 1 < text.size()) {
      QChar low = text[i + 1];
      if (low.isLowSurrogate()) {
        uint ucs4 = QChar::surrogateToUcs4(ch, low);
        if (isInKeepSet(QChar::category(ucs4))) {
          result.append(ch);
          result.append(low);
        }
        ++i; // Skip low surrogate.
        continue;
      }
    }

    // Regular BMP character.
    if (isInKeepSet(ch.category())) {
      result.append(ch);
    }
  }

  // Step 4: Replace spaces with hyphens.
  result.replace(QLatin1Char(' '), QLatin1Char('-'));

  return result;
}
