#include <QtWidgets>
#include <QVector>
#include <QDebug>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "veditoperations.h"
#include "vedittab.h"

extern VConfigManager vconfig;
extern VNote *g_vnote;

void VEditConfig::init(const QFontMetrics &p_metric)
{
    update(p_metric);

    m_enableVimMode = vconfig.getEnableVimMode();

    m_highlightWholeBlock = m_enableVimMode;
}

void VEditConfig::update(const QFontMetrics &p_metric)
{
    if (vconfig.getTabStopWidth() > 0) {
        m_tabStopWidth = vconfig.getTabStopWidth() * p_metric.width(' ');
    } else {
        m_tabStopWidth = 0;
    }

    m_expandTab = vconfig.getIsExpandTab();

    if (m_expandTab && (vconfig.getTabStopWidth() > 0)) {
        m_tabSpaces = QString(vconfig.getTabStopWidth(), ' ');
    } else {
        m_tabSpaces = "\t";
    }

    m_cursorLineBg = QColor(vconfig.getEditorCurrentLineBg());
}

VEdit::VEdit(VFile *p_file, QWidget *p_parent)
    : QTextEdit(p_parent), m_file(p_file),
      m_editOps(NULL), m_enableInputMethod(true)
{
    const int labelTimerInterval = 500;
    const int extraSelectionHighlightTimer = 500;
    const int labelSize = 64;

    m_selectedWordColor = QColor(vconfig.getEditorSelectedWordBg());
    m_searchedWordColor = QColor(vconfig.getEditorSearchedWordBg());
    m_searchedWordCursorColor = QColor(vconfig.getEditorSearchedWordCursorBg());
    m_incrementalSearchedWordColor = QColor(vconfig.getEditorIncrementalSearchedWordBg());
    m_trailingSpaceColor = QColor(vconfig.getEditorTrailingSpaceBg());

    QPixmap wrapPixmap(":/resources/icons/search_wrap.svg");
    m_wrapLabel = new QLabel(this);
    m_wrapLabel->setPixmap(wrapPixmap.scaled(labelSize, labelSize));
    m_wrapLabel->hide();
    m_labelTimer = new QTimer(this);
    m_labelTimer->setSingleShot(true);
    m_labelTimer->setInterval(labelTimerInterval);
    connect(m_labelTimer, &QTimer::timeout,
            this, &VEdit::labelTimerTimeout);

    m_highlightTimer = new QTimer(this);
    m_highlightTimer->setSingleShot(true);
    m_highlightTimer->setInterval(extraSelectionHighlightTimer);
    connect(m_highlightTimer, &QTimer::timeout,
            this, &VEdit::doHighlightExtraSelections);

    connect(document(), &QTextDocument::modificationChanged,
            (VFile *)m_file, &VFile::setModified);

    m_extraSelections.resize((int)SelectionId::MaxSelection);

    updateFontAndPalette();

    m_config.init(QFontMetrics(font()));
    updateConfig();

    connect(this, &VEdit::cursorPositionChanged,
            this, &VEdit::handleCursorPositionChanged);

    connect(this, &VEdit::selectionChanged,
            this, &VEdit::highlightSelectedWord);

    m_lineNumberArea = new LineNumberArea(this);
    connect(document(), &QTextDocument::blockCountChanged,
            this, &VEdit::updateLineNumberAreaMargin);
    connect(this, &QTextEdit::textChanged,
            this, &VEdit::updateLineNumberArea);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &VEdit::updateLineNumberArea);

    updateLineNumberAreaMargin();
}

VEdit::~VEdit()
{
    if (m_file) {
        disconnect(document(), &QTextDocument::modificationChanged,
                   (VFile *)m_file, &VFile::setModified);
    }
}

void VEdit::updateConfig()
{
    m_config.update(QFontMetrics(font()));

    if (m_config.m_tabStopWidth > 0) {
        setTabStopWidth(m_config.m_tabStopWidth);
    }

    emit configUpdated();
}

void VEdit::beginEdit()
{
    updateFontAndPalette();

    updateConfig();

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

    QTextBlock block = document()->findBlockByLineNumber(p_lineNumber);
    if (block.isValid()) {
        VEditUtils::scrollBlockInPage(this, block.blockNumber(), 0);
        moveCursor(QTextCursor::EndOfBlock);
    }
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

bool VEdit::peekText(const QString &p_text, uint p_options, bool p_forward)
{
    if (p_text.isEmpty()) {
        makeBlockVisible(document()->findBlock(textCursor().selectionStart()));
        highlightIncrementalSearchedWord(QTextCursor());
        return false;
    }

    bool wrapped = false;
    QTextCursor retCursor;
    bool found = findTextHelper(p_text, p_options, p_forward,
                                p_forward ? textCursor().position() + 1
                                          : textCursor().position(),
                                wrapped, retCursor);
    if (found) {
        makeBlockVisible(document()->findBlock(retCursor.selectionStart()));
        highlightIncrementalSearchedWord(retCursor);
    }

    return found;
}

// Use QTextEdit::find() instead of QTextDocument::find() because the later has
// bugs in searching backward.
bool VEdit::findTextHelper(const QString &p_text, uint p_options,
                           bool p_forward, int p_start,
                           bool &p_wrapped, QTextCursor &p_cursor)
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
    QTextCursor cursor = textCursor();
    if (cursor.position() != p_start) {
        if (p_start < 0) {
            p_start = 0;
        } else if (p_start > document()->characterCount()) {
            p_start = document()->characterCount();
        }

        QTextCursor startCursor = cursor;
        startCursor.setPosition(p_start);
        setTextCursor(startCursor);
    }

    while (!found) {
        if (useRegExp) {
            found = find(exp, findFlags);
        } else {
            found = find(p_text, findFlags);
        }

        if (p_wrapped) {
            break;
        }

        if (!found) {
            // Wrap to the other end of the document to search again.
            p_wrapped = true;
            QTextCursor wrapCursor = textCursor();
            if (p_forward) {
                wrapCursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
            } else {
                wrapCursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
            }

            setTextCursor(wrapCursor);
        }
    }

    if (found) {
        p_cursor = textCursor();
    }

    // Restore the original cursor.
    setTextCursor(cursor);

    return found;
}

QList<QTextCursor> VEdit::findTextAll(const QString &p_text, uint p_options)
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
    QTextDocument *doc = document();
    while (true) {
        if (useRegExp) {
            cursor = doc->find(exp, startPos, findFlags);
        } else {
            cursor = doc->find(p_text, startPos, findFlags);
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

bool VEdit::findText(const QString &p_text, uint p_options, bool p_forward,
                     QTextCursor *p_cursor, QTextCursor::MoveMode p_moveMode)
{
    clearIncrementalSearchedWordHighlight();

    if (p_text.isEmpty()) {
        clearSearchedWordHighlight();
        return false;
    }

    QTextCursor cursor = textCursor();
    bool wrapped = false;
    QTextCursor retCursor;
    int matches = 0;
    int start = p_forward ? cursor.position() + 1 : cursor.position();
    if (p_cursor) {
        start = p_forward ? p_cursor->position() + 1 : p_cursor->position();
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
            setTextCursor(cursor);
        }

        highlightSearchedWord(p_text, p_options);
        highlightSearchedWordUnderCursor(retCursor);
        matches = m_extraSelections[(int)SelectionId::SearchedKeyword].size();
    } else {
        clearSearchedWordHighlight();
    }

    if (matches == 0) {
        statusMessage(tr("Found no match"));
    } else {
        statusMessage(tr("Found %1 %2").arg(matches)
                                       .arg(matches > 1 ? tr("matches") : tr("match")));
    }

    return found;
}

void VEdit::replaceText(const QString &p_text, uint p_options,
                        const QString &p_replaceText, bool p_findNext)
{
    QTextCursor cursor = textCursor();
    bool wrapped = false;
    QTextCursor retCursor;
    bool found = findTextHelper(p_text, p_options, true,
                                cursor.position(), wrapped, retCursor);
    if (found) {
        if (retCursor.selectionStart() == cursor.position()) {
            // Matched.
            retCursor.beginEditBlock();
            retCursor.insertText(p_replaceText);
            retCursor.endEditBlock();
            setTextCursor(retCursor);
        }

        if (p_findNext) {
            findText(p_text, p_options, true);
        }
    }
}

void VEdit::replaceTextAll(const QString &p_text, uint p_options,
                           const QString &p_replaceText)
{
    // Replace from the start to the end and restore the cursor.
    QTextCursor cursor = textCursor();
    int nrReplaces = 0;
    QTextCursor tmpCursor = cursor;
    tmpCursor.setPosition(0);
    setTextCursor(tmpCursor);
    int start = tmpCursor.position();
    while (true) {
        bool wrapped = false;
        QTextCursor retCursor;
        bool found = findTextHelper(p_text, p_options, true,
                                    start, wrapped, retCursor);
        if (!found) {
            break;
        } else {
            if (wrapped) {
                // Wrap back.
                break;
            }

            nrReplaces++;
            retCursor.beginEditBlock();
            retCursor.insertText(p_replaceText);
            retCursor.endEditBlock();
            setTextCursor(retCursor);
            start = retCursor.position();
        }
    }

    // Restore cursor position.
    cursor.clearSelection();
    setTextCursor(cursor);
    qDebug() << "replace all" << nrReplaces << "occurences";

    emit statusMessage(tr("Replace %1 %2").arg(nrReplaces)
                                          .arg(nrReplaces > 1 ? tr("occurences")
                                                              : tr("occurence")));
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

void VEdit::updateFontAndPalette()
{
    setFont(vconfig.getBaseEditFont());
    setPalette(vconfig.getBaseEditPalette());
}

void VEdit::highlightExtraSelections(bool p_now)
{
    m_highlightTimer->stop();
    if (p_now) {
        doHighlightExtraSelections();
    } else {
        m_highlightTimer->start();
    }
}

void VEdit::doHighlightExtraSelections()
{
    int nrExtra = m_extraSelections.size();
    Q_ASSERT(nrExtra == (int)SelectionId::MaxSelection);
    QList<QTextEdit::ExtraSelection> extraSelects;
    for (int i = 0; i < nrExtra; ++i) {
        extraSelects.append(m_extraSelections[i]);
    }

    setExtraSelections(extraSelects);
}

void VEdit::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::CurrentLine];
    if (vconfig.getHighlightCursorLine() && !isReadOnly()) {
        // Need to highlight current line.
        selects.clear();

        // A long block maybe splited into multiple visual lines.
        QTextEdit::ExtraSelection select;
        select.format.setBackground(m_config.m_cursorLineBg);
        select.format.setProperty(QTextFormat::FullWidthSelection, true);

        QTextCursor cursor = textCursor();
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

void VEdit::setReadOnly(bool p_ro)
{
    QTextEdit::setReadOnly(p_ro);
    highlightCurrentLine();
}

void VEdit::highlightSelectedWord()
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SelectedWord];
    if (!vconfig.getHighlightSelectedWord()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    QString text = textCursor().selectedText().trimmed();
    if (text.isEmpty() || wordInSearchedSelection(text)) {
        selects.clear();
        highlightExtraSelections(true);
        return;
    }

    QTextCharFormat format;
    format.setBackground(m_selectedWordColor);
    highlightTextAll(text, FindOption::CaseSensitive, SelectionId::SelectedWord,
                     format);
}

// Do not highlight trailing spaces with current cursor right behind.
static void trailingSpaceFilter(VEdit *p_editor, QList<QTextEdit::ExtraSelection> &p_result)
{
    QTextCursor cursor = p_editor->textCursor();
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

void VEdit::highlightTrailingSpace()
{
    if (!vconfig.getEnableTrailingSpaceHighlight()) {
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
    highlightTextAll(text, FindOption::RegularExpression,
                     SelectionId::TrailingSapce, format,
                     trailingSpaceFilter);
}

bool VEdit::wordInSearchedSelection(const QString &p_text)
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

void VEdit::highlightTextAll(const QString &p_text, uint p_options,
                             SelectionId p_id, QTextCharFormat p_format,
                             void (*p_filter)(VEdit *, QList<QTextEdit::ExtraSelection> &))
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

void VEdit::highlightSearchedWord(const QString &p_text, uint p_options)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeyword];
    if (!vconfig.getHighlightSearchedWord() || p_text.isEmpty()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    QTextCharFormat format;
    format.setBackground(m_searchedWordColor);
    highlightTextAll(p_text, p_options, SelectionId::SearchedKeyword, format);
}

void VEdit::highlightSearchedWordUnderCursor(const QTextCursor &p_cursor)
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
    select.format.setBackground(m_searchedWordCursorColor);
    select.cursor = p_cursor;
    selects.append(select);

    highlightExtraSelections(true);
}

void VEdit::highlightIncrementalSearchedWord(const QTextCursor &p_cursor)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::IncrementalSearchedKeyword];
    if (!vconfig.getHighlightSearchedWord() || !p_cursor.hasSelection()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    selects.clear();
    QTextEdit::ExtraSelection select;
    select.format.setBackground(m_incrementalSearchedWordColor);
    select.cursor = p_cursor;
    selects.append(select);

    highlightExtraSelections(true);
}

void VEdit::clearSearchedWordHighlight()
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

void VEdit::clearSearchedWordUnderCursorHighlight(bool p_now)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeywordUnderCursor];
    if (selects.isEmpty()) {
        return;
    }

    selects.clear();
    highlightExtraSelections(p_now);
}

void VEdit::clearIncrementalSearchedWordHighlight(bool p_now)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::IncrementalSearchedKeyword];
    if (selects.isEmpty()) {
        return;
    }

    selects.clear();
    highlightExtraSelections(p_now);
}

void VEdit::contextMenuEvent(QContextMenuEvent *p_event)
{
    QMenu *menu = createStandardContextMenu();
    menu->setToolTipsVisible(true);

    const QList<QAction *> actions = menu->actions();

    if (!textCursor().hasSelection()) {
        VEditTab *editTab = dynamic_cast<VEditTab *>(parent());
        V_ASSERT(editTab);
        if (editTab->isEditMode()) {
            QAction *saveExitAct = new QAction(QIcon(":/resources/icons/save_exit.svg"),
                                               tr("&Save Changes And Read"), this);
            saveExitAct->setToolTip(tr("Save changes and exit edit mode"));
            connect(saveExitAct, &QAction::triggered,
                    this, &VEdit::handleSaveExitAct);

            QAction *discardExitAct = new QAction(QIcon(":/resources/icons/discard_exit.svg"),
                                                  tr("&Discard Changes And Read"), this);
            discardExitAct->setToolTip(tr("Discard changes and exit edit mode"));
            connect(discardExitAct, &QAction::triggered,
                    this, &VEdit::handleDiscardExitAct);

            menu->insertAction(actions.isEmpty() ? NULL : actions[0], discardExitAct);
            menu->insertAction(discardExitAct, saveExitAct);
            if (!actions.isEmpty()) {
                menu->insertSeparator(actions[0]);
            }
        } else if (m_file->isModifiable()) {
            // HTML.
            QAction *editAct= new QAction(QIcon(":/resources/icons/edit_note.svg"),
                                          tr("&Edit"), this);
            editAct->setToolTip(tr("Edit current note"));
            connect(editAct, &QAction::triggered,
                    this, &VEdit::handleEditAct);
            menu->insertAction(actions.isEmpty() ? NULL : actions[0], editAct);
            // actions does not contain editAction.
            if (!actions.isEmpty()) {
                menu->insertSeparator(actions[0]);
            }
        }
    }

    menu->exec(p_event->globalPos());
    delete menu;
}

void VEdit::handleSaveExitAct()
{
    emit saveAndRead();
}

void VEdit::handleDiscardExitAct()
{
    emit discardAndRead();
}

void VEdit::handleEditAct()
{
    emit editNote();
}

VFile *VEdit::getFile() const
{
    return m_file;
}

void VEdit::handleCursorPositionChanged()
{
    static QTextCursor lastCursor;

    QTextCursor cursor = textCursor();
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

VEditConfig &VEdit::getConfig()
{
    return m_config;
}

void VEdit::mousePressEvent(QMouseEvent *p_event)
{
    if (p_event->button() == Qt::LeftButton
        && p_event->modifiers() == Qt::ControlModifier
        && !textCursor().hasSelection()) {
        m_oriMouseX = p_event->x();
        m_oriMouseY = p_event->y();
        m_readyToScroll = true;
        m_mouseMoveScrolled = false;
        p_event->accept();
        return;
    }

    m_readyToScroll = false;
    m_mouseMoveScrolled = false;

    QTextEdit::mousePressEvent(p_event);

    emit selectionChangedByMouse(textCursor().hasSelection());
}

void VEdit::mouseReleaseEvent(QMouseEvent *p_event)
{
    if (m_mouseMoveScrolled || m_readyToScroll) {
        viewport()->setCursor(Qt::IBeamCursor);
        m_readyToScroll = false;
        m_mouseMoveScrolled = false;
        p_event->accept();
        return;
    }

    m_readyToScroll = false;
    m_mouseMoveScrolled = false;

    QTextEdit::mouseReleaseEvent(p_event);
}

void VEdit::mouseMoveEvent(QMouseEvent *p_event)
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
                viewport()->setCursor(Qt::SizeAllCursor);
            }

            QScrollBar *verBar = verticalScrollBar();
            QScrollBar *horBar = horizontalScrollBar();
            if (verBar->isVisible()) {
                verBar->setValue(verBar->value() - deltaY);
            }

            if (horBar->isVisible()) {
                horBar->setValue(horBar->value() - deltaX);
            }
        }

        p_event->accept();
        return;
    }

    QTextEdit::mouseMoveEvent(p_event);

    emit selectionChangedByMouse(textCursor().hasSelection());
}

void VEdit::requestUpdateVimStatus()
{
    if (m_editOps) {
        m_editOps->requestUpdateVimStatus();
    } else {
        emit vimStatusUpdated(NULL);
    }
}

bool VEdit::jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat)
{
    Q_UNUSED(p_forward);
    Q_UNUSED(p_relativeLevel);
    Q_UNUSED(p_repeat);
    return false;
}

QVariant VEdit::inputMethodQuery(Qt::InputMethodQuery p_query) const
{
    if (p_query == Qt::ImEnabled) {
        return m_enableInputMethod;
    }

    return QTextEdit::inputMethodQuery(p_query);
}

void VEdit::setInputMethodEnabled(bool p_enabled)
{
    if (m_enableInputMethod != p_enabled) {
        m_enableInputMethod = p_enabled;

        QInputMethod *im = QGuiApplication::inputMethod();
        im->reset();
        // Ask input method to query current state, which will call inputMethodQuery().
        im->update(Qt::ImEnabled);
    }
}

void VEdit::decorateText(TextDecoration p_decoration)
{
    if (m_editOps) {
        m_editOps->decorateText(p_decoration);
    }
}

void VEdit::updateLineNumberAreaMargin()
{
    int width = 0;
    if (vconfig.getEditorLineNumber()) {
        width = m_lineNumberArea->calculateWidth();
    }

    setViewportMargins(width, 0, 0, 0);
}

void VEdit::updateLineNumberArea()
{
    if (vconfig.getEditorLineNumber()) {
        if (!m_lineNumberArea->isVisible()) {
            updateLineNumberAreaMargin();
            m_lineNumberArea->show();
        }

        m_lineNumberArea->update();
    } else if (m_lineNumberArea->isVisible()) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
    }
}

void VEdit::resizeEvent(QResizeEvent *p_event)
{
    QTextEdit::resizeEvent(p_event);

    if (vconfig.getEditorLineNumber()) {
        QRect rect = contentsRect();
        m_lineNumberArea->setGeometry(QRect(rect.left(),
                                            rect.top(),
                                            m_lineNumberArea->calculateWidth(),
                                            rect.height()));
    }
}

void VEdit::lineNumberAreaPaintEvent(QPaintEvent *p_event)
{
    if (!vconfig.getEditorLineNumber()) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
        return;
    }

    QPainter painter(m_lineNumberArea);
    painter.fillRect(p_event->rect(), vconfig.getEditorLineNumberBg());

    QTextDocument *doc = document();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int offsetY = contentOffsetY();
    QRectF rect = layout->blockBoundingRect(block);
    int top = offsetY + (int)rect.y();
    int bottom = top + (int)rect.height();
    int eventTop = p_event->rect().top();
    int eventBtm = p_event->rect().bottom();
    const int digitHeight = m_lineNumberArea->getDigitHeight();
    const int curBlockNumber = textCursor().block().blockNumber();
    const bool relative = vconfig.getEditorLineNumber() == 2;
    const QString &fg = vconfig.getEditorLineNumberFg();
    painter.setPen(fg);

    while (block.isValid() && top <= eventBtm) {
        if (block.isVisible() && bottom >= eventTop) {
            bool currentLine = false;
            int number = blockNumber + 1;
            if (relative) {
                number = blockNumber - curBlockNumber;
                if (number == 0) {
                    currentLine = true;
                    number = blockNumber + 1;
                } else if (number < 0) {
                    number = -number;
                }
            } else if (blockNumber == curBlockNumber) {
                currentLine = true;
            }

            QString numberStr = QString::number(number);

            if (currentLine) {
                QFont font = painter.font();
                font.setBold(true);
                painter.setFont(font);
            }

            painter.drawText(0,
                             top + 2,
                             m_lineNumberArea->width(),
                             digitHeight,
                             Qt::AlignRight,
                             numberStr);

            if (currentLine) {
                QFont font = painter.font();
                font.setBold(false);
                painter.setFont(font);
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + (int)layout->blockBoundingRect(block).height();
        ++blockNumber;
    }
}

int VEdit::contentOffsetY()
{
    int offsety = 0;
    QScrollBar *sb = verticalScrollBar();
    offsety = sb->value();
    return -offsety;
}

QTextBlock VEdit::firstVisibleBlock()
{
    QTextDocument *doc = document();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    int offsetY = contentOffsetY();

    // Binary search.
    int idx = -1;
    int start = 0, end = doc->blockCount() - 1;
    while (start <= end) {
        int mid = start + (end - start) / 2;
        QTextBlock block = doc->findBlockByNumber(mid);
        if (!block.isValid()) {
            break;
        }

        int y = offsetY + (int)layout->blockBoundingRect(block).y();
        if (y == 0) {
            return block;
        } else if (y < 0) {
            start = mid + 1;
        } else {
            if (idx == -1 || mid < idx) {
                idx = mid;
            }

            end = mid - 1;
        }
    }

    if (idx != -1) {
        return doc->findBlockByNumber(idx);
    }

    // Linear search.
    qDebug() << "fall back to linear search for first visible block";
    QTextBlock block = doc->begin();
    while (block.isValid()) {
        int y = offsetY + (int)layout->blockBoundingRect(block).y();
        if (y >= 0) {
            return block;
        }

        block = block.next();
    }

    Q_ASSERT(false);
    return doc->begin();
}

int LineNumberArea::calculateWidth() const
{
    int bc = m_document->blockCount();
    if (m_blockCount == bc) {
        return m_width;
    }

    const_cast<LineNumberArea *>(this)->m_blockCount = bc;
    int digits = 1;
    int max = qMax(1, m_blockCount);
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int width = m_digitWidth * digits;
    const_cast<LineNumberArea *>(this)->m_width = width;

    return m_width;
}

void VEdit::makeBlockVisible(const QTextBlock &p_block)
{
    if (!p_block.isValid() || !p_block.isVisible()) {
        return;
    }

    QScrollBar *vbar = verticalScrollBar();
    if (!vbar || !vbar->isVisible()) {
        // No vertical scrollbar. No need to scroll.
        return;
    }

    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    int height = rect().height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && hbar->isVisible()) {
        height -= hbar->height();
    }

    QRectF rect = layout->blockBoundingRect(p_block);
    int y = contentOffsetY() + (int)rect.y();
    while (y < 0 && vbar->value() > vbar->minimum()) {
        vbar->setValue(vbar->value() - vbar->singleStep());
        rect = layout->blockBoundingRect(p_block);
        y = contentOffsetY() + (int)rect.y();
    }

    while (y + (int)rect.height() > height && vbar->value() < vbar->maximum()) {
        vbar->setValue(vbar->value() + vbar->singleStep());
        rect = layout->blockBoundingRect(p_block);
        y = contentOffsetY() + (int)rect.y();
    }
}
