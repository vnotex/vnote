#include "veditutils.h"

#include <QTextDocument>
#include <QDebug>

void VEditUtils::removeBlock(QTextBlock &p_block, QString *p_text)
{
    QTextCursor cursor(p_block);
    removeBlock(cursor, p_text);
}

void VEditUtils::removeBlock(QTextCursor &p_cursor, QString *p_text)
{
    const QTextDocument *doc = p_cursor.document();
    int blockCount = doc->blockCount();
    int blockNum = p_cursor.block().blockNumber();

    p_cursor.select(QTextCursor::BlockUnderCursor);
    if (p_text) {
        *p_text = p_cursor.selectedText() + "\n";
    }

    p_cursor.deleteChar();

    // Deleting the first block will leave an empty block.
    // Deleting the last empty block will not work with deleteChar().
    if (blockCount == doc->blockCount()) {
        if (blockNum == blockCount - 1) {
            // The last block.
            p_cursor.deletePreviousChar();
        } else {
            p_cursor.deleteChar();
        }
    }

    if (p_cursor.block().blockNumber() < blockNum) {
        p_cursor.movePosition(QTextCursor::NextBlock);
    }

    p_cursor.movePosition(QTextCursor::StartOfBlock);
}
