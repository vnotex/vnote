#ifndef TEXTCURSORPRESERVER_H
#define TEXTCURSORPRESERVER_H

class QTextEdit;

namespace vnotex {

// Caret position expressed as (block number, UTF-16 offset within the block).
// The offset is NOT a visual column: it is stable under soft wrapping.
struct TextCursorPosition {
  int m_line = -1;            // Block number, -1 = not captured.
  int m_positionInBlock = -1; // UTF-16 offset within the block.

  bool isValid() const { return m_line >= 0; }
};

// Capture the caret of p_edit. Returns an invalid position for a null editor.
//
// Uses QTextCursor::positionInBlock(), NOT columnNumber(): the latter is
// relative to the current soft-wrapped visual line, so restoring it would put
// the caret near the paragraph start whenever the caret was on the 2nd+ wrapped
// line of a paragraph.
TextCursorPosition captureTextCursorPosition(const QTextEdit *p_edit);

// Best-effort caret restore after the editor's text was replaced: the line is
// clamped to the new block count and the offset to the target block's text
// length (see CursorPreservationPolicy). No-op when p_position was not
// captured, or when the document is empty.
//
// This only changes cursor/selection state, so it never dirties the document.
void restoreTextCursorPosition(QTextEdit *p_edit, const TextCursorPosition &p_position);

} // namespace vnotex

#endif // TEXTCURSORPRESERVER_H
