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
#include "vnote.h"

extern VConfigManager *g_config;

extern VMetaWordManager *g_mwMgr;

VEditor::VEditor(VFile *p_file,
                 QWidget *p_editor,
                 const QSharedPointer<VTextEditCompleter> &p_completer)
    : m_editor(p_editor),
      m_object(new VEditorObject(this, p_editor)),
      m_file(p_file),
      m_editOps(nullptr),
      m_document(nullptr),
      m_enableInputMethod(true),
      m_timeStamp(0),
      m_trailingSpaceSelectionTS(0),
      m_completer(p_completer),
      m_trailingSpaceHighlightEnabled(false),
      m_tabHighlightEnabled(false)
{
}

VEditor::~VEditor()
{
    if (m_completer->widget() == m_editor) {
        m_completer->setWidget(NULL);
    }

    cleanUp();
}

void VEditor::cleanUp()
{
    for (auto const & file : m_tempFiles) {
        VUtils::deleteFile(file);
    }

    m_tempFiles.clear();
}

void VEditor::init()
{
    const int labelTimerInterval = 500;
    const int extraSelectionHighlightTimer = 500;
    const int trailingSpaceUpdateTimer = 500;
    const int labelSize = 64;

    m_document = documentW();
    QObject::connect(m_document, &QTextDocument::contentsChanged,
                     m_object, &VEditorObject::clearFindCache);

    m_selectedWordFg = QColor(g_config->getEditorSelectedWordFg());
    m_selectedWordBg = QColor(g_config->getEditorSelectedWordBg());

    m_searchedWordFg = QColor(g_config->getEditorSearchedWordFg());
    m_searchedWordBg = QColor(g_config->getEditorSearchedWordBg());

    m_searchedWordCursorFg = QColor(g_config->getEditorSearchedWordCursorFg());
    m_searchedWordCursorBg = QColor(g_config->getEditorSearchedWordCursorBg());

    m_incrementalSearchedWordFg = QColor(g_config->getEditorIncrementalSearchedWordFg());
    m_incrementalSearchedWordBg = QColor(g_config->getEditorIncrementalSearchedWordBg());

    m_trailingSpaceColor = QColor(g_config->getEditorTrailingSpaceBg());

    m_tabColor = g_config->getMdEditPalette().color(QPalette::Text);

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

    m_trailingSpaceTimer = new QTimer(m_editor);
    m_trailingSpaceTimer->setSingleShot(true);
    m_trailingSpaceTimer->setInterval(trailingSpaceUpdateTimer);
    QObject::connect(m_trailingSpaceTimer, &QTimer::timeout,
                     m_object, &VEditorObject::doUpdateTrailingSpaceAndTabHighlights);

    m_extraSelections.resize((int)SelectionId::MaxSelection);

    updateFontAndPalette();

    m_config.init(QFontMetrics(m_editor->font()), false);
    updateEditConfig();

    // Init shortcuts.
    QKeySequence keySeq(g_config->getShortcutKeySequence("PastePlainText"));
    QShortcut *pastePlainTextShortcut = new QShortcut(keySeq, m_editor);
    pastePlainTextShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(pastePlainTextShortcut, &QShortcut::activated,
                     m_object, [this]() {
                        pastePlainText();
                     });
}

void VEditor::labelTimerTimeout()
{
    m_wrapLabel->hide();
}

bool VEditor::needUpdateTrailingSpaceAndTabHighlights()
{
    bool ret = false;
    bool space = g_config->getEnableTrailingSpaceHighlight();
    if (m_trailingSpaceHighlightEnabled != space) {
        m_trailingSpaceHighlightEnabled = space;
        ret = true;
    }

    bool tab = g_config->getEnableTabHighlight();
    if (m_tabHighlightEnabled != tab) {
        m_tabHighlightEnabled = tab;
        ret = true;
    }

    if (m_trailingSpaceSelectionTS != m_timeStamp) {
        ret = true;
    }

    return ret;
}

void VEditor::updateTrailingSpaceAndTabHighlights()
{
    if (needUpdateTrailingSpaceAndTabHighlights()) {
        m_trailingSpaceTimer->start();
    } else {
        highlightExtraSelections(false);
    }
}

void VEditor::doHighlightExtraSelections()
{
    int nrExtra = m_extraSelections.size();
    Q_ASSERT(nrExtra == (int)SelectionId::MaxSelection);
    QList<QTextEdit::ExtraSelection> extraSelects;
    for (int i = 0; i < nrExtra; ++i) {
        if (i == (int)SelectionId::TrailingSpace) {
            filterTrailingSpace(extraSelects, m_extraSelections[i]);
        } else {
            extraSelects.append(m_extraSelections[i]);
        }
    }

    setExtraSelectionsW(extraSelects);
}

void VEditor::doUpdateTrailingSpaceAndTabHighlights()
{
    bool needHighlight = false;

    // Trailing space.
    if (!m_trailingSpaceHighlightEnabled) {
        QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::TrailingSpace];
        if (!selects.isEmpty()) {
            selects.clear();
            needHighlight = true;
        }
    } else {
        needHighlight = true;
        QTextCharFormat format;
        format.setBackground(m_trailingSpaceColor);
        QString text("\\s+$");
        highlightTextAll(text,
                         FindOption::RegularExpression,
                         SelectionId::TrailingSpace,
                         format);
    }

    // Tab.
    if (!m_tabHighlightEnabled) {
        QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::Tab];
        if (!selects.isEmpty()) {
            selects.clear();
            needHighlight = true;
        }
    } else {
        needHighlight = true;
        QTextCharFormat format;
        format.setBackground(m_tabColor);
        QString text("\\t");
        highlightTextAll(text,
                         FindOption::RegularExpression,
                         SelectionId::Tab,
                         format);
    }

    m_trailingSpaceSelectionTS = m_timeStamp;

    if (needHighlight) {
        highlightExtraSelections(true);
    }
}

void VEditor::updateEditConfig()
{
    // FIXME: we need to use font of the code block here.
    QFont font(m_editor->font());
    font.setFamily(VNote::getMonospaceFont());
    m_config.update(QFontMetrics(font));

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
        updateTrailingSpaceAndTabHighlights();
        highlightCurrentLine();
    } else {
        // Judge whether we have trailing space at current line.
        QString text = cursor.block().text();
        if (!text.isEmpty()) {
            auto it = text.rbegin();
            bool needUpdate = it->isSpace();
            if (!needUpdate
                && cursor.atBlockEnd()
                && text.size() > 1) {
                ++it;
                needUpdate = it->isSpace();
                // Input two chars at once in Chinese.
                if (!needUpdate && text.size() > 2) {
                    ++it;
                    needUpdate = it->isSpace();
                }
            }

            if (needUpdate) {
                updateTrailingSpaceAndTabHighlights();
            }
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

void VEditor::updateTimeStamp()
{
    ++m_timeStamp;
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

void VEditor::filterTrailingSpace(QList<QTextEdit::ExtraSelection> &p_selects,
                                  const QList<QTextEdit::ExtraSelection> &p_src)
{
    QTextCursor cursor = textCursorW();
    bool blockEnd = cursor.atBlockEnd();
    int blockNum = cursor.blockNumber();
    for (auto it = p_src.begin(); it != p_src.end(); ++it) {
        if (it->cursor.blockNumber() == blockNum) {
            if (blockEnd) {
                // When cursor is at block end, we do not display any trailing space
                // at current line.
                continue;
            }

            QString text = cursor.block().text();
            if (text.isEmpty()) {
                continue;
            } else if (!text[text.size() - 1].isSpace()) {
                continue;
            }
        }

        p_selects.append(*it);
    }
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

static QTextDocument::FindFlags findOptionsToFlags(uint p_options, bool p_forward)
{
    QTextDocument::FindFlags findFlags;

    if (p_options & FindOption::CaseSensitive) {
        findFlags |= QTextDocument::FindCaseSensitively;
    }

    if (p_options & FindOption::WholeWordOnly) {
        findFlags |= QTextDocument::FindWholeWords;
    }

    if (!p_forward) {
        findFlags |= QTextDocument::FindBackward;
    }

    return findFlags;
}

QList<QTextCursor> VEditor::findTextAll(const QString &p_text,
                                        uint p_options,
                                        int p_start,
                                        int p_end)
{
    QList<QTextCursor> results;
    if (p_text.isEmpty()) {
        return results;
    }

    // Options
    bool caseSensitive = p_options & FindOption::CaseSensitive;
    QTextDocument::FindFlags findFlags = findOptionsToFlags(p_options, true);

    if (p_options & FindOption::RegularExpression) {
        QRegExp exp(p_text,
                    caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        results = findTextAllInRange(m_document, exp, findFlags, p_start, p_end);
    } else {
        results = findTextAllInRange(m_document, p_text, findFlags, p_start, p_end);
    }

    return results;
}

static void mergeResult(QList<QTextCursor> &p_result, const QList<QTextCursor> &p_part)
{
    if (p_result.isEmpty()) {
        p_result = p_part;
        return;
    }

    int idx = 0;
    for (auto const & cur : p_part) {
        // Find position to insert into @p_result.
        while (idx < p_result.size()) {
            const QTextCursor &toCur = p_result[idx];
            if (cur.selectionEnd() <= toCur.selectionStart()) {
                // Insert it before toCur.
                p_result.insert(idx, cur);
                ++idx;
                break;
            } else if (cur.selectionStart() < toCur.selectionEnd()) {
                // Interleave. Abandon it.
                break;
            } else {
                ++idx;
            }
        }

        // Insert to the end.
        if (idx >= p_result.size()) {
            p_result.append(cur);
            idx = p_result.size();
        }
    }
}

QList<QTextCursor> VEditor::findTextAll(const VSearchToken &p_token,
                                        int p_start,
                                        int p_end)
{
    QList<QTextCursor> results;
    if (p_token.isEmpty()) {
        return results;
    }

    QTextDocument::FindFlags flags;
    if (p_token.m_caseSensitivity == Qt::CaseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    if (p_token.m_type == VSearchToken::RawString) {
        for (auto const & wd : p_token.m_keywords) {
            QList<QTextCursor> part = findTextAllInRange(m_document, wd, flags, p_start, p_end);
            mergeResult(results, part);
        }
    } else {
        // Regular expression.
        for (auto const & reg : p_token.m_regs) {
            QList<QTextCursor> part = findTextAllInRange(m_document, reg, flags, p_start, p_end);
            mergeResult(results, part);
        }
    }

    return results;
}

const QList<QTextCursor> &VEditor::findTextAllCached(const QString &p_text,
                                                     uint p_options,
                                                     int p_start,
                                                     int p_end)
{
    if (p_text.isEmpty()) {
        m_findInfo.clear();
        return m_findInfo.m_result;
    }

    if (m_findInfo.isCached(p_text, p_options, p_start, p_end)) {
        return m_findInfo.m_result;
    }

    QList<QTextCursor> result = findTextAll(p_text, p_options, p_start, p_end);
    m_findInfo.update(p_text, p_options, p_start, p_end, result);

    return m_findInfo.m_result;
}

const QList<QTextCursor> &VEditor::findTextAllCached(const VSearchToken &p_token,
                                                     int p_start,
                                                     int p_end)
{
    if (p_token.isEmpty()) {
        m_findInfo.clear();
        return m_findInfo.m_result;
    }

    if (m_findInfo.isCached(p_token, p_start, p_end)) {
        return m_findInfo.m_result;
    }

    QList<QTextCursor> result = findTextAll(p_token, p_start, p_end);
    m_findInfo.update(p_token, p_start, p_end, result);

    return m_findInfo.m_result;
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
    int start = textCursorW().position();
    int skipPosition = start;

    bool found = false;
    while (true) {
        found = findTextHelper(p_text, p_options, p_forward, start, wrapped, retCursor);
        if (found) {
            if (p_forward && retCursor.selectionStart() == skipPosition) {
                // Skip the first match.
                skipPosition = -1;
                start = retCursor.selectionEnd();
                continue;
            }

            makeBlockVisible(m_document->findBlock(retCursor.selectionStart()));
            highlightIncrementalSearchedWord(retCursor);
        }

        break;
    }

    return found;
}

// @p_cursors is in ascending order.
// If @p_forward is true, find the smallest cursor whose selection start is greater
// than @p_pos or the first cursor if wrapped.
// Otherwise, find the largest cursor whose selection start is smaller than @p_pos
// or the last cursor if wrapped.
static int selectCursor(const QList<QTextCursor> &p_cursors,
                        int p_pos,
                        bool p_forward,
                        bool &p_wrapped)
{
    Q_ASSERT(!p_cursors.isEmpty());

    p_wrapped = false;

    int first = 0, last = p_cursors.size() - 1;
    int lastMatch = -1;
    while (first <= last) {
        int mid = (first + last) / 2;
        const QTextCursor &cur = p_cursors.at(mid);
        if (p_forward) {
            if (cur.selectionStart() < p_pos) {
                first = mid + 1;
            } else if (cur.selectionStart() == p_pos) {
                // Next one is the right one.
                if (mid < p_cursors.size() - 1) {
                    lastMatch = mid + 1;
                } else {
                    lastMatch = 0;
                    p_wrapped = true;
                }
                break;
            } else {
                // It is a match.
                if (lastMatch == -1 || mid < lastMatch) {
                    lastMatch = mid;
                }

                last = mid - 1;
            }
        } else {
            if (cur.selectionStart() > p_pos) {
                last = mid - 1;
            } else if (cur.selectionStart() == p_pos) {
                // Previous one is the right one.
                if (mid > 0) {
                    lastMatch = mid - 1;
                } else {
                    lastMatch = p_cursors.size() - 1;
                    p_wrapped = true;
                }
                break;
            } else {
                // It is a match.
                if (lastMatch == -1 || mid > lastMatch) {
                    lastMatch = mid;
                }

                first = mid + 1;
            }
        }
    }

    if (lastMatch == -1) {
        p_wrapped = true;
        lastMatch = p_forward ? 0 : (p_cursors.size() - 1);
    }

    return lastMatch;
}

bool VEditor::findText(const QString &p_text,
                       uint p_options,
                       bool p_forward,
                       QTextCursor *p_cursor,
                       QTextCursor::MoveMode p_moveMode,
                       bool p_useLeftSideOfCursor)
{
    return findTextInRange(p_text,
                           p_options,
                           p_forward,
                           p_cursor,
                           p_moveMode,
                           p_useLeftSideOfCursor);
}

bool VEditor::findText(const VSearchToken &p_token, bool p_forward, bool p_fromStart)
{
    clearIncrementalSearchedWordHighlight();

    if (p_token.isEmpty()) {
        m_findInfo.clear();
        clearSearchedWordHighlight();
        return false;
    }

    const QList<QTextCursor> &result = findTextAllCached(p_token);

    if (result.isEmpty()) {
        clearSearchedWordHighlight();

        emit m_object->statusMessage(QObject::tr("No match found"));
    } else {
        // Locate to the right match and update current cursor.
        QTextCursor cursor = textCursorW();
        int pos = p_fromStart ? (m_document->characterCount() + 1) : cursor.position();
        bool wrapped = false;
        int idx = selectCursor(result, pos, p_forward, wrapped);
        const QTextCursor &tcursor = result.at(idx);
        if (wrapped && !p_fromStart) {
            showWrapLabel();
        }

        cursor.setPosition(tcursor.selectionStart(), QTextCursor::MoveAnchor);
        setTextCursorW(cursor);

        highlightSearchedWord(result);

        highlightSearchedWordUnderCursor(tcursor);

        emit m_object->statusMessage(QObject::tr("Match found: %1 of %2")
                                                .arg(idx + 1)
                                                .arg(result.size()));
    }

    return !result.isEmpty();
}

bool VEditor::findTextInRange(const QString &p_text,
                              uint p_options,
                              bool p_forward,
                              QTextCursor *p_cursor,
                              QTextCursor::MoveMode p_moveMode,
                              bool p_useLeftSideOfCursor,
                              int p_start,
                              int p_end)
{
    clearIncrementalSearchedWordHighlight();

    if (p_text.isEmpty()) {
        m_findInfo.clear();
        clearSearchedWordHighlight();
        return false;
    }

    const QList<QTextCursor> &result = findTextAllCached(p_text, p_options, p_start, p_end);

    if (result.isEmpty()) {
        clearSearchedWordHighlight();

        emit m_object->statusMessage(QObject::tr("No match found"));
    } else {
        // Locate to the right match and update current cursor.
        QTextCursor cursor = textCursorW();
        int pos = p_cursor ? p_cursor->position() : cursor.position();
        if (p_useLeftSideOfCursor) {
            --pos;
        }

        bool wrapped = false;
        int idx = selectCursor(result, pos, p_forward, wrapped);
        const QTextCursor &tcursor = result.at(idx);
        if (wrapped) {
            showWrapLabel();
        }

        if (p_cursor) {
            p_cursor->setPosition(tcursor.selectionStart(), p_moveMode);
        } else {
            cursor.setPosition(tcursor.selectionStart(), p_moveMode);
            setTextCursorW(cursor);
        }

        highlightSearchedWord(result);

        highlightSearchedWordUnderCursor(tcursor);

        emit m_object->statusMessage(QObject::tr("Match found: %1 of %2")
                                                .arg(idx + 1)
                                                .arg(result.size()));
    }

    return !result.isEmpty();
}

bool VEditor::findTextOne(const QString &p_text, uint p_options, bool p_forward)
{
    clearIncrementalSearchedWordHighlight();

    if (p_text.isEmpty()) {
        clearSearchedWordHighlight();
        return false;
    }

    QTextCursor cursor = textCursorW();
    bool wrapped = false;
    QTextCursor retCursor;
    int start = cursor.position();
    bool found = findTextHelper(p_text, p_options, p_forward, start, wrapped, retCursor);
    if (found) {
        Q_ASSERT(!retCursor.isNull());
        if (wrapped) {
            showWrapLabel();
        }

        cursor.setPosition(retCursor.selectionStart(), QTextCursor::MoveAnchor);
        setTextCursorW(cursor);

        highlightSearchedWordUnderCursor(retCursor);
    } else {
        clearSearchedWordHighlight();
    }

    return found;
}

bool VEditor::findTextInRange(const QString &p_text,
                              uint p_options,
                              bool p_forward,
                              int p_start,
                              int p_end)
{
    return findTextInRange(p_text,
                           p_options,
                           p_forward,
                           nullptr,
                           QTextCursor::MoveAnchor,
                           false,
                           p_start,
                           p_end);
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

static bool isRegularExpressionSupported(const QRegExp &p_reg)
{
    if (!p_reg.isValid()) {
        return false;
    }

    // FIXME: hang bug in Qt's find().
    QRegExp test("[$^]+");
    if (test.exactMatch(p_reg.pattern())) {
        return false;
    }

    return true;
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
    bool caseSensitive = p_options & FindOption::CaseSensitive;
    QTextDocument::FindFlags findFlags = findOptionsToFlags(p_options, p_forward);

    // Use regular expression
    bool useRegExp = p_options & FindOption::RegularExpression;
    QRegExp exp;
    if (useRegExp) {
        useRegExp = true;
        exp = QRegExp(p_text,
                      caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        if (!isRegularExpressionSupported(exp)) {
            return false;
        }
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

void VEditor::highlightSearchedWord(const QList<QTextCursor> &p_matches)
{
    QList<QTextEdit::ExtraSelection> &selects = m_extraSelections[(int)SelectionId::SearchedKeyword];
    if (!g_config->getHighlightSearchedWord() || p_matches.isEmpty()) {
        if (!selects.isEmpty()) {
            selects.clear();
            highlightExtraSelections(true);
        }

        return;
    }

    selects.clear();

    QTextCharFormat format;
    format.setForeground(m_searchedWordFg);
    format.setBackground(m_searchedWordBg);

    for (int i = 0; i < p_matches.size(); ++i) {
        QTextEdit::ExtraSelection select;
        select.format = format;
        select.cursor = p_matches[i];
        selects.append(select);
    }

    highlightExtraSelections();
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
                      (p_options & FindOption::CaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive);
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
            findTextOne(p_text, p_options, true);
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
                      (p_options & FindOption::CaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive);
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

static Qt::CaseSensitivity completionCaseSensitivity(const QString &p_text)
{
    bool upperCase = false;
    for (int i = 0; i < p_text.size(); ++i) {
        if (p_text[i].isUpper()) {
            upperCase = true;
            break;
        }
    }

    return upperCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
}

void VEditor::requestCompletion(bool p_reversed)
{
    QTextCursor cursor = textCursorW();
    cursor.clearSelection();
    setTextCursorW(cursor);

    QStringList words = generateCompletionCandidates(p_reversed);
    QString prefix = fetchCompletionPrefix();
    // Smart case.
    Qt::CaseSensitivity cs = completionCaseSensitivity(prefix);

    cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, prefix.size());
    QRect popupRect = cursorRectW(cursor);
    popupRect.setLeft(popupRect.left() + lineNumberAreaWidth());

    m_completer->performCompletion(words, prefix, cs, p_reversed, popupRect, this);
}

bool VEditor::isCompletionActivated() const
{
    if (m_completer->widget() == m_editor
        && m_completer->isPopupVisible()) {
        return true;
    }

    return false;
}

QStringList VEditor::generateCompletionCandidates(bool p_reversed) const
{
    QString content = getContent();
    QTextCursor cursor = textCursorW();
    int start, end;
    VEditUtils::findCurrentWord(cursor, start, end, true);

    QRegExp reg("\\W+");

    QStringList above = content.left(start).split(reg, QString::SkipEmptyParts);
    QStringList below = content.mid(end).split(reg, QString::SkipEmptyParts);

    if (p_reversed) {
        QStringList rev;
        rev.reserve(above.size() + below.size());
        for (auto it = above.rbegin(); it != above.rend(); ++it) {
            rev.append(*it);
        }

        for (auto it = below.rbegin(); it != below.rend(); ++it) {
            rev.append(*it);
        }

        rev.removeDuplicates();

        QStringList res;
        res.reserve(rev.size());
        for (auto it = rev.rbegin(); it != rev.rend(); ++it) {
            res.append(*it);
        }

        return res;
    } else {
        // below + above.
        below.append(above);
        below.removeDuplicates();
        return below;
    }
}

QString VEditor::fetchCompletionPrefix() const
{
    QTextCursor cursor = textCursorW();
    if (cursor.atBlockStart()) {
        return QString();
    }

    int pos = cursor.position() - 1;
    int blockPos = cursor.block().position();
    QString prefix;
    while (pos >= blockPos) {
        QChar ch = m_document->characterAt(pos);
        if (VEditUtils::isSpaceOrWordSeparator(ch)) {
            break;
        }

        prefix.prepend(ch);
        --pos;
    }

    return prefix;
}

// @p_prefix may be longer than @p_completion.
void VEditor::insertCompletion(const QString &p_prefix, const QString &p_completion)
{
    QTextCursor cursor = textCursorW();

    cursor.joinPreviousEditBlock();
    cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, p_prefix.size());
    cursor.insertText(p_completion);
    cursor.endEditBlock();

    setTextCursorW(cursor);
}

QList<QTextCursor> VEditor::findTextAllInRange(const QTextDocument *p_doc,
                                               const QString &p_text,
                                               QTextDocument::FindFlags p_flags,
                                               int p_start,
                                               int p_end)
{
    QList<QTextCursor> results;
    if (p_text.isEmpty()) {
        return results;
    }

    int start = p_start;
    int end = p_end == -1 ? p_doc->characterCount() + 1 : p_end;

    while (start < end) {
        QTextCursor cursor = p_doc->find(p_text, start, p_flags);
        if (cursor.isNull()) {
            break;
        } else {
            start = cursor.selectionEnd();
            if (start <= end) {
                results.append(cursor);
            }
        }
    }

    return results;
}

QList<QTextCursor> VEditor::findTextAllInRange(const QTextDocument *p_doc,
                                               const QRegExp &p_reg,
                                               QTextDocument::FindFlags p_flags,
                                               int p_start,
                                               int p_end)
{
    QList<QTextCursor> results;
    if (!isRegularExpressionSupported(p_reg)) {
        return results;
    }

    int start = p_start;
    int end = p_end == -1 ? p_doc->characterCount() + 1 : p_end;

    while (start < end) {
        QTextCursor cursor = p_doc->find(p_reg, start, p_flags);
        if (cursor.isNull()) {
            break;
        } else {
            start = cursor.selectionEnd();
            if (start <= end) {
                results.append(cursor);
            }
        }
    }

    return results;
}

void VEditor::clearFindCache()
{
    m_findInfo.clearResult();
}

void VEditor::nextMatch(bool p_forward)
{
    if (m_findInfo.isNull()) {
        return;
    }

    if (m_findInfo.m_useToken) {
        findText(m_findInfo.m_token, p_forward, false);
    } else {
        findTextInRange(m_findInfo.m_text,
                        m_findInfo.m_options,
                        p_forward,
                        m_findInfo.m_start,
                        m_findInfo.m_end);
    }
}

void VEditor::pastePlainText()
{
    if (!m_file || !m_file->isModifiable()) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    QString text(clipboard->text());
    if (text.isEmpty()) {
        return;
    }

    m_editOps->insertText(text);
}

void VEditor::scrollCursorLineIfNecessary()
{
    bool moved = false;
    QTextCursor cursor = textCursorW();
    int mode = g_config->getAutoScrollCursorLine();
    switch (mode) {
    case AutoScrollEndOfDoc:
        V_FALLTHROUGH;
    case AutoScrollAlways:
    {
        const int bc = m_document->blockCount();
        int bn = cursor.blockNumber();
        int margin = -1;
        if (mode == AutoScrollAlways) {
            margin = m_editor->rect().height() / 8;
        } else if (bn == bc - 1) {
            margin = m_editor->rect().height() / 4;
        }

        if (margin > -1) {
            moved = true;
            scrollBlockInPage(bn, 1, margin);
        }

        break;
    }

    default:
        break;
    }

    if (!moved) {
        makeBlockVisible(cursor.block());
    }
}

QFont VEditor::getFont() const
{
    return m_editor->font();
}
