#include <QTextCursor>
#include <QTextDocument>
#include "vedit.h"
#include "veditoperations.h"

VEditOperations::VEditOperations(VEdit *editor, VNoteFile *noteFile)
    : editor(editor), noteFile(noteFile)
{
}

void VEditOperations::insertTextAtCurPos(const QString &text)
{
    QTextCursor cursor(editor->document());
    cursor.setPosition(editor->textCursor().position());
    cursor.insertText(text);
}
