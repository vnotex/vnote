#include <QtWidgets>
#include <QVector>
#include <QDebug>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtableofcontent.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "utils/vmetawordmanager.h"
#include "veditoperations.h"
#include "vedittab.h"
#include "dialog/vinsertlinkdialog.h"
#include "utils/viconutils.h"

extern VConfigManager *g_config;

extern VNote *g_vnote;

extern VMetaWordManager *g_mwMgr;

VEdit::VEdit(VFile *p_file, QWidget *p_parent)
    : QTextEdit(p_parent), m_file(p_file),
      m_editOps(NULL), m_enableInputMethod(true)
{
    const int labelTimerInterval = 500;
    const int extraSelectionHighlightTimer = 500;
    const int labelSize = 64;

    m_selectedWordColor = QColor(g_config->getEditorSelectedWordBg());
    m_searchedWordColor = QColor(g_config->getEditorSearchedWordBg());
    m_searchedWordCursorColor = QColor(g_config->getEditorSearchedWordCursorBg());
    m_incrementalSearchedWordColor = QColor(g_config->getEditorIncrementalSearchedWordBg());
    m_trailingSpaceColor = QColor(g_config->getEditorTrailingSpaceBg());

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

    m_extraSelections.resize((int)SelectionId::MaxSelection);

    updateFontAndPalette();

    m_config.init(QFontMetrics(font()), false);
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

    connect(document(), &QTextDocument::contentsChange,
            this, &VEdit::updateBlockLineDistanceHeight);
}

VEdit::~VEdit()
{
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

    setReadOnlyAndHighlight(false);

    setModified(false);
}

void VEdit::endEdit()
{
    setReadOnlyAndHighlight(true);
}

void VEdit::saveFile()
{
    Q_ASSERT(m_file->isModifiable());

    if (!document()->isModified()) {
        return;
    }

    m_file->setContent(toHtml());
    setModified(false);
}

void VEdit::reloadFile()
{
    setHtml(m_file->getContent());

    setModified(false);
}

bool VEdit::scrollToBlock(int p_blockNumber)
{
    QTextBlock block = document()->findBlockByNumber(p_blockNumber);
    if (block.isValid()) {
        VEditUtils::scrollBlockInPage(this, block.blockNumber(), 0);
        moveCursor(QTextCursor::EndOfBlock);
        return true;
    }

    return false;
}

bool VEdit::isModified() const
{
    return document()->isModified();
}

void VEdit::setModified(bool p_modified)
{
    document()->setModified(p_modified);
}

void VEdit::insertImage()
{
    if (m_editOps) {
        m_editOps->insertImage();
    }
}

void VEdit::insertLink()
{
    if (!m_editOps) {
        return;
    }

    QString text;
    QString linkText, linkUrl;
    QTextCursor cursor = textCursor();
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

    VInsertLinkDialog dialog(tr("Insert Link"),
                             "",
                             "",
                             linkText,
                             linkUrl,
                             false,
                             this);
    if (dialog.exec() == QDialog::Accepted) {
        linkText = dialog.getLinkText();
        linkUrl = dialog.getLinkUrl();
        Q_ASSERT(!linkText.isEmpty() && !linkUrl.isEmpty());

        m_editOps->insertLink(linkText, linkUrl);
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
    setFont(g_config->getBaseEditFont());
    setPalette(g_config->getBaseEditPalette());
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
    if (g_config->getHighlightCursorLine() && !isReadOnly()) {
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

void VEdit::highlightSelectedWord()
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SelectedWord];
    if (!g_config->getHighlightSelectedWord()) {
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
    if (!g_config->getEnableTrailingSpaceHighlight()) {
        QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::TrailingSpace];
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
                     SelectionId::TrailingSpace, format,
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
    if (!g_config->getHighlightSearchedWord() || p_text.isEmpty()) {
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
    if (!g_config->getHighlightSearchedWord() || !p_cursor.hasSelection()) {
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
            QAction *saveExitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/save_exit.svg"),
                                               tr("&Save Changes And Read"), this);
            saveExitAct->setToolTip(tr("Save changes and exit edit mode"));
            connect(saveExitAct, &QAction::triggered,
                    this, &VEdit::handleSaveExitAct);

            QAction *discardExitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/discard_exit.svg"),
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
            QAction *editAct= new QAction(VIconUtils::menuIcon(":/resources/icons/edit_note.svg"),
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

    alterContextMenu(menu, actions);

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
    if (g_config->getEditorLineNumber()) {
        width = m_lineNumberArea->calculateWidth();
    }

    setViewportMargins(width, 0, 0, 0);
}

void VEdit::updateLineNumberArea()
{
    if (g_config->getEditorLineNumber()) {
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

    if (g_config->getEditorLineNumber()) {
        QRect rect = contentsRect();
        m_lineNumberArea->setGeometry(QRect(rect.left(),
                                            rect.top(),
                                            m_lineNumberArea->calculateWidth(),
                                            rect.height()));
    }
}

void VEdit::lineNumberAreaPaintEvent(QPaintEvent *p_event)
{
    int lineNumberType = g_config->getEditorLineNumber();
    if (!lineNumberType) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
        return;
    }

    QPainter painter(m_lineNumberArea);
    painter.fillRect(p_event->rect(), g_config->getEditorLineNumberBg());

    QAbstractTextDocumentLayout *layout = document()->documentLayout();

    QTextBlock block = firstVisibleBlock();
    if (!block.isValid()) {
        return;
    }

    int blockNumber = block.blockNumber();
    int offsetY = contentOffsetY();
    QRectF rect = layout->blockBoundingRect(block);
    int top = offsetY + (int)rect.y();
    int bottom = top + (int)rect.height();
    int eventTop = p_event->rect().top();
    int eventBtm = p_event->rect().bottom();
    const int digitHeight = m_lineNumberArea->getDigitHeight();
    const int curBlockNumber = textCursor().block().blockNumber();
    const QString &fg = g_config->getEditorLineNumberFg();
    const int lineDistanceHeight = m_config.m_lineDistanceHeight;
    painter.setPen(fg);

    // Display line number only in code block.
    if (lineNumberType == 3) {
        if (m_file->getDocType() != DocType::Markdown) {
            return;
        }

        int number = 0;
        while (block.isValid() && top <= eventBtm) {
            int blockState = block.userState();
            switch (blockState) {
            case HighlightBlockState::CodeBlockStart:
                Q_ASSERT(number == 0);
                number = 1;
                break;

            case HighlightBlockState::CodeBlockEnd:
                number = 0;
                break;

            case HighlightBlockState::CodeBlock:
                if (number == 0) {
                    // Need to find current line number in code block.
                    QTextBlock startBlock = block.previous();
                    while (startBlock.isValid()) {
                        if (startBlock.userState() == HighlightBlockState::CodeBlockStart) {
                            number = block.blockNumber() - startBlock.blockNumber();
                            break;
                        }

                        startBlock = startBlock.previous();
                    }
                }

                break;

            default:
                break;
            }

            if (blockState == HighlightBlockState::CodeBlock) {
                if (block.isVisible() && bottom >= eventTop) {
                    QString numberStr = QString::number(number);
                    painter.drawText(0,
                                     top + 2,
                                     m_lineNumberArea->width(),
                                     digitHeight,
                                     Qt::AlignRight,
                                     numberStr);
                }

                ++number;
            }

            block = block.next();
            top = bottom;
            bottom = top + (int)layout->blockBoundingRect(block).height() + lineDistanceHeight;
        }

        return;
    }

    // Handle lineNumberType 1 and 2.
    Q_ASSERT(lineNumberType == 1 || lineNumberType == 2);
    while (block.isValid() && top <= eventBtm) {
        if (block.isVisible() && bottom >= eventTop) {
            bool currentLine = false;
            int number = blockNumber + 1;
            if (lineNumberType == 2) {
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
        bottom = top + (int)layout->blockBoundingRect(block).height() + lineDistanceHeight;
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

    return QTextBlock();
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

    bool moved = false;

    QRectF rect = layout->blockBoundingRect(p_block);
    int y = contentOffsetY() + (int)rect.y();
    int rectHeight = (int)rect.height();

    // Handle the case rectHeight >= height.
    if (rectHeight >= height) {
        if (y <= 0) {
            if (y + rectHeight < height) {
                // Need to scroll up.
                while (y + rectHeight < height && vbar->value() > vbar->minimum()) {
                    moved = true;
                    vbar->setValue(vbar->value() - vbar->singleStep());
                    rect = layout->blockBoundingRect(p_block);
                    rectHeight = (int)rect.height();
                    y = contentOffsetY() + (int)rect.y();
                }
            }
        } else {
            // Need to scroll down.
            while (y > 0 && vbar->value() < vbar->maximum()) {
                moved = true;
                vbar->setValue(vbar->value() + vbar->singleStep());
                rect = layout->blockBoundingRect(p_block);
                rectHeight = (int)rect.height();
                y = contentOffsetY() + (int)rect.y();
            }
        }

        if (moved) {
            qDebug() << "scroll to make huge block visible";
        }

        return;
    }

    while (y < 0 && vbar->value() > vbar->minimum()) {
        moved = true;
        vbar->setValue(vbar->value() - vbar->singleStep());
        rect = layout->blockBoundingRect(p_block);
        rectHeight = (int)rect.height();
        y = contentOffsetY() + (int)rect.y();
    }

    if (moved) {
        qDebug() << "scroll page down to make block visible";
        return;
    }

    while (y + rectHeight > height && vbar->value() < vbar->maximum()) {
        moved = true;
        vbar->setValue(vbar->value() + vbar->singleStep());
        rect = layout->blockBoundingRect(p_block);
        rectHeight = (int)rect.height();
        y = contentOffsetY() + (int)rect.y();
    }

    if (moved) {
        qDebug() << "scroll page up to make block visible";
    }
}

bool VEdit::isBlockVisible(const QTextBlock &p_block)
{
    if (!p_block.isValid() || !p_block.isVisible()) {
        return false;
    }

    QScrollBar *vbar = verticalScrollBar();
    if (!vbar || !vbar->isVisible()) {
        // No vertical scrollbar.
        return true;
    }

    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    int height = rect().height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && hbar->isVisible()) {
        height -= hbar->height();
    }

    QRectF rect = layout->blockBoundingRect(p_block);
    int y = contentOffsetY() + (int)rect.y();
    int rectHeight = (int)rect.height();

    return (y >= 0 && y < height) || (y < 0 && y + rectHeight > 0);
}

void VEdit::alterContextMenu(QMenu *p_menu, const QList<QAction *> &p_actions)
{
    Q_UNUSED(p_menu);
    Q_UNUSED(p_actions);
}

void VEdit::updateBlockLineDistanceHeight(int p_pos,
                                          int p_charsRemoved,
                                          int p_charsAdded)
{
    if ((p_charsRemoved == 0 && p_charsAdded == 0)
        || m_config.m_lineDistanceHeight <= 0) {
        return;
    }

    QTextDocument *doc = document();
    QTextBlock block = doc->findBlock(p_pos);
    QTextBlock lastBlock = doc->findBlock(p_pos + p_charsRemoved + p_charsAdded);
    QTextCursor cursor(block);
    bool changed = false;
    while (block.isValid()) {
        cursor.setPosition(block.position());
        QTextBlockFormat fmt = cursor.blockFormat();
        if (fmt.lineHeightType() != QTextBlockFormat::LineDistanceHeight
            || fmt.lineHeight() != m_config.m_lineDistanceHeight) {
            fmt.setLineHeight(m_config.m_lineDistanceHeight,
                              QTextBlockFormat::LineDistanceHeight);
            if (!changed) {
                changed = true;
                cursor.joinPreviousEditBlock();
            }

            cursor.mergeBlockFormat(fmt);
            qDebug() << "merge block format line distance" << block.blockNumber();
        }

        if (block == lastBlock) {
            break;
        }

        block = block.next();
    }

    if (changed) {
        cursor.endEditBlock();
    }
}

void VEdit::evaluateMagicWords()
{
    QString text;
    QTextCursor cursor = textCursor();
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

        setTextCursor(cursor);
    }
}

void VEdit::setReadOnlyAndHighlight(bool p_readonly)
{
    setReadOnly(p_readonly);
    highlightCurrentLine();
}
