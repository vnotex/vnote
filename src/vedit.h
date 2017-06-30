#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>
#include <QPointer>
#include <QVector>
#include <QList>
#include <QColor>
#include <QFontMetrics>
#include "vconstants.h"
#include "vtoc.h"
#include "vfile.h"

class VEditOperations;
class QLabel;
class QTimer;
class VVim;

enum class SelectionId {
    CurrentLine = 0,
    SelectedWord,
    SearchedKeyword,
    TrailingSapce,
    MaxSelection
};

class VEditConfig {
public:
    VEditConfig() : m_tabStopWidth(0),
                    m_tabSpaces("\t"),
                    m_enableVimMode(false),
                    m_highlightWholeBlock(false)
    {}

    void init(const QFontMetrics &p_metric);

    // Width in pixels.
    int m_tabStopWidth;

    bool m_expandTab;

    // The literal string for Tab. It is spaces if Tab is expanded.
    QString m_tabSpaces;

    bool m_enableVimMode;

    // The background color of cursor line.
    QColor m_cursorLineBg;

    // Whether highlight a visual line or a whole block.
    bool m_highlightWholeBlock;
};

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    VEdit(VFile *p_file, QWidget *p_parent = 0);
    virtual ~VEdit();
    virtual void beginEdit();
    virtual void endEdit();
    // Save buffer content to VFile.
    virtual void saveFile();
    virtual void setModified(bool p_modified);
    bool isModified() const;
    virtual void reloadFile();
    virtual void scrollToLine(int p_lineNumber);
    // User requests to insert an image.
    virtual void insertImage();
    bool findTextHelper(const QString &p_text, uint p_options,
                        bool p_forward, bool &p_wrapped);
    bool peekText(const QString &p_text, uint p_options);
    bool findText(const QString &p_text, uint p_options, bool p_forward);
    void replaceText(const QString &p_text, uint p_options,
                     const QString &p_replaceText, bool p_findNext);
    void replaceTextAll(const QString &p_text, uint p_options,
                        const QString &p_replaceText);
    void setReadOnly(bool p_ro);
    void clearSearchedWordHighlight();
    VFile *getFile() const;

    VEditConfig &getConfig();

    // Request to update Vim status.
    void requestUpdateVimStatus();

signals:
    // Request VEditTab to save and exit edit mode.
    void saveAndRead();

    // Request VEditTab to discard and exit edit mode.
    void discardAndRead();

    // Request VEditTab to edit current note.
    void editNote();

    // Request VEditTab to save this file.
    void saveNote();

    // Emit when m_config has been updated.
    void configUpdated();

    // Emit when want to show message in status bar.
    void statusMessage(const QString &p_msg);

    // Emit when Vim status updated.
    void vimStatusUpdated(const VVim *p_vim);

public slots:
    virtual void highlightCurrentLine();

    // Jump to a title.
    // @p_forward: jump forward or backward.
    // @p_relativeLevel: 0 for the same level as current header;
    //                   negative value for upper level;
    //                   positive value is ignored.
    // Returns true if the jump succeeded.
    virtual bool jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat);

private slots:
    void labelTimerTimeout();
    void highlightSelectedWord();
    void handleSaveExitAct();
    void handleDiscardExitAct();
    void handleEditAct();
    void highlightTrailingSpace();
    void handleCursorPositionChanged();

protected:
    QPointer<VFile> m_file;
    VEditOperations *m_editOps;

    VEditConfig m_config;

    virtual void updateFontAndPalette();
    virtual void contextMenuEvent(QContextMenuEvent *p_event) Q_DECL_OVERRIDE;

    // Used to implement dragging mouse with Ctrl and left button pressed to scroll.
    virtual void mousePressEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    // Update m_config according to VConfigManager.
    void updateConfig();

private:
    QLabel *m_wrapLabel;
    QTimer *m_labelTimer;

    // doHighlightExtraSelections() will highlight these selections.
    // Selections are indexed by SelectionId.
    QVector<QList<QTextEdit::ExtraSelection> > m_extraSelections;

    QColor m_selectedWordColor;
    QColor m_searchedWordColor;
    QColor m_trailingSpaceColor;

    // Timer for extra selections highlight.
    QTimer *m_highlightTimer;

    bool m_readyToScroll;
    bool m_mouseMoveScrolled;
    int m_oriMouseX;
    int m_oriMouseY;

    void showWrapLabel();

    // Trigger the timer to request highlight.
    // If @p_now is true, stop the timer and highlight immediately.
    void highlightExtraSelections(bool p_now = false);

    // Do the real work to highlight extra selections.
    void doHighlightExtraSelections();

    // Find all the occurences of @p_text.
    QList<QTextCursor> findTextAll(const QString &p_text, uint p_options);

    // @p_fileter: a function to filter out highlight results.
    void highlightTextAll(const QString &p_text, uint p_options,
                          SelectionId p_id, QTextCharFormat p_format,
                          void (*p_filter)(VEdit *, QList<QTextEdit::ExtraSelection> &) = NULL);

    void highlightSearchedWord(const QString &p_text, uint p_options);
    bool wordInSearchedSelection(const QString &p_text);
};



#endif // VEDIT_H
