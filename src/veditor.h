#ifndef VEDITOR_H
#define VEDITOR_H

#include <QPointer>
#include <QVector>
#include <QList>
#include <QTextEdit>
#include <QColor>

#include "veditconfig.h"
#include "vconstants.h"
#include "vfile.h"
#include "vwordcountinfo.h"

class QWidget;
class VEditorObject;
class VEditOperations;
class QTimer;
class QLabel;
class VVim;
enum class VimMode;
class QMouseEvent;


enum class SelectionId {
    CurrentLine = 0,
    SelectedWord,
    SearchedKeyword,
    SearchedKeywordUnderCursor,
    IncrementalSearchedKeyword,
    TrailingSapce,
    MaxSelection
};


// Abstract class for an edit.
// Should inherit this class as well as QPlainTextEdit or QTextEdit.
// Will replace VEdit eventually.
class VEditor
{
public:
    explicit VEditor(VFile *p_file, QWidget *p_editor);

    virtual ~VEditor();

    void highlightCurrentLine();

    virtual void beginEdit() = 0;

    virtual void endEdit() = 0;

    // Save buffer content to VFile.
    virtual void saveFile() = 0;

    virtual void reloadFile() = 0;

    virtual bool scrollToBlock(int p_blockNumber) = 0;

    bool isModified() const;

    void setModified(bool p_modified);

    // User requests to insert an image.
    void insertImage();

    // User requests to insert a link.
    void insertLink();

    // Used for incremental search.
    // User has enter the content to search, but does not enter the "find" button yet.
    bool peekText(const QString &p_text, uint p_options, bool p_forward = true);

    // If @p_cursor is not null, set the position of @p_cursor instead of current
    // cursor.
    bool findText(const QString &p_text,
                  uint p_options,
                  bool p_forward,
                  QTextCursor *p_cursor = nullptr,
                  QTextCursor::MoveMode p_moveMode = QTextCursor::MoveAnchor,
                  bool p_useLeftSideOfCursor = false);

    void replaceText(const QString &p_text,
                     uint p_options,
                     const QString &p_replaceText,
                     bool p_findNext);

    void replaceTextAll(const QString &p_text,
                        uint p_options,
                        const QString &p_replaceText);

    // Scroll the content to make @p_block visible.
    // If the @p_block is too long to hold in one page, just let it occupy the
    // whole page.
    // Will not change current cursor.
    virtual void makeBlockVisible(const QTextBlock &p_block) = 0;

    // Clear IncrementalSearchedKeyword highlight.
    void clearIncrementalSearchedWordHighlight(bool p_now = true);

    // Clear SearchedKeyword highlight.
    void clearSearchedWordHighlight();

    // Clear SearchedKeywordUnderCursor Highlight.
    void clearSearchedWordUnderCursorHighlight(bool p_now = true);

    // Evaluate selected text or cursor word as magic words.
    void evaluateMagicWords();

    VFile *getFile() const;

    VEditConfig &getConfig();

    // Request to update Vim status.
    void requestUpdateVimStatus();

    // Jump to a title.
    // @p_forward: jump forward or backward.
    // @p_relativeLevel: 0 for the same level as current header;
    //                   negative value for upper level;
    //                   positive value is ignored.
    // Returns true if the jump succeeded.
    virtual bool jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat) = 0;

    void setInputMethodEnabled(bool p_enabled);

    // Insert decoration markers or decorate selected text.
    void decorateText(TextDecoration p_decoration, int p_level);

    virtual bool isBlockVisible(const QTextBlock &p_block) = 0;

    VEditorObject *object() const;

    QWidget *getEditor() const;

    // Scroll block @p_blockNum into the visual window.
    // @p_dest is the position of the window: 0 for top, 1 for center, 2 for bottom.
    // @p_blockNum is based on 0.
    // Will set the cursor to the block.
    virtual void scrollBlockInPage(int p_blockNum, int p_dest) = 0;

    // Update config according to global configurations.
    virtual void updateConfig();

    void setVimMode(VimMode p_mode);

    VVim *getVim() const;

    virtual QString getContent() const = 0;

    // @p_modified: if true, delete the whole content and insert the new content.
    virtual void setContent(const QString &p_content, bool p_modified = false) = 0;

    virtual VWordCountInfo fetchWordCountInfo() const = 0;

// Wrapper functions for QPlainTextEdit/QTextEdit.
// Ends with W to distinguish it from the original interfaces.
public:
    virtual void setExtraSelectionsW(const QList<QTextEdit::ExtraSelection> &p_selections) = 0;

    virtual QTextDocument *documentW() const = 0;

    virtual void setTabStopWidthW(int p_width) = 0;

    virtual QTextCursor textCursorW() const = 0;

    virtual void setTextCursorW(const QTextCursor &p_cursor) = 0;

    virtual void moveCursorW(QTextCursor::MoveOperation p_operation,
                             QTextCursor::MoveMode p_mode = QTextCursor::MoveAnchor) = 0;

    virtual QScrollBar *verticalScrollBarW() const = 0;

    virtual QScrollBar *horizontalScrollBarW() const = 0;

    virtual bool findW(const QString &p_exp,
                       QTextDocument::FindFlags p_options = QTextDocument::FindFlags()) = 0;

    virtual bool findW(const QRegExp &p_exp,
                       QTextDocument::FindFlags p_options = QTextDocument::FindFlags()) = 0;

    virtual void setReadOnlyW(bool p_ro) = 0;

    virtual QWidget *viewportW() const = 0;

    virtual void insertPlainTextW(const QString &p_text) = 0;

    virtual void undoW() = 0;

    virtual void redoW() = 0;

    // Whether display cursor as block.
    virtual void setCursorBlockModeW(CursorBlock p_mode) = 0;

    virtual void zoomInW(int p_range = 1) = 0;

    virtual void zoomOutW(int p_range = 1) = 0;

protected:
    void init();

    virtual void updateFontAndPalette() = 0;

    // Update m_config according to VConfigManager.
    void updateEditConfig();

    // Do some highlight on cursor position changed.
    void highlightOnCursorPositionChanged();

    // Highlight selected text.
    void highlightSelectedWord();

    bool wordInSearchedSelection(const QString &p_text);

    // Set read-only property and highlight current line.
    void setReadOnlyAndHighlightCurrentLine(bool p_readonly);

    // Handle the mouse press event of m_editor.
    // Returns true if no further process is needed.
    bool handleMousePressEvent(QMouseEvent *p_event);

    bool handleMouseReleaseEvent(QMouseEvent *p_event);

    bool handleMouseMoveEvent(QMouseEvent *p_event);

    bool handleInputMethodQuery(Qt::InputMethodQuery p_query,
                                QVariant &p_var) const;

    bool handleWheelEvent(QWheelEvent *p_event);

    QWidget *m_editor;

    VEditorObject *m_object;

    QPointer<VFile> m_file;

    VEditOperations *m_editOps;

    VEditConfig m_config;

private:
    friend class VEditorObject;

    void highlightTrailingSpace();

    // Trigger the timer to request highlight.
    // If @p_now is true, stop the timer and highlight immediately.
    void highlightExtraSelections(bool p_now = false);

    // @p_fileter: a function to filter out highlight results.
    void highlightTextAll(const QString &p_text,
                          uint p_options,
                          SelectionId p_id,
                          QTextCharFormat p_format,
                          void (*p_filter)(VEditor *,
                                           QList<QTextEdit::ExtraSelection> &) = NULL);

    // Find all the occurences of @p_text.
    QList<QTextCursor> findTextAll(const QString &p_text, uint p_options);

    // Highlight @p_cursor as the incremental searched keyword.
    void highlightIncrementalSearchedWord(const QTextCursor &p_cursor);

    // Find @p_text in the document starting from @p_start.
    // Returns true if @p_text is found and set @p_cursor to indicate
    // the position.
    // Will NOT change current cursor.
    bool findTextHelper(const QString &p_text,
                        uint p_options,
                        bool p_forward,
                        int p_start,
                        bool &p_wrapped,
                        QTextCursor &p_cursor);

    void showWrapLabel();

    void highlightSearchedWord(const QString &p_text, uint p_options);

    // Highlight @p_cursor as the searched keyword under cursor.
    void highlightSearchedWordUnderCursor(const QTextCursor &p_cursor);

    QLabel *m_wrapLabel;
    QTimer *m_labelTimer;

    QTextDocument *m_document;

    // doHighlightExtraSelections() will highlight these selections.
    // Selections are indexed by SelectionId.
    QVector<QList<QTextEdit::ExtraSelection> > m_extraSelections;

    QColor m_selectedWordFg;
    QColor m_selectedWordBg;

    QColor m_searchedWordFg;
    QColor m_searchedWordBg;

    QColor m_searchedWordCursorFg;
    QColor m_searchedWordCursorBg;

    QColor m_incrementalSearchedWordFg;
    QColor m_incrementalSearchedWordBg;

    QColor m_trailingSpaceColor;

    // Timer for extra selections highlight.
    QTimer *m_highlightTimer;

    bool m_readyToScroll;
    bool m_mouseMoveScrolled;
    int m_oriMouseX;
    int m_oriMouseY;

    // Whether enable input method.
    bool m_enableInputMethod;

// Functions for private slots.
private:
    void labelTimerTimeout();

    // Do the real work to highlight extra selections.
    void doHighlightExtraSelections();
};


// Since one class could not inherit QObject multiple times, we use this class
// for VEditor to signal/slot.
class VEditorObject : public QObject
{
    Q_OBJECT
public:
    explicit VEditorObject(VEditor *p_editor, QObject *p_parent = nullptr)
        : QObject(p_parent), m_editor(p_editor)
    {
    }

signals:
    // Emit when editor config has been updated.
    void configUpdated();

    // Emit when want to show message in status bar.
    void statusMessage(const QString &p_msg);

    // Request VEditTab to save and exit edit mode.
    void saveAndRead();

    // Request VEditTab to discard and exit edit mode.
    void discardAndRead();

    // Request VEditTab to edit current note.
    void editNote();

    // Request VEditTab to save this file.
    void saveNote();

    // Emit when Vim status updated.
    void vimStatusUpdated(const VVim *p_vim);

    // Emit when all initialization is ready.
    void ready();

    // Request the edit tab to close find and replace dialog.
    void requestCloseFindReplaceDialog();

    void mouseMoved(QMouseEvent *p_event);

    void mousePressed(QMouseEvent *p_event);

    void mouseReleased(QMouseEvent *p_event);

    void cursorPositionChanged();

private slots:
    // Timer for find-wrap label.
    void labelTimerTimeout()
    {
        m_editor->labelTimerTimeout();
    }

    // Do the real work to highlight extra selections.
    void doHighlightExtraSelections()
    {
        m_editor->doHighlightExtraSelections();
    }

private:
    friend class VEditor;

    VEditor *m_editor;
};

inline VFile *VEditor::getFile() const
{
    return m_file;
}

inline VEditConfig &VEditor::getConfig()
{
    return m_config;
}

inline VEditorObject *VEditor::object() const
{
    return m_object;
}

inline QWidget *VEditor::getEditor() const
{
    return m_editor;
}

#endif // VEDITOR_H
