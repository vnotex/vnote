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

bool VEdit::findText(const QString &p_text, uint p_options, bool p_peek,
                     bool p_forward)
{
    static int startPos = textCursor().selectionStart();
    static int lastPos = startPos;
    bool found = false;

    if (p_text.isEmpty() && p_peek) {
        // Clear previous selection
        QTextCursor cursor = textCursor();
        cursor.clearSelection();
        cursor.setPosition(startPos);
        setTextCursor(cursor);
    } else {
        QTextCursor cursor = textCursor();
        if (p_peek) {
            int curPos = cursor.selectionStart();
            if (curPos != lastPos) {
                // Cursor has been moved. Just start at current position.
                startPos = curPos;
                lastPos = curPos;
            } else {
                // Move cursor to startPos to search.
                cursor.setPosition(startPos);
                setTextCursor(cursor);
            }
        }

        // Options
        QTextDocument::FindFlags flags;
        if (p_options & FindOption::CaseSensitive) {
            flags |= QTextDocument::FindCaseSensitively;
        }
        if (p_options & FindOption::WholeWordOnly) {
            flags |= QTextDocument::FindWholeWords;
        }
        if (!p_forward) {
            flags |= QTextDocument::FindBackward;
        }
        // Use regular expression
        if (p_options & FindOption::RegularExpression) {
            QRegExp exp(p_text,
                        p_options & FindOption::CaseSensitive ?
                        Qt::CaseSensitive : Qt::CaseInsensitive);
            found = find(exp, flags);
        } else {
            found = find(p_text, flags);
        }
        cursor = textCursor();
        if (!p_peek) {
            startPos = cursor.selectionStart();
        }
        lastPos = cursor.selectionStart();
    }
    return found;
}

void VEdit::replaceText(const QString &p_text, uint p_options,
                        const QString &p_replaceText, bool p_findNext)
{
    // Options
    QTextDocument::FindFlags flags;
    if (p_options & FindOption::CaseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (p_options & FindOption::WholeWordOnly) {
        flags |= QTextDocument::FindWholeWords;
    }

    bool useRegExp = false;
    QRegExp exp;
    if (p_options & FindOption::RegularExpression) {
        useRegExp = true;
        exp = QRegExp(p_text,
                      p_options & FindOption::CaseSensitive ?
                      Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        // Replace occurs only if the selected text matches @p_text with @p_options.
        QTextCursor matchCursor;
        if (useRegExp) {
            matchCursor = document()->find(exp, cursor.selectionStart(),
                                           flags);
        } else {
            matchCursor = document()->find(p_text, cursor.selectionStart(),
                                           flags);
        }
        bool matched = (cursor.selectionStart() == matchCursor.selectionStart())
                       && (cursor.selectionEnd() == matchCursor.selectionEnd());
        if (matched) {
            cursor.beginEditBlock();
            cursor.removeSelectedText();
            cursor.insertText(p_replaceText);
            cursor.endEditBlock();
            setTextCursor(cursor);
        }
    }
    if (p_findNext) {
        findText(p_text, p_options, false, true);
    }
}

void VEdit::replaceTextAll(const QString &p_text, uint p_options,
                           const QString &p_replaceText)
{
    // Options
    QTextDocument::FindFlags flags;
    if (p_options & FindOption::CaseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (p_options & FindOption::WholeWordOnly) {
        flags |= QTextDocument::FindWholeWords;
    }

    bool useRegExp = false;
    QRegExp exp;
    if (p_options & FindOption::RegularExpression) {
        useRegExp = true;
        exp = QRegExp(p_text,
                      p_options & FindOption::CaseSensitive ?
                      Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    QTextCursor cursor = textCursor();
    int pos = cursor.position();
    int nrReplaces = 0;
    int startPos = 0;
    int lastMatch = -1;
    while (true) {
        QTextCursor matchCursor;
        if (useRegExp) {
            matchCursor = document()->find(exp, startPos, flags);
        } else {
            matchCursor = document()->find(p_text, startPos, flags);
        }
        if (matchCursor.isNull()) {
            break;
        } else {
            if (matchCursor.selectionStart() <= lastMatch) {
                // Wrap back.
                break;
            } else {
                lastMatch = matchCursor.selectionStart();
            }
            nrReplaces++;
            matchCursor.beginEditBlock();
            matchCursor.removeSelectedText();
            matchCursor.insertText(p_replaceText);
            matchCursor.endEditBlock();
            setTextCursor(matchCursor);
            startPos = matchCursor.position();
        }
    }
    // Restore cursor position.
    cursor.setPosition(pos);
    setTextCursor(cursor);
    qDebug() << "replace all" << nrReplaces << "occurencs";
}

