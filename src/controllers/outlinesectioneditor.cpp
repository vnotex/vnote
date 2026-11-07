#include "outlinesectioneditor.h"

#include <QRegularExpression>
#include <QStringView>
#include <QTextCursor>
#include <QTextDocument>

#include <limits>

using namespace vnotex;

namespace {
struct HeadingMarkerReplacement {
  int m_start = -1;
  int m_end = -1;
  QString m_text;
};

QRegularExpressionMatch matchAtxHeading(QStringView p_source) {
  static const QRegularExpression pattern(
      QStringLiteral("\\A( {0,3})(#{1,6})(?=[\\t ]|$)([\\t ]*)"));
  return pattern.matchView(p_source);
}

QRegularExpressionMatch matchSetextUnderline(QStringView p_source) {
  static const QRegularExpression pattern(QStringLiteral("\\A( {0,3})(=+|-+)([\\t ]*)\\z"));
  return pattern.matchView(p_source);
}

// cmark can finalize a Setext span on the following blank/prose line. Locate its
// underline only within the certified span, retaining everything after that line
// outside title/marker replacements. validHeadings() still rejects raw overlaps.
QStringView markdownHeadingSource(QStringView p_source) {
  const int firstBreak = p_source.indexOf(QLatin1Char('\n'));
  if (firstBreak < 0) {
    return p_source;
  }
  if (matchAtxHeading(p_source.left(firstBreak)).hasMatch()) {
    return p_source.left(firstBreak);
  }
  for (int start = firstBreak + 1; start < p_source.size();) {
    const int nextBreak = p_source.indexOf(QLatin1Char('\n'), start);
    const int end = nextBreak < 0 ? p_source.size() : nextBreak;
    if (matchSetextUnderline(p_source.mid(start, end - start)).hasMatch()) {
      return p_source.left(end);
    }
    start = end + 1;
  }
  return p_source;
}

int boundaryLineBreaks(QStringView p_text, bool p_fromStart) {
  int count = 0;
  for (int i = p_fromStart ? 0 : p_text.size() - 1; i >= 0 && i < p_text.size() && count < 2;
       i += p_fromStart ? 1 : -1) {
    const auto ch = p_text[i];
    if (ch == QLatin1Char('\n')) {
      ++count;
    } else if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t')) {
      break;
    }
  }
  return count;
}

bool makeHeadingMarkerReplacement(const QString &p_text,
                                  const OutlineSectionEditor::HeadingBlockInfo &p_heading,
                                  int p_newLevel, OutlineSectionEditor::Syntax p_syntax,
                                  HeadingMarkerReplacement *p_replacement) {
  auto source = QStringView(p_text).mid(p_heading.startPos, p_heading.endPos - p_heading.startPos);
  if (p_syntax == OutlineSectionEditor::Syntax::Typst) {
    // The adapter proves the AST context; only check that its marker span is current.
    int markerLength = 0;
    while (markerLength < source.size() && source[markerLength] == QLatin1Char('=')) {
      ++markerLength;
    }
    if (markerLength != p_heading.level || (p_heading.startPos + markerLength < p_text.size() &&
                                            !p_text[p_heading.startPos + markerLength].isSpace())) {
      return false;
    }
    for (int i = p_heading.startPos - 1; i >= 0 && p_text[i] != QLatin1Char('\n'); --i) {
      if (p_text[i] != QLatin1Char(' ') && p_text[i] != QLatin1Char('\t')) {
        return false;
      }
    }
    if (p_replacement) {
      p_replacement->m_start = p_heading.startPos;
      p_replacement->m_end = p_heading.startPos + markerLength;
      p_replacement->m_text = QString(p_newLevel, QLatin1Char('='));
    }
    return true;
  }
  source = markdownHeadingSource(source);
  const auto atxMatch = matchAtxHeading(source);
  if (atxMatch.hasMatch()) {
    if (atxMatch.capturedLength(2) != p_heading.level) {
      return false;
    }
    if (!p_replacement) {
      return true;
    }
    p_replacement->m_start = p_heading.startPos + atxMatch.capturedStart(2);
    p_replacement->m_end = p_replacement->m_start + atxMatch.capturedLength(2);
    p_replacement->m_text = QString(p_newLevel, QLatin1Char('#'));
    return true;
  }

  const int underlineStart = source.lastIndexOf(QLatin1Char('\n'));
  if (underlineStart <= 0) {
    return false;
  }

  const auto underline = source.mid(underlineStart + 1);
  const auto setextMatch = matchSetextUnderline(underline);
  if (!setextMatch.hasMatch()) {
    return false;
  }

  const int parsedLevel = underline[setextMatch.capturedStart(2)] == QLatin1Char('=') ? 1 : 2;
  if (parsedLevel != p_heading.level) {
    return false;
  }

  if (!p_replacement) {
    return true;
  }

  if (p_newLevel <= 2) {
    p_replacement->m_start = p_heading.startPos + underlineStart + 1 + setextMatch.capturedStart(2);
    p_replacement->m_end = p_replacement->m_start + setextMatch.capturedLength(2);
    p_replacement->m_text = QString(setextMatch.capturedLength(2),
                                    p_newLevel == 1 ? QLatin1Char('=') : QLatin1Char('-'));
    return true;
  }

  const auto titleLine = source.left(underlineStart);
  int indentation = 0;
  while (indentation < titleLine.size() && indentation < 3 &&
         titleLine[indentation] == QLatin1Char(' ')) {
    ++indentation;
  }
  if (titleLine.mid(indentation).isEmpty()) {
    return false;
  }

  p_replacement->m_start = p_heading.startPos;
  p_replacement->m_end = p_heading.startPos + source.size();
  p_replacement->m_text = titleLine.left(indentation).toString() +
                          QString(p_newLevel, QLatin1Char('#')) + QLatin1Char(' ') +
                          titleLine.mid(indentation).toString();
  return true;
}

int mapThroughHeadingReplacements(int p_position,
                                  const QVector<HeadingMarkerReplacement> &p_replacements) {
  int delta = 0;
  for (const auto &replacement : p_replacements) {
    if (p_position < replacement.m_start) {
      break;
    }

    const int replacementLength = replacement.m_text.size();
    if (p_position <= replacement.m_end) {
      return replacement.m_start + delta +
             qMin(p_position - replacement.m_start, replacementLength);
    }
    delta += replacementLength - (replacement.m_end - replacement.m_start);
  }
  return p_position + delta;
}

bool utf16Boundary(const QString &p_text, int p_position) {
  return p_position >= 0 && p_position <= p_text.size() &&
         (p_position == 0 || p_position == p_text.size() ||
          !p_text[p_position - 1].isHighSurrogate() || !p_text[p_position].isLowSurrogate());
}

bool validTopic(const QString &p_topic) {
  for (int i = 0; i < p_topic.size(); ++i) {
    const auto ch = p_topic[i];
    if (ch.isNull() || ch == QLatin1Char('\r') || ch == QLatin1Char('\n') ||
        ch.unicode() == 0x2028 || ch.unicode() == 0x2029) {
      return false;
    }
    if (ch.isHighSurrogate()) {
      if (++i == p_topic.size() || !p_topic[i].isLowSurrogate()) {
        return false;
      }
    } else if (ch.isLowSurrogate()) {
      return false;
    }
  }
  return true;
}

bool validHeadings(const QString &p_text,
                   const QVector<OutlineSectionEditor::HeadingBlockInfo> &p_headings,
                   OutlineSectionEditor::Syntax p_syntax) {
  if (p_syntax != OutlineSectionEditor::Syntax::Markdown &&
      p_syntax != OutlineSectionEditor::Syntax::Typst) {
    return false;
  }
  const int maximumLevel =
      p_syntax == OutlineSectionEditor::Syntax::Markdown ? 6 : std::numeric_limits<int>::max();
  int previousEnd = -1;
  for (const auto &heading : p_headings) {
    if (heading.startPos < 0 || heading.endPos < 0) {
      if (heading.startPos != -1 || heading.endPos != -1 || heading.bodyStartPos != -1 ||
          heading.bodyEndPos != -1 || !heading.preservedBodyRanges.isEmpty()) {
        return false;
      }
      continue;
    }
    if (heading.level < 1 || heading.level > maximumLevel || heading.startPos < previousEnd ||
        heading.endPos <= heading.startPos || !utf16Boundary(p_text, heading.startPos) ||
        !utf16Boundary(p_text, heading.endPos) ||
        !makeHeadingMarkerReplacement(p_text, heading, heading.level, p_syntax, nullptr)) {
      return false;
    }
    if (heading.bodyStartPos != -1 || heading.bodyEndPos != -1 ||
        !heading.preservedBodyRanges.isEmpty()) {
      if (heading.bodyStartPos < heading.startPos || heading.bodyEndPos > heading.endPos ||
          heading.bodyStartPos > heading.bodyEndPos ||
          !utf16Boundary(p_text, heading.bodyStartPos) ||
          !utf16Boundary(p_text, heading.bodyEndPos)) {
        return false;
      }
      int preservedEnd = heading.bodyStartPos;
      for (const auto &range : heading.preservedBodyRanges) {
        if (range.first < preservedEnd || range.second <= range.first ||
            range.second > heading.bodyEndPos || !utf16Boundary(p_text, range.first) ||
            !utf16Boundary(p_text, range.second)) {
          return false;
        }
        preservedEnd = range.second;
      }
    }
    previousEnd = heading.endPos;
  }
  return true;
}

QString literalTopic(const QString &p_topic, OutlineSectionEditor::Syntax p_syntax,
                     bool p_setext = false) {
  QString encoded;
  if (p_syntax == OutlineSectionEditor::Syntax::Typst) {
    encoded = QStringLiteral("#\"");
    for (const auto ch : p_topic) {
      if (ch == QLatin1Char('\\') || ch == QLatin1Char('"')) {
        encoded += QLatin1Char('\\');
        encoded += ch;
      } else if (ch.category() == QChar::Other_Control) {
        encoded += QStringLiteral("\\u{%1}").arg(uint(ch.unicode()), 0, 16);
      } else {
        encoded += ch;
      }
    }
    encoded += QLatin1Char('"');
    return encoded;
  }
  int first = 0;
  int last = p_topic.size();
  while (first < last && p_topic[first].isSpace()) {
    ++first;
  }
  while (last > first && p_topic[last - 1].isSpace()) {
    --last;
  }
  for (int i = 0; i < p_topic.size(); ++i) {
    const auto ch = p_topic[i];
    const auto code = ch.unicode();
    const bool asciiPunctuation = (code >= 0x21 && code <= 0x2f) ||
                                  (code >= 0x3a && code <= 0x40) ||
                                  (code >= 0x5b && code <= 0x60) || (code >= 0x7b && code <= 0x7e);
    // ATX bodies cannot start lists. Leave their literal list delimiters readable
    // by the existing source-number normalizer; Setext titles need block escaping.
    const bool punctuation =
        asciiPunctuation && (p_setext || (ch != QLatin1Char('.') && ch != QLatin1Char(')')));
    if (punctuation || i < first || i >= last || ch.category() == QChar::Other_Control) {
      encoded += QStringLiteral("&#%1;").arg(uint(ch.unicode()));
    } else {
      encoded += ch;
    }
  }
  return encoded;
}

OutlineSectionEditor::HeadingEditResult
applyHeadingReplacements(QTextDocument *p_document, const QString &p_text,
                         const QVector<OutlineSectionEditor::HeadingBlockInfo> &p_headings,
                         const QVector<HeadingMarkerReplacement> &p_replacements,
                         int p_insertedPosition, int p_cursorPosition, int p_cursorAnchor,
                         int p_selectionStart, int p_selectionEnd) {
  OutlineSectionEditor::HeadingEditResult result;
  result.cursorPosition = p_cursorPosition;
  result.cursorAnchor = p_cursorAnchor;
  result.selectionStart = p_selectionStart;
  result.selectionEnd = p_selectionEnd;
  qint64 size = p_text.size();
  bool changed = false;
  for (const auto &replacement : p_replacements) {
    size += replacement.m_text.size() - (replacement.m_end - replacement.m_start);
    changed = changed || QStringView(p_text).mid(replacement.m_start,
                                                 replacement.m_end - replacement.m_start) !=
                             replacement.m_text;
  }
  if (!changed || size > std::numeric_limits<int>::max() - 1) {
    return result;
  }
  const auto mapped = [&](int p_position) {
    return p_position < 0 ? -1
                          : mapThroughHeadingReplacements(qMin(p_position, int(p_text.size())),
                                                          p_replacements);
  };
  result.headingPositions.reserve(p_headings.size() + (p_insertedPosition >= 0 ? 1 : 0));
  for (const auto &heading : p_headings) {
    // Existing headings at an insertion point must follow the new empty section.
    int position = mapped(heading.startPos);
    for (const auto &replacement : p_replacements) {
      if (heading.startPos == replacement.m_start && replacement.m_start == replacement.m_end) {
        position += replacement.m_text.size();
      }
    }
    result.headingPositions.append(position);
  }
  if (p_insertedPosition >= 0) {
    result.headingPositions.append(p_insertedPosition);
  }
  result.cursorPosition = mapped(p_cursorPosition);
  result.cursorAnchor = mapped(p_cursorAnchor);
  result.selectionStart = mapped(p_selectionStart);
  result.selectionEnd = mapped(p_selectionEnd);
  QTextCursor editCursor(p_document);
  editCursor.beginEditBlock();
  for (int i = p_replacements.size() - 1; i >= 0; --i) {
    const auto &replacement = p_replacements[i];
    editCursor.setPosition(replacement.m_start);
    editCursor.setPosition(replacement.m_end, QTextCursor::KeepAnchor);
    editCursor.insertText(replacement.m_text);
  }
  editCursor.endEditBlock();
  result.changed = true;
  return result;
}
} // namespace

OutlineSectionEditor::HeadingBlockMoveResult OutlineSectionEditor::reorderHeadingBlock(
    QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings,
    int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel, int p_cursorPosition,
    int p_cursorAnchor, int p_selectionStart, int p_selectionEnd, Syntax p_syntax,
    QVector<int> *p_headingPositions) {
  HeadingBlockMoveResult result;
  result.cursorPosition = p_cursorPosition;
  result.cursorAnchor = p_cursorAnchor;
  result.selectionStart = p_selectionStart;
  result.selectionEnd = p_selectionEnd;

  const int maximumLevel = p_syntax == Syntax::Markdown ? 6 : std::numeric_limits<int>::max();
  if (!p_document || (p_syntax != Syntax::Markdown && p_syntax != Syntax::Typst) ||
      p_sourceHeadingIndex < 0 || p_sourceHeadingIndex >= p_headings.size() ||
      p_beforeHeadingIndex < -1 || p_targetLevel < 1 || p_targetLevel > maximumLevel) {
    return result;
  }

  const QString text = p_document->toPlainText();
  if (!validHeadings(text, p_headings, p_syntax)) {
    return result;
  }

  const auto &source = p_headings[p_sourceHeadingIndex];
  if (source.startPos < 0 || source.endPos < 0) {
    return result;
  }

  int sourceEndHeadingIndex = p_sourceHeadingIndex + 1;
  while (sourceEndHeadingIndex < p_headings.size()) {
    const auto &heading = p_headings[sourceEndHeadingIndex];
    if (heading.startPos >= 0 && heading.level <= source.level) {
      break;
    }
    ++sourceEndHeadingIndex;
  }

  if (p_beforeHeadingIndex >= 0) {
    if (p_beforeHeadingIndex >= p_headings.size() ||
        p_headings[p_beforeHeadingIndex].startPos < 0 ||
        (p_beforeHeadingIndex >= p_sourceHeadingIndex &&
         p_beforeHeadingIndex < sourceEndHeadingIndex)) {
      return result;
    }
  }

  const qint64 levelDelta = qint64(p_targetLevel) - source.level;
  if (levelDelta == 0 &&
      (p_beforeHeadingIndex == sourceEndHeadingIndex ||
       (p_beforeHeadingIndex == -1 && sourceEndHeadingIndex == p_headings.size()))) {
    return result;
  }

  qint64 rewrittenSize = text.size();
  for (int i = p_sourceHeadingIndex; i < sourceEndHeadingIndex; ++i) {
    const auto &heading = p_headings[i];
    if (heading.startPos < 0) {
      continue;
    }
    const qint64 newLevel = qint64(heading.level) + levelDelta;
    if (newLevel < 1 || newLevel > maximumLevel) {
      return result;
    }
    if (p_syntax == Syntax::Typst) {
      rewrittenSize += levelDelta;
    }
  }
  // QTextDocument positions include its final paragraph separator and use int.
  // Check all descendants before allocating any potentially very deep '=' marker.
  if (rewrittenSize > std::numeric_limits<int>::max() - 1) {
    return result;
  }

  QVector<HeadingMarkerReplacement> replacements;
  if (levelDelta != 0) {
    replacements.reserve(sourceEndHeadingIndex - p_sourceHeadingIndex);
    for (int i = p_sourceHeadingIndex; i < sourceEndHeadingIndex; ++i) {
      const auto &heading = p_headings[i];
      if (heading.startPos < 0) {
        continue;
      }
      HeadingMarkerReplacement replacement;
      if (!makeHeadingMarkerReplacement(text, heading, int(qint64(heading.level) + levelDelta),
                                        p_syntax, &replacement)) {
        return result;
      }
      replacements.append(replacement);
    }
  }

  const auto sectionStart = [&](int p_headingIndex) {
    int start = p_headings[p_headingIndex].startPos;
    // Parser spans may start at the marker rather than its line indentation.
    while (start > 0 && text[start - 1] != QLatin1Char('\n')) {
      --start;
    }
    return start;
  };
  int sourceStart = sectionStart(p_sourceHeadingIndex);
  const int sourceEnd =
      sourceEndHeadingIndex < p_headings.size() ? sectionStart(sourceEndHeadingIndex) : text.size();
  const int destination =
      p_beforeHeadingIndex >= 0 ? sectionStart(p_beforeHeadingIndex) : text.size();
  bool carriedLeadingSeparator = false;
  if (sourceEnd == text.size() && !text.endsWith(QLatin1Char('\n')) && sourceStart > 0 &&
      text[sourceStart - 1] == QLatin1Char('\n')) {
    --sourceStart;
    carriedLeadingSeparator = true;
  }
  if (sourceEnd <= sourceStart || (destination > sourceStart && destination < sourceEnd)) {
    return result;
  }

  for (auto &replacement : replacements) {
    replacement.m_start -= sourceStart;
    replacement.m_end -= sourceStart;
  }

  QString movedText = text.mid(sourceStart, sourceEnd - sourceStart);
  for (int i = replacements.size() - 1; i >= 0; --i) {
    const auto &replacement = replacements[i];
    movedText.replace(replacement.m_start, replacement.m_end - replacement.m_start,
                      replacement.m_text);
  }

  bool rotatedLeadingSeparator = false;
  bool rotatedTrailingSeparator = false;
  if (carriedLeadingSeparator && destination < sourceStart &&
      movedText.startsWith(QLatin1Char('\n'))) {
    movedText.remove(0, 1);
    movedText.append(QLatin1Char('\n'));
    rotatedLeadingSeparator = true;
  } else if (destination == text.size() && !text.endsWith(QLatin1Char('\n')) &&
             sourceEnd < text.size() && movedText.endsWith(QLatin1Char('\n'))) {
    movedText.chop(1);
    movedText.prepend(QLatin1Char('\n'));
    rotatedTrailingSeparator = true;
  }

  const int removedLength = sourceEnd - sourceStart;
  const int insertionPosition =
      destination >= sourceEnd ? destination - removedLength : destination;
  const int remainingLength = text.size() - removedLength;
  const auto remainingCharacter = [&](int p_position) {
    return text[p_position < sourceStart ? p_position : p_position + removedLength];
  };
  int addedLeadingSeparator = 0;
  if (insertionPosition > 0 && !movedText.isEmpty() &&
      remainingCharacter(insertionPosition - 1) != QLatin1Char('\n') &&
      !movedText.startsWith(QLatin1Char('\n'))) {
    movedText.prepend(QLatin1Char('\n'));
    addedLeadingSeparator = 1;
  }
  if (insertionPosition < remainingLength && !movedText.isEmpty() &&
      !movedText.endsWith(QLatin1Char('\n')) &&
      remainingCharacter(insertionPosition) != QLatin1Char('\n')) {
    movedText.append(QLatin1Char('\n'));
  }
  if (p_syntax == Syntax::Markdown) {
    const auto isSetext = [&](int p_index) {
      const auto &heading = p_headings[p_index];
      return markdownHeadingSource(
                 QStringView(text).mid(heading.startPos, heading.endPos - heading.startPos))
          .contains(QLatin1Char('\n'));
    };
    // Unlike ATX, a Setext title can absorb the preceding paragraph. Keep the
    // two newly joined section boundaries blank-line separated when needed.
    if (insertionPosition > 0 && p_targetLevel <= 2 && isSetext(p_sourceHeadingIndex)) {
      int breaks = boundaryLineBreaks(movedText, true);
      for (int i = insertionPosition - 1; i >= 0 && breaks < 2; --i) {
        const auto ch = remainingCharacter(i);
        if (ch == QLatin1Char('\n')) {
          ++breaks;
        } else if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t')) {
          break;
        }
      }
      while (breaks++ < 2) {
        movedText.prepend(QLatin1Char('\n'));
        ++addedLeadingSeparator;
      }
    }
    if (insertionPosition < remainingLength && p_beforeHeadingIndex >= 0 &&
        isSetext(p_beforeHeadingIndex)) {
      int breaks = boundaryLineBreaks(movedText, false);
      for (int i = insertionPosition; i < remainingLength && breaks < 2; ++i) {
        const auto ch = remainingCharacter(i);
        if (ch == QLatin1Char('\n')) {
          ++breaks;
        } else if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t')) {
          break;
        }
      }
      while (breaks++ < 2) {
        movedText.append(QLatin1Char('\n'));
      }
    }
  }
  if (qint64(remainingLength) + movedText.size() > std::numeric_limits<int>::max() - 1) {
    return result;
  }

  auto mapPosition = [&](int p_position) {
    if (p_position < 0) {
      return -1;
    }
    const int boundedPosition = qBound(0, p_position, int(text.size()));
    if (boundedPosition >= sourceStart && boundedPosition < sourceEnd) {
      int localPosition =
          mapThroughHeadingReplacements(boundedPosition - sourceStart, replacements);
      if (rotatedLeadingSeparator) {
        localPosition = qMax(0, localPosition - 1);
      } else if (rotatedTrailingSeparator) {
        ++localPosition;
      }
      localPosition += addedLeadingSeparator;
      return insertionPosition + localPosition;
    }

    int remainingPosition = boundedPosition;
    if (boundedPosition >= sourceEnd) {
      remainingPosition -= sourceEnd - sourceStart;
    }
    if (remainingPosition >= insertionPosition) {
      remainingPosition += movedText.size();
    }
    return remainingPosition;
  };

  QVector<int> headingPositions;
  if (p_headingPositions) {
    headingPositions.reserve(p_headings.size());
    for (const auto &heading : p_headings) {
      headingPositions.append(mapPosition(heading.startPos));
    }
  }
  QTextCursor editCursor(p_document);
  editCursor.beginEditBlock();
  editCursor.setPosition(sourceStart);
  editCursor.setPosition(sourceEnd, QTextCursor::KeepAnchor);
  editCursor.removeSelectedText();
  editCursor.setPosition(insertionPosition);
  editCursor.insertText(movedText);
  editCursor.endEditBlock();

  if (p_headingPositions) {
    p_headingPositions->swap(headingPositions);
  }
  result.moved = true;
  result.cursorPosition = mapPosition(p_cursorPosition);
  result.cursorAnchor = mapPosition(p_cursorAnchor);
  result.selectionStart = mapPosition(p_selectionStart);
  result.selectionEnd = mapPosition(p_selectionEnd);
  return result;
}

OutlineSectionEditor::HeadingEditResult OutlineSectionEditor::renameHeading(
    QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings, int p_headingIndex,
    const QString &p_topic, int p_cursorPosition, int p_cursorAnchor, int p_selectionStart,
    int p_selectionEnd, Syntax p_syntax) {
  HeadingEditResult result;
  result.cursorPosition = p_cursorPosition;
  result.cursorAnchor = p_cursorAnchor;
  result.selectionStart = p_selectionStart;
  result.selectionEnd = p_selectionEnd;
  if (!p_document || p_headingIndex < 0 || p_headingIndex >= p_headings.size() ||
      !validTopic(p_topic)) {
    return result;
  }
  const QString text = p_document->toPlainText();
  if (!validHeadings(text, p_headings, p_syntax)) {
    return result;
  }
  const auto &heading = p_headings[p_headingIndex];
  if (heading.startPos < 0) {
    return result;
  }
  HeadingMarkerReplacement replacement;
  replacement.m_text = literalTopic(p_topic, p_syntax);
  if (p_syntax == Syntax::Typst) {
    if (heading.bodyStartPos <= heading.startPos + heading.level || heading.bodyEndPos < 0) {
      return result;
    }
    replacement.m_start = heading.bodyStartPos;
    replacement.m_end = heading.bodyEndPos;
    for (int i = 0; i < heading.preservedBodyRanges.size(); ++i) {
      const auto &range = heading.preservedBodyRanges[i];
      // Added spaces are visible Typst content, even around invisible comments/labels.
      replacement.m_text += QStringView(text).mid(range.first, range.second - range.first);
      const int next = i + 1 < heading.preservedBodyRanges.size()
                           ? heading.preservedBodyRanges[i + 1].first
                           : heading.bodyEndPos;
      // Preserve source line boundaries, not removed visible syntax. In particular,
      // a certified line comment cannot consume a following label or section.
      for (int position = range.second; position < next; ++position) {
        if (text[position] == QLatin1Char('\n')) {
          replacement.m_text += QLatin1Char('\n');
        }
      }
    }
    if (heading.bodyEndPos > heading.bodyStartPos &&
        text[heading.bodyEndPos - 1] == QLatin1Char('\n') &&
        !replacement.m_text.endsWith(QLatin1Char('\n'))) {
      replacement.m_text += QLatin1Char('\n');
    }
  } else {
    const auto source = markdownHeadingSource(
        QStringView(text).mid(heading.startPos, heading.endPos - heading.startPos));
    const auto match = matchAtxHeading(source);
    if (match.hasMatch()) {
      replacement.m_start = heading.startPos + match.capturedEnd();
      replacement.m_end = heading.startPos + source.size();
      const auto body = source.mid(match.capturedEnd());
      static const QRegularExpression closing(QStringLiteral("(?:[\\t ]+|\\A)#+[\\t ]*\\z"));
      const auto suffix = closing.matchView(body);
      if (suffix.hasMatch()) {
        replacement.m_end = replacement.m_start + suffix.capturedStart();
        if (suffix.capturedStart() == 0 && !p_topic.isEmpty()) {
          replacement.m_text += QLatin1Char(' ');
        }
      }
      if (match.capturedLength(3) == 0 && !replacement.m_text.isEmpty()) {
        replacement.m_text.prepend(QLatin1Char(' '));
      }
    } else {
      int indent = 0;
      while (indent < 3 && indent < source.size() && source[indent] == QLatin1Char(' ')) {
        ++indent;
      }
      replacement.m_start = heading.startPos + indent;
      if (p_topic.isEmpty()) {
        replacement.m_end = heading.startPos + source.size();
        replacement.m_text = QString(heading.level, QLatin1Char('#'));
      } else {
        replacement.m_end = heading.startPos + source.lastIndexOf(QLatin1Char('\n'));
        replacement.m_text = literalTopic(p_topic, p_syntax, true);
      }
    }
  }
  return applyHeadingReplacements(p_document, text, p_headings, {replacement}, -1, p_cursorPosition,
                                  p_cursorAnchor, p_selectionStart, p_selectionEnd);
}

OutlineSectionEditor::HeadingEditResult OutlineSectionEditor::insertHeading(
    QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings,
    int p_beforeHeadingIndex, int p_parentHeadingIndex, int p_level, const QString &p_topic,
    int p_cursorPosition, int p_cursorAnchor, int p_selectionStart, int p_selectionEnd,
    Syntax p_syntax) {
  HeadingEditResult result;
  result.cursorPosition = p_cursorPosition;
  result.cursorAnchor = p_cursorAnchor;
  result.selectionStart = p_selectionStart;
  result.selectionEnd = p_selectionEnd;
  if (!p_document || p_beforeHeadingIndex < -1 || p_beforeHeadingIndex >= p_headings.size() ||
      p_parentHeadingIndex < -1 || p_parentHeadingIndex >= p_headings.size() || p_level < 1 ||
      (p_syntax == Syntax::Markdown && p_level > 6) || !validTopic(p_topic)) {
    return result;
  }
  const QString text = p_document->toPlainText();
  if (!validHeadings(text, p_headings, p_syntax) ||
      (p_beforeHeadingIndex >= 0 && p_headings[p_beforeHeadingIndex].startPos < 0) ||
      (p_parentHeadingIndex >= 0 && p_headings[p_parentHeadingIndex].startPos < 0)) {
    return result;
  }
  const int before = p_beforeHeadingIndex < 0 ? p_headings.size() : p_beforeHeadingIndex;
  int parent = -1;
  for (int i = before - 1; i >= 0; --i) {
    if (p_headings[i].startPos >= 0 && p_headings[i].level < p_level) {
      parent = i;
      break;
    }
  }
  if (parent != p_parentHeadingIndex ||
      (before < p_headings.size() && p_headings[before].level > p_level)) {
    return result;
  }
  int position = before < p_headings.size() ? p_headings[before].startPos : text.size();
  if (before < p_headings.size()) {
    while (position > 0 && text[position - 1] != QLatin1Char('\n')) {
      --position;
    }
  }
  // Bound marker allocation before constructing the replacement.
  const QString topic = literalTopic(p_topic, p_syntax);
  if (qint64(text.size()) + p_level + topic.size() + 5 > std::numeric_limits<int>::max() - 1) {
    return result;
  }
  int leading = 0;
  for (int i = position - 1; i >= 0 && leading < 2 && text[i] == QLatin1Char('\n'); --i) {
    ++leading;
  }
  leading = position == 0 ? 0 : 2 - leading;
  HeadingMarkerReplacement replacement;
  replacement.m_start = replacement.m_end = position;
  replacement.m_text =
      QString(leading, QLatin1Char('\n')) +
      QString(p_level, p_syntax == Syntax::Markdown ? QLatin1Char('#') : QLatin1Char('=')) +
      QLatin1Char(' ') + topic;
  // A new section stays empty; existing following prose belongs to the old section.
  replacement.m_text += before < p_headings.size() ? QStringLiteral("\n\n") : QStringLiteral("\n");
  return applyHeadingReplacements(p_document, text, p_headings, {replacement}, position + leading,
                                  p_cursorPosition, p_cursorAnchor, p_selectionStart,
                                  p_selectionEnd);
}
