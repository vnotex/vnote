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
#include "vconfigmanager.h"
#include "vedit.h"
#include "utils/veditutils.h"

extern VConfigManager vconfig;

const QChar VVim::c_unnamedRegister = QChar('"');
const QChar VVim::c_blackHoleRegister = QChar('_');
const QChar VVim::c_selectionRegister = QChar('+');

VVim::VVim(VEdit *p_editor)
    : QObject(p_editor), m_editor(p_editor),
      m_editConfig(&p_editor->getConfig()), m_mode(VimMode::Normal),
      m_resetPositionInBlock(true), m_regName(c_unnamedRegister)
{
    initRegisters();

    connect(m_editor, &VEdit::copyAvailable,
            this, &VVim::selectionToVisualMode);
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

// Find the start and end of the word @p_cursor locates in (within a single block).
// @p_start and @p_end will be the global position of the start and end of the word.
// @p_start will equals to @p_end if @p_cursor is a space.
static void findCurrentWord(QTextCursor p_cursor, int &p_start, int &p_end)
{
    QString text = p_cursor.block().text();
    int pib = p_cursor.positionInBlock();

    if (pib < text.size() && text[pib].isSpace()) {
        p_start = p_end = p_cursor.position();
        return;
    }

    p_cursor.movePosition(QTextCursor::StartOfWord);
    p_start = p_cursor.position();
    p_cursor.movePosition(QTextCursor::EndOfWord);
    p_end = p_cursor.position();
}

// Find the start and end of the WORD @p_cursor locates in (within a single block).
// @p_start and @p_end will be the global position of the start and end of the WORD.
// @p_start will equals to @p_end if @p_cursor is a space.
// Attention: www|sss will select www, which is different from findCurrentWord().
static void findCurrentWORD(const QTextCursor &p_cursor, int &p_start, int &p_end)
{
    QTextBlock block = p_cursor.block();
    QString text = block.text();
    int pib = p_cursor.positionInBlock();

    if (pib < text.size() && text[pib].isSpace()) {
        p_start = p_end = p_cursor.position();
        return;
    }

    // Find the start.
    p_start = 0;
    for (int i = pib - 1; i >= 0; --i) {
        if (text[i].isSpace()) {
            p_start = i + 1;
            break;
        }
    }

    // Find the end.
    p_end = block.length() - 1;
    for (int i = pib; i < text.size(); ++i) {
        if (text[i].isSpace()) {
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
                                   bool p_forward)
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

            if (pib == text.size()) {
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

            if (idx == -1) {
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
void expandSelectionAcrossSpacesWithinBlock(QTextCursor &p_cursor)
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
        p_cursor.insertBlock();
        p_cursor.movePosition(QTextCursor::PreviousBlock);
    }

    if (vconfig.getAutoIndent()) {
        VEditUtils::indentBlockAsPreviousBlock(p_cursor);
    }
}

bool VVim::handleKeyPressEvent(QKeyEvent *p_event)
{
    bool ret = false;
    int modifiers = p_event->modifiers();
    int key = p_event->key();
    bool resetPositionInBlock = true;
    Key keyInfo(key, modifiers);
    bool unindent = false;

    // Handle Insert mode key press.
    if (VimMode::Insert == m_mode) {
        if (key == Qt::Key_Escape
            || (key == Qt::Key_BracketLeft && modifiers == Qt::ControlModifier)) {
            // Clear selection and enter Normal mode.
            clearSelection();

            setMode(VimMode::Normal);
            goto clear_accept;
        }

        // Let it be handled outside VVim.
        goto exit;
    }

    // Ctrl and Shift may be sent out first.
    if (key == Qt::Key_Control || key == Qt::Key_Shift) {
        goto accept;
    }

    if (expectingRegisterName()) {
        // Expecting a register name.
        QChar reg = keyToRegisterName(keyInfo);
        if (!reg.isNull()) {
            resetState();
            m_regName = reg;
            if (m_registers[reg].isNamedRegister()) {
                m_registers[reg].m_append = (modifiers == Qt::ShiftModifier);
            } else {
                Q_ASSERT(!m_registers[reg].m_append);
            }

            goto accept;
        }

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

        V_ASSERT(mm != Movement::Invalid);

        tryAddMoveAction();
        addMovementToken(mm, keyInfo);
        m_lastFindToken = m_tokens.last();
        processCommand(m_tokens);

        goto clear_accept;
    }

    // We will add key to m_keys. If all m_keys can combined to a token, add
    // a new token to m_tokens, clear m_keys and try to process m_tokens.
    switch (key) {
    case Qt::Key_0:
    {
        if (modifiers == Qt::NoModifier) {
            if (!m_keys.isEmpty()) {
                // Repeat.
                V_ASSERT(m_keys.last().isDigit());

                m_keys.append(keyInfo);
                resetPositionInBlock = false;
                goto accept;
            } else {
                // StartOfLine.
                tryAddMoveAction();

                m_tokens.append(Token(Movement::StartOfLine));

                processCommand(m_tokens);
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
        if (modifiers == Qt::NoModifier) {
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
        if (modifiers == Qt::NoModifier) {
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

            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
        }

        break;
    }

    case Qt::Key_I:
    {
        if (modifiers == Qt::NoModifier) {
            if (hasActionTokenValidForTextObject()) {
                // Inner text object.
                if (!m_keys.isEmpty()) {
                    // Invalid sequence;
                    break;
                }

                m_keys.append(keyInfo);
                goto accept;
            }

            // Enter Insert mode.
            if (m_mode == VimMode::Normal) {
                setMode(VimMode::Insert);
            }
        } else if (modifiers == Qt::ShiftModifier) {
            QTextCursor cursor = m_editor->textCursor();
            if (m_mode == VimMode::Normal) {
                // Insert at the first non-space character.
                VEditUtils::moveCursorFirstNonSpaceCharacter(cursor, QTextCursor::MoveAnchor);
            } else if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                // Insert at the start of line.
                cursor.movePosition(QTextCursor::StartOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
            }

            m_editor->setTextCursor(cursor);
            setMode(VimMode::Insert);
        }

        break;
    }

    case Qt::Key_A:
    {
        if (modifiers == Qt::NoModifier) {
            if (hasActionTokenValidForTextObject()) {
                // Around text object.
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
                QTextCursor cursor = m_editor->textCursor();
                V_ASSERT(!cursor.hasSelection());

                if (!cursor.atBlockEnd()) {
                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
                    m_editor->setTextCursor(cursor);
                }

                setMode(VimMode::Insert);
            }
        } else if (modifiers == Qt::ShiftModifier) {
            // Insert at the end of line.
            QTextCursor cursor = m_editor->textCursor();
            if (m_mode == VimMode::Normal) {
                cursor.movePosition(QTextCursor::EndOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
                m_editor->setTextCursor(cursor);
            } else if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                if (!cursor.atBlockEnd()) {
                    cursor.clearSelection();
                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
                    m_editor->setTextCursor(cursor);
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
                QTextCursor cursor = m_editor->textCursor();
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

                if (vconfig.getAutoIndent()) {
                    VEditUtils::indentBlockAsPreviousBlock(cursor);
                    if (vconfig.getAutoList()) {
                        VEditUtils::insertListMarkAsPreviousBlock(cursor);
                    }
                }

                cursor.endEditBlock();
                m_editor->setTextCursor(cursor);

                setMode(VimMode::Insert);
            }

            break;
        }

        break;
    }

    case Qt::Key_S:
    {
        if (modifiers == Qt::NoModifier) {
            // 1. If there is selection, delete the selected text.
            // 2. Otherwise, if cursor is not at the end of block, delete the
            //    character after current cursor.
            // 3. Enter Insert mode.
            QTextCursor cursor = m_editor->textCursor();
            if (cursor.hasSelection() || !cursor.atBlockEnd()) {
                cursor.deleteChar();
                m_editor->setTextCursor(cursor);
            }

            setMode(VimMode::Insert);
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
        if (modifiers == Qt::ControlModifier) {
            // Ctrl+B, page up, fall through.
            modifiers = Qt::NoModifier;
        } else if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
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
        if (modifiers == Qt::ControlModifier) {
            // Ctrl+U, HalfPageUp.
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            Movement mm = Movement::HalfPageUp;
            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
        } else if (m_keys.isEmpty() && m_tokens.isEmpty() && modifiers == Qt::NoModifier) {
            // u, Undo.
            break;
        } else {
            bool toLower = modifiers == Qt::NoModifier;
            tryGetRepeatToken(m_keys, m_tokens);
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
                    QTextCursor cursor = m_editor->textCursor();
                    cursor.beginEditBlock();
                    // Different from Vim:
                    // If there is no selection in Visual mode, we do nothing.
                    convertCaseOfSelectedText(cursor, toLower);
                    cursor.endEditBlock();
                    m_editor->setTextCursor(cursor);
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
        if (modifiers == Qt::ControlModifier) {
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
                if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                    QTextCursor cursor = m_editor->textCursor();
                    cursor.beginEditBlock();
                    // Different from Vim:
                    // If there is no selection in Visual mode, we do nothing.
                    if (cursor.hasSelection()) {
                        deleteSelectedText(cursor, m_mode == VimMode::VisualLine);
                    } else if (m_mode == VimMode::VisualLine) {
                        VEditUtils::removeBlock(cursor);
                        saveToRegister("\n");
                    }

                    cursor.endEditBlock();
                    m_editor->setTextCursor(cursor);
                    setMode(VimMode::Normal);
                    break;
                }

                addActionToken(Action::Delete);
                goto accept;
            }
        } else if (modifiers == Qt::ShiftModifier) {
            tryGetRepeatToken(m_keys, m_tokens);
            if (!hasActionToken() && m_mode == VimMode::Normal) {
                // D, same as d$.
                addActionToken(Action::Delete);
                addMovementToken(Movement::EndOfLine);
                processCommand(m_tokens);
            }

            break;
        }

        break;
    }

    // Should be kept together with Qt::Key_Escape.
    case Qt::Key_BracketLeft:
    {
        if (modifiers == Qt::ControlModifier) {
            // fallthrough.
        } else {
            break;
        }

    }

    case Qt::Key_Escape:
    {
        // Clear selection and enter normal mode.
        clearSelection();

        setMode(VimMode::Normal);
        break;
    }

    case Qt::Key_V:
    {
        if (modifiers == Qt::NoModifier) {
            // Toggle Visual Mode.
            clearSelection();
            VimMode mode = VimMode::Visual;
            if (m_mode == VimMode::Visual) {
                mode = VimMode::Normal;
            }

            setMode(mode);
        } else if (modifiers == Qt::ShiftModifier) {
            // Visual Line Mode.
            clearSelection();
            VimMode mode = VimMode::VisualLine;
            if (m_mode == VimMode::VisualLine) {
                mode = VimMode::Normal;
            }

            setMode(mode);

            if (m_mode == VimMode::VisualLine) {
                QTextCursor cursor = m_editor->textCursor();
                expandSelectionToWholeLines(cursor);
                m_editor->setTextCursor(cursor);
            }
        }

        break;
    }

    case Qt::Key_AsciiCircum:
    {
        if (modifiers == Qt::ShiftModifier) {
            // ~, go to first non-space character of current line (block).
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
            if (checkPendingKey(Key(Qt::Key_I)) || checkPendingKey(Key(Qt::Key_A))) {
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

            tryAddMoveAction();
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
            // Specify a register.
            if (!m_keys.isEmpty() || !m_tokens.isEmpty()) {
                // Invalid sequence.
                break;
            }

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
            if (!m_keys.isEmpty()) {
                break;
            }

            if (!hasActionToken()) {
                addActionToken(Action::Delete);
                addMovementToken(modifiers == Qt::ShiftModifier ? Movement::Left
                                                                : Movement::Right);
                processCommand(m_tokens);
            }

            break;
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
                if (checkActionToken(Action::Copy)) {
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
            if (!hasActionToken() && m_mode == VimMode::Normal) {
                // Y, same as yy.
                addRangeToken(Range::Line);
                processCommand(m_tokens);
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
                    break;
                } else {
                    // An invalid sequence.
                    break;
                }
            } else {
                // The first c, an action.
                if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                    QTextCursor cursor = m_editor->textCursor();
                    int pos = cursor.selectionStart();
                    cursor.beginEditBlock();
                    // Different from Vim:
                    // If there is no selection in Visual mode, we do nothing.
                    if (cursor.hasSelection()) {
                        deleteSelectedText(cursor, m_mode == VimMode::VisualLine);
                        if (m_mode == VimMode::VisualLine) {
                            insertChangeBlockAfterDeletion(cursor, pos);
                        }
                    } else if (m_mode == VimMode::VisualLine) {
                        saveToRegister("\n");
                    }

                    cursor.endEditBlock();
                    m_editor->setTextCursor(cursor);
                    setMode(VimMode::Insert);
                    break;
                }

                addActionToken(Action::Change);
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

            break;
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
                } else {
                    // An invalid sequence.
                    break;
                }
            } else {
                // The first >/<, an Action.
                if (m_mode == VimMode::Visual || m_mode == VimMode::VisualLine) {
                    QTextCursor cursor = m_editor->textCursor();
                    VEditUtils::indentSelectedBlocks(m_editor->document(),
                                                     cursor,
                                                     m_editConfig->m_tabSpaces,
                                                     !unindent);
                    setMode(VimMode::Normal);
                    break;
                }

                addActionToken(unindent ? Action::UnIndent : Action::Indent);
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

    default:
        break;
    }

clear_accept:
    resetState();

accept:
    p_event->accept();
    ret = true;

exit:
    m_resetPositionInBlock = resetPositionInBlock;
    return ret;
}

void VVim::resetState()
{
    m_keys.clear();
    m_tokens.clear();
    m_regName = c_unnamedRegister;
    m_resetPositionInBlock = true;
}

VimMode VVim::getMode() const
{
    return m_mode;
}

void VVim::setMode(VimMode p_mode)
{
    if (m_mode != p_mode) {
        clearSelection();
        m_mode = p_mode;
        resetState();

        emit modeChanged(m_mode);
    }
}

void VVim::processCommand(QList<Token> &p_tokens)
{
    if (p_tokens.isEmpty()) {
        return;
    }

    V_ASSERT(p_tokens.at(0).isAction());

    for (int i = 0; i < p_tokens.size(); ++i) {
        qDebug() << "token" << i << p_tokens[i].toString();
    }

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
        processIndentAction(p_tokens, true);
        break;

    case Action::UnIndent:
        processIndentAction(p_tokens, false);
        break;

    case Action::ToLower:
        processToLowerAction(p_tokens, true);
        break;

    case Action::ToUpper:
        processToLowerAction(p_tokens, false);
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

    QTextCursor cursor = m_editor->textCursor();
    if (m_resetPositionInBlock) {
        positionInBlock = cursor.positionInBlock();
    }

    QTextCursor::MoveMode moveMode = (m_mode == VimMode::Visual
                                      || m_mode == VimMode::VisualLine)
                                     ? QTextCursor::KeepAnchor
                                     : QTextCursor::MoveAnchor;
    bool hasMoved = processMovement(cursor, m_editor->document(),
                                    moveMode, mvToken, repeat);

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

        if (m_mode == VimMode::VisualLine) {
            expandSelectionToWholeLines(cursor);
        }

        m_editor->setTextCursor(cursor);
    }
}

#define ADDKEY(x, y) case (x): {ch = (y); break;}

// Returns NULL QChar if invalid.
static QChar keyToChar(int p_key, int p_modifiers)
{
    if (p_modifiers == Qt::ControlModifier) {
        return QChar();
    }

    if (p_key >= Qt::Key_0 && p_key <= Qt::Key_9) {
        return QChar('0' + (p_key - Qt::Key_0));
    } else if (p_key >= Qt::Key_A && p_key <= Qt::Key_Z) {
        if (p_modifiers == Qt::ShiftModifier) {
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

bool VVim::processMovement(QTextCursor &p_cursor, const QTextDocument *p_doc,
                           QTextCursor::MoveMode p_moveMode,
                           const Token &p_token, int p_repeat)
{
    V_ASSERT(p_token.isMovement());

    bool hasMoved = false;
    bool inclusive = true;
    bool forward = true;

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

        int blockCount = p_doc->blockCount();
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
        p_cursor.setPosition(p_doc->findBlockByNumber(block).position(), p_moveMode);
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
        block = qMin(block + blockStep, p_doc->blockCount() - 1);
        p_cursor.setPosition(p_doc->findBlockByNumber(block).position(), p_moveMode);
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
        p_cursor.setPosition(p_doc->findBlockByNumber(block).position(), p_moveMode);
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
        block = qMin(block + blockStep, p_doc->blockCount() - 1);
        p_cursor.setPosition(p_doc->findBlockByNumber(block).position(), p_moveMode);
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
        hasMoved = true;
        break;
    }

    case Movement::LineJump:
    {
        // Jump to the first non-space character of @p_repeat line (block).
        V_ASSERT(p_repeat > 0);

        // @p_repeat starts from 1 while block number starts from 0.
        QTextBlock block = p_doc->findBlockByNumber(p_repeat - 1);
        if (block.isValid()) {
            p_cursor.setPosition(block.position(), p_moveMode);
        } else {
            // Go beyond the document.
            p_cursor.movePosition(QTextCursor::End, p_moveMode, 1);
        }

        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        hasMoved = true;
        break;
    }

    case Movement::StartOfDocument:
    {
        // Jump to the first non-space character of the start of the document.
        V_ASSERT(p_repeat == -1);
        p_cursor.movePosition(QTextCursor::Start, p_moveMode, 1);
        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        hasMoved = true;
        break;
    }

    case Movement::EndOfDocument:
    {
        // Jump to the first non-space character of the end of the document.
        V_ASSERT(p_repeat == -1);
        p_cursor.movePosition(QTextCursor::End, p_moveMode, 1);
        VEditUtils::moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        hasMoved = true;
        break;
    }

    case Movement::WordForward:
    {
        // Go to the start of next word.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        p_cursor.movePosition(QTextCursor::NextWord, p_moveMode, p_repeat);
        hasMoved = true;
        break;
    }

    case Movement::WORDForward:
    {
        // Go to the start of next WORD.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        for (int i = 0; i < p_repeat; ++i) {
            int start, end;
            // [start, end] is current WORD.
            findCurrentWORD(p_cursor, start, end);

            // Move cursor to end of current WORD.
            p_cursor.setPosition(end, p_moveMode);

            // Skip spaces.
            moveCursorAcrossSpaces(p_cursor, p_moveMode, true);
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
        // First move to the end of current word.
        p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode, 1);
        if (pos != p_cursor.position()) {
            // We did move.
            p_repeat -= 1;
        }

        if (p_repeat) {
            p_cursor.movePosition(QTextCursor::NextWord, p_moveMode, p_repeat);
            p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode);
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

        for (int i = 0; i < p_repeat; ++i) {
            // Skip spaces.
            moveCursorAcrossSpaces(p_cursor, p_moveMode, true);

            int start, end;
            // [start, end] is current WORD.
            findCurrentWORD(p_cursor, start, end);

            // Move cursor to the end of current WORD.
            p_cursor.setPosition(end, p_moveMode);
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
        p_cursor.movePosition(QTextCursor::StartOfWord, p_moveMode, 1);
        if (pos != p_cursor.position()) {
            // We did move.
            p_repeat -= 1;
        }

        if (p_repeat) {
            p_cursor.movePosition(QTextCursor::PreviousWord, p_moveMode, p_repeat);
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

        for (int i = 0; i < p_repeat; ++i) {
            // Skip Spaces.
            moveCursorAcrossSpaces(p_cursor, p_moveMode, false);

            int start, end;
            // [start, end] is current WORD.
            findCurrentWORD(p_cursor, start, end);

            // Move cursor to the start of current WORD.
            p_cursor.setPosition(start, p_moveMode);
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

        int pib = p_cursor.positionInBlock();
        if (!(pib > 0 && p_cursor.block().text()[pib -1].isSpace())) {
            ++p_repeat;
        }

        p_cursor.movePosition(QTextCursor::PreviousWord, p_moveMode, p_repeat);
        p_cursor.movePosition(QTextCursor::EndOfWord, p_moveMode, 1);
        hasMoved = true;
        break;
    }

    case Movement::BackwardEndOfWORD:
    {
        // Go to the end of previous WORD.
        if (p_repeat == -1) {
            p_repeat = 1;
        }

        for (int i = 0; i < p_repeat; ++i) {
            int start, end;
            findCurrentWORD(p_cursor, start, end);

            p_cursor.setPosition(start, p_moveMode);

            moveCursorAcrossSpaces(p_cursor, p_moveMode, false);
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
        const Key &key = p_token.m_key;
        QChar target = keyToChar(key.m_key, key.m_modifiers);
        if (!target.isNull()) {
            hasMoved = VEditUtils::findTargetWithinBlock(p_cursor, p_moveMode,
                                                         target, forward, inclusive);
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

    Q_UNUSED(p_doc);

    switch (p_range) {
    case Range::Line:
    {
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
        findCurrentWord(p_cursor, start, end);

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
            findCurrentWORD(p_cursor, start, end);
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
                        findCurrentWORD(p_cursor, start, end);

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

    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *doc = m_editor->document();
    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        cursor.beginEditBlock();
        hasMoved = selectRange(cursor, doc, to.m_range, repeat);
        bool around = false;
        if (hasMoved) {
            switch (to.m_range) {
            case Range::Line:
            {
                // dd, delete current line.
                if (repeat == -1) {
                    repeat = 1;
                }

                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, true);
                } else {
                    VEditUtils::removeBlock(cursor);
                }

                qDebug() << "delete" << repeat << "lines";
                break;
            }

            case Range::WordAround:
                around = true;
                // Fall through.
            case Range::WordInner:
            {
                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, false);
                }

                qDebug() << "delete" << (around ? "around" : "inner") << "word";
                break;
            }

            case Range::WORDAround:
                around = true;
                // Fall through.
            case Range::WORDInner:
            {
                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, false);
                }

                qDebug() << "delete" << (around ? "around" : "inner") << "WORD";
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
    hasMoved = processMovement(cursor, doc, moveMode, to, repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (hasMoved) {
        bool clearEmptyBlock = false;
        switch (to.m_movement) {
        case Movement::Left:
        {
            qDebug() << "delete backward" << repeat << "chars";
            break;
        }

        case Movement::Right:
        {
            qDebug() << "delete forward" << repeat << "chars";
            break;
        }

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

        case Movement::VisualUp:
        {
            qDebug() << "delete visual up" << repeat << "lines";
            break;
        }

        case Movement::VisualDown:
        {
            qDebug() << "delete visual down" << repeat << "lines";
            break;
        }

        case Movement::StartOfLine:
        {
            qDebug() << "delete till start of line";
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

        case Movement::FirstCharacter:
        {
            qDebug() << "delete till first non-space character";
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

        case Movement::WordForward:
        {
            qDebug() << "delete" << repeat << "words forward";
            break;
        }

        case Movement::WORDForward:
        {
            qDebug() << "delete" << repeat << "WORDs forward";
            break;
        }

        case Movement::ForwardEndOfWord:
        {
            qDebug() << "delete" << repeat << "end of words forward";
            break;
        }

        case Movement::ForwardEndOfWORD:
        {
            qDebug() << "delete" << repeat << "end of WORDs forward";
            break;
        }

        case Movement::WordBackward:
        {
            qDebug() << "delete" << repeat << "words backward";
            break;
        }

        case Movement::WORDBackward:
        {
            qDebug() << "delete" << repeat << "WORDs backward";
            break;
        }

        case Movement::BackwardEndOfWord:
        {
            qDebug() << "delete" << repeat << "end of words backward";
            break;
        }

        case Movement::BackwardEndOfWORD:
        {
            qDebug() << "delete" << repeat << "end of WORDs backward";
            break;
        }

        default:
            break;
        }

        deleteSelectedText(cursor, clearEmptyBlock);
    }

    cursor.endEditBlock();

exit:
    if (hasMoved) {
        m_editor->setTextCursor(cursor);
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

    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *doc = m_editor->document();
    int oriPos = cursor.position();
    bool changed = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        cursor.beginEditBlock();
        changed = selectRange(cursor, doc, to.m_range, repeat);
        bool around = false;
        if (changed) {
            switch (to.m_range) {
            case Range::Line:
            {
                // yy, delete current line.
                if (repeat == -1) {
                    repeat = 1;
                }

                if (cursor.hasSelection()) {
                    copySelectedText(cursor, true);
                } else {
                    saveToRegister("\n");
                }

                qDebug() << "copy" << repeat << "lines";
                break;
            }

            case Range::WordAround:
                around = true;
                // Fall through.
            case Range::WordInner:
            {
                if (cursor.hasSelection()) {
                    copySelectedText(cursor, false);
                }

                qDebug() << "copy" << (around ? "around" : "inner") << "word";
                break;
            }

            case Range::WORDAround:
                around = true;
                // Fall through.
            case Range::WORDInner:
            {
                if (cursor.hasSelection()) {
                    copySelectedText(cursor, false);
                }

                qDebug() << "copy" << (around ? "around" : "inner") << "WORD";
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
    changed = processMovement(cursor, doc, moveMode, to, repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (changed) {
        bool addNewLine = false;
        switch (to.m_movement) {
        case Movement::Left:
        {
            qDebug() << "copy backward" << repeat << "chars";
            break;
        }

        case Movement::Right:
        {
            qDebug() << "copy forward" << repeat << "chars";
            break;
        }

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

        case Movement::VisualUp:
        {
            qDebug() << "copy visual up" << repeat << "lines";
            break;
        }

        case Movement::VisualDown:
        {
            qDebug() << "copy visual down" << repeat << "lines";
            break;
        }

        case Movement::StartOfLine:
        {
            qDebug() << "copy till start of line";
            break;
        }

        case Movement::EndOfLine:
        {
            // End of line (block).
            // Do not need to add new line even if repeat > 1.
            qDebug() << "copy till end of" << repeat << "line";
            break;
        }

        case Movement::FirstCharacter:
        {
            qDebug() << "copy till first non-space character";
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

        case Movement::WordForward:
        {
            qDebug() << "copy" << repeat << "words forward";
            break;
        }

        case Movement::WORDForward:
        {
            qDebug() << "copy" << repeat << "WORDs forward";
            break;
        }

        case Movement::ForwardEndOfWord:
        {
            qDebug() << "copy" << repeat << "end of words forward";
            break;
        }

        case Movement::ForwardEndOfWORD:
        {
            qDebug() << "copy" << repeat << "end of WORDs forward";
            break;
        }

        case Movement::WordBackward:
        {
            qDebug() << "copy" << repeat << "words backward";
            break;
        }

        case Movement::WORDBackward:
        {
            qDebug() << "copy" << repeat << "WORDs backward";
            break;
        }

        case Movement::BackwardEndOfWord:
        {
            qDebug() << "copy" << repeat << "end of words backward";
            break;
        }

        case Movement::BackwardEndOfWORD:
        {
            qDebug() << "copy" << repeat << "end of WORDs backward";
            break;
        }

        default:
            break;
        }

        copySelectedText(cursor, addNewLine);
        if (cursor.position() != oriPos) {
            cursor.setPosition(oriPos);
        }
    }

    cursor.endEditBlock();

exit:
    if (changed) {
        m_editor->setTextCursor(cursor);
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

    Register &reg = m_registers[m_regName];
    QString value = reg.read();
    if (value.isEmpty()) {
        return;
    }

    QString text;
    text.reserve(repeat * value.size() + 1);
    for (int i = 0; i < repeat; ++i) {
        text.append(value);
    }

    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();
    if (reg.isBlock()) {
        if (p_pasteBefore) {
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.insertBlock();
            cursor.movePosition(QTextCursor::PreviousBlock);
        } else {
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
        }

        // inserBlock() already insert a new line, so eliminate one here.
        cursor.insertText(text.left(text.size() - 1));
    } else {
        if (!p_pasteBefore && !cursor.atBlockEnd()) {
            // Insert behind current cursor.
            cursor.movePosition(QTextCursor::Right);
        }

        cursor.insertText(text);
    }

    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);

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

    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *doc = m_editor->document();
    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        cursor.beginEditBlock();
        hasMoved = selectRange(cursor, doc, to.m_range, repeat);
        bool around = false;
        if (hasMoved) {
            int pos = cursor.selectionStart();
            switch (to.m_range) {
            case Range::Line:
            {
                // cc, change current line.
                if (repeat == -1) {
                    repeat = 1;
                }

                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, true);
                    insertChangeBlockAfterDeletion(cursor, pos);
                } else {
                    saveToRegister("\n");
                }

                qDebug() << "change" << repeat << "lines";
                break;
            }

            case Range::WordAround:
                around = true;
                // Fall through.
            case Range::WordInner:
            {
                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, false);
                } else {
                    saveToRegister("\n");
                }

                qDebug() << "delete" << (around ? "around" : "inner") << "word";
                break;
            }

            case Range::WORDAround:
                around = true;
                // Fall through.
            case Range::WORDInner:
            {
                if (cursor.hasSelection()) {
                    deleteSelectedText(cursor, false);
                } else {
                    saveToRegister("\n");
                }

                qDebug() << "delete" << (around ? "around" : "inner") << "WORD";
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
    hasMoved = processMovement(cursor, doc, moveMode, to, repeat);
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
                QTextBlock block = m_editor->document()->lastBlock();
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
        m_editor->setTextCursor(cursor);
    }

    setMode(VimMode::Insert);
}

void VVim::processIndentAction(QList<Token> &p_tokens, bool p_isIndent)
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

    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *doc = m_editor->document();

    if (to.isRange()) {
        selectRange(cursor, doc, to.m_range, repeat);
        switch (to.m_range) {
        case Range::Line:
        {
            // >>/<<, indent/unindent current line.
            if (repeat == -1) {
                repeat = 1;
            }

            VEditUtils::indentSelectedBlocks(doc,
                                             cursor,
                                             m_editConfig->m_tabSpaces,
                                             p_isIndent);
            break;
        }

        case Range::WordAround:
            // Fall through.
        case Range::WordInner:
            // Fall through.
        case Range::WORDAround:
            // Fall through.
        case Range::WORDInner:
        {
            cursor.clearSelection();
            VEditUtils::indentSelectedBlocks(doc,
                                             cursor,
                                             m_editConfig->m_tabSpaces,
                                             p_isIndent);
            break;
        }

        default:
            return;
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

    processMovement(cursor,
                    doc,
                    QTextCursor::KeepAnchor,
                    to,
                    repeat);
    VEditUtils::indentSelectedBlocks(doc,
                                     cursor,
                                     m_editConfig->m_tabSpaces,
                                     p_isIndent);
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

    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *doc = m_editor->document();
    bool changed = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;
    int oriPos = cursor.position();

    if (to.isRange()) {
        cursor.beginEditBlock();
        changed = selectRange(cursor, doc, to.m_range, repeat);
        if (changed) {
            oriPos = cursor.selectionStart();
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
                              doc,
                              moveMode,
                              to,
                              repeat);
    if (repeat == -1) {
        repeat = 1;
    }

    if (changed) {
        oriPos = cursor.selectionStart();

        switch (to.m_movement) {
        case Movement::Left:
        {
            break;
        }

        case Movement::Right:
        {
            break;
        }

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

        case Movement::VisualUp:
        {
            break;
        }

        case Movement::VisualDown:
        {
            break;
        }

        case Movement::StartOfLine:
        {
            break;
        }

        case Movement::EndOfLine:
        {
            break;
        }

        case Movement::FirstCharacter:
        {
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

        case Movement::WordForward:
        {
            break;
        }

        case Movement::WORDForward:
        {
            break;
        }

        case Movement::ForwardEndOfWord:
        {
            break;
        }

        case Movement::ForwardEndOfWORD:
        {
            break;
        }

        case Movement::WordBackward:
        {
            break;
        }

        case Movement::WORDBackward:
        {
            break;
        }

        case Movement::BackwardEndOfWord:
        {
            break;
        }

        case Movement::BackwardEndOfWORD:
        {
            break;
        }

        default:
            break;
        }

        convertCaseOfSelectedText(cursor, p_toLower);

        cursor.setPosition(oriPos);
    }

    cursor.endEditBlock();

exit:
    if (changed) {
        m_editor->setTextCursor(cursor);
    }
}

bool VVim::clearSelection()
{
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        cursor.clearSelection();
        m_editor->setTextCursor(cursor);
        return true;
    }

    return false;
}

int VVim::blockCountOfPageStep() const
{
    int lineCount = m_editor->document()->blockCount();
    QScrollBar *bar = m_editor->verticalScrollBar();
    int steps = (bar->maximum() - bar->minimum() + bar->pageStep());
    int pageLineCount = lineCount * (bar->pageStep() * 1.0 / steps);
    return pageLineCount;
}

void VVim::selectionToVisualMode(bool p_hasText)
{
    if (p_hasText && m_mode == VimMode::Normal) {
        // Enter visual mode.
        setMode(VimMode::Visual);
    }
}

void VVim::expandSelectionToWholeLines(QTextCursor &p_cursor)
{
    QTextDocument *doc = m_editor->document();
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
    m_registers.clear();
    for (char ch = 'a'; ch <= 'z'; ++ch) {
        m_registers[QChar(ch)] = Register(QChar(ch));
    }

    m_registers[c_unnamedRegister] = Register(c_unnamedRegister);
    m_registers[c_blackHoleRegister] = Register(c_blackHoleRegister);
    m_registers[c_selectionRegister] = Register(c_selectionRegister);
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

bool VVim::hasActionTokenValidForTextObject() const
{
    if (hasActionToken()) {
        Action act = m_tokens.first().m_action;
        if (act == Action::Delete
            || act == Action::Copy
            || act == Action::Change
            || act == Action::ToLower
            || act == Action::ToUpper) {
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
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        cursor.beginEditBlock();
        copySelectedText(cursor, p_addNewLine);
        cursor.endEditBlock();
        m_editor->setTextCursor(cursor);
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

    Register &reg = m_registers[m_regName];
    reg.update(text);

    if (!reg.isBlackHoleRegister() && !reg.isUnnamedRegister()) {
        // Save it to unnamed register.
        m_registers[c_unnamedRegister].update(reg.m_value);
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
