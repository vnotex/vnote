#include "textcursorpreserver.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <utils/cursorpreservationpolicy.h>

namespace vnotex {

TextCursorPosition captureTextCursorPosition(const QTextEdit *p_edit) {
  TextCursorPosition position;
  if (!p_edit) {
    return position;
  }
  const QTextCursor cursor = p_edit->textCursor();
  position.m_line = cursor.blockNumber();
  position.m_positionInBlock = cursor.positionInBlock();
  return position;
}

void restoreTextCursorPosition(QTextEdit *p_edit, const TextCursorPosition &p_position) {
  if (!p_edit || !p_position.isValid()) {
    return;
  }

  auto *doc = p_edit->document();
  const int line =
      CursorPreservationPolicy::computeRestoredCursorLine(p_position.m_line, doc->blockCount());
  if (line < 0) {
    return;
  }

  const QTextBlock block = doc->findBlockByNumber(line);
  if (!block.isValid()) {
    return;
  }

  // QTextBlock::length() counts the trailing block separator, so the last valid
  // offset within the block's text is length() - 1.
  const int offset = CursorPreservationPolicy::computeRestoredCursorOffset(
      p_position.m_positionInBlock, block.length() - 1);

  QTextCursor cursor = p_edit->textCursor();
  cursor.setPosition(block.position() + offset);
  p_edit->setTextCursor(cursor);
}

} // namespace vnotex
