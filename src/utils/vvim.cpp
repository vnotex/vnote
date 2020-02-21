#include "vvim.h"
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QString>
#include <QScrollBar>
#include <QDebug>
#include <QClipboard>
#include <QApplication>
#include <QMimeData>
#include <QDir>
#include "vconfigmanager.h"
#include "veditor.h"
#include "utils/veditutils.h"
#include "vconstants.h"
#include "vmdeditor.h"
#include "vmainwindow.h"
#include "vdirectory.h"
#include "vdirectorytree.h"

extern VConfigManager *g_config;
extern VMainWindow *g_mainWin;

const QChar VVim::c_unnamedRegister = QChar('"');
const QChar VVim::c_blackHoleRegister = QChar('_');
const QChar VVim::c_selectionRegister = QChar('+');
QMap<QChar, VVim::Register> VVim::s_registers;

const int VVim::SearchHistory::c_capacity = 50;

#define ADDKEY(x, y) case (x): {ch = (y); break;}

// Returns NULL QChar if invalid.
static QChar keyToChar(int p_key, int p_modifiers)
{
    if (p_key >= Qt::Key_0 && p_key <= Qt::Key_9) {
        return QChar('0' + (p_key - Qt::Key_0));
    } else if (p_key >= Qt::Key_A && p_key <= Qt::Key_Z) {
        if (p_modifiers == Qt::ShiftModifier
            || VUtils::isControlModifierForVim(p_modifiers)) {
            return QChar('A' + (p_key - Qt::Key_A));
        } else {
            return QChar('a' + (p_key - Qt::Key_A));
        }
    }

    QChar ch;
    switch (p_key) {
    ADDKEY(Qt::Key_Tab, '\t');
    ADDKEY(Qt::Key_Space, ' ');
    ADDKEY(Qt::Key_Exclam, '!');
    ADDKEY(Qt::Key_QuoteDbl, '"');
    ADDKEY(Qt::Key_NumberSign, '#');
    ADDKEY(Qt::Key_Dollar, '$');
    ADDKEY(Qt::Key_Percent, '%');
    ADDKEY(Qt::Key_Ampersand, '&');
    ADDKEY(Qt::Key_Apostrophe, '\'');
    ADDKEY(Qt::Key_ParenLeft, '(');
    ADDKEY(Qt::Key_ParenRight, ')');
    ADDKEY(Qt::Key_Asterisk, '*');
    ADDKEY(Qt::Key_Plus, '+');
    ADDKEY(Qt::Key_Comma, ',');
    ADDKEY(Qt::Key_Minus, '-');
    ADDKEY(Qt::Key_Period, '.');
    ADDKEY(Qt::Key_Slash, '/');
    ADDKEY(Qt::Key_Colon, ':');
    ADDKEY(Qt::Key_Semicolon, ';');
    ADDKEY(Qt::Key_Less, '<');
    ADDKEY(Qt::Key_Equal, '=');
    ADDKEY(Qt::Key_Greater, '>');
    ADDKEY(Qt::Key_Question, '?');
    ADDKEY(Qt::Key_At, '@');
    ADDKEY(Qt::Key_BracketLeft, '[');
    ADDKEY(Qt::Key_Backslash, '\\');
    ADDKEY(Qt::Key_BracketRight, ']');
    ADDKEY(Qt::Key_AsciiCircum, '^');
    ADDKEY(Qt::Key_Underscore, '_');
    ADDKEY(Qt::Key_QuoteLeft, '`');
    ADDKEY(Qt::Key_BraceLeft, '{');
    ADDKEY(Qt::Key_Bar, '|');
    ADDKEY(Qt::Key_BraceRight, '}');
    ADDKEY(Qt::Key_AsciiTilde, '~');

    default:
        break;
    }

    return ch;
}

#define ADDCHAR(x, y) case (x): {p_key = (y); break;}
#define ADDCHAR_SHIFT(x, y) case (x): {p_key = (y); p_modifiers = Qt::ShiftModifier; break;}

static void charToKey(const QChar &p_char, int &p_key, int &p_modifiers)
{
    p_modifiers = Qt::NoModifier;

    ushort ucode = p_char.unicode();
    if (ucode >= '0' && ucode <= '9') {
        p_key = Qt::Key_0 + ucode - '0';
        return;
    } else if (ucode >= 'a' && ucode <= 'z') {
        p_key = Qt::Key_A + ucode - 'a';
        return;
    } else if (ucode >= 'A' && ucode <= 'Z') {
        p_key = Qt::Key_A + ucode - 'A';
        p_modifiers = Qt::ShiftModifier;
        return;
    }

    switch (p_char.unicode()) {
    ADDCHAR('\t', Qt::Key_Tab);
    ADDCHAR(' ', Qt::Key_Space);
    ADDCHAR_SHIFT('!', Qt::Key_Exclam);
    ADDCHAR_SHIFT('"', Qt::Key_QuoteDbl);
    ADDCHAR_SHIFT('#', Qt::Key_NumberSign);
    ADDCHAR_SHIFT('$', Qt::Key_Dollar);
    ADDCHAR_SHIFT('%', Qt::Key_Percent);
    ADDCHAR_SHIFT('&', Qt::Key_Ampersand);
    ADDCHAR('\'', Qt::Key_Apostrophe);
    ADDCHAR_SHIFT('(', Qt::Key_ParenLeft);
    ADDCHAR_SHIFT(')', Qt::Key_ParenRight);
    ADDCHAR_SHIFT('*', Qt::Key_Asterisk);
    ADDCHAR_SHIFT('+', Qt::Key_Plus);
    ADDCHAR(',', Qt::Key_Comma);
    ADDCHAR('-', Qt::Key_Minus);
    ADDCHAR('.', Qt::Key_Period);
    ADDCHAR('/', Qt::Key_Slash);
    ADDCHAR_SHIFT(':', Qt::Key_Colon);
    ADDCHAR(';', Qt::Key_Semicolon);
    ADDCHAR_SHIFT('<', Qt::Key_Less);
    ADDCHAR('=', Qt::Key_Equal);
    ADDCHAR_SHIFT('>', Qt::Key_Greater);
    ADDCHAR_SHIFT('?', Qt::Key_Question);
    ADDCHAR_SHIFT('@', Qt::Key_At);
    ADDCHAR('[', Qt::Key_BracketLeft);
    ADDCHAR('\\', Qt::Key_Backslash);
    ADDCHAR(']', Qt::Key_BracketRight);
    ADDCHAR_SHIFT('^', Qt::Key_AsciiCircum);
    ADDCHAR_SHIFT('_', Qt::Key_Underscore);
    ADDCHAR('`', Qt::Key_QuoteLeft);
    ADDCHAR_SHIFT('{', Qt::Key_BraceLeft);
    ADDCHAR_SHIFT('|', Qt::Key_Bar);
    ADDCHAR_SHIFT('}', Qt::Key_BraceRight);
    ADDCHAR_SHIFT('~', Qt::Key_AsciiTilde);

    default:
        p_key = -1;
        break;
    }
}

static QString keyToString(int p_key, int p_modifiers)
{
    QChar ch = keyToChar(p_key, p_modifiers);
    if (ch.isNull()) {
        return QString();
    }

    if (VUtils::isControlModifierForVim(p_modifiers)) {
        return QString("^") + ch;
    } else {
        return ch;
    }
}

VVim::VVim(VEditor *p_editor)
    : QObject(p_editor->getEditor()),
      m_editor(p_editor),
      m_editConfig(&p_editor->getConfig()),
      m_mode(VimMode::Invalid),
      m_resetPositionInBlock(true),
      m_regName(c_unnamedRegister),
      m_leaderKey(Key(Qt::Key_Space)),
      m_replayLeaderSequence(false),
      m_registerPending(false),
      m_insertModeAfterCommand(false),
      m_positionBeforeVisualMode(0)
{
    Q_ASSERT(m_editConfig->m_enableVimMode);

    setMode(VimMode::Normal);

    initLeaderKey();

    initRegisters();

    connect(m_editor->object(), &VEditorObject::mousePressed,
            this, &VVim::handleMousePressed);
    connect(m_editor->object(), &VEditorObject::mouseMoved,
            this, &VVim::handleMouseMoved);
    connect(m_editor->object(), &VEditorObject::mouseReleased,
            this, &VVim::handleMouseReleased);
    connect(m_editor->object(), &VEditorObject::mouseDoubleClicked,
            this, &VVim::handleMouseDoubleClicked);
}

void VVim::initLeaderKey()
{
    QChar ch = g_config->getVimLeaderKey();
    Q_ASSERT(!ch.isNull());

    int key, modifiers;
    charToKey(ch, key, modifiers);
    m_leaderKey = Key(key, modifiers);
    qDebug() << "Vim leader key" << ch << key << modifiers;
}

// Set @p_cursor's position specified by @p_positionInBlock.
// If @p_positionInBlock is bigger than the block's length, move to the end of block.
// Need to setTextCursor() after calling this.
static void setCursorPositionInBlock(QTextCursor &p_cursor, int p_positionInBlock,
                                     QTextCursor::MoveMode p_mode)
{
    QTextBlock block = p_cursor.block();
    if (block.length() > p_positionInBlock) {
        p_cursor.setPosition(block.position() + p_positionInBlock, p_mode);
    } else {
        p_cursor.movePosition(QTextCursor::EndOfBlock, p_mode, 1);
    }
}

// Find the start and end of the spaces @p_cursor locates in (within a single block).
// @p_start and @p_end will be the global position of the start and end of the spaces.
// @p_start will equals to @p_end if @p_cursor is not a space.
static void findCurrentSpace(const QTextCursor &p_cursor, int &p_start, int &p_end)
{
    QTextBlock block = p_cursor.block();
    QString text = block.text();
    int pib = p_cursor.positionInBlock();

    if (pib < text.size() && !text[pib].isSpace()) {
        p_start = p_end = p_cursor.position();
        return;
    }

    p_start = 0;
    for (int i = pib - 1; i >= 0; --i) {
        if (!text[i].isSpace()) {
            p_start = i + 1;
            break;
        }
    }

    p_end = block.length() - 1;
    for (int i = pib; i < text.size(); ++i) {
        if (!text[i].isSpace()) {
            p_end = i;
            break;
        }
    }

    p_start += block.position();
    p_end += block.position();
}

// Move @p_cursor to skip spaces if current cursor is placed at a space
// (may move across blocks). It will stop by the empty block on the way.
// Forward: wwwwsssss|wwww
// Backward: wwww|ssssswwww
static void moveCursorAcrossSpaces(QTextCursor &p_cursor,
                                   QTextCursor::MoveMode p_mode,
                                   bool p_forward,
                                   bool p_stopAtBoundary = false)
{
    while (true) {
        QTextBlock block = p_cursor.block();
        QString text = block.text();
        int pib = p_cursor.positionInBlock();

        if (p_forward) {
            for (; pib < text.size(); ++pib) {
                if (!text[pib].isSpace()) {
                    break;
                }
            }

            if (pib == text.size() && !p_stopAtBoundary) {
                // Move to next block.
                p_cursor.movePosition(QTextCursor::Down, p_mode, 1);
                if (block.blockNumber() == p_cursor.block().blockNumber()) {
                    // Already at the last block.
                    p_cursor.movePosition(QTextCursor::EndOfBlock, p_mode, 1);
                    break;
                } else {
                    p_cursor.movePosition(QTextCursor::StartOfBlock, p_mode, 1);
                    if (p_cursor.block().length() <= 1) {
                        break;
                    }
                }
            } else {
                // Found non-space character.
                p_cursor.setPosition(block.position() + pib, p_mode);
                break;
            }
        } else {
            int idx = pib - 1;
            for (; idx >= 0; --idx) {
                if (!text[idx].isSpace()) {
                    break;
                }
            }

            if (idx == -1 && !p_stopAtBoundary) {
                // Move to previous block.
                p_cursor.movePosition(QTextCursor::Up, p_mode, 1);
                if (block.blockNumber() == p_cursor.block().blockNumber()) {
                    // Already at the first block.
                    p_cursor.movePosition(QTextCursor::StartOfBlock, p_mode, 1);
                    break;
                } else {
                    p_cursor.movePosition(QTextCursor::EndOfBlock, p_mode, 1);
                    if (p_cursor.block().length() <= 1) {
                        break;
                    }
                }
            } else {
                // Found non-space character.
                p_cursor.setPosition(block.position() + idx + 1, p_mode);
                break;
            }
        }
    }
}

// Expand the selection of @p_cursor to contain additional spaces at the two ends
// within a block.
static void expandSelectionAcrossSpacesWithinBlock(QTextCursor &p_cursor)
{
    QTextBlock block = p_cursor.block();
    QString text = block.text();
    int start = p_cursor.selectionStart() - block.position();
    int end = p_cursor.selectionEnd() - block.position();

    for (int i = start - 1; i >= 0; --i) {
        if (!text[i].isSpace()) {
            start = i + 1;
            break;
        }
    }

    for (int i = end; i < text.size(); ++i) {
        if (!text[i].isSpace()) {
            end = i;
            break;
        }
    }

    start += block.position();
    end += block.position();

    if (start == p_cursor.selectionStart() && end == p_cursor.selectionEnd()) {
        return;
    }

    if (p_cursor.anchor() <= p_cursor.position()) {
        p_cursor.setPosition(start, QTextCursor::MoveAnchor);
        p_cursor.setPosition(end, QTextCursor::KeepAnchor);
    } else {
        p_cursor.setPosition(end, QTextCursor::MoveAnchor);
        p_cursor.setPosition(start, QTextCursor::KeepAnchor);
    }
}

// In Change action, after deleting selected block text, we need to insert a new
// block for user input.
// @p_deletionStart is the global position of the start of the deletion.
// Should be called immediately after the deletion.
static void insertChangeBlockAfterDeletion(QTextCursor &p_cursor, int p_deletionStart)
{
    if (p_cursor.position() < p_deletionStart) {
        // Insert a new block below.
        p_cursor.movePosition(QTextCursor::EndOfBlock);
        p_cursor.insertBlock();
    } else {
        // Insert a new block above.
        p_cursor.movePosition(QTextCursor::StartOfBlock);
        p_cursor.insertBlock();
        p_cursor.movePosition(QTextCursor::PreviousBlock);
    }

    if (g_config->getAutoIndent()) {
        VEditUtils::indentBlockAsBlock(p_cursor, false);
    }
}

// Given the percentage of the text, return the corresponding block number.
// Notice that the block number is based on 0.
// Returns -1 if it is not valid.
static int percentageToBlockNumber(const QTextDocument *p_doc, int p_percent)
{
    if (p_percent > 100 || p_percent <= 0) {
        return -1;
    }

    int nrBlock = p_doc->blockCount();
    int num = nrBlock * (p_percent * 1.0 / 100) - 1;

    return num >= 0 ? num : 0;
}

// Replace each of the character of selected text with @p_char.
// Returns true if replacement has taken place.
// Need to setTextCursor() after calling this.
static bool replaceSelectedTextWithCharacter(QTextCursor &p_cursor, QChar p_char)
{
    if (!p_cursor.hasSelection()) {
        return false;
    }

    int start = p_cursor.selectionStart();
    int end = p_cursor.selectionEnd();
    p_cursor.setPosition(start, QTextCursor::MoveAnchor);
    while (p_cursor.position() < end) {
        if (p_cursor.atBlockEnd()) {
            p_cursor.movePosition(QTextCursor::NextCharacter);
        } else {
            p_cursor.deleteChar();
            // insertText() will move the cursor right after the inserted text.
            p_cursor.insertText(p_char);
        }
    }

    return true;
}

// Reverse the case of selected text.
// Returns true if the reverse has taken place.
// Need to setTextCursor() after calling this.
static bool reverseSelectedTextCase(QTextCursor &p_cursor)
{
    if (!p_cursor.hasSelection()) {
        return false;
    }

    QTextDocument *doc = p_cursor.document();
    int start = p_cursor.selectionStart();
    int end = p_cursor.selectionEnd();
    p_cursor.setPosition(start, QTextCursor::MoveAnchor);
    while (p_cursor.position() < end) {
        if (p_cursor.atBlockEnd()) {
            p_cursor.movePosition(QTextCursor::NextCharacter);
        } else {
            QChar ch = doc->characterAt(p_cursor.position());
            bool changed = false;
            if (ch.isLower()) {
                ch = ch.toUpper();
                changed = true;
            } else if (ch.isUpper()) {
                ch = ch.toLower();
                changed = true;
            }

            if (changed) {
                p_cursor.deleteChar();
                // insertText() will move the cursor right after the inserted text.
                p_cursor.insertText(ch);
            } else {
                p_cursor.movePosition(QTextCursor::NextCharacter);
            }
        }
    }

    return true;
}

// Join current cursor line and the next line.
static void joinTwoLines(QTextCursor &p_cursor, bool p_modifySpaces)
{
    QTextDocument *doc = p_cursor.document();
    QTextBlock firstBlock = p_cursor.block();
    QString textToAppend = firstBlock.next().text();
    p_cursor.movePosition(QTextCursor::EndOfBlock);

    if (p_modifySpaces) {
        bool insertSpaces = false;
        if (firstBlock.length() > 1) {
            QChar lastChar = doc->characterAt(p_cursor.position() - 1);
            if (!lastChar.isSpace()) {
                insertSpaces = true;
            }
        }

        if (insertSpaces) {
            p_cursor.insertText(" ");
        }

        // Remove indentation.
        int idx = 0;
        for (idx = 0; idx < textToAppend.size(); ++idx) {
            if (!textToAppend[idx].isSpace()) {
                break;
            }
        }

        textToAppend = textToAppend.right(textToAppend.size() - idx);
    }

    // Now p_cursor is at the end of the first block.
    int position = p_cursor.block().position() + p_cursor.positionInBlock();
    p_cursor.insertText(textToAppend);

    // Delete the second block.
    p_cursor.movePosition(QTextCursor::NextBlock);
    VEditUtils::removeBlock(p_cursor);

    // Position p_cursor right at the front of appended text.
    p_cursor.setPosition(position);
}

// Join lines specified by [@p_firstBlock, @p_firstBlock + p_blockCount).
// Need to check the block range (based on 0).
static bool joinLines(QTextCursor &p_cursor,
                      int p_firstBlock,
                      int p_blockCount,
                      bool p_modifySpaces)
{
    QTextDocument *doc = p_cursor.document();
    int totalBlockCount = doc->blockCount();
    if (p_blockCount <= 0
        || p_firstBlock >= totalBlockCount - 1) {
        return false;
    }

    p_blockCount = qMin(p_blockCount, totalBlockCount - p_firstBlock);
    p_cursor.setPosition(doc->findBlockByNumber(p_firstBlock).position());
    for (int i = 1; i < p_blockCount; ++i) {
        joinTwoLines(p_cursor, p_modifySpaces);
    }

    return true;
}

bool VVim::handleKeyPressEvent(QKeyEvent *p_event, int *p_autoIndentPos)
{
    int modifiers = p_event->modifiers();
    QString keyText = p_event->text();
    if (keyText.size() == 1) {
        if (keyText[0].isUpper()) {
            // Upper case.
            modifiers |= Qt::ShiftModifier;
        } else if (keyText[0].isLower()) {
            // Lower case.
            if (modifiers & Qt::ShiftModifier) {
                modifiers &= ~Qt::ShiftModifier;
            }
        }
    }

    bool ret = handleKeyPressEvent(p_event->key(), modifiers, p_autoIndentPos);
    if (ret) {
        p_event->accept();
    }

    return ret;
}

bool VVim::handleKeyPressEvent(int key, int modifiers, int *p_autoIndentPos)
{
    bool ret = false;
    bool resetPositionInBlock = true;
    Key keyInfo(key, modifiers);
    bool unindent = false;
    int autoIndentPos = p_autoIndentPos ? *p_autoIndentPos : -1;

    // Handle Insert mode key press.
    if (VimMode::Insert == m_mode) {
        if (checkEnterNormalMode(key, modifiers)) {
            // See if we need to cancel auto indent.
            bool cancelAutoIndent = false;
            if (p_autoIndentPos && *p_autoIndentPos > -1) {
                QTextCursor cursor = m_editor->textCursorW();
                cancelAutoIndent = VEditUtils::needToCancelAutoIndent(*p_autoIndentPos, cursor);

                if (cancelAutoIndent) {
                    autoIndentPos = -1;
                    VEditUtils::deleteIndentAndListMark(cursor);
                    m_editor->setTextCursorW(cursor);
                }
            }

            // Clear selection and enter Normal mode.
            if (!cancelAutoIndent) {
                clearSelection();
            }

            setMode(VimMode::Normal);
            goto clear_accept;
        }

        if (m_registerPending) {
            // Ctrl and Shift may be sent out first.
            if (VUtils::isMetaKey(key)) {
                goto accept;
            }

            // Expecting a register name.
            QChar reg = keyToRegisterName(keyInfo);
            if (!reg.isNull()) {
                // Insert register content.
                m_editor->insertPlainTextW(getRegister(reg).read());
            }

            goto clear_accept;
        } else if (key == Qt::Key_R && VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+R, insert the content of a register.
            m_pendingKeys.append(keyInfo);
            m_registerPending = true;
            goto accept;
        }

        if (key == Qt::Key_O && VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+O, enter normal mode, execute one command, then return to insert mode.
            m_insertModeAfterCommand = true;
            clearSelection();
            setMode(VimMode::Normal);
            goto accept;
        }

        // Let it be handled outside VVim.
        goto exit;
    }

    // Ctrl and Shift may be sent out first.
    if (isKeyShouldBeIgnored(key)) {
        goto accept;
    }

    if (m_replayLeaderSequence) {
        qDebug() << "replaying sequence" << keyToChar(key, modifiers);
    }

    m_pendingKeys.append(keyInfo);

    if (expectingLeaderSequence()) {
        if (processLeaderSequence(keyInfo)) {
            goto accept;
        } else {
            goto clear_accept;
        }
    }

    if (expectingRegisterName()) {
        // Expecting a register name.
        QChar reg = keyToRegisterName(keyInfo);
        if (!reg.isNull()) {
            m_keys.clear();
            setCurrentRegisterName(reg);
            Register &r = getRegister(reg);
            if (r.isNamedRegister()) {
                r.m_append = (modifiers == Qt::ShiftModifier);
            } else {
                Q_ASSERT(!r.m_append);
            }

            goto accept;
        }

        goto clear_accept;
    }

    if (expectingMarkName()) {
        // Expecting a mark name to create a mark.
        if (keyInfo.isAlphabet() && modifiers == Qt::NoModifier) {
            m_keys.clear();
            m_marks.setMark(keyToChar(key, modifiers), m_editor->textCursorW());
        }

        goto clear_accept;
    }

    if (expectingMarkTarget()) {
        // Expecting a mark name as the target.
        Movement mm = Movement::Invalid;
        const Key &aKey = m_keys.first();
        if (aKey == Key(Qt::Key_Apostrophe)) {
            mm = Movement::MarkJumpLine;
        } else {
            Q_ASSERT(aKey == Key(Qt::Key_QuoteLeft));
            mm = Movement::MarkJump;
        }

        tryAddMoveAction();
        addMovementToken(mm, keyInfo);
        processCommand(m_tokens);
        goto clear_accept;
    }

    if (expectingCharacterTarget()) {
        // Expecting a target character for f/F/t/T.
        Movement mm = Movement::Invalid;
        const Key &aKey = m_keys.first();
        if (aKey.m_key == Qt::Key_F) {
            if (aKey.m_modifiers == Qt::NoModifier) {
                mm = Movement::FindForward;
            } else {
                mm = Movement::FindBackward;
            }
        } else {
            if (aKey.m_modifiers == Qt::NoModifier) {
                mm = Movement::TillForward;
            } else {
                mm = Movement::TillBackward;
            }
        }

        tryAddMoveAction();
        addMovementToken(mm, keyInfo);
        m_lastFindToken = m_tokens.last();
        processCommand(m_tokens);

        goto clear_accept;
    }

    if (expectingReplaceCharacter()) {
        // Expecting a character to replace with for r.
        addActionToken(Action::Replace);
        addKeyToken(keyInfo);
        processCommand(m_tokens);

        goto clear_accept;
    }

    // Check leader key here. If leader key conflicts with other keys, it will
    // overwrite it.
    // Leader sequence is just like an action.
    if (keyInfo == m_leaderKey
        && !hasActionToken()
        && !hasNonDigitPendingKeys()
        && !m_replayLeaderSequence) {
        tryGetRepeatToken(m_keys, m_tokens);

        Q_ASSERT(m_keys.isEmpty());

        m_pendingKeys.pop_back();
        m_pendingKeys.append(Key(Qt::Key_Backslash));
        m_keys.append(Key(Qt::Key_Backslash));
        goto accept;
    }

    // We will add key to m_keys. If all m_keys can combined to a token, add
    // a new token to m_tokens, clear m_keys and try to process m_tokens.
    switch (key) {
    case Qt::Key_0:
    {
        if (modifiers == Qt::NoModifier
            || modifiers == Qt::KeypadModifier) {
            if (checkPendingKey(Key(Qt::Key_G))) {
                // StartOfVisualLine.
                tryAddMoveAction();
                m_tokens.append(Token(Movement::StartOfVisualLine));
                processCommand(m_tokens);
            } else if (m_keys.isEmpty()) {
                // StartOfLine.
                tryAddMoveAction();
                m_tokens.append(Token(Movement::StartOfLine));
                processCommand(m_tokens);
            } else if (m_keys.last().isDigit()) {
                // Repeat.
                m_keys.append(keyInfo);
                resetPositionInBlock = false;
                goto accept;
            }
        }

        break;
    }

    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:
    {
        if (modifiers == Qt::NoModifier
            || modifiers == Qt::KeypadModifier) {
            if (!m_keys.isEmpty() && numberFromKeySequence(m_keys) == -1) {
                // Invalid sequence.
                break;
            }

            m_keys.append(keyInfo);
            resetPositionInBlock = false;
            goto accept;
        }

        break;
    }

    case Qt::Key_Left:
    case Qt::Key_Down:
    case Qt::Key_Up:
    case Qt::Key_Right:
    case Qt::Key_H:
    case Qt::Key_J:
    case Qt::Key_K:
    case Qt::Key_L:
    {
        if (modifiers == Qt::NoModifier || modifiers == Qt::KeypadModifier) {
            // Check if we could generate a Repeat token.
            tryGetRepeatToken(m_keys, m_tokens);

            // Generate a Movement token.
            Movement mm = Movement::Invalid;

            if (!m_keys.isEmpty()) {
                // gj, gk.
                Key gKey(Qt::Key_G);
                if (m_keys.size() == 1 && m_keys.at(0) == gKey) {
                    if (key == Qt::Key_J) {
                        mm = Movement::VisualDown;
                    } else if (key == Qt::Key_K) {
                        mm = Movement::VisualUp;
                    } else {
                        break;
                    }
                } else {
                    // Not a valid sequence.
                    break;
                }
            } else {
                // h, j, k, l.
                switch (key) {
                case Qt::Key_H:
                case Qt::Key_Left:
                    mm = Movement::Left;
                    break;

                case Qt::Key_L:
                case Qt::Key_Right:
                    mm = Movement::Right;
                    break;

                case Qt::Key_J:
                case Qt::Key_Down:
                    mm = Movement::Down;
                    break;

                case Qt::Key_K:
                case Qt::Key_Up:
                    mm = Movement::Up;
                    break;

                default:
                    V_ASSERT(false);
                }
            }

            V_ASSERT(mm != Movement::Invalid);
            tryAddMoveAction();
            addMovementToken(mm);
            processCommand(m_tokens);
            resetPositionInBlock = false;
        } else if (modifiers == Qt::ShiftModifier) {
            if (key == Qt::Key_J) {
                tryGetRepeatToken(m_keys, m_tokens);

                if (hasActionToken()) {
                    break;
                }

                if (checkPendingKey(Key(Qt::Key_G))) {
                    // gJ, JoinNoModification.
                    addActionToken(Action::JoinNoModification);
                } else if (m_keys.isEmpty()) {
                    // J, Join.
                    addActionToken(Action::Join);
                }

                processCommand(m_tokens);
            }
        } else if (VUtils::isControlModifierForVim(modifiers)) {
            if (key == Qt::Key_J || key == Qt::Key_K) {
                // Let it be handled outside.
                resetState();
                goto exit;
            }
        }

        break;
    }

    case Qt::Key_I:
    {
        if (modifiers == Qt::NoModifier) {
            if (hasActionTokenValidForTextObject()) {
                // Inner text object.
                tryGetRepeatToken(m_keys, m_tokens);
                if (!m_keys.isEmpty()) {
                    // Invalid sequence;
                    break;
                }

                m_keys.append(keyInfo);
                goto accept;
            }

            // Enter Insert mode.
            // Different from Vim:
            // We enter Insert mode even in Visual and VisualLine mode. We
            // also keep the selection after the mode change.
            if (checkMode(VimMode::Normal)
                || checkMode(VimMode::Visual)
                || checkMode(VimMode::VisualLine)) {
                setMode(VimMode::Insert, false);
            }
        } else if (modifiers == Qt::ShiftModifier) {
            QTextCursor cursor = m_editor->textCursorW();
            if (m_mode == VimMode::Normal) {
                // Insert at the first non-space character.
                VEditUtils::moveCursorFirstNonSpaceCharacter(cursor, QTextCursor::MoveAnchor);
            } else if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                // Insert at the start of line.
                cursor.movePosition(QTextCursor::StartOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
            }

            m_editor->setTextCursorW(cursor);
            setMode(VimMode::Insert);
        } else if (VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+I, jump to next location.
            if (!m_tokens.isEmpty()
                || !checkMode(VimMode::Normal)) {
                break;
            }

            tryGetRepeatToken(m_keys, m_tokens);

            if (!m_keys.isEmpty()) {
                break;
            }

            addActionToken(Action::JumpNextLocation);
            processCommand(m_tokens);
            break;
        }

        break;
    }

    case Qt::Key_A:
    {
        if (modifiers == Qt::NoModifier) {
            if (hasActionTokenValidForTextObject()) {
                // Around text object.
                tryGetRepeatToken(m_keys, m_tokens);
                if (!m_keys.isEmpty()) {
                    // Invalid sequence;
                    break;
                }

                m_keys.append(keyInfo);
                goto accept;
            }

            // Enter Insert mode.
            // Move cursor back one character.
            if (m_mode == VimMode::Normal) {
                QTextCursor cursor = m_editor->textCursorW();
                V_ASSERT(!cursor.hasSelection());

                if (!cursor.atBlockEnd()) {
                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
                    m_editor->setTextCursorW(cursor);
                }

                setMode(VimMode::Insert);
            }
        } else if (modifiers == Qt::ShiftModifier) {
            // Insert at the end of line.
            QTextCursor cursor = m_editor->textCursorW();
            if (m_mode == VimMode::Normal) {
                cursor.movePosition(QTextCursor::EndOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
                m_editor->setTextCursorW(cursor);
            } else if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                if (!cursor.atBlockEnd()) {
                    cursor.clearSelection();
                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
                    m_editor->setTextCursorW(cursor);
                }
            }

            setMode(VimMode::Insert);
        }

        break;
    }

    case Qt::Key_O:
    {
        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            // Insert a new block under/above current block and enter insert mode.
            bool insertAbove = modifiers == Qt::ShiftModifier;
            if (m_mode == VimMode::Normal) {
                QTextCursor cursor = m_editor->textCursorW();
                cursor.beginEditBlock();
                cursor.movePosition(insertAbove ? QTextCursor::StartOfBlock
                                                : QTextCursor::EndOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);

                cursor.insertBlock();

                if (insertAbove) {
                    cursor.movePosition(QTextCursor::PreviousBlock,
                                        QTextCursor::MoveAnchor,
                                        1);
                }

                bool textInserted = false;
                if (g_config->getAutoIndent()) {
                    textInserted = VEditUtils::indentBlockAsBlock(cursor, false);
                    bool listInserted = false;
                    if (g_config->getAutoList()) {
                        listInserted = VEditUtils::insertListMarkAsPreviousBlock(cursor);
                        textInserted = listInserted || textInserted;
                    }

                    if (!listInserted && g_config->getAutoQuote()) {
                        textInserted = VEditUtils::insertQuoteMarkAsPreviousBlock(cursor)
                                       || textInserted;
                    }
                }

                cursor.endEditBlock();
                m_editor->setTextCursorW(cursor);

                if (textInserted) {
                    autoIndentPos = cursor.position();
                }

                setMode(VimMode::Insert);
            }

            break;
        } else if (VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+O, jump to previous location.
            if (!m_tokens.isEmpty()
                || !checkMode(VimMode::Normal)) {
                break;
            }

            tryGetRepeatToken(m_keys, m_tokens);

            if (!m_keys.isEmpty()) {
                break;
            }

            addActionToken(Action::JumpPreviousLocation);
            processCommand(m_tokens);
            break;
        }

        break;
    }

    case Qt::Key_S:
    {
        tryGetRepeatToken(m_keys, m_tokens);
        if (!m_keys.isEmpty() || hasActionToken()) {
            break;
        }

        if (modifiers == Qt::NoModifier) {
            addActionToken(Action::Change);
            // Movement will be ignored in Visual mode.
            addMovementToken(Movement::Right);
            processCommand(m_tokens);
        } else if (modifiers == Qt::ShiftModifier) {
            // S, change current line.
            addActionToken(Action::Change);
            addRangeToken(Range::Line);
            processCommand(m_tokens);
        }

        break;
    }

    case Qt::Key_Dollar:
    {
        if (modifiers == Qt::ShiftModifier) {
            // $, move to end of line.
            tryGetRepeatToken(m_keys, m_tokens);

            if (m_keys.isEmpty()) {
                tryAddMoveAction();

                m_tokens.append(Token(Movement::EndOfLine));
                processCommand(m_tokens);
            }
        }

        break;
    }

    case Qt::Key_G:
    {
        Movement mm = Movement::Invalid;
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (m_keys.isEmpty()) {
                // First g, pend it.
                m_keys.append(keyInfo);
                goto accept;
            } else if (m_keys.size() == 1 && m_keys.at(0) == keyInfo) {
                // gg, go to a certain line or first line.
                if (!m_tokens.isEmpty() && m_tokens.last().isRepeat()) {
                    mm = Movement::LineJump;
                } else {
                    mm = Movement::StartOfDocument;
                }
            }
        } else if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (m_keys.isEmpty()) {
                // G, go to a certain line or the last line.
                if (!m_tokens.isEmpty() && m_tokens.last().isRepeat()) {
                    mm = Movement::LineJump;
                } else {
                    mm = Movement::EndOfDocument;
                }
            }
        }

        if (mm != Movement::Invalid) {
            tryAddMoveAction();

            m_tokens.append(Token(mm));
            processCommand(m_tokens);
        }

        break;
    }

    // Should be kept together with Qt::Key_PageUp.
    case Qt::Key_B:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+B, page up, fall through.
            modifiers = Qt::NoModifier;
        } else if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                if (modifiers == Qt::NoModifier && checkPendingKey(Key(Qt::Key_Z))) {
                    // zb, redraw to make a certain line the bottom of window.
                    addActionToken(Action::RedrawAtBottom);
                    processCommand(m_tokens);
                    break;
                }

                break;
            }

            // b, go to the start of previous or current word.
            Movement mm = Movement::WordBackward;
            if (modifiers == Qt::ShiftModifier) {
                // B, go to the start of previous or current WORD.
                mm = Movement::WORDBackward;
            }

            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            break;
        } else {
            break;
        }

        V_FALLTHROUGH;
    }

    case Qt::Key_PageUp:
    {
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            Movement mm = Movement::PageUp;
            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
        }

        break;
    }

    case Qt::Key_U:
    {
        tryGetRepeatToken(m_keys, m_tokens);
        bool toLower = modifiers == Qt::NoModifier;

        if (VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+U, HalfPageUp.
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            Movement mm = Movement::HalfPageUp;
            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
        } else if (m_keys.isEmpty() && !hasActionToken()) {
            if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                // u/U for tolower and toupper selected text.
                QTextCursor cursor = m_editor->textCursorW();
                cursor.beginEditBlock();
                // Different from Vim:
                // If there is no selection in Visual mode, we do nothing.
                if (m_mode == VimMode::VisualLine) {
                    int nrBlock = VEditUtils::selectedBlockCount(cursor);
                    message(tr("%1 %2 changed").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                             : tr("line")));
                }

                convertCaseOfSelectedText(cursor, toLower);
                cursor.endEditBlock();
                m_editor->setTextCursorW(cursor);

                setMode(VimMode::Normal);
                break;
            }

            // u, Undo.
            if (modifiers == Qt::NoModifier) {
                addActionToken(Action::Undo);
                processCommand(m_tokens);
            }
            break;
        } else {
            if (hasActionToken()) {
                // guu/gUU.
                if ((toLower && checkActionToken(Action::ToLower))
                    || (!toLower && checkActionToken(Action::ToUpper))) {
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                    break;
                } else {
                    // An invalid sequence.
                    break;
                }
            } else if (checkPendingKey(Key(Qt::Key_G))) {
                // gu/gU, ToLower/ToUpper action.
                if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                    QTextCursor cursor = m_editor->textCursorW();
                    cursor.beginEditBlock();
                    // Different from Vim:
                    // If there is no selection in Visual mode, we do nothing.
                    if (m_mode == VimMode::VisualLine) {
                        int nrBlock = VEditUtils::selectedBlockCount(cursor);
                        message(tr("%1 %2 changed").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                                 : tr("line")));
                    }

                    convertCaseOfSelectedText(cursor, toLower);
                    cursor.endEditBlock();
                    m_editor->setTextCursorW(cursor);
                    setMode(VimMode::Normal);
                    break;
                }

                addActionToken(toLower ? Action::ToLower : Action::ToUpper);
                m_keys.clear();
                goto accept;
            } else {
                // An invalid sequence.
                break;
            }
        }

        break;
    }

    // Ctrl+F is used for Find dialog, not used here.
    case Qt::Key_PageDown:
    {
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            Movement mm = Movement::PageDown;
            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
        }

        break;
    }

    case Qt::Key_D:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+D, HalfPageDown.
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            Movement mm = Movement::HalfPageDown;
            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
        } else if (modifiers == Qt::NoModifier) {
            // d, delete action.
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken()) {
                // This is another d, something like dd.
                if (checkActionToken(Action::Delete)) {
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                    break;
                } else {
                    // An invalid sequence.
                    break;
                }
            } else {
                // The first d, an Action.
                addActionToken(Action::Delete);
                if (checkMode(VimMode::Visual) || checkMode(VimMode::VisualLine)) {
                    // Movement will be ignored.
                    addMovementToken(Movement::Left);
                    processCommand(m_tokens);
                    setMode(VimMode::Normal);
                    break;
                }

                goto accept;
            }
        } else if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!hasActionToken()) {
                if (checkMode(VimMode::Normal)) {
                    // D, same as d$.
                    addActionToken(Action::Delete);
                    addMovementToken(Movement::EndOfLine);
                    processCommand(m_tokens);
                } else if (checkMode(VimMode::Visual) || checkMode(VimMode::VisualLine)) {
                    // D, same as dd.
                    addActionToken(Action::Delete);
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                    setMode(VimMode::Normal);
                }
            }

            break;
        }

        break;
    }

    case Qt::Key_BracketRight:
    {
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // BracketInner/BracketAround.
                Range range = Range::BracketInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::BracketAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            } else if (hasActionToken() || !checkMode(VimMode::Normal)) {
                // Invalid sequence.
                break;
            } else if (m_keys.isEmpty()) {
                // First ], pend it.
                m_keys.append(keyInfo);
                goto accept;
            } else if (checkPendingKey(keyInfo)) {
                // ]], goto next title, regardless of level.
                processTitleJump(m_tokens, true, 1);
            } else if (checkPendingKey(Key(Qt::Key_BracketLeft))) {
                // [], goto previous title at the same level.
                processTitleJump(m_tokens, false, 0);
            }

            break;
        }

        break;
    }

    // Should be kept together with Qt::Key_Escape.
    case Qt::Key_BracketLeft:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            clearSelectionAndEnterNormalMode();
        } else if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // BracketInner/BracketAround.
                Range range = Range::BracketInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::BracketAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            } else if (hasActionToken() || !checkMode(VimMode::Normal)) {
                // Invalid sequence.
                break;
            } else if (m_keys.isEmpty()) {
                // First [, pend it.
                m_keys.append(keyInfo);
                goto accept;
            } else if (checkPendingKey(keyInfo)) {
                // [[, goto previous title, regardless of level.
                processTitleJump(m_tokens, false, 1);
            } else if (checkPendingKey(Key(Qt::Key_BracketRight))) {
                // ][, goto next title at the same level.
                processTitleJump(m_tokens, true, 0);
            }
        }

        break;
    }

    case Qt::Key_Escape:
    {
        clearSelectionAndEnterNormalMode();
        break;
    }

    case Qt::Key_V:
    {
        if (modifiers == Qt::NoModifier) {
            if (checkMode(VimMode::Visual)) {
                setMode(VimMode::Normal, true);
            } else {
                // Toggle Visual Mode.
                setMode(VimMode::Visual);
                maintainSelectionInVisualMode();
            }
        } else if (modifiers == Qt::ShiftModifier) {
            // Visual Line Mode.
            clearSelection();
            VimMode mode = VimMode::VisualLine;
            if (m_mode == VimMode::VisualLine) {
                mode = VimMode::Normal;
            }

            setMode(mode);

            if (m_mode == VimMode::VisualLine) {
                QTextCursor cursor = m_editor->textCursorW();
                expandSelectionToWholeLines(cursor);
                m_editor->setTextCursorW(cursor);
            }
        } else if (VUtils::isControlModifierForVim(modifiers)) {
            if (g_config->getVimExemptionKeys().contains('v')) {
                // Let it be handled outside.
                resetState();
                goto exit;
            }
        }

        break;
    }

    case Qt::Key_AsciiCircum:
    {
        if (modifiers == Qt::ShiftModifier) {
            // ^, go to first non-space character of current line (block).
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            Movement mm = Movement::FirstCharacter;
            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
        }

        break;
    }

    case Qt::Key_W:
    {
        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            bool shift = modifiers == Qt::ShiftModifier;
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // WordInner/WORDInner/WordAournd/WORDAround.
                bool around = checkPendingKey(Key(Qt::Key_A));
                Range range = Range::Invalid;
                if (shift) {
                    if (around) {
                        range = Range::WORDAround;
                    } else {
                        range = Range::WORDInner;
                    }
                } else {
                    if (around) {
                        range = Range::WordAround;
                    } else {
                        range = Range::WordInner;
                    }
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            } else if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            // w, go to the start of next word.
            Movement mm = Movement::WordForward;
            if (shift) {
                // W, go to the start of next WORD.
                mm = Movement::WORDForward;
            }

            if (checkActionToken(Action::Change)) {
                // In Change action, cw equals to ce.
                if (shift) {
                    mm = Movement::ForwardEndOfWORD;
                } else {
                    mm = Movement::ForwardEndOfWord;
                }
            } else {
                tryAddMoveAction();
            }

            addMovementToken(mm);
            processCommand(m_tokens);
            break;
        }

        break;
    }

    case Qt::Key_E:
    {
        // e, E, ge, gE.
        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            Movement mm = Movement::Invalid;
            if (!m_keys.isEmpty()) {
                if (m_keys.size() == 1 && m_keys.at(0) == Key(Qt::Key_G)) {
                    // ge, gE.
                    if (modifiers == Qt::NoModifier) {
                        mm = Movement::BackwardEndOfWord;
                    } else {
                        mm = Movement::BackwardEndOfWORD;
                    }
                } else {
                    // Not a valid sequence.
                    break;
                }
            } else {
                // e, E.
                if (modifiers == Qt::NoModifier) {
                    mm = Movement::ForwardEndOfWord;
                } else {
                    mm = Movement::ForwardEndOfWORD;
                }
            }

            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
        }

        break;
    }

    case Qt::Key_QuoteDbl:
    {
        if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // DoubleQuoteInner/DoubleQuoteAround.
                Range range = Range::DoubleQuoteInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::DoubleQuoteAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            } else if (!m_keys.isEmpty() || hasActionToken()) {
                // Invalid sequence.
                break;
            }

            // ", specify a register.
            m_keys.append(keyInfo);
            goto accept;
        }

        break;
    }

    case Qt::Key_X:
    {
        if (modifiers == Qt::ShiftModifier || modifiers == Qt::NoModifier) {
            // x, or X to delete one char.
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty() || hasActionToken()) {
                break;
            }

            addActionToken(Action::Delete);
            if (modifiers == Qt::ShiftModifier) {
                if (checkMode(VimMode::Visual) || checkMode(VimMode::VisualLine)) {
                    // X, same as dd.
                    addRangeToken(Range::Line);
                } else {
                    // X, delete one char.
                    addMovementToken(Movement::Left);
                }
            } else {
                // x.
                // Movement will be ignored in Visual mode.
                addMovementToken(Movement::Right);
            }

            processCommand(m_tokens);
            setMode(VimMode::Normal);
            break;
        } else if (VUtils::isControlModifierForVim(modifiers)) {
            if (g_config->getVimExemptionKeys().contains('x')) {
                // Let it be handled outside.
                resetState();
                goto exit;
            }
        }

        break;
    }

    case Qt::Key_Y:
    {
        if (modifiers == Qt::NoModifier) {
            // y, copy action.
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken()) {
                // This is another y, something like yy.
                if (checkActionToken(Action::Copy) && checkMode(VimMode::Normal)) {
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                } else {
                    // An invalid sequence.
                    break;
                }
            } else {
                // The first y, an Action.
                if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                    copySelectedText(m_mode == VimMode::VisualLine);
                    setMode(VimMode::Normal);
                    break;
                }

                addActionToken(Action::Copy);
                goto accept;
            }
        } else if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!hasActionToken()) {
                // Y, same as yy.
                addActionToken(Action::Copy);
                addRangeToken(Range::Line);
                processCommand(m_tokens);
                setMode(VimMode::Normal);
            }

            break;
        }

        break;
    }

    case Qt::Key_P:
    {
        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            // p/P, paste/pastebefore action.
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken() || !m_keys.isEmpty()) {
                // An invalid sequence.
                break;
            }

            addActionToken(modifiers == Qt::NoModifier ? Action::Paste
                                                       : Action::PasteBefore);
            processCommand(m_tokens);
            break;
        }

        break;
    }

    case Qt::Key_C:
    {
        if (modifiers == Qt::NoModifier) {
            // c, change action.
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken()) {
                // This is another c, something like cc.
                if (checkActionToken(Action::Change)) {
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                }
            } else {
                // The first c, an action.
                addActionToken(Action::Change);

                if (checkMode(VimMode::VisualLine) || checkMode(VimMode::Visual)) {
                    // Movement will be ignored.
                    addMovementToken(Movement::Left);
                    processCommand(m_tokens);
                    break;
                }

                goto accept;
            }
        } else if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!hasActionToken() && m_mode == VimMode::Normal) {
                // C, same as c$.
                addActionToken(Action::Change);
                addMovementToken(Movement::EndOfLine);
                processCommand(m_tokens);
            }
        } else if (VUtils::isControlModifierForVim(modifiers)) {
            if (g_config->getVimExemptionKeys().contains('c')) {
                // Let it be handled outside.
                resetState();
                goto exit;
            } else {
                clearSelectionAndEnterNormalMode();
            }
        }

        break;
    }

    case Qt::Key_Less:
        unindent = true;
        // Fall through.
    case Qt::Key_Greater:
    {
        if (modifiers == Qt::ShiftModifier) {
            // >/<, Indent/Unindent.
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken()) {
                // This is another >/<, something like >>/<<.
                if ((!unindent && checkActionToken(Action::Indent))
                    || (unindent && checkActionToken(Action::UnIndent))) {
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                    break;
                } else if (checkPendingKey(Key(Qt::Key_I))
                           || checkPendingKey(Key(Qt::Key_A))) {
                    // AngleBracketInner/AngleBracketAround.
                    Range range = Range::AngleBracketInner;
                    if (checkPendingKey(Key(Qt::Key_A))) {
                        range = Range::AngleBracketAround;
                    }

                    addRangeToken(range);
                    processCommand(m_tokens);
                    break;
                }
            } else {
                // The first >/<, an Action.
                addActionToken(unindent ? Action::UnIndent : Action::Indent);

                if (checkMode(VimMode::Visual)
                    || checkMode(VimMode::VisualLine)) {
                    // Movement will be ignored.
                    addMovementToken(Movement::Left);
                    processCommand(m_tokens);
                    break;
                }

                goto accept;
            }
        }

        break;
    }

    case Qt::Key_Equal:
    {
        if (modifiers == Qt::NoModifier) {
            // =, AutoIndent.
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken()) {
                // ==.
                if (checkActionToken(Action::AutoIndent)) {
                    addRangeToken(Range::Line);
                    processCommand(m_tokens);
                    break;
                }
            } else {
                // The first =, an Action.
                addActionToken(Action::AutoIndent);

                if (checkMode(VimMode::Visual)
                    || checkMode(VimMode::VisualLine)) {
                    // Movement will be ignored.
                    addMovementToken(Movement::Left);
                    processCommand(m_tokens);
                    break;
                }

                goto accept;
            }
        }

        break;
    }

    case Qt::Key_F:
    {
        if (m_mode == VimMode::VisualLine) {
            break;
        }

        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            // f/F, find forward/backward within a block.
            tryGetRepeatToken(m_keys, m_tokens);

            if (m_keys.isEmpty()) {
                m_keys.append(keyInfo);
                goto accept;
            }

            break;
        }

        break;
    }

    case Qt::Key_T:
    {
        if (m_mode == VimMode::VisualLine) {
            break;
        }

        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            // t/T, find till forward/backward within a block.
            tryGetRepeatToken(m_keys, m_tokens);

            if (m_keys.isEmpty()) {
                m_keys.append(keyInfo);
                goto accept;
            } else if (modifiers == Qt::NoModifier && checkPendingKey(Key(Qt::Key_Z))) {
                // zt, redraw to make a certain line the top of window.
                addActionToken(Action::RedrawAtTop);
                processCommand(m_tokens);
                break;
            }

            break;
        }

        break;
    }

    case Qt::Key_Comma:
    {
        if (m_mode == VimMode::VisualLine) {
            break;
        }

        // ,, repeat last find target movement, but reversely.
        tryGetRepeatToken(m_keys, m_tokens);

        if (m_keys.isEmpty()) {
            repeatLastFindMovement(true);
            break;
        }

        break;
    }

    case Qt::Key_Semicolon:
    {
        if (m_mode == VimMode::VisualLine) {
            break;
        }

        // ;, repeat last find target movement.
        tryGetRepeatToken(m_keys, m_tokens);

        if (m_keys.isEmpty()) {
            repeatLastFindMovement(false);
            break;
        }

        break;
    }

    case Qt::Key_R:
    {
        if (m_mode == VimMode::VisualLine) {
            break;
        }

        if (VUtils::isControlModifierForVim(modifiers)) {
            // Redo.
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty() || hasActionToken()) {
                break;
            }

            addActionToken(Action::Redo);
            processCommand(m_tokens);
            break;
        } else if (modifiers == Qt::NoModifier) {
            // r, replace.
            tryGetRepeatToken(m_keys, m_tokens);
            if (m_keys.isEmpty() && !hasActionToken()) {
                m_keys.append(keyInfo);
                goto accept;
            }
        }

        break;
    }

    case Qt::Key_Z:
    {
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (m_keys.isEmpty() && !hasActionToken()) {
                // First z, pend it.
                m_keys.append(keyInfo);
                goto accept;
            } else if (checkPendingKey(keyInfo)) {
                // zz, redraw to make a certain line the center of the window.
                addActionToken(Action::RedrawAtCenter);
                processCommand(m_tokens);
                break;
            }

            break;
        }

        break;
    }

    case Qt::Key_Colon:
    {
        if (modifiers == Qt::ShiftModifier) {
            if (m_keys.isEmpty()
                && m_tokens.isEmpty()
                && checkMode(VimMode::Normal)) {
                emit commandLineTriggered(CommandLineType::Command);
            }
        }

        break;
    }

    case Qt::Key_Percent:
    {
        if (modifiers == Qt::ShiftModifier) {
            if (m_keys.isEmpty()) {
                // %, FindPair movement.
                tryAddMoveAction();
                addMovementToken(Movement::FindPair);
                processCommand(m_tokens);
                break;
            } else {
                tryGetRepeatToken(m_keys, m_tokens);
                if (m_keys.isEmpty() && hasRepeatToken()) {
                    // xx%, jump to a certain line (percentage of the documents).
                    // Change the repeat from percentage to line number.
                    Token *token = getRepeatToken();
                    int bn = percentageToBlockNumber(m_editor->documentW(), token->m_repeat);
                    if (bn == -1) {
                        break;
                    } else {
                        // Repeat of LineJump is based on 1.
                        token->m_repeat = bn + 1;
                    }

                    tryAddMoveAction();
                    addMovementToken(Movement::LineJump);
                    processCommand(m_tokens);
                    break;
                }
            }

            break;
        }

        break;
    }

    case Qt::Key_M:
    {
        if (modifiers == Qt::NoModifier) {
            if (m_keys.isEmpty() && m_tokens.isEmpty()) {
                // m, creating a mark.
                // We can create marks in Visual mode, too.
                m_keys.append(keyInfo);
                goto accept;
            }
        }

        break;
    }

    case Qt::Key_Apostrophe:
    {
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // QuoteInner/QuoteAround.
                Range range = Range::QuoteInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::QuoteAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            }

            // ', jump to the start of line of a mark.
            // Repeat is useless in this case.
            if (m_keys.isEmpty()) {
                m_keys.append(keyInfo);
                goto accept;
            }
        }

        break;
    }

    case Qt::Key_QuoteLeft:
    {
        if (modifiers == Qt::NoModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // BackQuoteInner/BackQuoteAround.
                Range range = Range::BackQuoteInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::BackQuoteAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            }

            // `, jump to a mark.
            // Repeat is useless in this case.
            if (m_keys.isEmpty()) {
                m_keys.append(keyInfo);
                goto accept;
            }
        }

        break;
    }

    case Qt::Key_ParenLeft:
        // Fall through.
    case Qt::Key_ParenRight:
    {
        if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // ParenthesisInner/ParenthesisAround.
                Range range = Range::ParenthesisInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::ParenthesisAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            }
        }

        break;
    }

    case Qt::Key_BraceLeft:
    {
        if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // BraceInner/BraceAround.
                Range range = Range::BraceInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::BraceAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            } else if (m_keys.isEmpty()) {
                // {, ParagraphUp movement.
                tryAddMoveAction();
                addMovementToken(Movement::ParagraphUp);
                processCommand(m_tokens);
            } else if (!hasActionToken()
                       && checkPendingKey(Key(Qt::Key_BracketLeft))) {
                // [{, goto previous title at one higher level.
                if (checkMode(VimMode::Normal)) {
                    processTitleJump(m_tokens, false, -1);
                }
            }

            break;
        }

        break;
    }

    case Qt::Key_BraceRight:
    {
        if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (checkPendingKey(Key(Qt::Key_I))
                || checkPendingKey(Key(Qt::Key_A))) {
                // BraceInner/BraceAround.
                Range range = Range::BraceInner;
                if (checkPendingKey(Key(Qt::Key_A))) {
                    range = Range::BraceAround;
                }

                addRangeToken(range);
                processCommand(m_tokens);
                break;
            } else if (m_keys.isEmpty()) {
                // }, ParagraphDown movement.
                tryAddMoveAction();
                addMovementToken(Movement::ParagraphDown);
                processCommand(m_tokens);
            } else if (!hasActionToken()
                       && checkPendingKey(Key(Qt::Key_BracketRight))) {
                // ]}, goto next title at one higher level.
                if (checkMode(VimMode::Normal)) {
                    processTitleJump(m_tokens, true, -1);
                }
            }

            break;
        }

        break;
    }

    case Qt::Key_AsciiTilde:
    {
        if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (hasActionToken() || !m_keys.isEmpty()) {
                break;
            }

            // Reverse the case.
            addActionToken(Action::ReverseCase);
            processCommand(m_tokens);

            break;
        }

        break;
    }

    case Qt::Key_Slash:
    {
        if (modifiers == Qt::NoModifier) {
            if (m_tokens.isEmpty()
                && m_keys.isEmpty()
                && checkMode(VimMode::Normal)) {
                emit commandLineTriggered(CommandLineType::SearchForward);
            }
        }

        break;
    }

    case Qt::Key_Question:
    {
        if (modifiers == Qt::ShiftModifier) {
            if (m_tokens.isEmpty()
                && m_keys.isEmpty()
                && checkMode(VimMode::Normal)) {
                emit commandLineTriggered(CommandLineType::SearchBackward);
            }
        }

        break;
    }

    case Qt::Key_N:
    {
        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            // n, FindNext/FindPrevious movement.
            tryGetRepeatToken(m_keys, m_tokens);

            if (!m_keys.isEmpty()) {
                break;
            }

            Movement mm = Movement::FindNext;
            if (modifiers == Qt::ShiftModifier) {
                mm = Movement::FindPrevious;
            }

            tryAddMoveAction();
            addMovementToken(mm);
            processCommand(m_tokens);
        }

        break;
    }

    case Qt::Key_Asterisk:
    {
        if (modifiers == Qt::ShiftModifier) {
            // *, FindNextWordUnderCursor movement.
            tryGetRepeatToken(m_keys, m_tokens);

            if (!m_keys.isEmpty()) {
                break;
            }

            tryAddMoveAction();
            addMovementToken(Movement::FindNextWordUnderCursor);
            processCommand(m_tokens);
        }

        break;
    }

    case Qt::Key_NumberSign:
    {
        if (modifiers == Qt::ShiftModifier) {
            // #, FindPreviousWordUnderCursor movement.
            tryGetRepeatToken(m_keys, m_tokens);

            if (!m_keys.isEmpty()) {
                break;
            }

            tryAddMoveAction();
            addMovementToken(Movement::FindPreviousWordUnderCursor);
            processCommand(m_tokens);
        }

        break;
    }

    default:
        break;
    }

clear_accept:
    resetState();

    amendCursorPosition();

    if (m_insertModeAfterCommand
        && !checkMode(VimMode::Visual)
        && !checkMode(VimMode::VisualLine)) {
        m_insertModeAfterCommand = false;
        setMode(VimMode::Insert);
    }

    m_editor->makeBlockVisible(m_editor->textCursorW().block());

accept:
    ret = true;

    // Only alter the autoIndentPos when the key is handled by Vim.
    if (p_autoIndentPos) {
        *p_autoIndentPos = autoIndentPos;
    }

exit:
    m_resetPositionInBlock = resetPositionInBlock;
    emit vimStatusUpdated(this);
    return ret;
}

void VVim::resetState()
{
    m_keys.clear();
    m_tokens.clear();
    m_pendingKeys.clear();
    setCurrentRegisterName(c_unnamedRegister);
    m_resetPositionInBlock = true;
    m_registerPending = false;
}

VimMode VVim::getMode() const
{
    return m_mode;
}

void VVim::setMode(VimMode p_mode, bool p_clearSelection, int p_position)
{
    if (m_mode != p_mode) {
        QTextCursor cursor = m_editor->textCursorW();
        int position = p_position;
        if (position == -1) {
            if (m_mode == VimMode::Visual
                && p_mode == VimMode::Normal
                && cursor.position() > cursor.anchor()) {
                position = cursor.position() - 1;
            } else if (m_mode == VimMode::Insert
                       && p_mode == VimMode::Normal
                       && !cursor.atBlockStart()) {
                position = cursor.position() - 1;
                if (position < 0) {
                    position = 0;
                }
            }
        }

        if (p_clearSelection) {
            clearSelection();
        }

        if (p_mode == VimMode::Insert) {
            m_editor->setInputMethodEnabled(true);
        } else if (g_config->getEnableSmartImInVimMode()) {
            m_editor->setInputMethodEnabled(false);
        }

        m_mode = p_mode;
        resetState();

        VMdEditor *mdEditor = dynamic_cast<VMdEditor *>(m_editor);
        switch (m_mode) {
        case VimMode::Insert:
            setCursorBlockMode(m_editor, CursorBlock::None);
            if (mdEditor) {
                mdEditor->setHighlightCursorLineBlockEnabled(false);
            }

            break;

        case VimMode::Visual:
            m_positionBeforeVisualMode = cursor.anchor();
            V_FALLTHROUGH;

        default:
            setCursorBlockMode(m_editor, CursorBlock::RightSide);
            if (mdEditor && g_config->getHighlightCursorLine()) {
                QString color;
                if (m_mode == VimMode::Normal) {
                    color = g_config->getEditorVimNormalBg();
                } else {
                    color = g_config->getEditorVimVisualBg();
                }

                mdEditor->setCursorLineBlockBg(color);
                mdEditor->setHighlightCursorLineBlockEnabled(true);
            }

            break;
        }

        if (position != -1) {
            cursor.setPosition(position);
            m_editor->setTextCursorW(cursor);
        }

        amendCursorPosition();

        emit modeChanged(m_mode);
        emit vimStatusUpdated(this);
    }
}

void VVim::processCommand(QList<Token> &p_tokens)
{
    if (p_tokens.isEmpty()) {
        return;
    }

    V_ASSERT(p_tokens.at(0).isAction());

    Token act = p_tokens.takeFirst();
    switch (act.m_action) {
    case Action::Move:
        processMoveAction(p_tokens);
        break;

    case Action::Delete:
        processDeleteAction(p_tokens);
        break;

    case Action::Copy:
        processCopyAction(p_tokens);
        break;

    case Action::Paste:
        processPasteAction(p_tokens, false);
        break;

    case Action::PasteBefore:
        processPasteAction(p_tokens, true);
        break;

    case Action::Change:
        processChangeAction(p_tokens);
        break;

    case Action::Indent:
        processIndentAction(p_tokens, IndentType::Indent);
        break;

    case Action::UnIndent:
        processIndentAction(p_tokens, IndentType::UnIndent);
        break;

    case Action::AutoIndent:
        processIndentAction(p_tokens, IndentType::AutoIndent);
        break;

    case Action::ToLower:
        processToLowerAction(p_tokens, true);
        break;

    case Action::ToUpper:
        processToLowerAction(p_tokens, false);
        break;

    case Action::Undo:
        processUndoAction(p_tokens);
        break;

    case Action::Redo:
        processRedoAction(p_tokens);
        break;

    case Action::RedrawAtTop:
        processRedrawLineAction(p_tokens, 0);
        break;

    case Action::RedrawAtCenter:
        processRedrawLineAction(p_tokens, 1);
        break;

    case Action::RedrawAtBottom:
        processRedrawLineAction(p_tokens, 2);
        break;

    case Action::JumpPreviousLocation:
        processJumpLocationAction(p_tokens, false);
        break;

    case Action::JumpNextLocation:
        processJumpLocationAction(p_tokens, true);
        break;

    case Action::Replace:
        processReplaceAction(p_tokens);
        break;

    case Action::ReverseCase:
        processReverseCaseAction(p_tokens);
        break;

    case Action::Join:
        processJoinAction(p_tokens, true);
        break;

    case Action::JoinNoModification:
        processJoinAction(p_tokens, false);
        break;

    default:
        p_tokens.clear();
        break;
    }

    Q_ASSERT(p_tokens.isEmpty());
}

int VVim::numberFromKeySequence(const QList<Key> &p_keys)
{
    int num = 0;

    for (auto const & key : p_keys) {
        if (key.isDigit()) {
            num = num * 10 + key.toDigit();
        } else {
            return -1;
        }
    }

    return num == 0 ? -1 : num;
}

bool VVim::tryGetRepeatToken(QList<Key> &p_keys, QList<Token> &p_tokens)
{
    if (!p_keys.isEmpty()) {
        int repeat = numberFromKeySequence(p_keys);
        if (repeat != -1) {
            p_tokens.append(Token(repeat));
            p_keys.clear();

            return true;
        }
    }

    return false;
}

void VVim::processMoveAction(QList<Token> &p_tokens)
{
    // Only moving left/right could change this.
    static int positionInBlock = 0;

    Token to = p_tokens.takeFirst();
    V_ASSERT(to.isRepeat() || to.isMovement());
    Token mvToken;
    int repeat = -1;
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        mvToken = p_tokens.takeFirst();
    } else {
        mvToken = to;
    }

    if (!mvToken.isMovement() || !p_tokens.isEmpty()) {
        p_tokens.clear();
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    if (m_resetPositionInBlock) {
        positionInBlock = cursor.positionInBlock();
    }

    QTextCursor::MoveMode moveMode = (m_mode == VimMode::Visual
                                      || m_mode == VimMode::VisualLine)
                                     ? QTextCursor::KeepAnchor
                                     : QTextCursor::MoveAnchor;
    bool hasMoved = processMovement(cursor, moveMode, mvToken, repeat);

    if (hasMoved) {
        // Maintain positionInBlock.
        switch (mvToken.m_movement) {
        case Movement::Left:
        case Movement::Right:
            positionInBlock = cursor.positionInBlock();
            break;

        case Movement::Up:
        case Movement::Down:
        case Movement::PageUp:
        case Movement::PageDown:
        case Movement::HalfPageUp:
        case Movement::HalfPageDown:
            setCursorPositionInBlock(cursor, positionInBlock, moveMode);
            break;

        default:
            break;
        }

        if (checkMode(VimMode::VisualLine)) {
            expandSelectionToWholeLines(cursor);
        } else if (checkMode(VimMode::Visual)) {
            maintainSelectionInVisualMode(&cursor);
        }

        m_editor->setTextCursorW(cursor);
    }
}

bool VVim::processMovement(QTextCursor &p_cursor,
                           QTextCursor::MoveMode p_moveMode,
                           const Token &p_token,
                           int p_repeat)
{
    V_ASSERT(p_token.isMovement());

    bool hasMoved = false;
    bool inclusive = true;
    bool forward = true;
    QTextDocument *doc = p_cursor.document();

    switch (p_token.m_movement) {
    case Movement::Left:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int pib = p_cursor.positionInBlock();
        p_repeat = qMin(pib, p_repeat);

        if (p_repeat > 0) {
            p_cursor.movePosition(QTextCursor::Left, p_moveMode, p_repeat);
            hasMoved = true;
        }

        break;
    }

    case Movement::Right:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        if (checkMode(VimMode::Visual)) {
            int pos = p_cursor.position();
            if (pos == p_cursor.anchor() - 1 && pos == m_positionBeforeVisualMode) {
                ++p_repeat;
            }
        }

        int pib = p_cursor.positionInBlock();
        int length = p_cursor.block().length();
        if (length - pib <= p_repeat) {
            p_repeat = length - pib - 1;
        }

        if (p_repeat > 0) {
            p_cursor.movePosition(QTextCursor::Right, p_moveMode, p_repeat);
            hasMoved = true;
        }

        break;
    }

    case Movement::Up:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        p_repeat = qMin(p_cursor.block().blockNumber(), p_repeat);

        if (p_repeat > 0) {
            p_cursor.movePosition(QTextCursor::PreviousBlock, p_moveMode, p_repeat);
            hasMoved = true;
        }

        break;
    }

    case Movement::Down:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int blockCount = doc->blockCount();
        p_repeat = qMin(blockCount - 1 - p_cursor.block().blockNumber(), p_repeat);

        if (p_repeat > 0) {
            p_cursor.movePosition(QTextCursor::NextBlock, p_moveMode, p_repeat);
            hasMoved = true;
        }

        break;
    }

    case Movement::VisualUp:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        p_cursor.movePosition(QTextCursor::Up, p_moveMode, p_repeat);
        hasMoved = true;
        break;
    }

    case Movement::VisualDown:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        p_cursor.movePosition(QTextCursor::Down, p_moveMode, p_repeat);
        hasMoved = true;
        break;
    }

    case Movement::PageUp:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int blockStep = blockCountOfPageStep() * p_repeat;
        int block = p_cursor.block().blockNumber();
        block = qMax(0, block - blockStep);
        p_cursor.setPosition(doc->findBlockByNumber(block).position(), p_moveMode);
        hasMoved = true;

        break;
    }

    case Movement::PageDown:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int blockStep = blockCountOfPageStep() * p_repeat;
        int block = p_cursor.block().blockNumber();
        block = qMin(block + blockStep, doc->blockCount() - 1);
        p_cursor.setPosition(doc->findBlockByNumber(block).position(), p_moveMode);
        hasMoved = true;

        break;
    }

    case Movement::HalfPageUp:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int blockStep = blockCountOfPageStep();
        int halfBlockStep = qMax(blockStep / 2, 1);
        blockStep = p_repeat * halfBlockStep;
        int block = p_cursor.block().blockNumber();
        block = qMax(0, block - blockStep);
        p_cursor.setPosition(doc->findBlockByNumber(block).position(), p_moveMode);
        hasMoved = true;

        break;
    }

    case Movement::HalfPageDown:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int blockStep = blockCountOfPageStep();
        int halfBlockStep = qMax(blockStep / 2, 1);
        blockStep = p_repeat * halfBlockStep;
        int block = p_cursor.block().blockNumber();
        block = qMin(block + blockStep, doc->blockCount() - 1);
        p_cursor.setPosition(doc->findBlockByNumber(block).position(), p_moveMode);
        hasMoved = true;

        break;
    }

    case Movement::StartOfLine:
    {
        Q_ASSERT(p_repeat == -1);

        // Start of the Line (block).
        if (!p_cursor.atBlockStart()) {
            p_cursor.movePosition(QTextCursor::StartOfBlock, p_moveMode, 1);
            hasMoved = true;
        }

        break;
    }

    case Movement::StartOfVisualLine:
    {
        // Start of the visual line.
        if (!p_cursor.atBlockStart()) {
            p_cursor.movePosition(QTextCursor::StartOfLine, p_moveMode, 1);
            hasMoved = true;
        }

        break;
    }

    case Movement::EndOfLine:
    {
        // End of line (block).
        if (p_repeat == -1) {
            p_repeat = 1;
        } else if (p_repeat > 1) {
            // Move down (p_repeat-1) blocks.
            p_cursor.movePosition(QTextCursor::NextBlock, p_moveMode, p_repeat - 1);
        }

        // Move to the end of block.
        p_cursor.movePosition(QTextCursor::EndOfBlock, p_moveMode, 1);
        hasMoved = true;
        break;
    }

    case Movement::FirstCharacter:
    {
        // p_repeat is not considered in this command.
        // If all the block is space, just move to the end of block; otherwise,
        // move to the first non-space character.
        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);

        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::LineJump:
    {
        // Jump to the first non-space character of @p_repeat line (block).
        V_ASSERT(p_repeat > 0);

        // Record current location.
        m_locations.addLocation(p_cursor);

        // @p_repeat starts from 1 while block number starts from 0.
        QTextBlock block = doc->findBlockByNumber(p_repeat - 1);
        if (block.isValid()) {
            p_cursor.setPosition(block.position(), p_moveMode);
        } else {
            // Go beyond the document.
            p_cursor.movePosition(QTextCursor::End, p_moveMode, 1);
        }

        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::StartOfDocument:
    {
        // Jump to the first non-space character of the start of the document.
        V_ASSERT(p_repeat == -1);

        // Record current location.
        m_locations.addLocation(p_cursor);

        p_cursor.movePosition(QTextCursor::Start, p_moveMode, 1);
        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::EndOfDocument:
    {
        // Jump to the first non-space character of the end of the document.
        V_ASSERT(p_repeat == -1);

        // Record current location.
        m_locations.addLocation(p_cursor);

        p_cursor.movePosition(QTextCursor::End, p_moveMode, 1);
        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::WordForward:
    {
        // Go to the start of next word.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        while (p_repeat) {
            if (p_cursor.atEnd()) {
                break;
            }

            p_cursor.movePosition(QTextCursor::NextWord, p_moveMode);
            if (p_cursor.atBlockEnd()) {
                // dw/yw/cw will stop at the end of the line.
                if (p_repeat == 1
                    && checkMode(VimMode::Normal)
                    && p_moveMode == QTextCursor::KeepAnchor) {
                    --p_repeat;
                }

                continue;
            } else if (doc->characterAt(p_cursor.position()).isSpace()
                || VEditUtils::isSpaceBlock(p_cursor.block())) {
                continue;
            }

            --p_repeat;
        }

        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::WORDForward:
    {
        // Go to the start of next WORD.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        while (p_repeat) {
            if (p_cursor.atEnd()) {
                break;
            }

            int start, end;
            // [start, end] is current WORD.
            VEditUtils::findCurrentWORD(p_cursor, start, end);
            // Move cursor to end of current WORD.
            p_cursor.setPosition(end, p_moveMode);

            if (p_repeat == 1
                && checkMode(VimMode::Normal)
                && p_moveMode == QTextCursor::KeepAnchor) {
                // dW/yW/cW will stop at the end of the line.
                moveCursorAcrossSpaces(p_cursor, p_moveMode, true, true);
                if (p_cursor.atBlockEnd()) {
                    --p_repeat;
                    continue;
                }
            }

            // Skip spaces.
            moveCursorAcrossSpaces(p_cursor, p_moveMode, true);
            if (p_cursor.atBlockEnd()) {
                continue;
            }

            --p_repeat;
        }

        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::ForwardEndOfWord:
    {
        // Go to the end of current word or next word.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int pos = p_cursor.position();
        bool leftSideBefore = useLeftSideOfCursor(p_cursor);
        // First move to the end of current word.
        p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode, 1);
        if (p_cursor.position() > pos) {
            if (p_cursor.position() > pos + 1 || (leftSideBefore && useLeftSideOfCursor(p_cursor))) {
                // We did move.
                p_repeat -= 1;
            }
        }

        while (p_repeat) {
            if (p_cursor.atEnd()) {
                break;
            }

            pos = p_cursor.position();
            p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode);
            if (p_cursor.position() == pos) {
                // Need to move to the start of next word.
                p_cursor.movePosition(QTextCursor::NextWord, p_moveMode);
                if (p_cursor.atBlockEnd()) {
                    continue;
                }

                p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode);
                if (p_cursor.atBlockStart()) {
                    continue;
                }
            }

            --p_repeat;
        }

        // Move one character back.
        if (!p_cursor.atBlockStart()) {
            if (p_moveMode == QTextCursor::MoveAnchor
                || (checkMode(VimMode::Visual) && !useLeftSideOfCursor(p_cursor))) {
                p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
            }
        }

        hasMoved = true;
        break;
    }

    case Movement::ForwardEndOfWORD:
    {
        // Go to the end of current WORD or next WORD.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int pos = p_cursor.position();
        bool leftSideBefore = useLeftSideOfCursor(p_cursor);
        while (p_repeat) {
            if (p_cursor.atEnd()) {
                break;
            }

            // Skip spaces.
            moveCursorAcrossSpaces(p_cursor, p_moveMode, true);
            if (p_cursor.atBlockEnd()) {
                continue;
            }

            int start, end;
            // [start, end] is current WORD.
            VEditUtils::findCurrentWORD(p_cursor, start, end);

            // Move cursor to the end of current WORD.
            p_cursor.setPosition(end, p_moveMode);

            if (p_cursor.position() > pos + 1
                || (leftSideBefore && useLeftSideOfCursor(p_cursor))) {
                --p_repeat;
            }
        }

        // Move one character back.
        if (!p_cursor.atBlockStart()) {
            if (p_moveMode == QTextCursor::MoveAnchor
                || (checkMode(VimMode::Visual) && !useLeftSideOfCursor(p_cursor))) {
                p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
            }
        }

        hasMoved = true;
        break;
    }

    case Movement::WordBackward:
    {
        // Go to the start of previous word or current word.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int pos = p_cursor.position();
        // first move to the start of current word.
        p_cursor.movePosition(QTextCursor::StartOfWord, p_moveMode);
        if (p_cursor.position() == pos && useLeftSideOfCursor(p_cursor)) {
            // Cursor did not move and now is at the start of a word.
            // Actually we are using the left side character so we need to move
            // to previous word.
            p_cursor.movePosition(QTextCursor::PreviousWord, p_moveMode);
        }

        if (p_cursor.position() < pos) {
            if (p_cursor.position() < pos - 1 || !useLeftSideOfCursor(p_cursor)) {
                // We did move.
                p_repeat -= 1;
            }
        }

        while (p_repeat) {
            if (p_cursor.atStart()) {
                break;
            }

            pos = p_cursor.position();
            p_cursor.movePosition(QTextCursor::StartOfWord, p_moveMode);
            if (p_cursor.position() == pos) {
                // Need to move to the start of previous word.
                p_cursor.movePosition(QTextCursor::PreviousWord, p_moveMode);
                if (p_cursor.atBlockEnd()) {
                    continue;
                }

                if (p_cursor.atBlockStart() && doc->characterAt(p_cursor.position()).isSpace()) {
                    continue;
                }
            }

            --p_repeat;
        }

        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::WORDBackward:
    {
        // Go to the start of previous WORD or current WORD.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        int pos = p_cursor.position();
        while (p_repeat) {
            if (p_cursor.atStart()) {
                break;
            }

            // Skip Spaces.
            moveCursorAcrossSpaces(p_cursor, p_moveMode, false);
            if (p_cursor.atBlockStart()) {
                continue;
            }

            p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);

            int start, end;
            // [start, end] is current WORD.
            VEditUtils::findCurrentWORD(p_cursor, start, end);

            // Move cursor to the start of current WORD.
            p_cursor.setPosition(start, p_moveMode);

            if (p_cursor.position() < pos - 1 || !useLeftSideOfCursor(p_cursor)) {
                --p_repeat;
            }
        }

        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            // Move one character forward.
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::BackwardEndOfWord:
    {
        // Go to the end of previous word.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        if (useLeftSideOfCursor(p_cursor)) {
            // Move one previous char.
            p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
        }

        int pos = p_cursor.position();
        // Move across spaces backward.
        moveCursorAcrossSpaces(p_cursor, p_moveMode, false);
        int start, end;
        VEditUtils::findCurrentWord(p_cursor, start, end);
        if (pos != p_cursor.position() || start == end || start == pos) {
            // We are alreay at the end of previous word.
            --p_repeat;

            // Move it to the start of current word.
            p_cursor.movePosition(QTextCursor::PreviousWord, p_moveMode);
        } else {
            p_cursor.movePosition(QTextCursor::StartOfWord, p_moveMode);
        }

        while (p_repeat) {
            if (p_cursor.atStart()) {
                break;
            }

            p_cursor.movePosition(QTextCursor::PreviousWord, p_moveMode);
            if (p_cursor.atBlockEnd()) {
                continue;
            }

            if (p_cursor.atBlockStart() && doc->characterAt(p_cursor.position()).isSpace()) {
                continue;
            }

            --p_repeat;
        }

        // Move it to the end.
        p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode);

        // Move one character back.
        if (!p_cursor.atBlockStart()) {
            if (p_moveMode == QTextCursor::MoveAnchor
                || (checkMode(VimMode::Visual) && !useLeftSideOfCursor(p_cursor))
                || p_cursor.position() <= p_cursor.anchor()) {
                p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
            }
        }

        hasMoved = true;
        break;
    }

    case Movement::BackwardEndOfWORD:
    {
        // Go to the end of previous WORD.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        if (useLeftSideOfCursor(p_cursor)) {
            // Move one previous char.
            p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
        }

        int pos = p_cursor.position();
        // Move across spaces backward.
        moveCursorAcrossSpaces(p_cursor, p_moveMode, false);
        int start, end;
        VEditUtils::findCurrentWORD(p_cursor, start, end);
        if (pos != p_cursor.position() || start == end) {
            // We are alreay at the end of previous WORD.
            --p_repeat;

            // Move it to the start of current WORD.
            p_cursor.setPosition(start, p_moveMode);
        } else {
            // Move it to the start of current WORD.
            p_cursor.setPosition(start, p_moveMode);
        }

        while (p_repeat) {
            if (p_cursor.atStart()) {
                break;
            }

            moveCursorAcrossSpaces(p_cursor, p_moveMode, false);
            if (p_cursor.atBlockStart()) {
                continue;
            }

            if (p_cursor.atBlockStart() && doc->characterAt(p_cursor.position()).isSpace()) {
                continue;
            }

            VEditUtils::findCurrentWORD(p_cursor, start, end);
            p_cursor.setPosition(start, p_moveMode);

            --p_repeat;
        }

        // Move it to the end.
        VEditUtils::findCurrentWORD(p_cursor, start, end);
        p_cursor.setPosition(end, p_moveMode);

        // Move one character back.
        if (!p_cursor.atBlockStart()) {
            if (p_moveMode == QTextCursor::MoveAnchor
                || (checkMode(VimMode::Visual) && !useLeftSideOfCursor(p_cursor))
                || p_cursor.position() <= p_cursor.anchor()) {
                p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
            }
        }

        hasMoved = true;
        break;
    }

    case Movement::TillBackward:
        forward = false;
        // Fall through.
    case Movement::TillForward:
        inclusive = false;
        goto handle_target;

    case Movement::FindBackward:
        forward = false;
        // Fall through.
    case Movement::FindForward:
    {
handle_target:
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        const Key &key = p_token.m_key;
        QChar target = keyToChar(key.m_key, key.m_modifiers);
        if (!target.isNull()) {
            hasMoved = VEditUtils::findTargetWithinBlock(p_cursor,
                                                         p_moveMode,
                                                         target,
                                                         forward,
                                                         inclusive,
                                                         useLeftSideOfCursor(p_cursor),
                                                         p_repeat);
        }

        break;
    }

    case Movement::MarkJump:
        // Fall through.
    case Movement::MarkJumpLine:
    {
        // repeat is useless here.
        const Key &key = p_token.m_key;
        QChar target = keyToChar(key.m_key, key.m_modifiers);
        Location loc = m_marks.getMarkLocation(target);
        if (loc.isValid()) {
            if (loc.m_blockNumber >= doc->blockCount()) {
                // Invalid block number.
                message(tr("Mark not set"));
                m_marks.clearMark(target);
                break;
            }

            // Different from Vim:
            // We just use the block number for mark, so if we delete the line
            // where the mark locates, we could not detect if it is set or not.
            QTextBlock block = doc->findBlockByNumber(loc.m_blockNumber);
            p_cursor.setPosition(block.position(), p_moveMode);
            if (p_token.m_movement == Movement::MarkJump) {
                setCursorPositionInBlock(p_cursor, loc.m_positionInBlock, p_moveMode);
            } else {
                // Jump to the first non-space character.
                VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
            }

            if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
                // Move one character forward.
                p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
            }

            hasMoved = true;
        }

        break;
    }

    case Movement::FindPair:
    {
        Q_ASSERT(p_repeat == -1);
        int anchor = p_cursor.anchor();
        int position = p_cursor.position();
        QList<QPair<QChar, QChar>> pairs;
        pairs.append(QPair<QChar, QChar>('(', ')'));
        pairs.append(QPair<QChar, QChar>('[', ']'));
        pairs.append(QPair<QChar, QChar>('{', '}'));

        // Find forward for a pair (), [], and {}.
        QList<QChar> targets;
        for (auto const & pair : pairs) {
            targets.append(pair.first);
            targets.append(pair.second);
        }

        // First check if current char hits the targets.
        bool useLeftSideBefore = useLeftSideOfCursor(p_cursor);
        QChar ch = doc->characterAt(useLeftSideBefore ? position - 1
                                                      : position);
        int idx = targets.indexOf(ch);
        if (idx == -1) {
            idx = VEditUtils::findTargetsWithinBlock(p_cursor,
                                                     targets,
                                                     true,
                                                     useLeftSideOfCursor(p_cursor),
                                                     true);
        } else if (useLeftSideBefore) {
            // Move one character back to let p_cursor position at the pair.
            p_cursor.movePosition(QTextCursor::PreviousCharacter, p_moveMode);
        }

        if (idx == -1) {
            break;
        }

        idx /= 2;
        int pairPosition = p_cursor.position();
        bool ret = VEditUtils::selectPairTargetAround(p_cursor,
                                                      pairs.at(idx).first,
                                                      pairs.at(idx).second,
                                                      true,
                                                      true,
                                                      1);

        if (ret) {
            // Found matched pair.
            int first = p_cursor.position();
            int second = p_cursor.anchor();
            if (first > second) {
                int tmp = first;
                first = second;
                second = tmp;
            }

            if (!(checkMode(VimMode::Normal) && p_moveMode == QTextCursor::KeepAnchor)) {
                --second;
            }

            int target = first;
            if (first == pairPosition) {
                target = second;
            }

            if (anchor > target
                && !p_cursor.atEnd()
                && !useLeftSideOfCursor(p_cursor)) {
                ++anchor;
            }

            p_cursor.setPosition(anchor);
            p_cursor.setPosition(target, p_moveMode);

            if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
                // Move one character forward.
                p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
            }

            hasMoved = true;
            break;
        } else {
            // Restore the cursor position.
            p_cursor.setPosition(anchor);
            if (anchor != position) {
                p_cursor.setPosition(position, QTextCursor::KeepAnchor);
            }
        }

        break;
    }

    case Movement::FindPrevious:
        forward = false;
        // Fall through.
    case Movement::FindNext:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        if (m_searchHistory.isEmpty()) {
            break;
        }

        // Record current location.
        m_locations.addLocation(p_cursor);

        bool useLeftSideBefore = useLeftSideOfCursor(p_cursor);
        const SearchItem &item = m_searchHistory.lastItem();
        while (--p_repeat >= 0) {
            bool found = m_editor->findText(item.m_text,
                                            item.m_options,
                                            forward ? item.m_forward : !item.m_forward,
                                            &p_cursor,
                                            p_moveMode,
                                            useLeftSideBefore);
            if (found) {
                hasMoved = true;
                useLeftSideBefore = false;
            } else {
                break;
            }
        }

        if (hasMoved) {
            if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
                p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
            }
        }

        break;
    }

    case Movement::FindPreviousWordUnderCursor:
        forward = false;
        // Fall through.
    case Movement::FindNextWordUnderCursor:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        // Get current word under cursor.
        // Different from Vim:
        // We do not recognize a word as strict as Vim.
        int start, end;
        VEditUtils::findCurrentWord(p_cursor, start, end);
        if (start == end) {
            // Spaces, find next word.
            QTextCursor cursor = p_cursor;
            while (true) {
                moveCursorAcrossSpaces(cursor, p_moveMode, true);
                if (cursor.atEnd()) {
                    break;
                }

                if (!doc->characterAt(cursor.position()).isSpace()) {
                    VEditUtils::findCurrentWord(cursor, start, end);
                    Q_ASSERT(start != end);
                    break;
                }
            }

            if (start == end) {
                break;
            }
        }

        QTextCursor cursor = p_cursor;
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        QString text = cursor.selectedText();
        if (text.isEmpty()) {
            break;
        }

        // Record current location.
        m_locations.addLocation(p_cursor);

        p_cursor.setPosition(start, p_moveMode);

        // Case-insensitive, non-regularexpression.
        SearchItem item;
        item.m_rawStr = text;
        item.m_text = text;
        item.m_forward = forward;

        m_searchHistory.addItem(item);
        m_searchHistory.resetIndex();
        while (--p_repeat >= 0) {
            hasMoved = m_editor->findText(item.m_text, item.m_options,
                                          item.m_forward,
                                          &p_cursor, p_moveMode);
        }

        Q_ASSERT(hasMoved);
        if (!p_cursor.atEnd() && useLeftSideOfCursor(p_cursor)) {
            p_cursor.movePosition(QTextCursor::NextCharacter, p_moveMode);
        }

        break;
    }

    case Movement::ParagraphUp:
        forward = false;
        // Fall through.
    case Movement::ParagraphDown:
    {
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        // Record current location.
        m_locations.addLocation(p_cursor);

        int oriPos = p_cursor.position();

        int position = VEditUtils::findNextEmptyBlock(p_cursor,
                                                      forward,
                                                      p_repeat);
        if (position == -1) {
            // No empty block. Move to the first/last character.
            p_cursor.movePosition(forward ? QTextCursor::End : QTextCursor::Start,
                                  p_moveMode);
            hasMoved = p_cursor.position() != oriPos;
        } else {
            p_cursor.setPosition(position, p_moveMode);
            hasMoved = true;
        }

        break;
    }

    default:
        break;
    }

    return hasMoved;
}

bool VVim::selectRange(QTextCursor &p_cursor, const QTextDocument *p_doc,
                       Range p_range, int p_repeat)
{
    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;
    bool around = false;
    QChar opening;
    QChar closing;
    bool crossBlock = false;
    bool multipleTargets = false;

    Q_UNUSED(p_doc);

    switch (p_range) {
    case Range::Line:
    {
        // Visual mode, just select selected lines.
        if (checkMode(VimMode::Visual) || checkMode(VimMode::VisualLine)) {
            expandSelectionToWholeLines(p_cursor);
            hasMoved = true;
            break;
        }

        // Current line and next (p_repeat - 1) lines.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        if (p_repeat > 1) {
            p_cursor.movePosition(QTextCursor::NextBlock, moveMode, p_repeat - 1);
        }

        expandSelectionToWholeLines(p_cursor);
        hasMoved = true;
        break;
    }

    case Range::WordAround:
        around = true;
        // Fall through.
    case Range::WordInner:
    {
        Q_ASSERT(p_repeat == -1);
        bool spaces = false;
        int start, end;
        VEditUtils::findCurrentWord(p_cursor, start, end);

        if (start == end) {
            // Select the space between previous word and next word.
            findCurrentSpace(p_cursor, start, end);
            spaces = true;
        }

        if (start != end) {
            p_cursor.setPosition(start, QTextCursor::MoveAnchor);
            p_cursor.setPosition(end, moveMode);
            hasMoved = true;

            if (around) {
                if (spaces) {
                    // Select the word by the end of spaces.
                    if (!p_cursor.atBlockEnd()) {
                        p_cursor.movePosition(QTextCursor::EndOfWord, moveMode);
                    }
                } else {
                    // Select additional spaces at two ends.
                    expandSelectionAcrossSpacesWithinBlock(p_cursor);
                }
            }
        }

        break;
    }

    case Range::WORDAround:
        around = true;
        // Fall through.
    case Range::WORDInner:
    {
        Q_ASSERT(p_repeat == -1);
        bool spaces = false;
        int start, end;
        findCurrentSpace(p_cursor, start, end);

        if (start == end) {
            VEditUtils::findCurrentWORD(p_cursor, start, end);
        } else {
            // Select the space between previous WORD and next WORD.
            spaces = true;
        }

        if (start != end) {
            p_cursor.setPosition(start, QTextCursor::MoveAnchor);
            p_cursor.setPosition(end, moveMode);
            hasMoved = true;

            if (around) {
                if (spaces) {
                    // Select the WORD by the end of spaces.
                    if (!p_cursor.atBlockEnd()) {
                        // Skip spaces (mainly across block).
                        moveCursorAcrossSpaces(p_cursor, moveMode, true);

                        // [start, end] is current WORD.
                        VEditUtils::findCurrentWORD(p_cursor, start, end);

                        // Move cursor to the end of current WORD.
                        p_cursor.setPosition(end, moveMode);
                    }
                } else {
                    // Select additional spaces at two ends.
                    expandSelectionAcrossSpacesWithinBlock(p_cursor);
                }
            }
        }

        break;
    }

    case Range::ParenthesisAround:
    {
        around = true;
        opening = '(';
        closing = ')';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::ParenthesisInner:
    {
        around = false;
        opening = '(';
        closing = ')';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::BracketAround:
    {
        around = true;
        opening = '[';
        closing = ']';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::BracketInner:
    {
        around = false;
        opening = '[';
        closing = ']';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::AngleBracketAround:
    {
        around = true;
        opening = '<';
        closing = '>';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::AngleBracketInner:
    {
        around = false;
        opening = '<';
        closing = '>';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::BraceAround:
    {
        around = true;
        opening = '{';
        closing = '}';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::BraceInner:
    {
        around = false;
        opening = '{';
        closing = '}';
        crossBlock = true;
        multipleTargets = true;
        goto handlePairTarget;
    }

    case Range::DoubleQuoteAround:
    {
        around = true;
        opening = '"';
        closing = '"';
        crossBlock = false;
        multipleTargets = false;
        goto handlePairTarget;
    }

    case Range::DoubleQuoteInner:
    {
        around = false;
        opening = '"';
        closing = '"';
        crossBlock = false;
        multipleTargets = false;
        goto handlePairTarget;
    }

    case Range::BackQuoteAround:
    {
        around = true;
        opening = '`';
        closing = '`';
        crossBlock = false;
        multipleTargets = false;
        goto handlePairTarget;
    }

    case Range::BackQuoteInner:
    {
        around = false;
        opening = '`';
        closing = '`';
        crossBlock = false;
        multipleTargets = false;
        goto handlePairTarget;
    }

    case Range::QuoteAround:
    {
        around = true;
        opening = '\'';
        closing = '\'';
        crossBlock = false;
        multipleTargets = false;
        goto handlePairTarget;
    }

    case Range::QuoteInner:
    {
        around = false;
        opening = '\'';
        closing = '\'';
        crossBlock = false;
        multipleTargets = false;

handlePairTarget:

        if (p_repeat == -1) {
            p_repeat = 1;
        } else if (p_repeat > 1 && !multipleTargets) {
            // According to the behavior of Vim.
            p_repeat = 1;
            around = true;
        }

        hasMoved = VEditUtils::selectPairTargetAround(p_cursor,
                                                      opening,
                                                      closing,
                                                      around,
                                                      crossBlock,
                                                      p_repeat);
        break;
    }

    default:
        break;
    }

    return hasMoved;
}

void VVim::processDeleteAction(QList<Token> &p_tokens)
{
    Token to = p_tokens.takeFirst();
    int repeat = -1;
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        to = p_tokens.takeFirst();
    }

    if ((!to.isMovement() && !to.isRange()) || !p_tokens.isEmpty()) {
        p_tokens.clear();
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    QTextDocument *doc = m_editor->documentW();
    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        cursor.beginEditBlock();
        hasMoved = selectRange(cursor, doc, to.m_range, repeat);
        if (hasMoved) {
            // Whether the range may cross blocks.
            bool mayCrossBlock = false;

            switch (to.m_range) {
            case Range::Line:
            {
                // dd, delete current line.
                if (cursor.hasSelection()) {
                    repeat = VEditUtils::selectedBlockCount(cursor);
                    deleteSelectedText(cursor, true);
                } else {
                    VEditUtils::removeBlock(cursor);
                    saveToRegister("\n");
                    repeat = 1;
                }

                message(tr("%1 fewer %2").arg(repeat).arg(repeat > 1 ? tr("lines")
                                                                     : tr("line")));
                break;
            }

            case Range::ParenthesisInner:
                // Fall through.
            case Range::ParenthesisAround:
                // Fall through.
            case Range::BracketInner:
                // Fall through.
            case Range::BracketAround:
                // Fall through.
            case Range::AngleBracketInner:
                // Fall through.
            case Range::AngleBracketAround:
                // Fall through.
            case Range::BraceInner:
                // Fall through.
            case Range::BraceAround:
                // Fall through.
                mayCrossBlock = true;

                V_FALLTHROUGH;

            case Range::WordAround:
                // Fall through.
            case Range::WordInner:
                // Fall through.
            case Range::WORDAround:
                // Fall through.
            case Range::WORDInner:
                // Fall through.
            case Range::QuoteInner:
                // Fall through.
            case Range::QuoteAround:
                // Fall through.
            case Range::DoubleQuoteInner:
                // Fall through.
            case Range::DoubleQuoteAround:
                // Fall through.
            case Range::BackQuoteInner:
                // Fall through.
            case Range::BackQuoteAround:
            {
                if (cursor.hasSelection()) {
                    bool clearEmptyBlock = false;
                    if (mayCrossBlock
                        && VEditUtils::selectedBlockCount(cursor) > 1) {
                        clearEmptyBlock = true;
                    }

                    int blockCount = 0;
                    if (clearEmptyBlock) {
                        blockCount = doc->blockCount();
                    }

                    deleteSelectedText(cursor, clearEmptyBlock);

                    if (clearEmptyBlock) {
                        int nrBlock = blockCount - doc->blockCount();
                        Q_ASSERT(nrBlock > 0);
                        message(tr("%1 fewer %2").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                               : tr("line")));
                    }
                }

                break;
            }

            default:
                return;
            }
        }

        cursor.endEditBlock();
        goto exit;
    }

    V_ASSERT(to.isMovement());

    // Filter out not supported movement for DELETE action.
    switch (to.m_movement) {
    case Movement::PageUp:
    case Movement::PageDown:
    case Movement::HalfPageUp:
    case Movement::HalfPageDown:
        return;

    default:
        break;
    }

    cursor.beginEditBlock();
    if (checkMode(VimMode::VisualLine) || checkMode(VimMode::Visual)) {
        // Visual mode, omitting repeat and movement.
        // Different from Vim:
        // If there is no selection in Visual mode, we do nothing.
        if (cursor.hasSelection()) {
            hasMoved = true;
            deleteSelectedText(cursor, m_mode == VimMode::VisualLine);
        } else if (checkMode(VimMode::Visual)) {
            hasMoved = true;
            VEditUtils::removeBlock(cursor);
            saveToRegister("\n");
        }

        cursor.endEditBlock();
        goto exit;
    }

    hasMoved = processMovement(cursor, moveMode, to, repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (hasMoved) {
        bool clearEmptyBlock = false;
        switch (to.m_movement) {
        case Movement::Up:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "delete up" << repeat << "lines";
            break;
        }

        case Movement::Down:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "delete down" << repeat << "lines";
            break;
        }

        case Movement::EndOfLine:
        {
            // End of line (block).
            if (repeat > 1) {
                clearEmptyBlock = true;
            }

            qDebug() << "delete till end of" << repeat << "line";
            break;
        }

        case Movement::LineJump:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "delete till line" << repeat;
            break;
        }

        case Movement::StartOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "delete till start of document";
            break;
        }

        case Movement::EndOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "delete till end of document";
            break;
        }

        // ParagraphUp and ParagraphDown are a little different from Vim in
        // block deletion.

        default:
            break;
        }

        if (clearEmptyBlock) {
            int nrBlock = VEditUtils::selectedBlockCount(cursor);
            message(tr("%1 fewer %2").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                   : tr("line")));
        }

        deleteSelectedText(cursor, clearEmptyBlock);
    }

    cursor.endEditBlock();

exit:
    if (hasMoved) {
        m_editor->setTextCursorW(cursor);
    }
}

void VVim::processCopyAction(QList<Token> &p_tokens)
{
    Token to = p_tokens.takeFirst();
    int repeat = -1;
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        to = p_tokens.takeFirst();
    }

    if ((!to.isMovement() && !to.isRange()) || !p_tokens.isEmpty()) {
        p_tokens.clear();
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    QTextDocument *doc = m_editor->documentW();
    int oriPos = cursor.position();
    bool changed = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        cursor.beginEditBlock();
        changed = selectRange(cursor, doc, to.m_range, repeat);
        if (changed) {
            // Whether the range may cross blocks.
            bool mayCrossBlock = false;

            switch (to.m_range) {
            case Range::Line:
            {
                // yy, copy current line.
                if (cursor.hasSelection()) {
                    repeat = VEditUtils::selectedBlockCount(cursor);
                    copySelectedText(cursor, true);
                } else {
                    saveToRegister("\n");
                    repeat = 1;
                }

                message(tr("%1 %2 yanked").arg(repeat).arg(repeat > 1 ? tr("lines")
                                                                      : tr("line")));
                break;
            }

            case Range::ParenthesisInner:
                // Fall through.
            case Range::ParenthesisAround:
                // Fall through.
            case Range::BracketInner:
                // Fall through.
            case Range::BracketAround:
                // Fall through.
            case Range::AngleBracketInner:
                // Fall through.
            case Range::AngleBracketAround:
                // Fall through.
            case Range::BraceInner:
                // Fall through.
            case Range::BraceAround:
                // Fall through.
                mayCrossBlock = true;

                V_FALLTHROUGH;

            case Range::WordAround:
                // Fall through.
            case Range::WordInner:
                // Fall through.
            case Range::WORDAround:
                // Fall through.
            case Range::WORDInner:
                // Fall through.
            case Range::QuoteInner:
                // Fall through.
            case Range::QuoteAround:
                // Fall through.
            case Range::DoubleQuoteInner:
                // Fall through.
            case Range::DoubleQuoteAround:
                // Fall through.
            case Range::BackQuoteInner:
                // Fall through.
            case Range::BackQuoteAround:
            {
                if (cursor.hasSelection()) {
                    bool multipleBlocks = false;
                    int nrBlock = VEditUtils::selectedBlockCount(cursor);
                    if (mayCrossBlock && nrBlock > 1) {
                        multipleBlocks = true;
                    }

                    // No need to add new line even crossing multiple blocks.
                    copySelectedText(cursor, false);

                    if (multipleBlocks) {
                        message(tr("%1 %2 yanked").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                                : tr("line")));
                    }
                }

                break;
            }

            default:
                return;
            }
        }

        if (cursor.position() != oriPos) {
            cursor.setPosition(oriPos);
            changed = true;
        }

        cursor.endEditBlock();
        goto exit;
    }

    V_ASSERT(to.isMovement());

    // Filter out not supported movement for Copy action.
    switch (to.m_movement) {
    case Movement::PageUp:
    case Movement::PageDown:
    case Movement::HalfPageUp:
    case Movement::HalfPageDown:
        return;

    default:
        break;
    }

    cursor.beginEditBlock();
    changed = processMovement(cursor, moveMode, to, repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (changed) {
        bool addNewLine = false;
        switch (to.m_movement) {
        case Movement::Up:
        {
            expandSelectionToWholeLines(cursor);
            addNewLine = true;
            qDebug() << "copy up" << repeat << "lines";
            break;
        }

        case Movement::Down:
        {
            expandSelectionToWholeLines(cursor);
            addNewLine = true;
            qDebug() << "copy down" << repeat << "lines";
            break;
        }

        case Movement::EndOfLine:
        {
            // End of line (block).
            // Do not need to add new line even if repeat > 1.
            qDebug() << "copy till end of" << repeat << "line";
            break;
        }

        case Movement::LineJump:
        {
            expandSelectionToWholeLines(cursor);
            addNewLine = true;
            qDebug() << "copy till line" << repeat;
            break;
        }

        case Movement::StartOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            addNewLine = true;
            qDebug() << "copy till start of document";
            break;
        }

        case Movement::EndOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            addNewLine = true;
            qDebug() << "copy till end of document";
            break;
        }

        default:
            break;
        }

        if (addNewLine) {
            int nrBlock = VEditUtils::selectedBlockCount(cursor);
            message(tr("%1 %2 yanked").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                    : tr("line")));
        }

        copySelectedText(cursor, addNewLine);
        if (cursor.position() != oriPos) {
            cursor.setPosition(oriPos);
        }
    }

    cursor.endEditBlock();

exit:
    if (changed) {
        m_editor->setTextCursorW(cursor);
    }
}

void VVim::processPasteAction(QList<Token> &p_tokens, bool p_pasteBefore)
{
    int repeat = 1;
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        if (!p_tokens.isEmpty() || !to.isRepeat()) {
            p_tokens.clear();
            return;
        }

        repeat = to.m_repeat;
    }

    Register &reg = getRegister(m_regName);
    QString value = reg.read();
    bool isBlock = reg.isBlock();
    if (value.isEmpty()) {
        if (checkMode(VimMode::Visual) || checkMode(VimMode::VisualLine)) {
            setMode(VimMode::Normal);
        }

        return;
    }

    if (!(checkMode(VimMode::Normal)
          || checkMode(VimMode::Visual)
          || checkMode(VimMode::VisualLine))) {
        return;
    }

    QString text;
    text.reserve(repeat * value.size() + 1);
    for (int i = 0; i < repeat; ++i) {
        text.append(value);
    }

    bool changed = false;
    int nrBlock = 0;
    int restorePos = -1;
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();

    // Different from Vim:
    //  In visual mode, by default vim select the current char, so paste operation will replace
    //  current char, but here it is strange to follow this specification.
    bool isVisualLine = checkMode(VimMode::VisualLine);
    if (!checkMode(VimMode::Normal)) {
        // Visual or VisualLine mode.
        if (cursor.hasSelection()) {
            int pos = cursor.selectionStart();
            deleteSelectedText(cursor, isVisualLine);
            if (isVisualLine) {
                // Insert a new block for insertion.
                insertChangeBlockAfterDeletion(cursor, pos);

                restorePos = cursor.position();

                if (isBlock) {
                    nrBlock = text.count('\n');
                    // insertChangeBlockAfterDeletion() already insert a new line, so eliminate one here.
                    text = text.left(text.size() - 1);
                }
            } else if (isBlock) {
                // Insert new block right at current cursor.
                nrBlock = text.count('\n');
                cursor.insertBlock();
                restorePos = cursor.position();
            } else if (text.count('\n') > 0) {
                restorePos = cursor.position();
            }

            changed = true;
        }
    } else {
        // Normal mode.
        if (isBlock) {
            if (p_pasteBefore) {
                cursor.movePosition(QTextCursor::StartOfBlock);
                cursor.insertBlock();
                cursor.movePosition(QTextCursor::PreviousBlock);
            } else {
                cursor.movePosition(QTextCursor::EndOfBlock);
                cursor.insertBlock();
            }

            restorePos = cursor.position();

            nrBlock = text.count('\n');

            // inserBlock() already insert a new line, so eliminate one here.
            text = text.left(text.size() - 1);
        } else {
            // Not a block.
            if (!p_pasteBefore && !cursor.atBlockEnd()) {
                // Insert behind current cursor.
                cursor.movePosition(QTextCursor::Right);
            }

            if (text.count('\n') > 0) {
                restorePos = cursor.position();
            }
        }

        changed = true;
    }

    if (changed) {
        cursor.insertText(text);

        if (restorePos == -1) {
            // Move cursor one character left.
            cursor.movePosition(QTextCursor::Left);
        } else {
            // Move cursor at the right position.
            cursor.setPosition(restorePos);
        }

        if (nrBlock > 0) {
            message(tr("%1 more %2").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                  : tr("line")));
        }
    }

    cursor.endEditBlock();

    if (!checkMode(VimMode::Normal)) {
        setMode(VimMode::Normal);
    }

    if (changed) {
        m_editor->setTextCursorW(cursor);
    }

    qDebug() << "text pasted" << text;
}

void VVim::processChangeAction(QList<Token> &p_tokens)
{
    Token to = p_tokens.takeFirst();
    int repeat = -1;
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        to = p_tokens.takeFirst();
    }

    if ((!to.isMovement() && !to.isRange()) || !p_tokens.isEmpty()) {
        p_tokens.clear();
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    QTextDocument *doc = m_editor->documentW();
    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        cursor.beginEditBlock();
        hasMoved = selectRange(cursor, doc, to.m_range, repeat);
        if (hasMoved) {
            int pos = cursor.selectionStart();

            switch (to.m_range) {
            case Range::Line:
            {
                // cc, change current line.
                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, true);
                    insertChangeBlockAfterDeletion(cursor, pos);
                } else {
                    saveToRegister("\n");
                }

                break;
            }

            case Range::ParenthesisInner:
                // Fall through.
            case Range::ParenthesisAround:
                // Fall through.
            case Range::BracketInner:
                // Fall through.
            case Range::BracketAround:
                // Fall through.
            case Range::AngleBracketInner:
                // Fall through.
            case Range::AngleBracketAround:
                // Fall through.
            case Range::BraceInner:
                // Fall through.
            case Range::BraceAround:
                // Fall through.
            case Range::WordAround:
                // Fall through.
            case Range::WordInner:
                // Fall through.
            case Range::WORDAround:
                // Fall through.
            case Range::WORDInner:
                // Fall through.
            case Range::QuoteInner:
                // Fall through.
            case Range::QuoteAround:
                // Fall through.
            case Range::DoubleQuoteInner:
                // Fall through.
            case Range::DoubleQuoteAround:
                // Fall through.
            case Range::BackQuoteInner:
                // Fall through.
            case Range::BackQuoteAround:
            {
                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, false);
                }

                break;
            }

            default:
                return;
            }
        }

        cursor.endEditBlock();
        goto exit;
    }

    V_ASSERT(to.isMovement());

    // Filter out not supported movement for Change action.
    switch (to.m_movement) {
    case Movement::PageUp:
    case Movement::PageDown:
    case Movement::HalfPageUp:
    case Movement::HalfPageDown:
        return;

    default:
        break;
    }

    cursor.beginEditBlock();
    if (checkMode(VimMode::VisualLine) || checkMode(VimMode::Visual)) {
        // Visual mode, omitting repeat and movement.
        // Different from Vim:
        // If there is no selection in Visual mode, we do nothing.
        bool visualLine = checkMode(VimMode::VisualLine);
        if (cursor.hasSelection()) {
            hasMoved = true;
            int pos = cursor.selectionStart();
            deleteSelectedText(cursor, visualLine);
            if (visualLine) {
                insertChangeBlockAfterDeletion(cursor, pos);
            }
        } else if (visualLine) {
            hasMoved = true;
            saveToRegister("\n");
        }

        cursor.endEditBlock();
        goto exit;
    }

    hasMoved = processMovement(cursor, moveMode, to, repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (hasMoved) {
        bool clearEmptyBlock = false;
        switch (to.m_movement) {
        case Movement::Left:
        {
            qDebug() << "change backward" << repeat << "chars";
            break;
        }

        case Movement::Right:
        {
            qDebug() << "change forward" << repeat << "chars";
            break;
        }

        case Movement::Up:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "change up" << repeat << "lines";
            break;
        }

        case Movement::Down:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "change down" << repeat << "lines";
            break;
        }

        case Movement::VisualUp:
        {
            qDebug() << "change visual up" << repeat << "lines";
            break;
        }

        case Movement::VisualDown:
        {
            qDebug() << "change visual down" << repeat << "lines";
            break;
        }

        case Movement::StartOfLine:
        {
            qDebug() << "change till start of line";
            break;
        }

        case Movement::EndOfLine:
        {
            // End of line (block).
            if (repeat > 1) {
                clearEmptyBlock = true;
            }

            qDebug() << "change till end of" << repeat << "line";
            break;
        }

        case Movement::FirstCharacter:
        {
            qDebug() << "change till first non-space character";
            break;
        }

        case Movement::LineJump:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "change till line" << repeat;
            break;
        }

        case Movement::StartOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "change till start of document";
            break;
        }

        case Movement::EndOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            clearEmptyBlock = true;
            qDebug() << "change till end of document";
            break;
        }

        case Movement::WordForward:
        {
            qDebug() << "change" << repeat << "words forward";
            break;
        }

        case Movement::WORDForward:
        {
            qDebug() << "change" << repeat << "WORDs forward";
            break;
        }

        case Movement::ForwardEndOfWord:
        {
            qDebug() << "change" << repeat << "end of words forward";
            break;
        }

        case Movement::ForwardEndOfWORD:
        {
            qDebug() << "change" << repeat << "end of WORDs forward";
            break;
        }

        case Movement::WordBackward:
        {
            qDebug() << "change" << repeat << "words backward";
            break;
        }

        case Movement::WORDBackward:
        {
            qDebug() << "change" << repeat << "WORDs backward";
            break;
        }

        case Movement::BackwardEndOfWord:
        {
            qDebug() << "change" << repeat << "end of words backward";
            break;
        }

        case Movement::BackwardEndOfWORD:
        {
            qDebug() << "change" << repeat << "end of WORDs backward";
            break;
        }

        default:
            break;
        }

        if (cursor.hasSelection()) {
            int pos = cursor.selectionStart();
            bool allDeleted = false;
            if (pos == 0) {
                QTextBlock block = m_editor->documentW()->lastBlock();
                if (block.position() + block.length() - 1 == cursor.selectionEnd()) {
                    allDeleted = true;
                }
            }

            deleteSelectedText(cursor, clearEmptyBlock);
            if (clearEmptyBlock && !allDeleted) {
                insertChangeBlockAfterDeletion(cursor, pos);
            }
        }
    }

    cursor.endEditBlock();

exit:
    if (hasMoved) {
        m_editor->setTextCursorW(cursor);
    }

    setMode(VimMode::Insert);
}

void VVim::processIndentAction(QList<Token> &p_tokens, IndentType p_type)
{
    Token to = p_tokens.takeFirst();
    int repeat = -1;
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        to = p_tokens.takeFirst();
    }

    if ((!to.isMovement() && !to.isRange()) || !p_tokens.isEmpty()) {
        p_tokens.clear();
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();

    QString op;
    switch (p_type) {
    case IndentType::Indent:
        op = ">";
        break;

    case IndentType::UnIndent:
        op = "<";
        break;

    case IndentType::AutoIndent:
        op = "=";
        break;

    default:
        Q_ASSERT(false);
    }

    if (to.isRange()) {
        bool changed = selectRange(cursor, m_editor->documentW(), to.m_range, repeat);
        if (changed) {
            switch (to.m_range) {
            case Range::Line:
            {
                // >>/<<, indent/unindent current line.
                if (repeat == -1) {
                    repeat = 1;
                }

                if (p_type == IndentType::AutoIndent) {
                    VEditUtils::indentSelectedBlocksAsBlock(cursor, false);
                } else {
                    VEditUtils::indentSelectedBlocks(cursor,
                                                     m_editConfig->m_tabSpaces,
                                                     p_type == IndentType::Indent);
                }

                message(tr("%1 %2 %3ed 1 time").arg(repeat)
                                               .arg(repeat > 1 ? tr("lines")
                                                               : tr("line"))
                                               .arg(op));
                break;
            }

            case Range::ParenthesisInner:
                // Fall through.
            case Range::ParenthesisAround:
                // Fall through.
            case Range::BracketInner:
                // Fall through.
            case Range::BracketAround:
                // Fall through.
            case Range::AngleBracketInner:
                // Fall through.
            case Range::AngleBracketAround:
                // Fall through.
            case Range::BraceInner:
                // Fall through.
            case Range::BraceAround:
                // Fall through.
            case Range::WordAround:
                // Fall through.
            case Range::WordInner:
                // Fall through.
            case Range::WORDAround:
                // Fall through.
            case Range::WORDInner:
                // Fall through.
            case Range::QuoteInner:
                // Fall through.
            case Range::QuoteAround:
                // Fall through.
            case Range::DoubleQuoteInner:
                // Fall through.
            case Range::DoubleQuoteAround:
                // Fall through.
            case Range::BackQuoteInner:
                // Fall through.
            case Range::BackQuoteAround:
            {
                int nrBlock = VEditUtils::selectedBlockCount(cursor);
                if (p_type == IndentType::AutoIndent) {
                    VEditUtils::indentSelectedBlocksAsBlock(cursor, false);
                } else {
                    VEditUtils::indentSelectedBlocks(cursor,
                                                     m_editConfig->m_tabSpaces,
                                                     p_type == IndentType::Indent);
                }

                message(tr("%1 %2 %3ed 1 time").arg(nrBlock)
                                               .arg(nrBlock > 1 ? tr("lines")
                                                                : tr("line"))
                                               .arg(op));
                break;
            }

            default:
                return;
            }
        }

        return;
    }

    V_ASSERT(to.isMovement());

    // Filter out not supported movement for Indent/UnIndent action.
    switch (to.m_movement) {
    case Movement::PageUp:
    case Movement::PageDown:
    case Movement::HalfPageUp:
    case Movement::HalfPageDown:
        return;

    default:
        break;
    }

    if (checkMode(VimMode::VisualLine) || checkMode(VimMode::Visual)) {
        // Visual mode, omitting repeat and movement.
        // Different from Vim:
        // Do not exit Visual mode after indentation/unindentation.
        if (p_type == IndentType::AutoIndent) {
            VEditUtils::indentSelectedBlocksAsBlock(cursor, false);
        } else {
            VEditUtils::indentSelectedBlocks(cursor,
                                             m_editConfig->m_tabSpaces,
                                             p_type == IndentType::Indent);
        }

        return;
    }

    processMovement(cursor,
                    QTextCursor::KeepAnchor,
                    to,
                    repeat);

    int nrBlock = VEditUtils::selectedBlockCount(cursor);
    if (p_type == IndentType::AutoIndent) {
        VEditUtils::indentSelectedBlocksAsBlock(cursor, false);
    } else {
        VEditUtils::indentSelectedBlocks(cursor,
                                         m_editConfig->m_tabSpaces,
                                         p_type == IndentType::Indent);
    }

    message(tr("%1 %2 %3ed 1 time").arg(nrBlock)
                                   .arg(nrBlock > 1 ? tr("lines")
                                                    : tr("line"))
                                   .arg(op));
}

void VVim::processToLowerAction(QList<Token> &p_tokens, bool p_toLower)
{
    Token to = p_tokens.takeFirst();
    int repeat = -1;
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        to = p_tokens.takeFirst();
    }

    if ((!to.isMovement() && !to.isRange()) || !p_tokens.isEmpty()) {
        p_tokens.clear();
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    QTextDocument *doc = m_editor->documentW();
    bool changed = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;
    int oriPos = cursor.position();

    if (to.isRange()) {
        cursor.beginEditBlock();
        changed = selectRange(cursor, doc, to.m_range, repeat);
        if (changed) {
            oriPos = cursor.selectionStart();
            int nrBlock = VEditUtils::selectedBlockCount(cursor);
            message(tr("%1 %2 changed").arg(nrBlock)
                                       .arg(nrBlock > 1 ? tr("lines") : tr("line")));

            convertCaseOfSelectedText(cursor, p_toLower);

            cursor.setPosition(oriPos);
        }

        cursor.endEditBlock();
        goto exit;
    }

    V_ASSERT(to.isMovement());

    // Filter out not supported movement for ToLower/ToUpper action.
    switch (to.m_movement) {
    case Movement::PageUp:
    case Movement::PageDown:
    case Movement::HalfPageUp:
    case Movement::HalfPageDown:
        return;

    default:
        break;
    }

    cursor.beginEditBlock();
    changed = processMovement(cursor,
                              moveMode,
                              to,
                              repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (changed) {
        oriPos = cursor.selectionStart();

        switch (to.m_movement) {
        case Movement::Up:
        {
            expandSelectionToWholeLines(cursor);
            break;
        }

        case Movement::Down:
        {
            expandSelectionToWholeLines(cursor);
            break;
        }

        case Movement::LineJump:
        {
            expandSelectionToWholeLines(cursor);
            break;
        }

        case Movement::StartOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            break;
        }

        case Movement::EndOfDocument:
        {
            expandSelectionToWholeLines(cursor);
            break;
        }

        default:
            break;
        }

        int nrBlock = VEditUtils::selectedBlockCount(cursor);
        message(tr("%1 %2 changed").arg(nrBlock).arg(nrBlock > 1 ? tr("lines")
                                                                 : tr("line")));

        convertCaseOfSelectedText(cursor, p_toLower);

        cursor.setPosition(oriPos);
    }

    cursor.endEditBlock();

exit:
    if (changed) {
        m_editor->setTextCursorW(cursor);
    }
}

void VVim::processUndoAction(QList<Token> &p_tokens)
{
    int repeat = 1;
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        if (!p_tokens.isEmpty() || !to.isRepeat()) {
            p_tokens.clear();
            return;
        }

        repeat = to.m_repeat;
    }

    QTextDocument *doc = m_editor->documentW();
    int i = 0;
    for (i = 0; i < repeat && doc->isUndoAvailable(); ++i) {
        m_editor->undoW();
    }

    message(tr("Undo %1 %2").arg(i).arg(i > 1 ? tr("changes") : tr("change")));
}

void VVim::processRedoAction(QList<Token> &p_tokens)
{
    int repeat = 1;
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        if (!p_tokens.isEmpty() || !to.isRepeat()) {
            p_tokens.clear();
            return;
        }

        repeat = to.m_repeat;
    }

    QTextDocument *doc = m_editor->documentW();
    int i = 0;
    for (i = 0; i < repeat && doc->isRedoAvailable(); ++i) {
        m_editor->redoW();
    }

    message(tr("Redo %1 %2").arg(i).arg(i > 1 ? tr("changes") : tr("change")));
}

void VVim::processRedrawLineAction(QList<Token> &p_tokens, int p_dest)
{
    QTextCursor cursor = m_editor->textCursorW();
    int repeat = cursor.block().blockNumber();
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        if (!p_tokens.isEmpty() || !to.isRepeat()) {
            p_tokens.clear();
            return;
        }

        repeat = to.m_repeat - 1;
    }

    m_editor->scrollBlockInPage(repeat, p_dest);
}

void VVim::processJumpLocationAction(QList<Token> &p_tokens, bool p_next)
{
    int repeat = 1;
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        if (!p_tokens.isEmpty() || !to.isRepeat()) {
            p_tokens.clear();
            return;
        }

        repeat = to.m_repeat;
    }

    QTextCursor cursor = m_editor->textCursorW();
    Location loc;
    if (p_next) {
        while (m_locations.hasNext() && repeat > 0) {
            --repeat;
            loc = m_locations.nextLocation();
        }
    } else {
        while (m_locations.hasPrevious() && repeat > 0) {
            --repeat;
            loc = m_locations.previousLocation(cursor);
        }
    }

    if (loc.isValid()) {
        QTextDocument *doc = m_editor->documentW();
        if (loc.m_blockNumber >= doc->blockCount()) {
            message(tr("Mark has invalid line number"));
            return;
        }

        QTextBlock block = doc->findBlockByNumber(loc.m_blockNumber);
        int pib = loc.m_positionInBlock;
        if (pib >= block.length()) {
            pib = block.length() - 1;
        }

        if (!m_editor->isBlockVisible(block)) {
            // Scroll the block to the center of screen.
            m_editor->scrollBlockInPage(block.blockNumber(), 1);
        }

        cursor.setPosition(block.position() + pib);
        m_editor->setTextCursorW(cursor);
    }
}

void VVim::processReplaceAction(QList<Token> &p_tokens)
{
    int repeat = 1;
    QChar replaceChar;
    Q_ASSERT(!p_tokens.isEmpty());
    Token to = p_tokens.takeFirst();
    if (to.isRepeat()) {
        repeat = to.m_repeat;
        Q_ASSERT(!p_tokens.isEmpty());
        to = p_tokens.takeFirst();
    }

    Q_ASSERT(to.isKey() && p_tokens.isEmpty());
    replaceChar = keyToChar(to.m_key.m_key, to.m_key.m_modifiers);
    if (replaceChar.isNull()) {
        return;
    }

    if (!(checkMode(VimMode::Normal)
          || checkMode(VimMode::Visual)
          || checkMode(VimMode::VisualLine))) {
        return;
    }

    // Replace the next repeat characters with replaceChar until the end of line.
    // If repeat is greater than the number of left characters in current line,
    // do nothing.
    // In visual mode, repeat is ignored.
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (checkMode(VimMode::Normal)) {
        // Select the characters to be replaced.
        cursor.clearSelection();
        int pib = cursor.positionInBlock();
        int nrChar = cursor.block().length() - 1 - pib;
        if (repeat <= nrChar) {
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, repeat);
        }
    }

    bool changed = replaceSelectedTextWithCharacter(cursor, replaceChar);
    cursor.endEditBlock();

    if (changed) {
        m_editor->setTextCursorW(cursor);
        setMode(VimMode::Normal);
    }
}

void VVim::processReverseCaseAction(QList<Token> &p_tokens)
{
    int repeat = 1;
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        Q_ASSERT(to.isRepeat() && p_tokens.isEmpty());
        repeat = to.m_repeat;
    }

    if (!(checkMode(VimMode::Normal)
          || checkMode(VimMode::Visual)
          || checkMode(VimMode::VisualLine))) {
        return;
    }

    // Reverse the next repeat characters' case until the end of line.
    // If repeat is greater than the number of left characters in current line,
    // just change the actual number of left characters.
    // In visual mode, repeat is ignored and reverse the selected text.
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (checkMode(VimMode::Normal)) {
        // Select the characters to be replaced.
        cursor.clearSelection();
        int pib = cursor.positionInBlock();
        int nrChar = cursor.block().length() - 1 - pib;
        cursor.movePosition(QTextCursor::Right,
                            QTextCursor::KeepAnchor,
                            repeat > nrChar ? nrChar : repeat);
    }

    bool changed = reverseSelectedTextCase(cursor);
    cursor.endEditBlock();

    if (changed) {
        m_editor->setTextCursorW(cursor);
        setMode(VimMode::Normal);
    }
}

void VVim::processJoinAction(QList<Token> &p_tokens, bool p_modifySpaces)
{
    int repeat = 2;
    if (!p_tokens.isEmpty()) {
        Token to = p_tokens.takeFirst();
        Q_ASSERT(to.isRepeat() && p_tokens.isEmpty());
        repeat = qMax(to.m_repeat, repeat);
    }

    if (!(checkMode(VimMode::Normal)
          || checkMode(VimMode::Visual)
          || checkMode(VimMode::VisualLine))) {
        return;
    }

    // Join repeat lines, with the minimum of two lines. Do nothing when on the
    // last line.
    // If @p_modifySpaces is true, remove the indent and insert up to two spaces.
    // If repeat is too big, it is reduced to the number of lines available.
    // In visual mode, repeat is ignored and join the highlighted lines.
    int firstBlock = -1;
    int blockCount = repeat;
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (checkMode(VimMode::Normal)) {
        firstBlock = cursor.block().blockNumber();
    } else {
        QTextDocument *doc = m_editor->documentW();
        firstBlock = doc->findBlock(cursor.selectionStart()).blockNumber();
        int lastBlock = doc->findBlock(cursor.selectionEnd()).blockNumber();
        blockCount = lastBlock - firstBlock + 1;
    }

    bool changed = joinLines(cursor, firstBlock, blockCount, p_modifySpaces);
    cursor.endEditBlock();

    if (changed) {
        m_editor->setTextCursorW(cursor);
        setMode(VimMode::Normal);
    }
}

bool VVim::clearSelection()
{
    QTextCursor cursor = m_editor->textCursorW();
    if (cursor.hasSelection()) {
        cursor.clearSelection();
        m_editor->setTextCursorW(cursor);
        return true;
    }

    return false;
}

int VVim::blockCountOfPageStep() const
{
    int lineCount = m_editor->documentW()->blockCount();
    QScrollBar *bar = m_editor->verticalScrollBarW();
    int steps = (bar->maximum() - bar->minimum() + bar->pageStep());
    int pageLineCount = lineCount * (bar->pageStep() * 1.0 / steps);
    return pageLineCount;
}

void VVim::maintainSelectionInVisualMode(QTextCursor *p_cursor)
{
    // We need to always select the character on current position.
    QTextCursor *cursor = p_cursor;
    QTextCursor tmpCursor = m_editor->textCursorW();
    if (!cursor) {
        cursor = &tmpCursor;
    }

    bool hasChanged = false;
    int pos = cursor->position();
    int anchor = cursor->anchor();

    if (pos > anchor) {
        Q_ASSERT(pos > m_positionBeforeVisualMode);
        if (anchor > m_positionBeforeVisualMode) {
            // Re-select.
            cursor->setPosition(m_positionBeforeVisualMode);
            cursor->setPosition(pos, QTextCursor::KeepAnchor);
            hasChanged = true;
        }

        setCursorBlockMode(m_editor, CursorBlock::LeftSide);
    } else if (pos == anchor) {
        Q_ASSERT(anchor >= m_positionBeforeVisualMode);
        // Re-select.
        if (anchor == m_positionBeforeVisualMode) {
            cursor->setPosition(m_positionBeforeVisualMode + 1);
            cursor->setPosition(pos, QTextCursor::KeepAnchor);
            hasChanged = true;

            setCursorBlockMode(m_editor, CursorBlock::RightSide);
        } else {
            cursor->setPosition(m_positionBeforeVisualMode);
            cursor->setPosition(pos, QTextCursor::KeepAnchor);
            hasChanged = true;

            setCursorBlockMode(m_editor, CursorBlock::LeftSide);
        }
    } else {
        // Re-select.
        if (anchor <= m_positionBeforeVisualMode) {
            cursor->setPosition(m_positionBeforeVisualMode + 1);
            cursor->setPosition(pos, QTextCursor::KeepAnchor);
            hasChanged = true;
        }

        setCursorBlockMode(m_editor, CursorBlock::RightSide);
    }

    if (hasChanged && !p_cursor) {
        m_editor->setTextCursorW(*cursor);
    }
}

void VVim::expandSelectionToWholeLines(QTextCursor &p_cursor)
{
    QTextDocument *doc = m_editor->documentW();
    int curPos = p_cursor.position();
    int anchorPos = p_cursor.anchor();
    QTextBlock curBlock = doc->findBlock(curPos);
    QTextBlock anchorBlock = doc->findBlock(anchorPos);

    if (curPos >= anchorPos) {
        p_cursor.setPosition(anchorBlock.position(), QTextCursor::MoveAnchor);
        p_cursor.setPosition(curBlock.position() + curBlock.length() - 1,
                             QTextCursor::KeepAnchor);
    } else {
        p_cursor.setPosition(anchorBlock.position() + anchorBlock.length() - 1,
                             QTextCursor::MoveAnchor);
        p_cursor.setPosition(curBlock.position(),
                             QTextCursor::KeepAnchor);
    }
}

void VVim::initRegisters()
{
    if (!s_registers.isEmpty()) {
        return;
    }

    for (char ch = 'a'; ch <= 'z'; ++ch) {
        s_registers[QChar(ch)] = Register(QChar(ch));
    }

    s_registers[c_unnamedRegister] = Register(c_unnamedRegister);
    s_registers[c_blackHoleRegister] = Register(c_blackHoleRegister);
    s_registers[c_selectionRegister] = Register(c_selectionRegister);
}

bool VVim::expectingRegisterName() const
{
    return m_keys.size() == 1
           && m_keys.at(0) == Key(Qt::Key_QuoteDbl, Qt::ShiftModifier);
}

bool VVim::expectingCharacterTarget() const
{
    if (m_keys.size() != 1) {
        return false;
    }

    const Key &key = m_keys.first();
    return (key == Key(Qt::Key_F, Qt::NoModifier)
            || key == Key(Qt::Key_F, Qt::ShiftModifier)
            || key == Key(Qt::Key_T, Qt::NoModifier)
            || key == Key(Qt::Key_T, Qt::ShiftModifier));
}

bool VVim::expectingReplaceCharacter() const
{
    return m_keys.size() == 1
           && m_keys.first() == Key(Qt::Key_R, Qt::NoModifier);
}

bool VVim::expectingLeaderSequence() const
{
    if (m_replayLeaderSequence || m_keys.isEmpty()) {
        return false;
    }

    return m_keys.first() == Key(Qt::Key_Backslash);
}

bool VVim::expectingMarkName() const
{
    return checkPendingKey(Key(Qt::Key_M)) && m_tokens.isEmpty();
}

bool VVim::expectingMarkTarget() const
{
    return checkPendingKey(Key(Qt::Key_Apostrophe))
           || checkPendingKey(Key(Qt::Key_QuoteLeft));
}

QChar VVim::keyToRegisterName(const Key &p_key) const
{
    if (p_key.isAlphabet()) {
        return p_key.toAlphabet().toLower();
    }

    switch (p_key.m_key) {
    case Qt::Key_QuoteDbl:
        if (p_key.m_modifiers == Qt::ShiftModifier) {
            return c_unnamedRegister;
        }

        break;

    case Qt::Key_Plus:
        if (p_key.m_modifiers == Qt::ShiftModifier) {
            return c_selectionRegister;
        }

        break;

    case Qt::Key_Underscore:
        if (p_key.m_modifiers == Qt::ShiftModifier) {
            return c_blackHoleRegister;
        }

        break;

    default:
        break;
    }

    return QChar();
}

bool VVim::hasActionToken() const
{
    // There will be only one action token and it is placed at the front.
    bool has = false;
    if (m_tokens.isEmpty()) {
        return false;
    }

    if (m_tokens.at(0).isAction()) {
        has = true;
    }

    for (int i = 1; i < m_tokens.size(); ++i) {
        V_ASSERT(!m_tokens.at(i).isAction());
    }

    return has;
}

bool VVim::hasRepeatToken() const
{
    // There will be only one repeat token.
    bool has = false;
    if (m_tokens.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_tokens.size(); ++i) {
        if (m_tokens.at(i).isRepeat()) {
            V_ASSERT(!has);
            has = true;
        }
    }

    return has;
}

bool VVim::hasActionTokenValidForTextObject() const
{
    if (hasActionToken()) {
        Action act = m_tokens.first().m_action;
        if (act == Action::Delete
            || act == Action::Copy
            || act == Action::Change
            || act == Action::ToLower
            || act == Action::ToUpper
            || act == Action::Indent
            || act == Action::UnIndent) {
            return true;
        }
    }

    return false;
}

bool VVim::checkActionToken(Action p_action) const
{
    if (hasActionToken()) {
        return m_tokens.first().m_action == p_action;
    }

    return false;
}

bool VVim::checkPendingKey(const Key &p_key) const
{
    return (m_keys.size() == 1 && m_keys.first() == p_key);
}

void VVim::tryAddMoveAction()
{
    if (!hasActionToken()) {
        addActionToken(Action::Move);
    }
}

void VVim::addActionToken(Action p_action)
{
    V_ASSERT(!hasActionToken());
    m_tokens.prepend(Token(p_action));
}

const VVim::Token *VVim::getActionToken() const
{
    V_ASSERT(hasActionToken());
    return &m_tokens.first();
}

VVim::Token *VVim::getRepeatToken()
{
    V_ASSERT(hasRepeatToken());

    for (auto & token : m_tokens) {
        if (token.isRepeat()) {
            return &token;
        }
    }

    return NULL;
}

void VVim::addRangeToken(Range p_range)
{
    m_tokens.append(Token(p_range));
}

void VVim::addMovementToken(Movement p_movement)
{
    m_tokens.append(Token(p_movement));
}

void VVim::addMovementToken(Movement p_movement, Key p_key)
{
    m_tokens.append(Token(p_movement, p_key));
}

void VVim::addKeyToken(Key p_key)
{
    m_tokens.append(Token(p_key));
}

void VVim::deleteSelectedText(QTextCursor &p_cursor, bool p_clearEmptyBlock)
{
    if (p_cursor.hasSelection()) {
        QString deletedText = VEditUtils::selectedText(p_cursor);
        p_cursor.removeSelectedText();
        if (p_clearEmptyBlock && p_cursor.block().length() == 1) {
            deletedText += "\n";
            VEditUtils::removeBlock(p_cursor);
        }

        saveToRegister(deletedText);
    }
}

void VVim::copySelectedText(bool p_addNewLine)
{
    QTextCursor cursor = m_editor->textCursorW();
    if (cursor.hasSelection()) {
        cursor.beginEditBlock();
        copySelectedText(cursor, p_addNewLine);
        cursor.endEditBlock();
        m_editor->setTextCursorW(cursor);
    }
}

void VVim::copySelectedText(QTextCursor &p_cursor, bool p_addNewLine)
{
    if (p_cursor.hasSelection()) {
        QString text = VEditUtils::selectedText(p_cursor);
        p_cursor.clearSelection();
        if (p_addNewLine) {
            text += "\n";
        }

        saveToRegister(text);
    }
}

void VVim::convertCaseOfSelectedText(QTextCursor &p_cursor, bool p_toLower)
{
    if (p_cursor.hasSelection()) {
        QTextDocument *doc = p_cursor.document();
        int start = p_cursor.selectionStart();
        int end = p_cursor.selectionEnd();
        p_cursor.clearSelection();
        p_cursor.setPosition(start);
        int pos = p_cursor.position();
        while (pos < end) {
            QChar ch = doc->characterAt(pos);
            bool modified = false;
            if (p_toLower) {
                if (ch.isUpper()) {
                    ch = ch.toLower();
                    modified = true;
                }
            } else if (ch.isLower()) {
                ch = ch.toUpper();
                modified = true;
            }

            if (modified) {
                p_cursor.deleteChar();
                p_cursor.insertText(ch);
            } else {
                p_cursor.movePosition(QTextCursor::NextCharacter);
            }

            pos = p_cursor.position();
        }
    }
}

void VVim::saveToRegister(const QString &p_text)
{
    QString text(p_text);
    VEditUtils::removeObjectReplacementCharacter(text);

    qDebug() << QString("save text(%1) to register(%2)").arg(text).arg(m_regName);

    Register &reg = getRegister(m_regName);
    reg.update(text);

    if (!reg.isBlackHoleRegister() && !reg.isUnnamedRegister()) {
        // Save it to unnamed register.
        setRegister(c_unnamedRegister, reg.m_value);
    }
}

void VVim::Register::update(const QString &p_value)
{
    QChar newLine('\n');
    bool newIsBlock = false;
    if (p_value.endsWith(newLine)) {
        newIsBlock = true;
    }

    bool oriIsBlock = isBlock();
    if (isNamedRegister() && m_append) {
        // Append @p_value to m_value.
        if (newIsBlock) {
            if (oriIsBlock) {
                m_value += p_value;
            } else {
                m_value.append(newLine);
                m_value += p_value;
            }
        } else if (oriIsBlock) {
            m_value += p_value;
            m_value.append(newLine);
        } else {
            m_value += p_value;
        }
    } else {
        // Set m_value to @p_value.
        m_value = p_value;
    }

    if (isSelectionRegister()) {
        // Change system clipboard.
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(m_value);
    }
}

const QString &VVim::Register::read()
{
    if (isSelectionRegister()) {
        // Update from system clipboard.
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData->hasText()) {
            m_value = mimeData->text();
        } else {
            m_value.clear();
        }
    }

    return m_value;
}

void VVim::repeatLastFindMovement(bool p_reverse)
{
    if (!m_lastFindToken.isValid()) {
        return;
    }

    V_ASSERT(m_lastFindToken.isMovement());

    Movement mm = m_lastFindToken.m_movement;
    Key key = m_lastFindToken.m_key;

    V_ASSERT(key.isValid());

    if (p_reverse) {
        switch (mm) {
        case Movement::FindForward:
            mm = Movement::FindBackward;
            break;

        case Movement::FindBackward:
            mm = Movement::FindForward;
            break;

        case Movement::TillForward:
            mm = Movement::TillBackward;
            break;

        case Movement::TillBackward:
            mm = Movement::TillForward;
            break;

        default:
            break;
        }
    }

    tryAddMoveAction();
    addMovementToken(mm, key);
    processCommand(m_tokens);
}

void VVim::message(const QString &p_msg)
{
    if (!p_msg.isEmpty()) {
        qDebug() << "vim msg:" << p_msg;
        emit vimMessage(p_msg);
    }
}

const QMap<QChar, VVim::Register> &VVim::getRegisters() const
{
    return s_registers;
}

const VVim::Marks &VVim::getMarks() const
{
    return m_marks;
}

QChar VVim::getCurrentRegisterName() const
{
    return m_regName;
}

QString VVim::getPendingKeys() const
{
    QString str;
    for (auto const & key : m_pendingKeys) {
        str.append(keyToString(key.m_key, key.m_modifiers));
    }

    return str;
}

void VVim::setCurrentRegisterName(QChar p_reg)
{
    m_regName = p_reg;
}

bool VVim::checkMode(VimMode p_mode)
{
    return m_mode == p_mode;
}

bool VVim::processCommandLine(VVim::CommandLineType p_type, const QString &p_cmd)
{
    setMode(VimMode::Normal);

    bool ret = false;
    switch (p_type) {
    case CommandLineType::Command:
        ret = executeCommand(p_cmd);
        break;

    case CommandLineType::SearchForward:
        // Fall through.
    case CommandLineType::SearchBackward:
    {
        SearchItem item = fetchSearchItem(p_type, p_cmd);
        m_editor->findText(item.m_text, item.m_options, item.m_forward);
        m_searchHistory.addItem(item);
        m_searchHistory.resetIndex();
        break;
    }

    default:
        break;
    }

    return ret;
}

void VVim::processCommandLineChanged(VVim::CommandLineType p_type,
                                     const QString &p_cmd)
{
    setMode(VimMode::Normal);

    if (p_type == CommandLineType::SearchForward
        || p_type == CommandLineType::SearchBackward) {
        // Peek text.
        SearchItem item = fetchSearchItem(p_type, p_cmd);
        m_editor->peekText(item.m_text, item.m_options, item.m_forward);
    }
}

void VVim::processCommandLineCancelled()
{
    m_searchHistory.resetIndex();
    m_editor->clearIncrementalSearchedWordHighlight();
}

VVim::SearchItem VVim::fetchSearchItem(VVim::CommandLineType p_type,
                                       const QString &p_cmd)
{
    Q_ASSERT(p_type == CommandLineType::SearchForward
             || p_type == CommandLineType::SearchBackward);

    SearchItem item;
    item.m_rawStr = p_cmd;
    item.m_text = p_cmd;
    item.m_forward = p_type == CommandLineType::SearchForward;

    if (p_cmd.indexOf("\\C") > -1) {
        item.m_options |= FindOption::CaseSensitive;
        item.m_text.remove("\\C");
    }

    item.m_options |= FindOption::RegularExpression;

    return item;
}

bool VVim::executeCommand(const QString &p_cmd)
{
    bool validCommand = true;
    QString msg;

    Q_ASSERT(m_tokens.isEmpty() && m_keys.isEmpty());
    if (p_cmd.isEmpty()) {
        return true;
    } else if (p_cmd.size() == 1) {
        if (p_cmd == "w") {
            // :w, save current file.
            emit m_editor->object()->saveNote();
            msg = tr("Note has been saved");
        } else if (p_cmd == "q") {
            // :q, quit edit mode.
            emit m_editor->object()->discardAndRead();
            msg = tr("Quit");
        } else if (p_cmd == "x") {
            // :x, save if there is any change and quit edit mode.
            emit m_editor->object()->saveAndRead();
            msg = tr("Quit with note having been saved");
        } else {
            validCommand = false;
        }
    } else if (p_cmd.size() == 2) {
        if (p_cmd == "wq") {
            // :wq, save change and quit edit mode.
            // We treat it same as :x.
            emit m_editor->object()->saveAndRead();
            msg = tr("Quit with note having been saved");
        } else if (p_cmd == "q!") {
            // :q!, discard change and quit edit mode.
            emit m_editor->object()->discardAndRead();
            msg = tr("Quit");
        } else {
            validCommand = false;
        }
    } else if (p_cmd == "nohlsearch" || p_cmd == "noh") {
        // :nohlsearch, clear highlight search.
        clearSearchHighlight();
    } else if (p_cmd.size() > 3 && p_cmd.left(2) == "e ") {
        // :e <file>, open a note in edit mode.
        auto filePath = p_cmd.mid(2).trimmed();

        if(filePath.left(2) == "~/") {
            filePath.remove(0, 1).insert(0, QDir::homePath());
        } else if(filePath.left(2) == "./" || filePath[0] != '/') {
            VDirectory *dir = g_mainWin->getDirectoryTree()->currentDirectory();
            if(filePath.left(2) == "./") {
                filePath.remove(0, 1);
            } else {
                filePath.insert(0, '/');
            }
            filePath.insert(0, dir->fetchPath());
        }

        if(g_mainWin->openFiles(QStringList(filePath), false, OpenFileMode::Edit).size()) {
            msg = tr("Open %1 in edit mode.").arg(filePath);
        } else {
            msg = tr("Unable to open %1").arg(filePath);
        }
    } else {
        validCommand = false;
    }

    if (!validCommand) {
        bool allDigits = true;
        for (int i = 0; i < p_cmd.size(); ++i) {
            if (!p_cmd[i].isDigit()) {
                allDigits = false;
                break;
            }
        }

        // All digits.
        // Jump to a specific line.
        if (allDigits) {
            bool ok;
            int num = p_cmd.toInt(&ok, 10);
            if (num == 0) {
                num = 1;
            }

            if (ok && num > 0) {
                m_tokens.append(Token(num));
                tryAddMoveAction();
                addMovementToken(Movement::LineJump);
                processCommand(m_tokens);
                validCommand = true;
            }
        }
    }

    if (!validCommand) {
        message(tr("Not an editor command: %1").arg(p_cmd));
    } else {
        message(msg);
    }

    return validCommand;
}

bool VVim::hasNonDigitPendingKeys(const QList<Key> &p_keys)
{
    for (auto const &key : p_keys) {
        if (!key.isDigit()) {
            return true;
        }
    }

    return false;
}

bool VVim::hasNonDigitPendingKeys()
{
    return hasNonDigitPendingKeys(m_keys);
}

bool VVim::processLeaderSequence(const Key &p_key)
{
    // Different from Vim:
    // If it is not a valid sequence, we just do nothing here.
    V_ASSERT(checkPendingKey(Key(Qt::Key_Backslash)));
    bool validSequence = true;
    QList<Key> replaySeq;

    if (p_key == Key(Qt::Key_Y)) {
        // <leader>y, "+y
        replaySeq.append(Key(Qt::Key_QuoteDbl, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_Plus, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_Y));
    } else if (p_key == Key(Qt::Key_D)) {
        // <leader>d, "+d
        replaySeq.append(Key(Qt::Key_QuoteDbl, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_Plus, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_D));
    } else if (p_key == Key(Qt::Key_P)) {
        // <leader>p, "+p
        replaySeq.append(Key(Qt::Key_QuoteDbl, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_Plus, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_P));
    } else if (p_key == Key(Qt::Key_P, Qt::ShiftModifier)) {
        // <leader>P, "+P
        replaySeq.append(Key(Qt::Key_QuoteDbl, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_Plus, Qt::ShiftModifier));
        replaySeq.append(Key(Qt::Key_P, Qt::ShiftModifier));
    } else if (p_key == Key(Qt::Key_Space)) {
        // <leader><space>, clear search highlight
        clearSearchHighlight();
    } else if (p_key == Key(Qt::Key_W)) {
        // <leader>w, save note
        emit m_editor->object()->saveNote();
        message(tr("Note has been saved"));
    } else {
        validSequence = false;
    }

    if (!replaySeq.isEmpty()) {
        // Replay the sequence.
        m_replayLeaderSequence = true;
        m_keys.clear();
        for (int i = 0; i < 2; ++i) {
            m_pendingKeys.pop_back();
        }

        for (auto const &key : replaySeq) {
            bool ret = handleKeyPressEvent(key.m_key, key.m_modifiers);
            if (!ret) {
                break;
            }
        }

        m_replayLeaderSequence = false;
    } else {
        resetState();
    }

    return validSequence;
}

VVim::LocationStack::LocationStack(int p_maximum)
    : m_pointer(0), c_maximumLocations(p_maximum < 10 ? 10 : p_maximum)
{
}

bool VVim::LocationStack::hasPrevious() const
{
    return m_pointer > 0;
}

bool VVim::LocationStack::hasNext() const
{
    return m_pointer < m_locations.size() - 1;
}

void VVim::LocationStack::addLocation(const QTextCursor &p_cursor)
{
    int blockNumber = p_cursor.block().blockNumber();
    int pib = p_cursor.positionInBlock();

    // Remove older location with the same block number.
    for (auto it = m_locations.begin(); it != m_locations.end();) {
        if (it->m_blockNumber == blockNumber) {
            it = m_locations.erase(it);
            break;
        } else {
            ++it;
        }
    }

    if (m_locations.size() >= c_maximumLocations) {
        m_locations.removeFirst();
    }

    m_locations.append(Location(blockNumber, pib));

    m_pointer = m_locations.size();

    qDebug() << QString("add location (%1,%2), pointer=%3")
                       .arg(blockNumber).arg(pib).arg(m_pointer);
}

const VVim::Location &VVim::LocationStack::previousLocation(const QTextCursor &p_cursor)
{
    V_ASSERT(hasPrevious());
    if (m_pointer == m_locations.size()) {
        // Add current location to the stack.
        addLocation(p_cursor);

        m_pointer = m_locations.size() - 1;
    }

    // Jump to previous location.
    if (m_pointer > 0) {
        --m_pointer;
    }

    qDebug() << QString("previous location (%1,%2), pointer=%3, size=%4")
                       .arg(m_locations.at(m_pointer).m_blockNumber)
                       .arg(m_locations.at(m_pointer).m_positionInBlock)
                       .arg(m_pointer).arg(m_locations.size());

    return m_locations.at(m_pointer);
}

const VVim::Location &VVim::LocationStack::nextLocation()
{
    V_ASSERT(hasNext());
    ++m_pointer;

    qDebug() << QString("next location (%1,%2), pointer=%3, size=%4")
                       .arg(m_locations.at(m_pointer).m_blockNumber)
                       .arg(m_locations.at(m_pointer).m_positionInBlock)
                       .arg(m_pointer).arg(m_locations.size());

    return m_locations.at(m_pointer);
}

VVim::Marks::Marks()
{
    for (char ch = 'a'; ch <= 'z'; ++ch) {
        m_marks[QChar(ch)] = Mark();
    }
}

void VVim::Marks::setMark(QChar p_name, const QTextCursor &p_cursor)
{
    auto it = m_marks.find(p_name);
    if (it == m_marks.end()) {
        return;
    }

    it->m_location.m_blockNumber = p_cursor.block().blockNumber();
    it->m_location.m_positionInBlock = p_cursor.positionInBlock();
    it->m_text = p_cursor.block().text().trimmed();
    it->m_name = p_name;

    m_lastUsedMark = p_name;

    qDebug() << QString("set mark %1 to (%2,%3)")
                       .arg(p_name)
                       .arg(it->m_location.m_blockNumber)
                       .arg(it->m_location.m_positionInBlock);
}

void VVim::Marks::clearMark(QChar p_name)
{
    auto it = m_marks.find(p_name);
    if (it == m_marks.end()) {
        return;
    }

    it->m_location = Location();
    it->m_text.clear();

    if (m_lastUsedMark == p_name) {
        m_lastUsedMark = QChar();
    }
}

VVim::Location VVim::Marks::getMarkLocation(QChar p_name)
{
    auto it = m_marks.find(p_name);
    if (it == m_marks.end()) {
        return Location();
    }

    if (it->m_location.isValid()) {
        m_lastUsedMark = p_name;
    }

    return it->m_location;
}

const QMap<QChar, VVim::Mark> &VVim::Marks::getMarks() const
{
    return m_marks;
}

QChar VVim::Marks::getLastUsedMark() const
{
    return m_lastUsedMark;
}

void VVim::processTitleJump(const QList<Token> &p_tokens, bool p_forward, int p_relativeLevel)
{
    int repeat = 1;
    if (p_tokens.size() == 1) {
        Token to = p_tokens.first();
        if (to.isRepeat()) {
            repeat = to.m_repeat;
        } else {
            return;
        }
    } else if (!p_tokens.isEmpty()) {
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    if (m_editor->jumpTitle(p_forward, p_relativeLevel, repeat)) {
        // Record current location.
        m_locations.addLocation(cursor);
    }
}

void VVim::SearchHistory::addItem(const SearchItem &p_item)
{
    m_isLastItemForward = p_item.m_forward;
    if (m_isLastItemForward) {
        m_forwardItems.push_back(p_item);
        m_forwardIdx = m_forwardItems.size();
    } else {
        m_backwardItems.push_back(p_item);
        m_backwardIdx = m_forwardItems.size();
    }

    qDebug() << "search history add item" << m_isLastItemForward
             << m_forwardIdx << m_forwardItems.size()
             << m_backwardIdx << m_backwardItems.size();
}

const VVim::SearchItem &VVim::SearchHistory::lastItem() const
{
    if (m_isLastItemForward) {
        Q_ASSERT(!m_forwardItems.isEmpty());
        return m_forwardItems.back();
    } else {
        Q_ASSERT(!m_backwardItems.isEmpty());
        return m_backwardItems.back();
    }
}

const VVim::SearchItem &VVim::SearchHistory::nextItem(bool p_forward)
{
    Q_ASSERT(hasNext(p_forward));
    return p_forward ? m_forwardItems.at(++m_forwardIdx)
                     : m_backwardItems.at(++m_backwardIdx);
}

// Return previous item in the @p_forward stack.
const VVim::SearchItem &VVim::SearchHistory::previousItem(bool p_forward)
{
    Q_ASSERT(hasPrevious(p_forward));
    qDebug() << "previousItem" << p_forward << m_forwardItems.size() << m_backwardItems.size()
             << m_forwardIdx << m_backwardIdx;
    return p_forward ? m_forwardItems.at(--m_forwardIdx)
                     : m_backwardItems.at(--m_backwardIdx);
}

void VVim::SearchHistory::resetIndex()
{
    m_forwardIdx = m_forwardItems.size();
    m_backwardIdx = m_backwardItems.size();
}

QString VVim::getNextCommandHistory(VVim::CommandLineType p_type,
                                    const QString &p_cmd)
{
    Q_UNUSED(p_cmd);
    bool forward = false;
    QString cmd;
    switch (p_type) {
    case CommandLineType::SearchForward:
        forward = true;
        // Fall through.
    case CommandLineType::SearchBackward:
        if (m_searchHistory.hasNext(forward)) {
            return m_searchHistory.nextItem(forward).m_rawStr;
        } else {
            m_searchHistory.resetIndex();
        }

        break;

    default:
        break;
    }

    return cmd;
}

// Get the previous command in history of @p_type. @p_cmd is the current input.
QString VVim::getPreviousCommandHistory(VVim::CommandLineType p_type,
                                        const QString &p_cmd)
{
    Q_UNUSED(p_cmd);
    bool forward = false;
    QString cmd;
    switch (p_type) {
    case CommandLineType::SearchForward:
        forward = true;
        // Fall through.
    case CommandLineType::SearchBackward:
        if (m_searchHistory.hasPrevious(forward)) {
            return m_searchHistory.previousItem(forward).m_rawStr;
        }

        break;

    default:
        break;
    }

    return cmd;
}

void VVim::clearSearchHighlight()
{
    m_editor->clearSearchedWordHighlight();
}

QString VVim::readRegister(int p_key, int p_modifiers)
{
    Key keyInfo(p_key, p_modifiers);
    QChar reg = keyToRegisterName(keyInfo);
    if (!reg.isNull()) {
        return getRegister(reg).read();
    }

    return "";
}

void VVim::amendCursorPosition()
{
    if (checkMode(VimMode::Normal)) {
        QTextCursor cursor = m_editor->textCursorW();
        if (cursor.atBlockEnd() && !cursor.atBlockStart()) {
            // Normal mode and cursor at the end of a non-empty block.
            cursor.movePosition(QTextCursor::PreviousCharacter);
            m_editor->setTextCursorW(cursor);
        }
    }
}

void VVim::handleMousePressed(QMouseEvent *p_event)
{
    Q_UNUSED(p_event);
    if ((checkMode(VimMode::Visual) || checkMode(VimMode::VisualLine))
        && p_event->buttons() != Qt::RightButton) {
        setMode(VimMode::Normal);
    }
}

void VVim::handleMouseDoubleClicked(QMouseEvent *p_event)
{
    Q_UNUSED(p_event);
    if (checkMode(VimMode::Normal)) {
        if (m_editor->textCursorW().hasSelection()) {
            setMode(VimMode::Visual, false);
            maintainSelectionInVisualMode();
        }
    }
}

void VVim::handleMouseMoved(QMouseEvent *p_event)
{
    if (p_event->buttons() != Qt::LeftButton) {
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
    if (cursor.hasSelection()) {
        if (checkMode(VimMode::Normal)) {
            int pos = cursor.position();
            int anchor = cursor.anchor();
            QTextBlock block = cursor.document()->findBlock(anchor);
            if (anchor > 0 && anchor == block.position() + block.length() - 1) {
                // Move anchor left.
                cursor.setPosition(anchor - 1);
                cursor.setPosition(pos, QTextCursor::KeepAnchor);
                m_editor->setTextCursorW(cursor);
            }

            setMode(VimMode::Visual, false);
            maintainSelectionInVisualMode();
        } else if (checkMode(VimMode::Visual)) {
            // We need to assure we always select the character on m_positionBeforeVisualMode.
            maintainSelectionInVisualMode();
        }
    } else if (checkMode(VimMode::Visual)) {
        // User move cursor in Visual mode. Now the cursor and anchor
        // are at the same position.
        maintainSelectionInVisualMode();
    }
}

void VVim::handleMouseReleased(QMouseEvent *p_event)
{
    if (checkMode(VimMode::Normal) && p_event->button() == Qt::LeftButton) {
        QTextCursor cursor = m_editor->textCursorW();
        if (cursor.hasSelection()) {
            return;
        }

        amendCursorPosition();
    }
}

void VVim::setCursorBlockMode(VEditor *p_cursor, CursorBlock p_mode)
{
    p_cursor->setCursorBlockModeW(p_mode);
}

bool VVim::useLeftSideOfCursor(const QTextCursor &p_cursor)
{
    if (!checkMode(VimMode::Visual)) {
        return false;
    }

    Q_ASSERT(m_positionBeforeVisualMode >= 0);
    return p_cursor.position() > m_positionBeforeVisualMode;
}

bool VVim::checkEnterNormalMode(int p_key, int p_modifiers)
{
    if (p_key == Qt::Key_Escape) {
        return true;
    }

    if (!VUtils::isControlModifierForVim(p_modifiers)) {
        return false;
    }

    if (p_key == Qt::Key_BracketLeft) {
        return true;
    }

    if (p_key == Qt::Key_C && !g_config->getVimExemptionKeys().contains('c')) {
        return true;
    }

    return false;
}

void VVim::clearSelectionAndEnterNormalMode()
{
    int position = -1;
    if (checkMode(VimMode::Visual)) {
        QTextCursor cursor = m_editor->textCursorW();
        if (cursor.position() > cursor.anchor()) {
            position = cursor.position() - 1;
        }
    }

    bool ret = clearSelection();
    if (!ret && checkMode(VimMode::Normal)) {
        emit m_editor->object()->requestCloseFindReplaceDialog();
    }

    setMode(VimMode::Normal, true, position);
}

bool VVim::isKeyShouldBeIgnored(int p_key) const
{
    if (VUtils::isMetaKey(p_key)) {
        return true;
    }

    return false;
}
