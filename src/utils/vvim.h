#ifndef VVIM_H
#define VVIM_H

#include <QObject>
#include <QString>
#include <QTextCursor>
#include "vutils.h"

class VEdit;
class QKeyEvent;
class VEditConfig;
class QKeyEvent;

enum class VimMode {
    Normal = 0,
    Insert,
    Visual,
    VisualLine,
    Replace,
    Invalid
};

class VVim : public QObject
{
    Q_OBJECT
public:
    explicit VVim(VEdit *p_editor);

    // Handle key press event.
    // Returns true if the event is consumed and need no more handling.
    bool handleKeyPressEvent(QKeyEvent *p_event);

    // Return current mode.
    VimMode getMode() const;

    // Set current mode.
    void setMode(VimMode p_mode);

signals:
    // Emit when current mode has been changed.
    void modeChanged(VimMode p_mode);

private slots:
    // When user use mouse to select texts in Normal mode, we should change to
    // Visual mode.
    void selectionToVisualMode(bool p_hasText);

private:
    // Struct for a key press.
    struct Key
    {
        Key(int p_key, int p_modifiers = Qt::NoModifier)
            : m_key(p_key), m_modifiers(p_modifiers)
        {
        }

        int m_key;
        int m_modifiers;

        bool isDigit() const
        {
            return m_key >= Qt::Key_0
                   && m_key <= Qt::Key_9
                   && m_modifiers == Qt::NoModifier;
        }

        int toDigit() const
        {
            V_ASSERT(isDigit());
            return m_key - Qt::Key_0;
        }

        bool operator==(const Key &p_key) const
        {
            return p_key.m_key == m_key && p_key.m_modifiers == m_modifiers;
        }
    };

    // Supported actions.
    enum class Action
    {
        Move = 0,
        Delete,
        Copy,
        Paste,
        Change,
        Indent,
        UnIndent,
        ToUpper,
        ToLower,
        Invalid
    };

    // Supported movements.
    enum class Movement
    {
        Left = 0,
        Right,
        Up,
        Down,
        VisualUp,
        VisualDown,
        PageUp,
        PageDown,
        HalfPageUp,
        HalfPageDown,
        StartOfLine,
        EndOfLine,
        FirstCharacter,
        LineJump,
        StartOfDocument,
        EndOfDocument,
        WordForward,
        WORDForward,
        ForwardEndOfWord,
        ForwardEndOfWORD,
        WordBackward,
        WORDBackward,
        BackwardEndOfWord,
        BackwardEndOfWORD,
        Invalid
    };

    // Supported ranges.
    enum class Range
    {
        Line = 0,
        Word,
        Invalid
    };

    enum class TokenType { Action = 0, Repeat, Movement, Range, Invalid };

    struct Token
    {
        Token(Action p_action)
            : m_type(TokenType::Action), m_action(p_action) {}

        Token(int p_repeat)
            : m_type(TokenType::Repeat), m_repeat(p_repeat) {}

        Token(Movement p_movement)
            : m_type(TokenType::Movement), m_movement(p_movement) {}

        Token(Range p_range)
            : m_type(TokenType::Range), m_range(p_range) {}

        Token() : m_type(TokenType::Invalid) {}

        bool isRepeat() const
        {
            return m_type == TokenType::Repeat;
        }

        bool isAction() const
        {
            return m_type == TokenType::Action;
        }

        bool isMovement() const
        {
            return m_type == TokenType::Movement;
        }

        bool isRange() const
        {
            return m_type == TokenType::Range;
        }

        QString toString() const
        {
            QString str;
            switch (m_type) {
            case TokenType::Action:
                str = QString("action %1").arg((int)m_action);
                break;

            case TokenType::Repeat:
                str = QString("repeat %1").arg(m_repeat);
                break;

            case TokenType::Movement:
                str = QString("movement %1").arg((int)m_movement);
                break;

            case TokenType::Range:
                str = QString("range %1").arg((int)m_range);
                break;

            default:
                str = "invalid";
            }

            return str;
        }

        TokenType m_type;

        union
        {
            Action m_action;
            int m_repeat;
            Movement m_movement;
            Range m_range;
        };
    };

    // Reset all key state info.
    void resetState();

    // Now m_tokens constitute a command. Execute it.
    // Will clear @p_tokens.
    void processCommand(QList<Token> &p_tokens);

    // Return the number represented by @p_keys.
    // Return -1 if @p_keys is not a valid digit sequence.
    int numberFromKeySequence(const QList<Key> &p_keys);

    // Try to generate a Repeat token from @p_keys and insert it to @p_tokens.
    // If succeed, clear @p_keys and return true.
    bool tryGetRepeatToken(QList<Key> &p_keys, QList<Token> &p_tokens);

    // @p_tokens is the arguments of the Action::Move action.
    void processMoveAction(QList<Token> &p_tokens);

    // Clear selection if there is any.
    // Returns true if there is selection.
    bool clearSelection();

    // Get the block count of one page step in vertical scroll bar.
    int blockCountOfPageStep() const;

    // Expand selection in the VisualLiine mode which will change the position
    // of @p_cursor.
    void expandSelectionInVisualLineMode(QTextCursor &p_cursor);

    VEdit *m_editor;
    const VEditConfig *m_editConfig;
    VimMode m_mode;
    QList<Key> m_keys;
    QList<Token> m_tokens;

    // Whether reset the position in block when moving cursor.
    bool m_resetPositionInBlock;
};

#endif // VVIM_H
