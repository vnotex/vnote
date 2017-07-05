#ifndef VVIM_H
#define VVIM_H

#include <QObject>
#include <QString>
#include <QTextCursor>
#include <QMap>
#include <QDebug>
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

    // Struct for a location.
    struct Location
    {
        Location() : m_blockNumber(-1), m_positionInBlock(0)
        {
        }

        Location(int p_blockNumber, int p_positionInBlock)
            : m_blockNumber(p_blockNumber), m_positionInBlock(p_positionInBlock)
        {
        }

        bool isValid() const
        {
            return m_blockNumber > -1;
        }

        // Block number of the location, based on 0.
        int m_blockNumber;

        // Position in block, based on 0.
        int m_positionInBlock;
    };

    struct Register
    {
        Register(QChar p_name, const QString &p_value)
            : m_name(p_name), m_value(p_value), m_append(false)
        {
        }

        Register(QChar p_name)
            : m_name(p_name), m_append(false)
        {
        }

        Register()
            : m_append(false)
        {
        }

        // Register a-z.
        bool isNamedRegister() const
        {
            char ch = m_name.toLatin1();
            return ch >= 'a' && ch <= 'z';
        }

        bool isUnnamedRegister() const
        {
            return m_name == c_unnamedRegister;
        }

        bool isBlackHoleRegister() const
        {
            return m_name == c_blackHoleRegister;
        }

        bool isSelectionRegister() const
        {
            return m_name == c_selectionRegister;
        }

        bool isBlock() const
        {
            return m_value.endsWith('\n');
        }

        // @p_value is the content to update.
        // If @p_value ends with \n, then it is a block.
        // When @p_value is a block, we need to add \n at the end if necessary.
        // If @m_append is true and @p_value is a block, we need to add \n between
        // them if necessary.
        void update(const QString &p_value);

        // Read the value of this register.
        const QString &read();

        QChar m_name;
        QString m_value;

        // This is not info of Register itself, but a hint to the handling logics
        // whether we need to append the content to this register.
        // Only valid for a-z registers.
        bool m_append;
    };

    struct Mark
    {
        QChar m_name;
        Location m_location;
        QString m_text;
    };

    // We only support simple local marks a-z.
    class Marks
    {
    public:
        Marks();

        // Set mark @p_name to point to @p_cursor.
        void setMark(QChar p_name, const QTextCursor &p_cursor);

        void clearMark(QChar p_name);

        // Return the location of mark @p_name.
        Location getMarkLocation(QChar p_name);

        const QMap<QChar, VVim::Mark> &getMarks() const;

        QChar getLastUsedMark() const;

    private:
        QMap<QChar, Mark> m_marks;

        QChar m_lastUsedMark;
    };

    // Handle key press event.
    // @p_autoIndentPos: the cursor position of last auto indent.
    // Returns true if the event is consumed and need no more handling.
    bool handleKeyPressEvent(QKeyEvent *p_event, int *p_autoIndentPos = NULL);

    // Return current mode.
    VimMode getMode() const;

    // Set current mode.
    void setMode(VimMode p_mode, bool p_clearSelection = true);

    // Set current register.
    void setRegister(QChar p_reg);

    // Get m_registers.
    const QMap<QChar, Register> &getRegisters() const;

    QChar getCurrentRegisterName() const;

    // Get pending keys.
    // Turn m_pendingKeys to a string.
    QString getPendingKeys() const;

    // Get m_marks.
    const VVim::Marks &getMarks() const;

signals:
    // Emit when current mode has been changed.
    void modeChanged(VimMode p_mode);

    // Emit when VVim want to display some messages.
    void vimMessage(const QString &p_msg);

    // Emit when current status updated.
    void vimStatusUpdated(const VVim *p_vim);

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

        Key() : m_key(-1), m_modifiers(Qt::NoModifier) {}

        int m_key;
        int m_modifiers;

        bool isDigit() const
        {
            return m_key >= Qt::Key_0
                   && m_key <= Qt::Key_9
                   && (m_modifiers == Qt::NoModifier || m_modifiers == Qt::KeypadModifier);
        }

        int toDigit() const
        {
            V_ASSERT(isDigit());
            return m_key - Qt::Key_0;
        }

        bool isAlphabet() const
        {
            return m_key >= Qt::Key_A
                   && m_key <= Qt::Key_Z
                   && (m_modifiers == Qt::NoModifier || m_modifiers == Qt::ShiftModifier);
        }

        QChar toAlphabet() const
        {
            V_ASSERT(isAlphabet());
            if (m_modifiers == Qt::NoModifier) {
                return QChar('a' + (m_key - Qt::Key_A));
            } else {
                return QChar('A' + (m_key - Qt::Key_A));
            }
        }

        bool isValid() const
        {
            return m_key > -1 && m_modifiers > -1;
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
        PasteBefore,
        Change,
        Indent,
        UnIndent,
        ToUpper,
        ToLower,
        ReverseCase,
        Undo,
        Redo,
        RedrawAtTop,
        RedrawAtCenter,
        RedrawAtBottom,
        JumpPreviousLocation,
        JumpNextLocation,
        Replace,
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
        FindForward,
        FindBackward,
        TillForward,
        TillBackward,
        MarkJump,
        MarkJumpLine,
        FindPair,
        Invalid
    };

    // Supported ranges.
    enum class Range
    {
        Line = 0,
        WordInner,
        WordAround,
        WORDInner,
        WORDAround,
        QuoteInner,
        QuoteAround,
        DoubleQuoteInner,
        DoubleQuoteAround,
        BackQuoteInner,
        BackQuoteAround,
        ParenthesisInner,
        ParenthesisAround,
        BracketInner,
        BracketAround,
        AngleBracketInner,
        AngleBracketAround,
        BraceInner,
        BraceAround,
        Invalid
    };

    enum class TokenType { Action = 0, Repeat, Movement, Range, Key, Invalid };

    struct Token
    {
        Token(Action p_action)
            : m_type(TokenType::Action), m_action(p_action) {}

        Token(int p_repeat)
            : m_type(TokenType::Repeat), m_repeat(p_repeat) {}

        Token(Movement p_movement)
            : m_type(TokenType::Movement), m_movement(p_movement) {}

        Token(Movement p_movement, Key p_key)
            : m_type(TokenType::Movement), m_movement(p_movement), m_key(p_key) {}

        Token(Range p_range)
            : m_type(TokenType::Range), m_range(p_range) {}

        Token(Key p_key)
            : m_type(TokenType::Key), m_key(p_key) {}

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

        bool isKey() const
        {
            return m_type == TokenType::Key;
        }

        bool isValid() const
        {
            return m_type != TokenType::Invalid;
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

            case TokenType::Key:
                str = QString("key %1 %2").arg(m_key.m_key).arg(m_key.m_modifiers);
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
            Range m_range;
            Movement m_movement;
        };

        // Used in some Movement and Key Token.
        Key m_key;
    };

    // Stack for all the jump locations.
    // When we execute a jump action, we push current location to the stack and
    // remove older location with the same block number.
    // Ctrl+O is also a jum action. If m_pointer points to the top of the stack,
    // Ctrl+O will insert a location to the stack.
    class LocationStack
    {
    public:
        LocationStack(int p_maximum = 100);

        // Add @p_cursor's location to stack.
        // Need to delete all older locations with the same block number.
        void addLocation(const QTextCursor &p_cursor);

        // Go up through the stack. Need to add current location if we are at
        // the top of the stack currently.
        const Location &previousLocation(const QTextCursor &p_cursor);

        // Go down through the stack.
        const Location &nextLocation();

        bool hasPrevious() const;

        bool hasNext() const;

    private:
        // A stack containing locations.
        QList<Location> m_locations;

        // Pointer to current element in the stack.
        // If we are not in the history of the locations, it points to the next
        // element to the top element.
        int m_pointer;

        // Maximum number of locations in stack.
        const int c_maximumLocations;
    };

    // Returns true if the event is consumed and need no more handling.
    bool handleKeyPressEvent(int key, int modifiers, int *p_autoIndentPos = NULL);

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

    // @p_tokens is the arguments of the Action::Delete action.
    void processDeleteAction(QList<Token> &p_tokens);

    // @p_tokens is the arguments of the Action::Copy action.
    void processCopyAction(QList<Token> &p_tokens);

    // @p_tokens is the arguments of the Action::Paste and Action::PasteBefore action.
    void processPasteAction(QList<Token> &p_tokens, bool p_pasteBefore);

    // @p_tokens is the arguments of the Action::Change action.
    void processChangeAction(QList<Token> &p_tokens);

    // @p_tokens is the arguments of the Action::Indent and Action::UnIndent action.
    void processIndentAction(QList<Token> &p_tokens, bool p_isIndent);

    // @p_tokens is the arguments of the Action::ToLower and Action::ToUpper action.
    void processToLowerAction(QList<Token> &p_tokens, bool p_toLower);

    // @p_tokens is the arguments of the Action::Undo action.
    void processUndoAction(QList<Token> &p_tokens);

    // @p_tokens is the arguments of the Action::Redo action.
    void processRedoAction(QList<Token> &p_tokens);

    // @p_tokens is the arguments of the Action::RedrawAtBottom/RedrawAtCenter/RedrawAtTop action.
    // @p_dest: 0 for top, 1 for center, 2 for bottom.
    void processRedrawLineAction(QList<Token> &p_tokens, int p_dest);

    // Action::JumpPreviousLocation and Action::JumpNextLocation action.
    void processJumpLocationAction(QList<Token> &p_tokens, bool p_next);

    // Action::Replace.
    void processReplaceAction(QList<Token> &p_tokens);

    // Action::ReverseCase.
    void processReverseCaseAction(QList<Token> &p_tokens);

    // Clear selection if there is any.
    // Returns true if there is selection.
    bool clearSelection();

    // Get the block count of one page step in vertical scroll bar.
    int blockCountOfPageStep() const;

    // Expand selection to whole lines which will change the position
    // of @p_cursor.
    void expandSelectionToWholeLines(QTextCursor &p_cursor);

    // Init m_registers.
    // Currently supported registers:
    // a-z, A-Z (append to a-z), ", +, _
    void initRegisters();

    // Check m_keys to see if we are expecting a register name.
    bool expectingRegisterName() const;

    // Check m_keys to see if we are expecting a target for f/t/F/T command.
    bool expectingCharacterTarget() const;

    // Check m_keys to see if we are expecting a character to replace with.
    bool expectingReplaceCharacter() const;

    // Check if we are in command line mode.
    bool expectingCommandLineInput() const;

    // Check if we are in a leader sequence.
    bool expectingLeaderSequence() const;

    // Check m_keys to see if we are expecting a mark name to create a mark.
    bool expectingMarkName() const;

    // Check m_keys to see if we are expecting a mark name as the target.
    bool expectingMarkTarget() const;

    // Return the corresponding register name of @p_key.
    // If @p_key is not a valid register name, return a NULL QChar.
    QChar keyToRegisterName(const Key &p_key) const;

    // Check if @m_tokens contains an action token.
    bool hasActionToken() const;

    // Check if @m_tokens contains a repeat token.
    bool hasRepeatToken() const;

    // Try to add an Action::Move action at the front if there is no any action
    // token.
    void tryAddMoveAction();

    // Add an Action token in front of m_tokens.
    void addActionToken(Action p_action);

    // Get the action token from m_tokens.
    const Token *getActionToken() const;

    // Get the repeat token from m_tokens.
    Token *getRepeatToken();

    // Add a Range token at the end of m_tokens.
    void addRangeToken(Range p_range);

    // Add a Movement token at the end of m_tokens.
    void addMovementToken(Movement p_movement);

    // Add a Movement token at the end of m_tokens.
    void addMovementToken(Movement p_movement, Key p_key);

    // Add a Key token at the end of m_tokens.
    void addKeyToken(Key p_key);

    // Delete selected text if there is any.
    // @p_clearEmptyBlock: whether to remove the empty block after deletion.
    void deleteSelectedText(QTextCursor &p_cursor, bool p_clearEmptyBlock);

    // Copy selected text if there is any.
    // Will clear selection.
    // @p_addNewLine: whether to add a new line \n to the selection.
    void copySelectedText(bool p_addNewLine);

    void copySelectedText(QTextCursor &p_cursor, bool p_addNewLine);

    // Convert the case of selected text if there is any.
    // Will clear selection.
    // @p_toLower: to lower or upper.
    void convertCaseOfSelectedText(QTextCursor &p_cursor, bool p_toLower);

    // Save @p_text to the Register pointed by m_regName.
    // Remove QChar::ObjectReplacementCharacter before saving.
    void saveToRegister(const QString &p_text);

    // Move @p_cursor according to @p_moveMode and @p_token.
    // Return true if it has moved @p_cursor.
    bool processMovement(QTextCursor &p_cursor,
                         QTextCursor::MoveMode p_moveMode,
                         const Token &p_token,
                         int p_repeat);

    // Move @p_cursor according to @p_moveMode and @p_range.
    // Return true if it has moved @p_cursor.
    bool selectRange(QTextCursor &p_cursor, const QTextDocument *p_doc,
                     Range p_range, int p_repeat);

    // Check if there is an Action token with Delete/Copy/Change action.
    bool hasActionTokenValidForTextObject() const;

    // Check if m_keys only contains @p_key.
    bool checkPendingKey(const Key &p_key) const;

    // Check if m_tokens only contains action token @p_action.
    bool checkActionToken(Action p_action) const;

    // Repeat m_lastFindToken.
    void repeatLastFindMovement(bool p_reverse);

    void message(const QString &p_str);

    // Check if m_mode equals to p_mode.
    bool checkMode(VimMode p_mode);

    // In command line mode, read input @p_key and process it.
    // Returns true if a command has been completed, otherwise returns false.
    bool processCommandLine(const Key &p_key);

    // Execute command specified by m_keys.
    // @p_keys does not contain the leading colon.
    // Following commands are supported:
    // :w, :wq, :q, :q!, :x
    void executeCommand();

    // Check if m_keys has non-digit key.
    bool hasNonDigitPendingKeys();

    bool hasNonDigitPendingKeys(const QList<Key> &p_keys);

    // Reading a leader sequence, read input @p_key and process it.
    // Returns true if a sequence has been replayed or it is being read,
    // otherwise returns false.
    // Following sequences are supported:
    // y: "+y
    // d: "+d
    // p: "+p
    // P: "+P
    bool processLeaderSequence(const Key &p_key);

    // Jump across titles.
    // [[, ]], [], ][, [{, ]}.
    void processTitleJump(const QList<Token> &p_tokens, bool p_forward, int p_relativeLevel);

    VEdit *m_editor;
    const VEditConfig *m_editConfig;
    VimMode m_mode;

    // A valid command token should follow the rule:
    // Action, Repeat, Movement.
    // Action, Repeat, Range.
    // Action, Repeat.
    QList<Key> m_keys;
    QList<Token> m_tokens;

    // Keys for status indication.
    QList<Key> m_pendingKeys;

    // Whether reset the position in block when moving cursor.
    bool m_resetPositionInBlock;

    QMap<QChar, Register> m_registers;

    // Currently used register.
    QChar m_regName;

    // Last f/F/t/T Token.
    Token m_lastFindToken;

    // Whether in command line mode.
    bool m_cmdMode;

    // The leader key, which is Key_Space by default.
    Key m_leaderKey;

    // Whether we are parsing a leader sequence.
    // We will map a leader sequence to another actual sequence. When replaying
    // this actual sequence, m_leaderSequence will be true.
    bool m_replayLeaderSequence;

    LocationStack m_locations;

    Marks m_marks;

    static const QChar c_unnamedRegister;
    static const QChar c_blackHoleRegister;
    static const QChar c_selectionRegister;
};

#endif // VVIM_H
