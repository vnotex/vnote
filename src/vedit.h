#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>
#include <QPointer>
#include <QVector>
#include <QList>
#include <QColor>
#include "vconstants.h"
#include "vtoc.h"
#include "vfile.h"

class VEditOperations;
class QLabel;
class QTimer;

enum class SelectionId {
    CurrentLine = 0,
    SelectedWord,
    SearchedKeyword,
    TrailingSapce,
    MaxSelection
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

signals:
    void saveAndRead();
    void discardAndRead();
    void editNote();

private slots:
    void labelTimerTimeout();
    void highlightSelectedWord();
    void handleSaveExitAct();
    void handleDiscardExitAct();
    void handleEditAct();
    void highlightTrailingSpace();
    void handleCursorPositionChanged();

protected slots:
    virtual void highlightCurrentLine();

protected:
    QPointer<VFile> m_file;
    VEditOperations *m_editOps;
    QColor m_cursorLineColor;

    virtual void updateFontAndPalette();
    virtual void contextMenuEvent(QContextMenuEvent *p_event) Q_DECL_OVERRIDE;

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
