#ifndef VEDITUTILS_H
#define VEDITUTILS_H

#include <QTextBlock>
#include <QTextCursor>

// Utils for text edit.
class VEditUtils
{
public:
    // Remove the whole block @p_block.
    // If @p_text is not NULL, return the deleted text here.
    static void removeBlock(QTextBlock &p_block, QString *p_text = NULL);

    // Remove the whole block under @p_cursor.
    // If @p_text is not NULL, return the deleted text here.
    // Need to call setTextCursor() to make it take effect.
    static void removeBlock(QTextCursor &p_cursor, QString *p_text = NULL);

    // Move @p_cursor to the first non-space character of current block.
    // Need to call setTextCursor() to make it take effect.
    static void moveCursorFirstNonSpaceCharacter(QTextCursor &p_cursor,
                                                 QTextCursor::MoveMode p_mode);
    // Indent current block as previous block.
    // Return true if some changes have been made.
    static bool indentBlockAsPreviousBlock(QTextCursor &p_cursor);

    // Insert a new block at current position with the same indentation as
    // current block. Should clear the selection before calling this.
    // Returns true if non-empty indentation has been inserted.
    // Need to call setTextCursor() to make it take effect.
    static bool insertBlockWithIndent(QTextCursor &p_cursor);

    // Fetch the list mark of previous block, and insert it at current position.
    // Returns true if list mark has been inserted.
    // Need to call setTextCursor() to make it take effect.
    static bool insertListMarkAsPreviousBlock(QTextCursor &p_cursor);

private:
    VEditUtils() {}
};

#endif // VEDITUTILS_H
