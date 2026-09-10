#ifndef SECTIONNUMBERUTILS_H
#define SECTIONNUMBERUTILS_H

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace vnotex {
class SectionNumberUtils {
public:
  struct Analysis {
    int m_firstNumberedHeading = -1;
    int m_baseLevel = 0;
    bool m_skip = true;
  };

  template <class T> static Analysis analyze(const QVector<T> &p_headings) {
    Analysis result;
    int first = -1;
    int h1Count = 0;
    for (int i = 0; i < p_headings.size(); ++i) {
      const auto &heading = p_headings[i];
      if (heading.m_isPlaceholder || heading.m_level <= 0) {
        continue;
      }
      if (first < 0) {
        first = i;
      }
      if (heading.m_level == 1) {
        ++h1Count;
      }
    }
    if (first < 0) {
      return result;
    }
    const bool exemptTitle = p_headings[first].m_level == 1 && h1Count == 1;
    int sampled = 0;
    for (int i = first + (exemptTitle ? 1 : 0); i < p_headings.size(); ++i) {
      const auto &heading = p_headings[i];
      if (heading.m_isPlaceholder || heading.m_level <= 0) {
        continue;
      }
      if (result.m_firstNumberedHeading < 0) {
        result.m_firstNumberedHeading = i;
        result.m_baseLevel = heading.m_level;
      } else {
        result.m_baseLevel = qMin(result.m_baseLevel, heading.m_level);
      }
      if (sampled < 5) {
        ++sampled;
        if (!hasSectionNumberPrefix(heading.m_name)) {
          result.m_skip = false;
        }
      }
    }
    return result;
  }

  static bool hasSectionNumberPrefix(const QString &p_text) {
    static const QRegularExpression prefix(
        QStringLiteral("^\\s*[0-9]+(?:\\.[0-9]+)*[.)]?(?:\\s+|$)"),
        QRegularExpression::UseUnicodePropertiesOption);
    return prefix.match(p_text).hasMatch();
  }

  static const QStringList &getSupportedPatterns() {
    static const QStringList patterns{QStringLiteral("1.1"), QStringLiteral("1.1."),
                                      QStringLiteral("1.1)")};
    return patterns;
  }

  static QString normalizePattern(const QString &p_pattern) {
    return getSupportedPatterns().contains(p_pattern) ? p_pattern : QStringLiteral("1.1.");
  }

  static void increaseSectionNumber(QVector<int> &p_numbers, int p_level, int p_baseLevel) {
    Q_ASSERT(p_baseLevel >= 1 && p_level >= p_baseLevel && p_level < p_numbers.size());
    for (int i = p_baseLevel; i < p_level; ++i) {
      if (p_numbers[i] == 0) {
        p_numbers[i] = 1;
      }
    }
    ++p_numbers[p_level];
    for (int i = p_level + 1; i < p_numbers.size(); ++i) {
      p_numbers[i] = 0;
    }
  }

  static QString joinSectionNumber(const QVector<int> &p_numbers, const QString &p_pattern) {
    QString result;
    for (auto number : p_numbers) {
      if (number != 0) {
        if (!result.isEmpty()) {
          result += QLatin1Char('.');
        }
        result += QString::number(number);
      } else if (!result.isEmpty()) {
        break;
      }
    }
    if (!result.isEmpty()) {
      if (p_pattern == QStringLiteral("1.1)")) {
        result += QLatin1Char(')');
      } else if (p_pattern != QStringLiteral("1.1")) {
        result += QLatin1Char('.');
      }
    }
    return result;
  }
};
} // namespace vnotex

#endif // SECTIONNUMBERUTILS_H
