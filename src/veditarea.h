#ifndef VEDITAREA_H
#define VEDITAREA_H

#include <QWidget>
#include <QJsonObject>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QVector>
#include <QPair>
#include <QSplitter>
#include <QStack>
#include "vnotebook.h"
#include "veditwindow.h"
#include "vnavigationmode.h"
#include "vfilesessioninfo.h"

class VFile;
class VDirectory;
class VFindReplaceDialog;
class QLabel;
class VVim;

class VEditArea : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VEditArea(QWidget *parent = 0);

    // Whether @p_file has been opened in edit area.
    bool isFileOpened(const VFile *p_file);

    bool closeAllFiles(bool p_forced);
    bool closeFile(const VFile *p_file, bool p_forced);
    bool closeFile(const VDirectory *p_dir, bool p_forced);
    bool closeFile(const VNotebook *p_notebook, bool p_forced);

    // Return current edit window.
    VEditWindow *getCurrentWindow() const;

    // Return current edit tab.
    VEditTab *getCurrentTab() const;

    // Return the @p_tabIdx tab in the @p_winIdx window.
    VEditTab *getTab(int p_winIdx, int p_tabIdx) const;

    // Return the tab for @p_file file.
    VEditTab *getTab(const VFile *p_file) const;

    // Return VEditTabInfo of all edit tabs.
    QVector<VEditTabInfo> getAllTabsInfo() const;

    // Return the count of VEditWindow.
    int windowCount() const;

    // Returns the index of @p_window.
    int windowIndex(const VEditWindow *p_window) const;
    // Move tab widget @p_widget from window @p_fromIdx to @p_toIdx.
    // @p_widget has been removed from the original window.
    // If fail, just delete the p_widget.
    void moveTab(QWidget *p_widget, int p_fromIdx, int p_toIdx);

    VFindReplaceDialog *getFindReplaceDialog() const;

    // Return selected text of current edit tab.
    QString getSelectedText();
    void splitCurrentWindow();
    void removeCurrentWindow();
    // Focus next window (curWindowIndex + p_biaIdx).
    // Return the new current window index, otherwise, return -1.
    int focusNextWindow(int p_biaIdx);
    void moveCurrentTabOneSplit(bool p_right);

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

    // Open files @p_files.
    int openFiles(const QVector<VFileSessionInfo> &p_files);

    // Record a closed file in the stack.
    void recordClosedFile(const VFileSessionInfo &p_file);

    // Return the rect not containing the tab bar.
    QRect editAreaRect() const;

signals:
    // Emit when current window's tab status updated.
    void tabStatusUpdated(const VEditTabInfo &p_info);

    // Emit when current window's tab's outline changed.
    void outlineChanged(const VTableOfContent &p_outline);

    // Emit when current window's tab's current header changed.
    void currentHeaderChanged(const VHeaderPointer &p_header);

    // Emit when want to show message in status bar.
    void statusMessage(const QString &p_msg);

    // Emit when Vim status updated.
    void vimStatusUpdated(const VVim *p_vim);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

public slots:
    // Open @p_file in mode @p_mode.
    // If @p_file has already been opened, change its mode to @p_mode only if
    // @p_forceMode is true.
    // A given file can be opened in multiple split windows. A given file could be
    // opened at most in one tab inside a window.
    VEditTab *openFile(VFile *p_file, OpenFileMode p_mode, bool p_forceMode = false);

    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();

    // Scroll current tab to @p_header.
    void scrollToHeader(const VHeaderPointer &p_header);

    void handleFileUpdated(const VFile *p_file, UpdateAction p_act);

    void handleDirectoryUpdated(const VDirectory *p_dir, UpdateAction p_act);

    void handleNotebookUpdated(const VNotebook *p_notebook);

private slots:
    // Split @curWindow via inserting a new window around it.
    // @p_right: insert the new window on the right side.
    void splitWindow(VEditWindow *p_window, bool p_right = true);

    void handleRemoveSplitRequest(VEditWindow *curWindow);
    void handleWindowFocused();

    void handleWindowOutlineChanged(const VTableOfContent &p_outline);

    void handleWindowCurrentHeaderChanged(const VHeaderPointer &p_header);

    void handleFindTextChanged(const QString &p_text, uint p_options);
    void handleFindOptionChanged(uint p_options);
    void handleFindNext(const QString &p_text, uint p_options, bool p_forward);
    void handleReplace(const QString &p_text, uint p_options,
                       const QString &p_replaceText, bool p_findNext);
    void handleReplaceAll(const QString &p_text, uint p_options,
                          const QString &p_replaceText);
    void handleFindDialogClosed();

    // Hanle the status update of current tab within a VEditWindow.
    void handleWindowTabStatusUpdated(const VEditTabInfo &p_info);

    // Handle the statusMessage signal of VEditWindow.
    void handleWindowStatusMessage(const QString &p_msg);

    // Handle the vimStatusUpdated signal of VEditWindow.
    void handleWindowVimStatusUpdated(const VVim *p_vim);

    // Handle the timeout signal of file timer.
    void handleFileTimerTimeout();

private:
    void setupUI();
    QVector<QPair<int, int> > findTabsByFile(const VFile *p_file);
    int openFileInWindow(int windowIndex, VFile *p_file, OpenFileMode p_mode);
    void setCurrentTab(int windowIndex, int tabIndex, bool setFocus);
    void setCurrentWindow(int windowIndex, bool setFocus);

    VEditWindow *getWindow(int windowIndex) const;

    void insertSplitWindow(int idx);
    void removeSplitWindow(VEditWindow *win);

    // Update status of current window.
    void updateWindowStatus();

    // Init targets for Captain mode.
    void registerCaptainTargets();

    // Captain mode functions.
    // Activate tab @p_idx.
    static bool activateTabByCaptain(void *p_target, void *p_data, int p_idx);

    static bool alternateTabByCaptain(void *p_target, void *p_data);

    static bool showOpenedFileListByCaptain(void *p_target, void *p_data);

    static bool activateSplitLeftByCaptain(void *p_target, void *p_data);

    static bool activateSplitRightByCaptain(void *p_target, void *p_data);

    static bool moveTabSplitLeftByCaptain(void *p_target, void *p_data);

    static bool moveTabSplitRightByCaptain(void *p_target, void *p_data);

    static bool activateNextTabByCaptain(void *p_target, void *p_data);

    static bool activatePreviousTabByCaptain(void *p_target, void *p_data);

    static bool verticalSplitByCaptain(void *p_target, void *p_data);

    static bool removeSplitByCaptain(void *p_target, void *p_data);

    // Evaluate selected text or the word on cursor as magic words.
    static bool evaluateMagicWordsByCaptain(void *p_target, void *p_data);

    // Prompt for user to apply a snippet.
    static bool applySnippetByCaptain(void *p_target, void *p_data);

    // Toggle live preview.
    static bool toggleLivePreviewByCaptain(void *p_target, void *p_data);

    // End Captain mode functions.

    int curWindowIndex;

    // Splitter holding multiple split windows
    QSplitter *splitter;
    VFindReplaceDialog *m_findReplace;

    // Last closed files stack.
    QStack<VFileSessionInfo> m_lastClosedFiles;

    // Whether auto save files.
    bool m_autoSave;
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
