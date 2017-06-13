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

private:
    VEditUtils() {}
};

#endif // VEDITUTILS_H
