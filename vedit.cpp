#include <QtWidgets>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "hgmarkdownhighlighter.h"

extern VConfigManager vconfig;

VEdit::VEdit(VNoteFile *noteFile, QWidget *parent)
    : QTextEdit(parent), noteFile(noteFile), mdHighlighter(NULL)
{
    if (noteFile->docType == DocType::Markdown) {
        setPalette(vconfig.getMdEditPalette());
        setFont(vconfig.getMdEditFont());
        setAcceptRichText(false);

        mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                  500, document());
    } else {
        setFont(vconfig.getBaseEditFont());
        setAutoFormatting(QTextEdit::AutoBulletList);
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
        setFont(vconfig.getMdEditFont());
        setPlainText(noteFile->content);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
}

void VEdit::saveFile()
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
