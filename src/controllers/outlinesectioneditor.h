#ifndef OUTLINESECTIONEDITOR_H
#define OUTLINESECTIONEDITOR_H

#include <QVector>

class QTextDocument;

namespace vnotex {

class OutlineSectionEditor {
public:
  struct HeadingBlockInfo {
    int level = -1;
    int startPos = -1;
    int endPos = -1;
  };

  struct HeadingBlockMoveResult {
    bool moved = false;
    int cursorPosition = -1;
    int cursorAnchor = -1;
    int selectionStart = -1;
    int selectionEnd = -1;
  };

  enum class Syntax { Markdown, Typst };

  // Headings must describe the same document snapshot in source order. Typst callers
  // must supply complete AST-proven top-level '=' headings and reject moves across
  // generated, functional, imported or otherwise non-rewritable section boundaries.
  // Marker validation here does not establish that source-structure guarantee.
  static HeadingBlockMoveResult
  reorderHeadingBlock(QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings,
                      int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel,
                      int p_cursorPosition, int p_cursorAnchor, int p_selectionStart,
                      int p_selectionEnd, Syntax p_syntax = Syntax::Markdown);
};

} // namespace vnotex

#endif // OUTLINESECTIONEDITOR_H
