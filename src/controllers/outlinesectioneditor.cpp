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

bool makeHeadingMarkerReplacement(const QString &p_text,
                                  const OutlineSectionEditor::HeadingBlockInfo &p_heading,
                                  int p_newLevel, OutlineSectionEditor::Syntax p_syntax,
                                  HeadingMarkerReplacement *p_replacement) {
  const auto source =
      QStringView(p_text).mid(p_heading.startPos, p_heading.endPos - p_heading.startPos);
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
  static const QRegularExpression atxPattern(QStringLiteral("\\A( {0,3})(#{1,6})(?=[\\t ]|$)"));
  const auto atxMatch = atxPattern.matchView(source);
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
  static const QRegularExpression setextPattern(QStringLiteral("\\A( {0,3})(=+|-+)([\\t ]*)\\z"));
  const auto setextMatch = setextPattern.matchView(underline);
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
  p_replacement->m_end = p_heading.endPos;
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
} // namespace

OutlineSectionEditor::HeadingBlockMoveResult OutlineSectionEditor::reorderHeadingBlock(
    QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings,
    int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel, int p_cursorPosition,
    int p_cursorAnchor, int p_selectionStart, int p_selectionEnd, Syntax p_syntax) {
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
  int previousEnd = -1;
  for (const auto &heading : p_headings) {
    if (heading.startPos < 0 || heading.endPos < 0) {
      if (heading.startPos != -1 || heading.endPos != -1) {
        return result;
      }
      continue;
    }
    if (heading.level < 1 || heading.level > maximumLevel || heading.startPos < previousEnd ||
        heading.endPos <= heading.startPos || heading.endPos > text.size()) {
      return result;
    }
    if (!makeHeadingMarkerReplacement(text, heading, heading.level, p_syntax, nullptr)) {
      return result;
    }
    previousEnd = heading.endPos;
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
    if (p_syntax == Syntax::Typst) {
      // AST heading spans start at '=', so carry preceding line indentation too.
      while (start > 0 && text[start - 1] != QLatin1Char('\n')) {
        --start;
      }
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

  QTextCursor editCursor(p_document);
  editCursor.beginEditBlock();
  editCursor.setPosition(sourceStart);
  editCursor.setPosition(sourceEnd, QTextCursor::KeepAnchor);
  editCursor.removeSelectedText();
  editCursor.setPosition(insertionPosition);
  editCursor.insertText(movedText);
  editCursor.endEditBlock();

  result.moved = true;
  result.cursorPosition = mapPosition(p_cursorPosition);
  result.cursorAnchor = mapPosition(p_cursorAnchor);
  result.selectionStart = mapPosition(p_selectionStart);
  result.selectionEnd = mapPosition(p_selectionEnd);
  return result;
}
