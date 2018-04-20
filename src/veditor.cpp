#include "veditor.h"

#include <QtWidgets>
#include <QTextDocument>

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "veditoperations.h"
#include "dialog/vinsertlinkdialog.h"
#include "utils/vmetawordmanager.h"
#include "utils/vvim.h"

extern VConfigManager *g_config;

extern VMetaWordManager *g_mwMgr;

VEditor::VEditor(VFile *p_file, QWidget *p_editor)
    : m_editor(p_editor),
      m_object(new VEditorObject(this, p_editor)),
      m_file(p_file),
      m_editOps(nullptr),
      m_document(nullptr),
      m_enableInputMethod(true)
{
}

VEditor::~VEditor()
{
}

void VEditor::init()
{
    const int labelTimerInterval = 500;
    const int extraSelectionHighlightTimer = 500;
    const int labelSize = 64;

    m_document = documentW();

    m_selectedWordFg = QColor(g_config->getEditorSelectedWordFg());
    m_selectedWordBg = QColor(g_config->getEditorSelectedWordBg());

    m_searchedWordFg = QColor(g_config->getEditorSearchedWordFg());
    m_searchedWordBg = QColor(g_config->getEditorSearchedWordBg());

    m_searchedWordCursorFg = QColor(g_config->getEditorSearchedWordCursorFg());
    m_searchedWordCursorBg = QColor(g_config->getEditorSearchedWordCursorBg());

    m_incrementalSearchedWordFg = QColor(g_config->getEditorIncrementalSearchedWordFg());
    m_incrementalSearchedWordBg = QColor(g_config->getEditorIncrementalSearchedWordBg());

    m_trailingSpaceColor = QColor(g_config->getEditorTrailingSpaceBg());

    QPixmap wrapPixmap(":/resources/icons/search_wrap.svg");
    m_wrapLabel = new QLabel(m_editor);
    m_wrapLabel->setPixmap(wrapPixmap.scaled(labelSize, labelSize));
    m_wrapLabel->hide();
    m_labelTimer = new QTimer(m_editor);
    m_labelTimer->setSingleShot(true);
    m_labelTimer->setInterval(labelTimerInterval);
    QObject::connect(m_labelTimer, &QTimer::timeout,
                     m_object, &VEditorObject::labelTimerTimeout);

    m_highlightTimer = new QTimer(m_editor);
    m_highlightTimer->setSingleShot(true);
    m_highlightTimer->setInterval(extraSelectionHighlightTimer);
    QObject::connect(m_highlightTimer, &QTimer::timeout,
                     m_object, &VEditorObject::doHighlightExtraSelections);

    m_extraSelections.resize((int)SelectionId::MaxSelection);

    updateFontAndPalette();

    m_config.init(QFontMetrics(m_editor->font()), false);
    updateEditConfig();
}

void VEditor::labelTimerTimeout()
{
    m_wrapLabel->hide();
}

void VEditor::doHighlightExtraSelections()
{
    int nrExtra = m_extraSelections.size();
    Q_ASSERT(nrExtra == (int)SelectionId::MaxSelection);
    QList<QTextEdit::ExtraSelection> extraSelects;
    for (int i = 0; i < nrExtra; ++i) {
        extraSelects.append(m_extraSelections[i]);
    }

    setExtraSelectionsW(extraSelects);
}

void VEditor::updateEditConfig()
{
    m_config.update(QFontMetrics(m_editor->font()));

    if (m_config.m_tabStopWidth > 0) {
        setTabStopWidthW(m_config.m_tabStopWidth);
    }

    emit m_object->configUpdated();
}

void VEditor::highlightOnCursorPositionChanged()
{
    static QTextCursor lastCursor;

    QTextCursor cursor = textCursorW();
    if (lastCursor.isNull() || cursor.blockNumber() != lastCursor.blockNumber()) {
        highlightCurrentLine();
        highlightTrailingSpace();
    } else {
        // Judge whether we have trailing space at current line.
        QString text = cursor.block().text();
        if (text.rbegin()->isSpace()) {
            highlightTrailingSpace();
        }

        // Handle word-wrap in one block.
        // Highlight current line if in different visual line.
        if ((lastCursor.positionInBlock() - lastCursor.columnNumber()) !=
            (cursor.positionInBlock() - cursor.columnNumber())) {
            highlightCurrentLine();
        }
    }

    lastCursor = cursor;
}

void VEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::CurrentLine];
    if (g_config->getHighlightCursorLine()) {
        // Need to highlight current line.
        selects.clear();

        // A long block maybe splited into multiple visual lines.
        QTextEdit::ExtraSelection select;
        select.format.setBackground(m_config.m_cursorLineBg);
        select.format.setProperty(QTextFormat::FullWidthSelection, true);

        QTextCursor cursor = textCursorW();
        if (m_config.m_highlightWholeBlock) {
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor, 1);
            QTextBlock block = cursor.block();
            int blockEnd = block.position() + block.length();
            int pos = -1;
            while (cursor.position() < blockEnd && pos != cursor.position()) {
                QTextEdit::ExtraSelection newSelect = select;
                newSelect.cursor = cursor;
                selects.append(newSelect);

                pos = cursor.position();
                cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, 1);
            }
        } else {
            cursor.clearSelection();
            select.cursor = cursor;
            selects.append(select);
        }
    } else {
        // Need to clear current line highlight.
        if (selects.isEmpty()) {
            return;
        }

        selects.clear();
    }

    highlightExtraSelections(true);
}

// Do not highlight trailing spaces with current cursor right behind.
static void trailingSpaceFilter(VEditor *p_editor, QList<QTextEdit::ExtraSelection> &p_result)
{
    QTextCursor cursor = p_editor->textCursorW();
    if (!cursor.atBlockEnd()) {
        return;
    }

    int cursorPos = cursor.position();
    for (auto it = p_result.begin(); it != p_result.end(); ++it) {
        if (it->cursor.selectionEnd() == cursorPos) {
            p_result.erase(it);

            // There will be only one.
            return;
        }
    }
}

void VEditor::highlightTrailingSpace()
{
    if (!g_config->getEnableTrailingSpaceHighlight()) {
        QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::TrailingSapce];
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }
        return;
    }

    QTextCharFormat format;
    format.setBackground(m_trailingSpaceColor);
    QString text("\\s+$");
    highlightTextAll(text,
                     FindOption::RegularExpression,
                     SelectionId::TrailingSapce,
                     format,
                     trailingSpaceFilter);
}

void VEditor::highlightExtraSelections(bool p_now)
{
    m_highlightTimer->stop();
    if (p_now) {
        doHighlightExtraSelections();
    } else {
        m_highlightTimer->start();
    }
}

void VEditor::highlightTextAll(const QString &p_text,
                               uint p_options,
                               SelectionId p_id,
                               QTextCharFormat p_format,
                               void (*p_filter)(VEditor *,
                                                QList<QTextEdit::ExtraSelection> &))
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)p_id];
    if (!p_text.isEmpty()) {
        selects.clear();

        QList<QTextCursor> occurs = findTextAll(p_text, p_options);
        for (int i = 0; i < occurs.size(); ++i) {
            QTextEdit::ExtraSelection select;
            select.format = p_format;
            select.cursor = occurs[i];
            selects.append(select);
        }
    } else {
        if (selects.isEmpty()) {
            return;
        }
        selects.clear();
    }

    if (p_filter) {
        p_filter(this, selects);
    }

    highlightExtraSelections();
}

QList<QTextCursor> VEditor::findTextAll(const QString &p_text, uint p_options)
{
    QList<QTextCursor> results;
    if (p_text.isEmpty()) {
        return results;
    }

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

    // Use regular expression
    bool useRegExp = false;
    QRegExp exp;
    if (p_options & FindOption::RegularExpression) {
        useRegExp = true;
        exp = QRegExp(p_text,
                      caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    int startPos = 0;
    QTextCursor cursor;
    while (true) {
        if (useRegExp) {
            cursor = m_document->find(exp, startPos, findFlags);
        } else {
            cursor = m_document->find(p_text, startPos, findFlags);
        }

        if (cursor.isNull()) {
            break;
        } else {
            results.append(cursor);
            startPos = cursor.selectionEnd();
        }
    }

    return results;
}

void VEditor::highlightSelectedWord()
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SelectedWord];
    if (!g_config->getHighlightSelectedWord()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    QString text = textCursorW().selectedText().trimmed();
    if (text.isEmpty() || wordInSearchedSelection(text)) {
        selects.clear();
        highlightExtraSelections(true);
        return;
    }

    QTextCharFormat format;
    format.setForeground(m_selectedWordFg);
    format.setBackground(m_selectedWordBg);
    highlightTextAll(text,
                     FindOption::CaseSensitive,
                     SelectionId::SelectedWord,
                     format);
}

bool VEditor::wordInSearchedSelection(const QString &p_text)
{
    QString text = p_text.trimmed();
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeyword];
    for (int i = 0; i < selects.size(); ++i) {
        QString searchedWord = selects[i].cursor.selectedText();
        if (text == searchedWord.trimmed()) {
            return true;
        }
    }

    return false;
}

bool VEditor::isModified() const
{
    return m_document->isModified();
}

void VEditor::setModified(bool p_modified)
{
    m_document->setModified(p_modified);
}

void VEditor::insertImage()
{
    if (m_editOps) {
        m_editOps->insertImage();
    }
}

void VEditor::insertLink()
{
    if (!m_editOps) {
        return;
    }

    QString text;
    QString linkText, linkUrl;
    QTextCursor cursor = textCursorW();
    if (cursor.hasSelection()) {
        text = VEditUtils::selectedText(cursor).trimmed();
        // Only pure space is accepted.
        QRegExp reg("[\\S ]*");
        if (reg.exactMatch(text)) {
            QUrl url = QUrl::fromUserInput(text,
                                           m_file->fetchBasePath());
            QRegExp urlReg("[\\.\\\\/]");
            if (url.isValid()
                && text.contains(urlReg)) {
                // Url.
                linkUrl = text;
            } else {
                // Text.
                linkText = text;
            }
        }
    }

    VInsertLinkDialog dialog(QObject::tr("Insert Link"),
                             "",
                             "",
                             linkText,
                             linkUrl,
                             false,
                             m_editor);
    if (dialog.exec() == QDialog::Accepted) {
        linkText = dialog.getLinkText();
        linkUrl = dialog.getLinkUrl();
        Q_ASSERT(!linkText.isEmpty() && !linkUrl.isEmpty());

        m_editOps->insertLink(linkText, linkUrl);
    }
}

bool VEditor::peekText(const QString &p_text, uint p_options, bool p_forward)
{
    if (p_text.isEmpty()) {
        makeBlockVisible(m_document->findBlock(textCursorW().selectionStart()));
        highlightIncrementalSearchedWord(QTextCursor());
        return false;
    }

    bool wrapped = false;
    QTextCursor retCursor;
    bool found = findTextHelper(p_text,
                                p_options,
                                p_forward,
                                p_forward ? textCursorW().position() + 1
                                          : textCursorW().position(),
                                wrapped,
                                retCursor);
    if (found) {
        makeBlockVisible(m_document->findBlock(retCursor.selectionStart()));
        highlightIncrementalSearchedWord(retCursor);
    }

    return found;
}

bool VEditor::findText(const QString &p_text,
                       uint p_options,
                       bool p_forward,
                       QTextCursor *p_cursor,
                       QTextCursor::MoveMode p_moveMode,
                       bool p_useLeftSideOfCursor)
{
    clearIncrementalSearchedWordHighlight();

    if (p_text.isEmpty()) {
        clearSearchedWordHighlight();
        return false;
    }

    QTextCursor cursor = textCursorW();
    bool wrapped = false;
    QTextCursor retCursor;
    int matches = 0;
    int start = p_forward ? cursor.position() + 1 : cursor.position();
    if (p_cursor) {
        start = p_forward ? p_cursor->position() + 1 : p_cursor->position();
    }

    if (p_useLeftSideOfCursor) {
        --start;
    }

    bool found = findTextHelper(p_text, p_options, p_forward, start,
                                wrapped, retCursor);
    if (found) {
        Q_ASSERT(!retCursor.isNull());
        if (wrapped) {
            showWrapLabel();
        }

        if (p_cursor) {
            p_cursor->setPosition(retCursor.selectionStart(), p_moveMode);
        } else {
            cursor.setPosition(retCursor.selectionStart(), p_moveMode);
            setTextCursorW(cursor);
        }

        highlightSearchedWord(p_text, p_options);
        highlightSearchedWordUnderCursor(retCursor);
        matches = m_extraSelections[(int)SelectionId::SearchedKeyword].size();
    } else {
        clearSearchedWordHighlight();
    }

    if (matches == 0) {
        emit m_object->statusMessage(QObject::tr("Found no match"));
    } else {
        emit m_object->statusMessage(QObject::tr("Found %1 %2").arg(matches)
                                                               .arg(matches > 1 ? QObject::tr("matches")
                                                                                : QObject::tr("match")));
    }

    return found;
}

void VEditor::highlightIncrementalSearchedWord(const QTextCursor &p_cursor)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::IncrementalSearchedKeyword];
    if (!g_config->getHighlightSearchedWord() || !p_cursor.hasSelection()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    selects.clear();
    QTextEdit::ExtraSelection select;
    select.format.setForeground(m_incrementalSearchedWordFg);
    select.format.setBackground(m_incrementalSearchedWordBg);
    select.cursor = p_cursor;
    selects.append(select);

    highlightExtraSelections(true);
}

// Use QPlainTextEdit::find() instead of QTextDocument::find() because the later has
// bugs in searching backward.
bool VEditor::findTextHelper(const QString &p_text,
                             uint p_options,
                             bool p_forward,
                             int p_start,
                             bool &p_wrapped,
                             QTextCursor &p_cursor)
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

    // Store current state of the cursor.
    QTextCursor cursor = textCursorW();
    if (cursor.position() != p_start) {
        if (p_start < 0) {
            p_start = 0;
        } else if (p_start > m_document->characterCount()) {
            p_start = m_document->characterCount();
        }
    }

    QTextCursor startCursor = cursor;
    // Will clear the selection.
    startCursor.setPosition(p_start);
    setTextCursorW(startCursor);

    while (!found) {
        if (useRegExp) {
            found = findW(exp, findFlags);
        } else {
            found = findW(p_text, findFlags);
        }

        if (p_wrapped) {
            break;
        }

        if (!found) {
            // Wrap to the other end of the document to search again.
            p_wrapped = true;
            QTextCursor wrapCursor = textCursorW();
            if (p_forward) {
                wrapCursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
            } else {
                wrapCursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
            }

            setTextCursorW(wrapCursor);
        }
    }

    if (found) {
        p_cursor = textCursorW();
    }

    // Restore the original cursor.
    setTextCursorW(cursor);

    return found;
}

void VEditor::clearIncrementalSearchedWordHighlight(bool p_now)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::IncrementalSearchedKeyword];
    if (selects.isEmpty()) {
        return;
    }

    selects.clear();
    highlightExtraSelections(p_now);
}

void VEditor::clearSearchedWordHighlight()
{
    clearIncrementalSearchedWordHighlight(false);
    clearSearchedWordUnderCursorHighlight(false);

    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeyword];
    if (selects.isEmpty()) {
        return;
    }

    selects.clear();
    highlightExtraSelections(true);
}

void VEditor::clearSearchedWordUnderCursorHighlight(bool p_now)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeywordUnderCursor];
    if (selects.isEmpty()) {
        return;
    }

    selects.clear();
    highlightExtraSelections(p_now);
}

void VEditor::showWrapLabel()
{
    int labelW = m_wrapLabel->width();
    int labelH = m_wrapLabel->height();
    int x = (m_editor->width() - labelW) / 2;
    int y = (m_editor->height() - labelH) / 2;
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

void VEditor::highlightSearchedWord(const QString &p_text, uint p_options)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeyword];
    if (!g_config->getHighlightSearchedWord() || p_text.isEmpty()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    QTextCharFormat format;
    format.setForeground(m_searchedWordFg);
    format.setBackground(m_searchedWordBg);
    highlightTextAll(p_text, p_options, SelectionId::SearchedKeyword, format);
}

void VEditor::highlightSearchedWordUnderCursor(const QTextCursor &p_cursor)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeywordUnderCursor];
    if (!p_cursor.hasSelection()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    selects.clear();
    QTextEdit::ExtraSelection select;
    select.format.setForeground(m_searchedWordCursorFg);
    select.format.setBackground(m_searchedWordCursorBg);
    select.cursor = p_cursor;
    selects.append(select);

    highlightExtraSelections(true);
}

static void fillReplaceTextWithBackReference(QString &p_replaceText,
                                             const QString &p_text,
                                             const QRegExp &p_exp)
{
    int idx = p_exp.indexIn(p_text);
    if (idx != 0) {
        return;
    }

    QStringList caps = p_exp.capturedTexts();
    if (caps.size() < 2) {
        return;
    }

    QChar sla('\\');
    int pos = 0;
    while (pos < p_replaceText.size()) {
        int idx = p_replaceText.indexOf(sla, pos);
        if (idx == -1 || idx == p_replaceText.size() - 1) {
            break;
        }

        QChar ne(p_replaceText[idx + 1]);
        if (ne == sla) {
            // \\ to \.
            p_replaceText.remove(idx, 1);
        } else if (ne.isDigit()) {
            // TODO: for now, we only support 1 - 9.
            int num = ne.digitValue();
            if (num > 0 && num < caps.size()) {
                // Replace it.
                p_replaceText.replace(idx, 2, caps[num]);
                pos = idx + caps[num].size() - 2;
                continue;
            }
        }

        pos = idx + 1;
    }
}

void VEditor::replaceText(const QString &p_text,
                          uint p_options,
                          const QString &p_replaceText,
                          bool p_findNext)
{
    QTextCursor cursor = textCursorW();
    bool wrapped = false;
    QTextCursor retCursor;
    QString newText(p_replaceText);
    bool useRegExp = p_options & FindOption::RegularExpression;
    QRegExp exp;
    if (useRegExp) {
        exp = QRegExp(p_text,
                      p_options & FindOption::CaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    bool found = findTextHelper(p_text,
                                p_options,
                                true,
                                cursor.position(),
                                wrapped,
                                retCursor);
    if (found) {
        if (retCursor.selectionStart() == cursor.position()) {
            // Matched.
            if (useRegExp) {
                // Handle \1, \2 in replace text.
                fillReplaceTextWithBackReference(newText,
                                                 VEditUtils::selectedText(retCursor),
                                                 exp);
            }

            retCursor.beginEditBlock();
            retCursor.insertText(newText);
            retCursor.endEditBlock();
            setTextCursorW(retCursor);
        }

        if (p_findNext) {
            findText(p_text, p_options, true);
        }
    }
}

void VEditor::replaceTextAll(const QString &p_text,
                             uint p_options,
                             const QString &p_replaceText)
{
    // Replace from the start to the end and restore the cursor.
    QTextCursor cursor = textCursorW();
    int nrReplaces = 0;
    QTextCursor tmpCursor = cursor;
    tmpCursor.setPosition(0);
    setTextCursorW(tmpCursor);
    int start = tmpCursor.position();

    QString newText;
    bool useRegExp = p_options & FindOption::RegularExpression;
    QRegExp exp;
    if (useRegExp) {
        exp = QRegExp(p_text,
                      p_options & FindOption::CaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    } else {
        newText = p_replaceText;
    }

    while (true) {
        bool wrapped = false;
        QTextCursor retCursor;
        bool found = findTextHelper(p_text,
                                    p_options,
                                    true,
                                    start,
                                    wrapped,
                                    retCursor);
        if (!found) {
            break;
        } else {
            if (wrapped) {
                // Wrap back.
                break;
            }

            if (useRegExp) {
                newText = p_replaceText;
                fillReplaceTextWithBackReference(newText,
                                                 VEditUtils::selectedText(retCursor),
                                                 exp);
            }

            nrReplaces++;
            retCursor.beginEditBlock();
            retCursor.insertText(newText);
            retCursor.endEditBlock();
            setTextCursorW(retCursor);
            start = retCursor.position();
        }
    }

    // Restore cursor position.
    cursor.clearSelection();
    setTextCursorW(cursor);
    qDebug() << "replace all" << nrReplaces << "occurences";

    emit m_object->statusMessage(QObject::tr("Replace %1 %2").arg(nrReplaces)
                                                             .arg(nrReplaces > 1 ? QObject::tr("occurences")
                                                                                 : QObject::tr("occurence")));
}

void VEditor::evaluateMagicWords()
{
    QString text;
    QTextCursor cursor = textCursorW();
    if (!cursor.hasSelection()) {
        // Get the WORD in current cursor.
        int start, end;
        VEditUtils::findCurrentWORD(cursor, start, end);

        if (start == end) {
            return;
        } else {
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
        }
    }

    text = VEditUtils::selectedText(cursor);
    Q_ASSERT(!text.isEmpty());
    QString evaText = g_mwMgr->evaluate(text);
    if (text != evaText) {
        qDebug() << "evaluateMagicWords" << text << evaText;

        cursor.insertText(evaText);

        if (m_editOps) {
            m_editOps->setVimMode(VimMode::Insert);
        }

        setTextCursorW(cursor);
    }
}

void VEditor::setReadOnlyAndHighlightCurrentLine(bool p_readonly)
{
    setReadOnlyW(p_readonly);
    highlightCurrentLine();
}

bool VEditor::handleMousePressEvent(QMouseEvent *p_event)
{
    if (p_event->button() == Qt::LeftButton
        && p_event->modifiers() == Qt::ControlModifier
        && !textCursorW().hasSelection()) {
        m_oriMouseX = p_event->x();
        m_oriMouseY = p_event->y();
        m_readyToScroll = true;
        m_mouseMoveScrolled = false;
        p_event->accept();
        return true;
    }

    m_readyToScroll = false;
    m_mouseMoveScrolled = false;

    return false;
}

bool VEditor::handleMouseReleaseEvent(QMouseEvent *p_event)
{
    if (m_mouseMoveScrolled || m_readyToScroll) {
        viewportW()->setCursor(Qt::IBeamCursor);
        m_readyToScroll = false;
        m_mouseMoveScrolled = false;
        p_event->accept();
        return true;
    }

    m_readyToScroll = false;
    m_mouseMoveScrolled = false;

    return false;
}

bool VEditor::handleMouseMoveEvent(QMouseEvent *p_event)
{
    const int threshold = 5;

    if (m_readyToScroll) {
        int deltaX = p_event->x() - m_oriMouseX;
        int deltaY = p_event->y() - m_oriMouseY;

        if (qAbs(deltaX) >= threshold || qAbs(deltaY) >= threshold) {
            m_oriMouseX = p_event->x();
            m_oriMouseY = p_event->y();

            if (!m_mouseMoveScrolled) {
                m_mouseMoveScrolled = true;
                viewportW()->setCursor(Qt::SizeAllCursor);
            }

            QScrollBar *verBar = verticalScrollBarW();
            QScrollBar *horBar = horizontalScrollBarW();
            if (verBar->isVisible()) {
                verBar->setValue(verBar->value() - deltaY);
            }

            if (horBar->isVisible()) {
                horBar->setValue(horBar->value() - deltaX);
            }
        }

        p_event->accept();
        return true;
    }

    return false;
}

bool VEditor::handleWheelEvent(QWheelEvent *p_event)
{
    Qt::KeyboardModifiers modifiers = p_event->modifiers();
    if (modifiers == Qt::ShiftModifier) {
        // Scroll horizontally.
        QPoint numPixels = p_event->pixelDelta();
        QPoint numDegrees = p_event->angleDelta() / 8;

        QScrollBar *horBar = horizontalScrollBarW();
        int steps = 0;
        if (!numPixels.isNull()) {
            steps = numPixels.y();
        } else if (!numDegrees.isNull()) {
            QPoint numSteps = numDegrees / 15;
            steps = numSteps.y() * horBar->singleStep();
        }

        if (horBar->minimum() != horBar->maximum()) {
            horBar->setValue(horBar->value() - steps);
        }

        p_event->accept();
        return true;
    } else if (modifiers == Qt::ControlModifier) {
        // Zoom in/out.
        QPoint angle = p_event->angleDelta();
        if (!angle.isNull() && (angle.y() != 0)) {
            if (angle.y() > 0) {
                zoomInW();
            } else {
                zoomOutW();
            }
        }

        p_event->accept();
        return true;
    }

    return false;
}

void VEditor::requestUpdateVimStatus()
{
    if (m_editOps) {
        m_editOps->requestUpdateVimStatus();
    } else {
        emit m_object->vimStatusUpdated(NULL);
    }
}

bool VEditor::handleInputMethodQuery(Qt::InputMethodQuery p_query,
                                     QVariant &p_var) const
{
    if (p_query == Qt::ImEnabled) {
        p_var = m_enableInputMethod;
        return true;
    }

    return false;
}

void VEditor::setInputMethodEnabled(bool p_enabled)
{
    if (m_enableInputMethod != p_enabled) {
        m_enableInputMethod = p_enabled;

        QInputMethod *im = QGuiApplication::inputMethod();
        im->reset();

        // Ask input method to query current state, which will call inputMethodQuery().
        im->update(Qt::ImEnabled);
    }
}

void VEditor::decorateText(TextDecoration p_decoration, int p_level)
{
    if (m_editOps) {
        m_editOps->decorateText(p_decoration, p_level);
    }
}

void VEditor::updateConfig()
{
    updateEditConfig();
}

void VEditor::setVimMode(VimMode p_mode)
{
    if (m_editOps) {
        m_editOps->setVimMode(p_mode);
    }
}

VVim *VEditor::getVim() const
{
    if (m_editOps) {
        return m_editOps->getVim();
    }

    return NULL;
}

bool VEditor::setCursorPosition(int p_blockNumber, int p_posInBlock)
{
    QTextDocument *doc = documentW();
    QTextBlock block = doc->findBlockByNumber(p_blockNumber);
    if (!block.isValid() || block.length() <= p_posInBlock) {
        return false;
    }

    QTextCursor cursor = textCursorW();
    cursor.setPosition(block.position() + p_posInBlock);
    setTextCursorW(cursor);
    return true;
}
