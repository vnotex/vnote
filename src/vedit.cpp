#include <QtWidgets>
#include <QVector>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "hgmarkdownhighlighter.h"
#include "vmdeditoperations.h"
#include "vtoc.h"

extern VConfigManager vconfig;

VEdit::VEdit(VNoteFile *noteFile, QWidget *parent)
    : QTextEdit(parent), noteFile(noteFile), mdHighlighter(NULL)
{
    if (noteFile->docType == DocType::Markdown) {
        setAcceptRichText(false);
        mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                  500, document());
        connect(mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
                this, &VEdit::generateEditOutline);
        editOps = new VMdEditOperations(this, noteFile);
    } else {
        setAutoFormatting(QTextEdit::AutoBulletList);
        editOps = NULL;
    }

    updateTabSettings();
    updateFontAndPalette();
    connect(this, &VEdit::cursorPositionChanged,
            this, &VEdit::updateCurHeader);
}

VEdit::~VEdit()
{
    if (editOps) {
        delete editOps;
        editOps = NULL;
    }
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

bool VEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls()
           || QTextEdit::canInsertFromMimeData(source);
}

void VEdit::insertFromMimeData(const QMimeData *source)
{
    if (source->hasImage()) {
        // Image data in the clipboard
        if (editOps) {
            bool ret = editOps->insertImageFromMimeData(source);
            if (ret) {
                return;
            }
        }
    } else if (source->hasUrls()) {
        // Paste an image file
        if (editOps) {
            bool ret = editOps->insertURLFromMimeData(source);
            if (ret) {
                return;
            }
        }
    }
    QTextEdit::insertFromMimeData(source);
}

void VEdit::generateEditOutline()
{
    QTextDocument *doc = document();
    headers.clear();
    // Assume that each block contains only one line
    // Only support # syntax for now
    QRegExp headerReg("(#{1,6})\\s*(\\S.*)");  // Need to trim the spaces
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        Q_ASSERT(block.lineCount() == 1);
        if ((block.userState() == HighlightBlockState::BlockNormal) &&
            headerReg.exactMatch(block.text())) {
            VHeader header(headerReg.cap(1).length(),
                           headerReg.cap(2).trimmed(), "", block.firstLineNumber());
            headers.append(header);
        }
    }

    emit headersChanged(headers);
    updateCurHeader();
}

void VEdit::scrollToLine(int lineNumber)
{
    Q_ASSERT(lineNumber >= 0);

    // Move the cursor to the end first
    moveCursor(QTextCursor::End);
    QTextCursor cursor(document()->findBlockByLineNumber(lineNumber));
    cursor.movePosition(QTextCursor::EndOfLine);
    setTextCursor(cursor);

    setFocus();
}

void VEdit::updateCurHeader()
{
    int curHeader = 0;
    QTextCursor cursor(this->textCursor());
    int curLine = cursor.block().firstLineNumber();
    for (int i = headers.size() - 1; i >= 0; --i) {
        if (headers[i].lineNumber <= curLine) {
            curHeader = headers[i].lineNumber;
            break;
        }
    }
    emit curHeaderChanged(curHeader);
}
