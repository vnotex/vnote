#include "vvim.h"
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QString>
#include <QScrollBar>
#include <QDebug>
#include "vedit.h"

VVim::VVim(VEdit *p_editor)
    : QObject(p_editor), m_editor(p_editor),
      m_editConfig(&p_editor->getConfig()), m_mode(VimMode::Normal),
      m_resetPositionInBlock(true)
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

    // We will add key to m_keys. If all m_keys can combined to a token, add
    // a new token to m_tokens, clear m_keys and try to process m_tokens.
    switch (key) {
    // Ctrl and Shift may be sent out first.
    case Qt::Key_Control:
        // Fall through.
    case Qt::Key_Shift:
    {
        goto accept;
    }

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
                if (m_tokens.isEmpty()) {
                    // Move.
                    m_tokens.append(Token(Action::Move));
                }

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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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
                if (m_tokens.isEmpty()) {
                    // Move.
                    m_tokens.append(Token(Action::Move));
                }

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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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

            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

            m_tokens.append(Token(mm));
            processCommand(m_tokens);
            resetPositionInBlock = false;
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
                expandSelectionInVisualLineMode(cursor);
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
            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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

            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

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

            if (m_tokens.isEmpty()) {
                // Move.
                m_tokens.append(Token(Action::Move));
            }

            m_tokens.append(Token(mm));
            processCommand(m_tokens);
        }

        break;
    }

    default:
        break;
    }

clear_accept:
    m_keys.clear();
    m_tokens.clear();

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

    qDebug() << "process tokens of size" << p_tokens.size();
    for (int i = 0; i < p_tokens.size(); ++i) {
        qDebug() << "token" << i << p_tokens[i].toString();
    }

    Token act = p_tokens.takeFirst();
    switch (act.m_action) {
    case Action::Move:
        processMoveAction(p_tokens);
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
            if (p_tokens.isEmpty()) {
                // Move.
                p_tokens.append(Token(Action::Move));
            }

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
    QTextDocument *doc = m_editor->document();
    if (m_resetPositionInBlock) {
        positionInBlock = cursor.positionInBlock();
    }

    bool hasMoved = false;
    QTextCursor::MoveMode moveMode = (m_mode == VimMode::Visual
                                      || m_mode == VimMode::VisualLine)
                                     ? QTextCursor::KeepAnchor
                                     : QTextCursor::MoveAnchor;
    switch (mvToken.m_movement) {
    case Movement::Left:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int pib = cursor.positionInBlock();
        repeat = qMin(pib, repeat);

        if (repeat > 0) {
            cursor.movePosition(QTextCursor::Left, moveMode, repeat);
            positionInBlock = cursor.positionInBlock();
            hasMoved = true;
        }

        break;
    }

    case Movement::Right:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int pib = cursor.positionInBlock();
        int length = cursor.block().length();
        if (length - pib <= repeat) {
            repeat = length - pib - 1;
        }

        if (repeat > 0) {
            cursor.movePosition(QTextCursor::Right, moveMode, repeat);
            positionInBlock = cursor.positionInBlock();
            hasMoved = true;
        }

        break;
    }

    case Movement::Up:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        repeat = qMin(cursor.block().blockNumber(), repeat);

        if (repeat > 0) {
            cursor.movePosition(QTextCursor::PreviousBlock, moveMode, repeat);
            if (positionInBlock > 0) {
                setCursorPositionInBlock(cursor, positionInBlock, moveMode);
            }

            hasMoved = true;
        }

        break;
    }

    case Movement::Down:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int blockCount = m_editor->document()->blockCount();
        repeat = qMin(blockCount - 1 - cursor.block().blockNumber(), repeat);

        if (repeat > 0) {
            cursor.movePosition(QTextCursor::NextBlock, moveMode, repeat);
            if (positionInBlock > 0) {
                setCursorPositionInBlock(cursor, positionInBlock, moveMode);
            }

            hasMoved = true;
        }

        break;
    }

    case Movement::VisualUp:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        cursor.movePosition(QTextCursor::Up, moveMode, repeat);
        hasMoved = true;
        break;
    }

    case Movement::VisualDown:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        cursor.movePosition(QTextCursor::Down, moveMode, repeat);
        hasMoved = true;
        break;
    }

    case Movement::PageUp:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int blockStep = blockCountOfPageStep() * repeat;
        int block = cursor.block().blockNumber();
        block = qMax(0, block - blockStep);
        cursor.setPosition(doc->findBlockByNumber(block).position(), moveMode);

        setCursorPositionInBlock(cursor, positionInBlock, moveMode);
        hasMoved = true;

        break;
    }

    case Movement::PageDown:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int blockStep = blockCountOfPageStep() * repeat;
        int block = cursor.block().blockNumber();
        block = qMin(block + blockStep, doc->blockCount() - 1);
        cursor.setPosition(doc->findBlockByNumber(block).position(), moveMode);

        setCursorPositionInBlock(cursor, positionInBlock, moveMode);
        hasMoved = true;

        break;
    }

    case Movement::HalfPageUp:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int blockStep = blockCountOfPageStep();
        int halfBlockStep = qMax(blockStep / 2, 1);
        blockStep = repeat * halfBlockStep;
        int block = cursor.block().blockNumber();
        block = qMax(0, block - blockStep);
        cursor.setPosition(doc->findBlockByNumber(block).position(), moveMode);

        setCursorPositionInBlock(cursor, positionInBlock, moveMode);
        hasMoved = true;

        break;
    }

    case Movement::HalfPageDown:
    {
        if (repeat == -1) {
            repeat = 1;
        }

        int blockStep = blockCountOfPageStep();
        int halfBlockStep = qMax(blockStep / 2, 1);
        blockStep = repeat * halfBlockStep;
        int block = cursor.block().blockNumber();
        block = qMin(block + blockStep, doc->blockCount() - 1);
        cursor.setPosition(doc->findBlockByNumber(block).position(), moveMode);

        setCursorPositionInBlock(cursor, positionInBlock, moveMode);
        hasMoved = true;

        break;
    }

    case Movement::StartOfLine:
    {
        Q_ASSERT(repeat == -1);

        // Start of the Line (block).
        cursor.movePosition(QTextCursor::StartOfBlock, moveMode, 1);
        hasMoved = true;
        break;
    }

    case Movement::EndOfLine:
    {
        // End of line (block).
        if (repeat == -1) {
            repeat = 1;
        } else if (repeat > 1) {
            // Move down (repeat-1) blocks.
            cursor.movePosition(QTextCursor::NextBlock, moveMode, repeat - 1);
        }

        // Move to the end of block.
        cursor.movePosition(QTextCursor::EndOfBlock, moveMode, 1);
        hasMoved = true;
        break;
    }

    case Movement::FirstCharacter:
    {
        // repeat is not considered in this command.
        // If all the block is space, just move to the end of block; otherwise,
        // move to the first non-space character.
        moveCursorFirstNonSpaceCharacter(cursor, moveMode);
        hasMoved = true;
        break;
    }

    case Movement::LineJump:
    {
        // Jump to the first non-space character of @repeat line (block).
        V_ASSERT(repeat > 0);

        // @repeat starts from 1 while block number starts from 0.
        QTextBlock block = doc->findBlockByNumber(repeat - 1);
        if (block.isValid()) {
            cursor.setPosition(block.position(), moveMode);
        } else {
            // Go beyond the document.
            cursor.movePosition(QTextCursor::End, moveMode, 1);
        }

        moveCursorFirstNonSpaceCharacter(cursor, moveMode);
        hasMoved = true;
        break;
    }

    case Movement::StartOfDocument:
    {
        // Jump to the first non-space character of the start of the document.
        V_ASSERT(repeat == -1);
        cursor.movePosition(QTextCursor::Start, moveMode, 1);
        moveCursorFirstNonSpaceCharacter(cursor, moveMode);
        hasMoved = true;
        break;
    }

    case Movement::EndOfDocument:
    {
        // Jump to the first non-space character of the end of the document.
        V_ASSERT(repeat == -1);
        cursor.movePosition(QTextCursor::End, moveMode, 1);
        moveCursorFirstNonSpaceCharacter(cursor, moveMode);
        hasMoved = true;
        break;
    }

    case Movement::WordForward:
    {
        // Go to the start of next word.
        if (repeat == -1) {
            repeat = 1;
        }

        cursor.movePosition(QTextCursor::NextWord, moveMode, repeat);
        hasMoved = true;
        break;
    }

    case Movement::WORDForward:
    {
        // Go to the start of next WORD.
        if (repeat == -1) {
            repeat = 1;
        }

        for (int i = 0; i < repeat; ++i) {
            int start, end;
            // [start, end] is current WORD.
            findCurrentWORD(cursor, start, end);

            // Move cursor to end of current WORD.
            cursor.setPosition(end, moveMode);

            // Skip spaces.
            moveCursorAcrossSpaces(cursor, moveMode, true);
        }

        hasMoved = true;
        break;
    }

    case Movement::ForwardEndOfWord:
    {
        // Go to the end of current word or next word.
        if (repeat == -1) {
            repeat = 1;
        }

        int pos = cursor.position();
        // First move to the end of current word.
        cursor.movePosition(QTextCursor::EndOfWord, moveMode, 1);
        if (pos != cursor.position()) {
            // We did move.
            repeat -= 1;
        }

        if (repeat) {
            cursor.movePosition(QTextCursor::NextWord, moveMode, repeat);
            cursor.movePosition(QTextCursor::EndOfWord, moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::ForwardEndOfWORD:
    {
        // Go to the end of current WORD or next WORD.
        if (repeat == -1) {
            repeat = 1;
        }

        for (int i = 0; i < repeat; ++i) {
            // Skip spaces.
            moveCursorAcrossSpaces(cursor, moveMode, true);

            int start, end;
            // [start, end] is current WORD.
            findCurrentWORD(cursor, start, end);

            // Move cursor to the end of current WORD.
            cursor.setPosition(end, moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::WordBackward:
    {
        // Go to the start of previous word or current word.
        if (repeat == -1) {
            repeat = 1;
        }

        int pos = cursor.position();
        // first move to the start of current word.
        cursor.movePosition(QTextCursor::StartOfWord, moveMode, 1);
        if (pos != cursor.position()) {
            // We did move.
            repeat -= 1;
        }

        if (repeat) {
            cursor.movePosition(QTextCursor::PreviousWord, moveMode, repeat);
        }

        hasMoved = true;
        break;
    }

    case Movement::WORDBackward:
    {
        // Go to the start of previous WORD or current WORD.
        if (repeat == -1) {
            repeat = 1;
        }

        for (int i = 0; i < repeat; ++i) {
            // Skip Spaces.
            moveCursorAcrossSpaces(cursor, moveMode, false);

            int start, end;
            // [start, end] is current WORD.
            findCurrentWORD(cursor, start, end);

            // Move cursor to the start of current WORD.
            cursor.setPosition(start, moveMode);
        }

        hasMoved = true;
        break;
    }

    case Movement::BackwardEndOfWord:
    {
        // Go to the end of previous word.
        if (repeat == -1) {
            repeat = 1;
        }

        int pib = cursor.positionInBlock();
        if (!(pib > 0 && cursor.block().text()[pib -1].isSpace())) {
            ++repeat;
        }

        cursor.movePosition(QTextCursor::PreviousWord, moveMode, repeat);
        cursor.movePosition(QTextCursor::EndOfWord, moveMode, 1);
        hasMoved = true;
        break;
    }

    case Movement::BackwardEndOfWORD:
    {
        // Go to the end of previous WORD.
        if (repeat == -1) {
            repeat = 1;
        }

        for (int i = 0; i < repeat; ++i) {
            int start, end;
            findCurrentWORD(cursor, start, end);

            cursor.setPosition(start, moveMode);

            moveCursorAcrossSpaces(cursor, moveMode, false);
        }

        hasMoved = true;
        break;
    }

    default:
        break;
    }

    if (hasMoved) {
        expandSelectionInVisualLineMode(cursor);
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

void VVim::expandSelectionInVisualLineMode(QTextCursor &p_cursor)
{
    if (m_mode != VimMode::VisualLine) {
        return;
    }

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
