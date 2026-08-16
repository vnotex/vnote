#ifndef CURSORPRESERVATIONPOLICY_H
#define CURSORPRESERVATIONPOLICY_H

namespace vnotex {

// Pure-helper that decides where to place the caret in a text editor after the
// content was re-pulled from the buffer (reload, encoding change, external
// change). Strategy: clamped line + block-relative offset. No content matching.
//
// Rule set for computeRestoredCursorLine(oldLine, newBlockCount):
//   - oldLine < 0 (not captured) or newBlockCount <= 0 (empty document) -> -1
//     (invalid sentinel; caller skips the cursor restore entirely).
//   - Otherwise min(oldLine, newBlockCount - 1): a shorter document places the
//     caret on its last block.
//
// Rule set for computeRestoredCursorOffset(oldOffset, newBlockTextLength):
//   - oldOffset < 0 (not captured) -> 0.
//   - newBlockTextLength < 0 (nonsensical input) -> 0.
//   - Otherwise min(oldOffset, newBlockTextLength). The offset is a UTF-16
//     offset within the block, NOT a visual column, so the caller must capture
//     it with QTextCursor::positionInBlock() and pass QTextBlock::length() - 1
//     as the length (length() counts the block separator). An offset equal to
//     the length means end-of-line, which is a valid caret position.
class CursorPreservationPolicy {
public:
  static int computeRestoredCursorLine(int p_oldLine, int p_newBlockCount);

  static int computeRestoredCursorOffset(int p_oldOffset, int p_newBlockTextLength);
};

} // namespace vnotex

#endif // CURSORPRESERVATIONPOLICY_H
