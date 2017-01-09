#include <QtWidgets>
#include <QVector>
#include <QDebug>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"
#include "veditoperations.h"
#include "dialog/vfindreplacedialog.h"

extern VConfigManager vconfig;

VEdit::VEdit(VFile *p_file, QWidget *p_parent)
    : QTextEdit(p_parent), m_file(p_file), m_editOps(NULL)
{
    const int labelTimerInterval = 500;
    const int labelSize = 64;

    QPixmap wrapPixmap(":/resources/icons/search_wrap.svg");
    m_wrapLabel = new QLabel(this);
    m_wrapLabel->setPixmap(wrapPixmap.scaled(labelSize, labelSize));
    m_wrapLabel->hide();
    m_labelTimer = new QTimer(this);
    m_labelTimer->setSingleShot(true);
    m_labelTimer->setInterval(labelTimerInterval);
    connect(m_labelTimer, &QTimer::timeout,
            this, &VEdit::labelTimerTimeout);
    connect(document(), &QTextDocument::modificationChanged,
            (VFile *)m_file, &VFile::setModified);
}

VEdit::~VEdit()
{
    if (m_file) {
        disconnect(document(), &QTextDocument::modificationChanged,
                   (VFile *)m_file, &VFile::setModified);
    }
}

void VEdit::beginEdit()
{
    setReadOnly(false);
    setModified(false);
}

void VEdit::endEdit()
{
    setReadOnly(true);
}

void VEdit::saveFile()
{
    if (!document()->isModified()) {
        return;
    }
    m_file->setContent(toHtml());
    document()->setModified(false);
}

void VEdit::reloadFile()
{
    setHtml(m_file->getContent());
    setModified(false);
}

void VEdit::scrollToLine(int p_lineNumber)
{
    Q_ASSERT(p_lineNumber >= 0);

    // Move the cursor to the end first
    moveCursor(QTextCursor::End);
    QTextCursor cursor(document()->findBlockByLineNumber(p_lineNumber));
    cursor.movePosition(QTextCursor::EndOfBlock);
    setTextCursor(cursor);

    setFocus();
}

bool VEdit::isModified() const
{
    return document()->isModified();
}

void VEdit::setModified(bool p_modified)
{
    document()->setModified(p_modified);
    if (m_file) {
        m_file->setModified(p_modified);
    }
}

void VEdit::insertImage()
{
    if (m_editOps) {
        m_editOps->insertImage();
    }
}

bool VEdit::peekText(const QString &p_text, uint p_options)
{
    static int startPos = textCursor().selectionStart();
    static int lastPos = startPos;
    bool found = false;

    if (p_text.isEmpty()) {
        // Clear previous selection
        QTextCursor cursor = textCursor();
        cursor.clearSelection();
        cursor.setPosition(startPos);
        setTextCursor(cursor);
    } else {
        QTextCursor cursor = textCursor();
        int curPos = cursor.selectionStart();
        if (curPos != lastPos) {
            // Cursor has been moved. Just start at current potition.
            startPos = curPos;
            lastPos = curPos;
        } else {
            cursor.setPosition(startPos);
            setTextCursor(cursor);
        }
    }
    bool wrapped = false;
    found = findTextHelper(p_text, p_options, true, wrapped);
    if (found) {
        lastPos = textCursor().selectionStart();
        found = true;
    }
    return found;
}

bool VEdit::findTextHelper(const QString &p_text, uint p_options,
                           bool p_forward, bool &p_wrapped)
{
    p_wrapped = false;
    bool found = false;

    // Options
    QTextDocument::FindFlags findFlags;
    bool caseSensitive = false;
    if (p_options & FindOption::CaseSensitive) {
        findFlags |= QTextDocument::FindCaseSensitively;
        caseSensitive = true;
    }
    if (p_options & FindOption::WholeWordOnly) {
        findFlags |= QTextDocument::FindWholeWords;
    }
    if (!p_forward) {
        findFlags |= QTextDocument::FindBackward;
    }
    // Use regular expression
    bool useRegExp = false;
    QRegExp exp;
    if (p_options & FindOption::RegularExpression) {
        useRegExp = true;
        exp = QRegExp(p_text,
                      caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }
    QTextCursor cursor = textCursor();
    while (!found) {
        if (useRegExp) {
            found = find(exp, findFlags);
        } else {
            found = find(p_text, findFlags);
        }
        if (p_wrapped) {
            if (!found) {
                setTextCursor(cursor);
            }
            break;
        }
        if (!found) {
            // Wrap to the other end of the document to search again.
            p_wrapped = true;
            QTextCursor wrapCursor = textCursor();
            wrapCursor.clearSelection();
            if (p_forward) {
                wrapCursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
            } else {
                wrapCursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
            }
            setTextCursor(wrapCursor);
        }
    }
    return found;
}

bool VEdit::findText(const QString &p_text, uint p_options, bool p_forward)
{
    bool found = false;
    if (p_text.isEmpty()) {
        QTextCursor cursor = textCursor();
        cursor.clearSelection();
        setTextCursor(cursor);
    } else {
        bool wrapped = false;
        found = findTextHelper(p_text, p_options, p_forward, wrapped);
        if (found && wrapped) {
            showWrapLabel();
        }
    }
    qDebug() << "findText" << p_text << p_options << p_forward
             << (found ? "Found" : "NotFound");
    return found;
}

void VEdit::replaceText(const QString &p_text, uint p_options,
                        const QString &p_replaceText, bool p_findNext)
{
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        // Replace occurs only if the selected text matches @p_text with @p_options.
        QTextCursor tmpCursor = cursor;
        tmpCursor.setPosition(tmpCursor.selectionStart());
        tmpCursor.clearSelection();
        setTextCursor(tmpCursor);
        bool wrapped = false;
        bool found = findTextHelper(p_text, p_options, true, wrapped);
        bool matched = false;
        if (found) {
            tmpCursor = textCursor();
            matched = (cursor.selectionStart() == tmpCursor.selectionStart())
                      && (cursor.selectionEnd() == tmpCursor.selectionEnd());
        }
        if (matched) {
            cursor.beginEditBlock();
            cursor.removeSelectedText();
            cursor.insertText(p_replaceText);
            cursor.endEditBlock();
            setTextCursor(cursor);
        } else {
            setTextCursor(cursor);
        }
    }
    if (p_findNext) {
        findText(p_text, p_options, true);
    }
}

void VEdit::replaceTextAll(const QString &p_text, uint p_options,
                           const QString &p_replaceText)
{
    // Replace from the start to the end and resotre the cursor.
    QTextCursor cursor = textCursor();
    int nrReplaces = 0;
    QTextCursor tmpCursor = cursor;
    tmpCursor.setPosition(0);
    setTextCursor(tmpCursor);
    while (true) {
        bool wrapped = false;
        bool found = findTextHelper(p_text, p_options, true, wrapped);
        if (!found) {
            break;
        } else {
            if (wrapped) {
                // Wrap back.
                break;
            }
            nrReplaces++;
            tmpCursor = textCursor();
            tmpCursor.beginEditBlock();
            tmpCursor.removeSelectedText();
            tmpCursor.insertText(p_replaceText);
            tmpCursor.endEditBlock();
            setTextCursor(tmpCursor);
        }
    }
    // Restore cursor position.
    cursor.clearSelection();
    setTextCursor(cursor);
    qDebug() << "replace all" << nrReplaces << "occurences";
}

void VEdit::showWrapLabel()
{
    int labelW = m_wrapLabel->width();
    int labelH = m_wrapLabel->height();
    int x = (width() - labelW) / 2;
    int y = (height() - labelH) / 2;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    m_wrapLabel->move(x, y);
    m_wrapLabel->show();
    m_labelTimer->stop();
    m_labelTimer->start();
}

void VEdit::labelTimerTimeout()
{
    m_wrapLabel->hide();
}
