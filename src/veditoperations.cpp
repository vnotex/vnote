#include <QTextCursor>
#include <QTextDocument>
#include "vedit.h"
#include "veditoperations.h"

VEditOperations::VEditOperations(VEdit *p_editor, VFile *p_file)
    : m_editor(p_editor), m_file(p_file)
{
}

void VEditOperations::insertTextAtCurPos(const QString &text)
{
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(m_editor->textCursor().position());
    cursor.insertText(text);
}

VEditOperations::~VEditOperations()
{

}
