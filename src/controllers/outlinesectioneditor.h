#ifndef OUTLINESECTIONEDITOR_H
#define OUTLINESECTIONEDITOR_H

#include <QPair>
#include <QString>
#include <QVector>

class QTextDocument;

namespace vnotex {

class OutlineSectionEditor {
public:
  struct HeadingBlockInfo {
    int level = -1;
    int startPos = -1;
    int endPos = -1;
    int bodyStartPos = -1;
    int bodyEndPos = -1;
    QVector<QPair<int, int>> preservedBodyRanges;
  };

  struct HeadingBlockMoveResult {
    bool moved = false;
    int cursorPosition = -1;
    int cursorAnchor = -1;
    int selectionStart = -1;
    int selectionEnd = -1;
  };

  struct HeadingEditResult {
    bool changed = false;
    int cursorPosition = -1;
    int cursorAnchor = -1;
    int selectionStart = -1;
    int selectionEnd = -1;
    // Input order, with the inserted heading appended for insertHeading().
    QVector<int> headingPositions;
  };

  enum class Syntax { Markdown, Typst };

  // Headings must describe the same document snapshot in source order. Typst callers
  // must supply complete AST-proven top-level '=' headings and reject moves across
  // generated, functional, imported or otherwise non-rewritable section boundaries.
  // Marker validation here does not establish that source-structure guarantee.
  // Markdown spans may include following blank/prose lines; title/marker edits
  // stop at the validated syntax and preserve that tail. Raw overlaps are rejected.
  // Moves keep Setext titles separate from adjacent body prose.
  static HeadingBlockMoveResult
  reorderHeadingBlock(QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings,
                      int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel,
                      int p_cursorPosition, int p_cursorAnchor, int p_selectionStart,
                      int p_selectionEnd, Syntax p_syntax = Syntax::Markdown,
                      QVector<int> *p_headingPositions = nullptr);

  static HeadingEditResult renameHeading(QTextDocument *p_document,
                                         const QVector<HeadingBlockInfo> &p_headings,
                                         int p_headingIndex, const QString &p_topic,
                                         int p_cursorPosition, int p_cursorAnchor,
                                         int p_selectionStart, int p_selectionEnd,
                                         Syntax p_syntax = Syntax::Markdown);
  static HeadingEditResult insertHeading(QTextDocument *p_document,
                                         const QVector<HeadingBlockInfo> &p_headings,
                                         int p_beforeHeadingIndex, int p_parentHeadingIndex,
                                         int p_level, const QString &p_topic, int p_cursorPosition,
                                         int p_cursorAnchor, int p_selectionStart,
                                         int p_selectionEnd, Syntax p_syntax = Syntax::Markdown);
};

} // namespace vnotex

#endif // OUTLINESECTIONEDITOR_H
