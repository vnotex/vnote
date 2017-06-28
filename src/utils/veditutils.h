#ifndef VEDITUTILS_H
#define VEDITUTILS_H

#include <QTextBlock>
#include <QTextCursor>

class QTextDocument;
class QTextEdit;

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
    // @p_cursor will be placed at the position after inserting leading spaces.
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

    // Remove ObjectReplaceCharacter in p_text.
    // If the ObjectReplaceCharacter is in a block with only other spaces, remove the
    // whole block.
    static void removeObjectReplacementCharacter(QString &p_text);

    // p_cursor.selectedText() will use U+2029 (QChar::ParagraphSeparator)
    // instead of \n for a new line.
    // This function will translate it to \n.
    static QString selectedText(const QTextCursor &p_cursor);

    // Indent selected blocks. If no selection, indent current block.
    // @p_isIndent: whether it is indentation or unindentation.
    static void indentSelectedBlocks(const QTextDocument *p_doc,
                                     const QTextCursor &p_cursor,
                                     const QString &p_indentationText,
                                     bool p_isIndent);

    // Indent current block.
    // Skip empty block.
    static void indentBlock(QTextCursor &p_cursor,
                            const QString &p_indentationText);

    static void unindentBlock(QTextCursor &p_cursor,
                              const QString &p_indentationText);

    // Find @p_repeat th occurence a char within a block.
    // Returns true if target is found.
    static bool findTargetWithinBlock(QTextCursor &p_cursor,
                                      QTextCursor::MoveMode p_mode,
                                      QChar p_target,
                                      bool p_forward,
                                      bool p_inclusive,
                                      int p_repeat);

    // Find a pair target (@p_opening, @p_closing) containing current cursor and
    // select the range between them.
    // Need to call setTextCursor() to make it take effect.
    // Returns true if target is found.
    static bool selectPairTargetAround(QTextCursor &p_cursor,
                                       QChar p_opening,
                                       QChar p_closing,
                                       bool p_inclusive,
                                       bool p_crossBlock,
                                       int p_repeat);

    // Get the count of blocks selected.
    static int selectedBlockCount(const QTextCursor &p_cursor);

    // Scroll block @p_blockNum into the visual window.
    // @p_dest is the position of the window: 0 for top, 1 for center, 2 for bottom.
    // @p_blockNum is based on 0.
    static void scrollBlockInPage(QTextEdit *p_edit,
                                  int p_blockNum,
                                  int p_dest);

    // Check if @p_block is a auto list block.
    // @p_seq will be the seq number of the ordered list, or -1.
    // Returns true if it is an auto list block.
    static bool isListBlock(const QTextBlock &p_block, int *p_seq = NULL);

    // If the start of @p_block to postition @p_posInBlock are spaces.
    static bool isSpaceToBlockStart(const QTextBlock &p_block, int p_posInBlock);

    // @p_cursor is positioned right after auto indetn and auto list.
    // Need to call setTextCursor() to make it take effect.
    static void deleteIndentAndListMark(QTextCursor &p_cursor);

private:
    VEditUtils() {}
};

#endif // VEDITUTILS_H
