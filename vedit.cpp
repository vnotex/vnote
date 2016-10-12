#include <QtWidgets>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"

VEdit::VEdit(VNoteFile *noteFile, QWidget *parent)
    : QTextEdit(parent), noteFile(noteFile)
{
    setFont(VConfigInst->baseEditFont);
    if (noteFile->docType == DocType::Markdown) {
        setPalette(VConfigInst->mdEditPalette);
    }
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
        noteFile->content = toPlainText();
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
