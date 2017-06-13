#include "vvim.h"
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QString>
#include <QScrollBar>
#include <QDebug>
#include "vedit.h"
#include "utils/veditutils.h"

const QChar VVim::c_unnamedRegister = QChar('"');
const QChar VVim::c_blackHoleRegister = QChar('_');
const QChar VVim::c_selectionRegister = QChar('+');

VVim::VVim(VEdit *p_editor)
    : QObject(p_editor), m_editor(p_editor),
      m_editConfig(&p_editor->getConfig()), m_mode(VimMode::Normal),
      m_resetPositionInBlock(true), m_register(c_unnamedRegister)
{
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

// Move @p_cursor to the first non-space character of current block.
// Need to setTextCursor() after calling this.
static void moveCursorFirstNonSpaceCharacter(QTextCursor &p_cursor,
                                             QTextCursor::MoveMode p_mode)
{
    QTextBlock block = p_cursor.block();
    QString text = block.text();
    int idx = 0;
    for (; idx < text.size(); ++idx) {
        if (text[idx].isSpace()) {
            continue;
        } else {
            break;
        }
    }

    p_cursor.setPosition(block.position() + idx, p_mode);
}

// Find the start and end of the WORD @p_cursor locates in (within a single block).
// @p_start and @p_end will be the global position of the start and end of the WORD.
// @p_start will equals to @p_end if @p_cursor is a space.
static void findCurrentWORD(const QTextCursor &p_cursor, int &p_start, int &p_end)
{
    QTextBlock block = p_cursor.block();
    QString text = block.text();
    int pib = p_cursor.positionInBlock();

    // Find the start.
    p_start = p_end = -1;
    for (int i = pib - 1; i >= 0; --i) {
        if (text[i].isSpace()) {
            ++i;
            p_start = i;
            break;
        }
    }

    if (p_start == -1) {
        p_start = 0;
    }

    // Find the end.
    for (int i = pib; i < text.size(); ++i) {
        if (text[i].isSpace()) {
            p_end = i;
            break;
        }
    }

    if (p_end == -1) {
        p_end = block.length() - 1;
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

bool VVim::handleKeyPressEvent(QKeyEvent *p_event)
{
    bool ret = false;
    int modifiers = p_event->modifiers();
    int key = p_event->key();
    bool resetPositionInBlock = true;
    Key keyInfo(key, modifiers);

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
            m_register = reg;
            m_registers[reg].m_append = (modifiers == Qt::ShiftModifier);

            qDebug() << "use register" << reg << m_registers[reg].m_append;

            goto accept;
        }

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
            // Enter Insert mode.
            if (m_mode == VimMode::Normal) {
                setMode(VimMode::Insert);
            }
        } else if (modifiers == Qt::ShiftModifier) {
            QTextCursor cursor = m_editor->textCursor();
            if (m_mode == VimMode::Normal) {
                // Insert at the first non-space character.
                moveCursorFirstNonSpaceCharacter(cursor, QTextCursor::MoveAnchor);
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
        if (modifiers == Qt::NoModifier) {
            // Insert a new block under current block and enter insert mode.
            if (m_mode == VimMode::Normal) {
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::EndOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
                cursor.insertBlock();
                m_editor->setTextCursor(cursor);
                setMode(VimMode::Insert);
            }
        } else if (modifiers == Qt::ShiftModifier) {
            // Insert a new block above current block and enter insert mode.
            if (m_mode == VimMode::Normal) {
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::StartOfBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
                cursor.insertBlock();
                cursor.movePosition(QTextCursor::PreviousBlock,
                                    QTextCursor::MoveAnchor,
                                    1);
                m_editor->setTextCursor(cursor);
                setMode(VimMode::Insert);
            }
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
                if (getActionToken()->m_action == Action::Delete) {
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
                    deleteSelectedText(m_mode == VimMode::VisualLine);
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
            tryGetRepeatToken(m_keys, m_tokens);
            if (!m_keys.isEmpty()) {
                // Not a valid sequence.
                break;
            }

            // w, go to the start of next word.
            Movement mm = Movement::WordForward;
            if (modifiers == Qt::ShiftModifier) {
                // W, go to the start of next WORD.
                mm = Movement::WORDForward;
            }

            tryAddMoveAction();
            m_tokens.append(Token(mm));
            processCommand(m_tokens);
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
    m_register = c_unnamedRegister;
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
                                    moveMode, mvToken.m_movement, repeat);

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

bool VVim::processMovement(QTextCursor &p_cursor, const QTextDocument *p_doc,
                           QTextCursor::MoveMode &p_moveMode,
                           Movement p_movement, int p_repeat)
{
    bool hasMoved = false;

    switch (p_movement) {
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
        moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
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

        moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        hasMoved = true;
        break;
    }

    case Movement::StartOfDocument:
    {
        // Jump to the first non-space character of the start of the document.
        V_ASSERT(p_repeat == -1);
        p_cursor.movePosition(QTextCursor::Start, p_moveMode, 1);
        moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
        hasMoved = true;
        break;
    }

    case Movement::EndOfDocument:
    {
        // Jump to the first non-space character of the end of the document.
        V_ASSERT(p_repeat == -1);
        p_cursor.movePosition(QTextCursor::End, p_moveMode, 1);
        moveCursorFirstNonSpaceCharacter(p_cursor, p_moveMode);
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
        return;
    }

    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *doc = m_editor->document();
    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = QTextCursor::KeepAnchor;

    if (to.isRange()) {
        switch (to.m_range) {
        case Range::Line:
        {
            // dd, Delete current line.
            if (repeat == -1) {
                repeat = 1;
            }

            QString deletedText;

            cursor.beginEditBlock();
            for (int i = 0; i < repeat; ++i) {
                QString tmp;
                int blockNum = cursor.block().blockNumber();
                VEditUtils::removeBlock(cursor, &tmp);
                deletedText += tmp;
                if (blockNum > cursor.block().blockNumber()) {
                    // The last block.
                    break;
                }
            }

            cursor.endEditBlock();

            saveToRegister(deletedText);

            hasMoved = true;

            break;
        }

        case Range::Word:
            break;

        default:
            return;
        }

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
    hasMoved = processMovement(cursor, doc, moveMode, to.m_movement, repeat);
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
    for (char ch = 'a'; ch > 'z'; ++ch) {
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
    for (auto const &token : m_tokens) {
        if (token.isAction()) {
            return true;
        }
    }

    return false;
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

    for (auto const &token : m_tokens) {
        if (token.isAction()) {
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

void VVim::deleteSelectedText(bool p_clearEmptyBlock)
{
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        cursor.beginEditBlock();
        deleteSelectedText(cursor, p_clearEmptyBlock);
        cursor.endEditBlock();
        m_editor->setTextCursor(cursor);
    }
}

void VVim::deleteSelectedText(QTextCursor &p_cursor, bool p_clearEmptyBlock)
{
    if (p_cursor.hasSelection()) {
        QString deletedText = p_cursor.selectedText();
        p_cursor.removeSelectedText();
        if (p_clearEmptyBlock && p_cursor.block().length() == 1) {
            deletedText += "\n";
            VEditUtils::removeBlock(p_cursor);
        }

        saveToRegister(deletedText);
    }
}

void VVim::saveToRegister(const QString &p_text)
{
    qDebug() << QString("save text(%1) to register(%2)").arg(p_text).arg(m_register);

    Register &reg = m_registers[m_register];
    if (reg.isNamedRegister() && reg.m_append) {
        // Append to current register.
        reg.m_value += p_text;
    } else {
        reg.m_value = p_text;
    }

    if (!reg.isBlackHoleRegister() && !reg.isUnnamedRegister()) {
        // Save it to unnamed register.
        m_registers[c_unnamedRegister].m_value = reg.m_value;
    }
}
