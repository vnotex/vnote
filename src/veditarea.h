#ifndef VEDITAREA_H
#define VEDITAREA_H

#include <QWidget>
#include <QJsonObject>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QVector>
#include <QPair>
#include <QtDebug>
#include <QSplitter>
#include "vnotebook.h"
#include "veditwindow.h"
#include "vtoc.h"

class VNote;
class VFile;
class VDirectory;
class VFindReplaceDialog;

class VEditArea : public QWidget
{
    Q_OBJECT
public:
    explicit VEditArea(VNote *vnote, QWidget *parent = 0);
    bool isFileOpened(const VFile *p_file);
    bool closeAllFiles(bool p_forced);
    bool closeFile(const VFile *p_file, bool p_forced);
    bool closeFile(const VDirectory *p_dir, bool p_forced);
    bool closeFile(const VNotebook *p_notebook, bool p_forced);
    // Returns current edit tab.
    VEditTab *currentEditTab();
    // Returns the count of VEditWindow.
    inline int windowCount() const;
    // Returns the index of @p_window.
    int windowIndex(const VEditWindow *p_window) const;
    // Move tab widget @p_widget from window @p_fromIdx to @p_toIdx.
    // @p_widget has been removed from the original window.
    // If fail, just delete the p_widget.
    void moveTab(QWidget *p_widget, int p_fromIdx, int p_toIdx);
    inline VFindReplaceDialog *getFindReplaceDialog() const;
    // Return selected text of current edit tab.
    QString getSelectedText();
    void splitCurrentWindow();
    void removeCurrentWindow();
    // Focus next window (curWindowIndex + p_biaIdx).
    // Return the new current window index, otherwise, return -1.
    int focusNextWindow(int p_biaIdx);
    void moveCurrentTabOneSplit(bool p_right);
    VEditWindow *getCurrentWindow() const;

signals:
    void curTabStatusChanged(const VFile *p_file, const VEditTab *p_editTab, bool p_editMode);
    void outlineChanged(const VToc &toc);
    void curHeaderChanged(const VAnchor &anchor);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

public slots:
    void openFile(VFile *p_file, OpenFileMode p_mode);
    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();
    void handleOutlineItemActivated(const VAnchor &anchor);
    void handleFileUpdated(const VFile *p_file);
    void handleDirectoryUpdated(const VDirectory *p_dir);
    void handleNotebookUpdated(const VNotebook *p_notebook);

private slots:
    void handleSplitWindowRequest(VEditWindow *curWindow);
    void handleRemoveSplitRequest(VEditWindow *curWindow);
    void handleWindowFocused();
    void handleOutlineChanged(const VToc &toc);
    void handleCurHeaderChanged(const VAnchor &anchor);
    void handleFindTextChanged(const QString &p_text, uint p_options);
    void handleFindOptionChanged(uint p_options);
    void handleFindNext(const QString &p_text, uint p_options, bool p_forward);
    void handleReplace(const QString &p_text, uint p_options,
                       const QString &p_replaceText, bool p_findNext);
    void handleReplaceAll(const QString &p_text, uint p_options,
                          const QString &p_replaceText);
    void handleFindDialogClosed();

private:
    void setupUI();
    QVector<QPair<int, int> > findTabsByFile(const VFile *p_file);
    int openFileInWindow(int windowIndex, VFile *p_file, OpenFileMode p_mode);
    void setCurrentTab(int windowIndex, int tabIndex, bool setFocus);
    void setCurrentWindow(int windowIndex, bool setFocus);
    inline VEditWindow *getWindow(int windowIndex) const;
    void insertSplitWindow(int idx);
    void removeSplitWindow(VEditWindow *win);
    void updateWindowStatus();

    VNote *vnote;
    int curWindowIndex;

    // Splitter holding multiple split windows
    QSplitter *splitter;
    VFindReplaceDialog *m_findReplace;
};

inline VEditWindow* VEditArea::getWindow(int windowIndex) const
{
    Q_ASSERT(windowIndex < splitter->count());
    return dynamic_cast<VEditWindow *>(splitter->widget(windowIndex));
}

inline int VEditArea::windowCount() const
{
    return splitter->count();
}

inline VFindReplaceDialog *VEditArea::getFindReplaceDialog() const
{
    return m_findReplace;
}

#endif // VEDITAREA_H
