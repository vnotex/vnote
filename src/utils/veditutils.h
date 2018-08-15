#ifndef VEDITUTILS_H
#define VEDITUTILS_H

#include <QTextBlock>
#include <QTextCursor>

class QTextDocument;
class QTextEdit;
class QPlainTextEdit;

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

    // Indent current block as next/previous block.
    // Return true if some changes have been made.
    // @p_cursor will be placed at the position after inserting leading spaces.
    // @p_next: indent as next block or previous block.
    static bool indentBlockAsBlock(QTextCursor &p_cursor, bool p_next);

    // Indent current block as @p_refBlock.
    static bool indentBlockAsBlock(QTextCursor &p_cursor, const QTextBlock &p_refBlock);

    // Returns true if two blocks has the same indent.
    static bool hasSameIndent(const QTextBlock &p_blocka, const QTextBlock &p_blockb);

    // Insert a new block at current position with the same indentation as
    // current block. Should clear the selection before calling this.
    // Returns true if non-empty indentation has been inserted.
    // Need to call setTextCursor() to make it take effect.
    static bool insertBlockWithIndent(QTextCursor &p_cursor);

    // Fetch the list mark of previous block, and insert it at current position.
    // Returns true if list mark has been inserted.
    // Need to call setTextCursor() to make it take effect.
    static bool insertListMarkAsPreviousBlock(QTextCursor &p_cursor);

    // Fetch the block quote mark of previous block, and insert it at current position.
    // Returns true if quote mark has been inserted.
    // Need to call setTextCursor() to make it take effect.
    static bool insertQuoteMarkAsPreviousBlock(QTextCursor &p_cursor);

    // Remove ObjectReplaceCharacter in p_text.
    // If the ObjectReplaceCharacter is in a block with only other spaces, remove the
    // whole block.
    static void removeObjectReplacementCharacter(QString &p_text);

    // p_cursor.selectedText() will use U+2029 (QChar::ParagraphSeparator)
    // instead of \n for a new line.
    // This function will translate it to \n.
    static QString selectedText(const QTextCursor &p_cursor);

    // Indent selected blocks. If no selection, indent current block.
    // Cursor position and selection is not changed.
    // @p_isIndent: whether it is indentation or unindentation.
    static void indentSelectedBlocks(const QTextCursor &p_cursor,
                                     const QString &p_indentationText,
                                     bool p_isIndent);

    // Indent seleced block as next/previous block.
    // Cursor position and selection is not changed.
    // Return true if some changes have been made.
    // @p_next: indent as next block or previous block.
    static void indentSelectedBlocksAsBlock(const QTextCursor &p_cursor, bool p_next);

    // Indent current block.
    // @p_skipEmpty: skip empty block.
    static void indentBlock(QTextCursor &p_cursor,
                            const QString &p_indentationText,
                            bool p_skipEmpty = true);

    static void unindentBlock(QTextCursor &p_cursor,
                              const QString &p_indentationText);

    // Find @p_repeat th occurence of a char within a block.
    // Returns true if target is found.
    // Please pay attention to the one-step-forward/backward in KeepAnchor mode
    // and exclusive case.
    static bool findTargetWithinBlock(QTextCursor &p_cursor,
                                      QTextCursor::MoveMode p_mode,
                                      QChar p_target,
                                      bool p_forward,
                                      bool p_inclusive,
                                      bool p_leftSideOfCursor,
                                      int p_repeat);

    // Find th first occurence of a char in @p_targets within a block.
    // Returns the index of the found char in @p_targets if found.
    // Returns -1 if none of the @p_targets is found.
    // Different from findTargetWithinBlock(), will not modify the ucrsor position
    // even in KeepAnchor mode.
    static int findTargetsWithinBlock(QTextCursor &p_cursor,
                                      const QList<QChar> &p_targets,
                                      bool p_forward,
                                      bool p_leftSideOfCursor,
                                      bool p_inclusive);

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
    // Will set the cursor to the block.
    static void scrollBlockInPage(QTextEdit *p_edit,
                                  int p_blockNum,
                                  int p_dest);

    // Scroll block @p_blockNum into the visual window.
    // @p_dest is the position of the window: 0 for top, 1 for center, 2 for bottom.
    // @p_blockNum is based on 0.
    // Will set the cursor to the block.
    static void scrollBlockInPage(QPlainTextEdit *p_edit,
                                  int p_blockNum,
                                  int p_dest);

    // Check if @p_block is a auto list block.
    // @p_seq will be the seq number of the ordered list, or -1.
    // Returns true if it is an auto list block.
    static bool isListBlock(const QTextBlock &p_block, int *p_seq = NULL);

    static bool isListBullet(const QString &p_str);

    static bool isBlockQuoteBlock(const QTextBlock &p_block);

    // If the start of @p_block to postition @p_posInBlock are spaces.
    static bool isSpaceToBlockStart(const QTextBlock &p_block, int p_posInBlock);

    // If block @p_block only contains spaces.
    static bool isSpaceBlock(const QTextBlock &p_block);

    // @p_cursor is positioned right after auto indetn and auto list.
    // Need to call setTextCursor() to make it take effect.
    static void deleteIndentAndListMark(QTextCursor &p_cursor);

    // Find next @p_repeat empty block.
    // Returns the position of that block if found. Otherwise, returns -1.
    static int findNextEmptyBlock(const QTextCursor &p_cursor,
                                  bool p_forward,
                                  int p_repeat);

    // Check if we need to cancel auto indent.
    // @p_autoIndentPos: the position of the cursor after auto indent.
    static bool needToCancelAutoIndent(int p_autoIndentPos,
                                       const QTextCursor &p_cursor);

    // Insert title Mark at level @p_level in front of block @p_block
    // If there already exists title marks, remove it first.
    // Move cursor at the end of the block after insertion.
    // If @p_level is 0, remove the title mark.
    static void insertTitleMark(QTextCursor &p_cursor,
                                const QTextBlock &p_block,
                                int p_level);

    // Find the start and end of the word @p_cursor locates in (within a single block).
    // @p_start and @p_end will be the global position of the start and end of the word.
    // @p_start will equals to @p_end if @p_cursor is a space.
    static void findCurrentWord(QTextCursor p_cursor,
                                int &p_start,
                                int &p_end);

    // Find the start and end of the WORD @p_cursor locates in (within a single block).
    // @p_start and @p_end will be the global position of the start and end of the WORD.
    // @p_start will equals to @p_end if @p_cursor is a space.
    static void findCurrentWORD(const QTextCursor &p_cursor,
                                int &p_start,
                                int &p_end);

    // Return the leading spaces of @p_block.
    static QString fetchIndentSpaces(const QTextBlock &p_block);

    static int fetchIndentation(const QString &p_text);

    static int fetchIndentation(const QTextBlock &p_block);

    // Insert a block above/below current block. Move the cursor to the start of
    // the new block after insertion.
    static void insertBlock(QTextCursor &p_cursor,
                            bool p_above);

    // Insert @p_str in the front of each line of @p_text.
    static void insertBeforeEachLine(QString &p_text, const QString &p_str);

    // Whether @p_block is empty.
    static bool isEmptyBlock(const QTextBlock &p_block);

    static bool isWordSeparator(QChar p_char);

private:
    VEditUtils() {}
};

#endif // VEDITUTILS_H
