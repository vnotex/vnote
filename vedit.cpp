#include <QtWidgets>
#include "vedit.h"

VEdit::VEdit(VNoteFile *noteFile, QWidget *parent)
    : QTextEdit(parent), noteFile(noteFile)
{
}

void VEdit::beginEdit()
{
    setReadOnly(false);
    switch (noteFile->docType) {
    case DocType::Html:
        setHtml(noteFile->content);
        break;
    case DocType::Markdown:
        setPlainText(noteFile->content);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
}

bool VEdit::tryEndEdit()
{
    return !document()->isModified();
}

void VEdit::beginSave()
{
    if (!document()->isModified()) {
        return;
    }

    switch (noteFile->docType) {
    case DocType::Html:
        noteFile->content = toHtml();
        break;
    case DocType::Markdown:

        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
}

void VEdit::endSave()
{
    document()->setModified(false);
}

void VEdit::reloadFile()
{
    switch (noteFile->docType) {
    case DocType::Html:
        setHtml(noteFile->content);
        break;
    case DocType::Markdown:
        setPlainText(noteFile->content);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
}
