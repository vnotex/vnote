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
        setAcceptRichText(false);
        mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                  500, document());
    } else {
        setAutoFormatting(QTextEdit::AutoBulletList);
    }

    updateTabSettings();
    updateFontAndPalette();
}

void VEdit::updateFontAndPalette()
{
    switch (noteFile->docType) {
    case DocType::Markdown:
        setFont(vconfig.getMdEditFont());
        setPalette(vconfig.getMdEditPalette());
        break;
    case DocType::Html:
        setFont(vconfig.getBaseEditFont());
        setPalette(vconfig.getBaseEditPalette());
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
        return;
    }
}

void VEdit::updateTabSettings()
{
    switch (noteFile->docType) {
    case DocType::Markdown:
        if (vconfig.getTabStopWidth() > 0) {
            QFontMetrics metrics(vconfig.getMdEditFont());
            setTabStopWidth(vconfig.getTabStopWidth() * metrics.width(' '));
        }
        break;
    case DocType::Html:
        if (vconfig.getTabStopWidth() > 0) {
            QFontMetrics metrics(vconfig.getBaseEditFont());
            setTabStopWidth(vconfig.getTabStopWidth() * metrics.width(' '));
        }
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
        return;
    }

    isExpandTab = vconfig.getIsExpandTab();
    if (isExpandTab && (vconfig.getTabStopWidth() > 0)) {
        tabSpaces = QString(vconfig.getTabStopWidth(), ' ');
    }
}

void VEdit::beginEdit()
{
    setReadOnly(false);
    updateTabSettings();
    updateFontAndPalette();
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

void VEdit::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Tab) && isExpandTab) {
        QTextCursor cursor(document());
        cursor.setPosition(textCursor().position());
        cursor.insertText(tabSpaces);
        return;
    }
    QTextEdit::keyPressEvent(event);
}
