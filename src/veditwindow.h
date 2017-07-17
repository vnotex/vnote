#ifndef VEDITWINDOW_H
#define VEDITWINDOW_H

#include <QTabWidget>
#include <QJsonObject>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include "vnotebook.h"
#include "vedittab.h"
#include "vtoc.h"
#include "vconstants.h"

class VNote;
class QPushButton;
class QActionGroup;
class VFile;
class VEditArea;

class VEditWindow : public QTabWidget
{
    Q_OBJECT
public:
    explicit VEditWindow(VNote *vnote, VEditArea *editArea, QWidget *parent = 0);
    int findTabByFile(const VFile *p_file) const;
    int openFile(VFile *p_file, OpenFileMode p_mode);
    bool closeFile(const VFile *p_file, bool p_forced);
    bool closeFile(const VDirectory *p_dir, bool p_forced);
    bool closeFile(const VNotebook *p_notebook, bool p_forced);
    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();
    bool closeAllFiles(bool p_forced);
    void requestUpdateOutline();
    void requestUpdateCurHeader();
    // Focus to current tab's editor
    void focusWindow();
    void scrollCurTab(const VAnchor &p_anchor);
    void updateFileInfo(const VFile *p_file);
    void updateDirectoryInfo(const VDirectory *p_dir);
    void updateNotebookInfo(const VNotebook *p_notebook);
    VEditTab *currentEditTab();
    // Insert a tab with @p_widget. @p_widget is a fully initialized VEditTab.
    bool addEditTab(QWidget *p_widget);
    // Set whether it is the current window.
    void setCurrentWindow(bool p_current);
    void clearSearchedWordHighlight();
    void moveCurrentTabOneSplit(bool p_right);
    void focusNextTab(bool p_right);
    // Return true if the file list is shown.
    bool showOpenedFileList();
    bool activateTab(int p_sequence);
    // Switch to previous activated tab.
    bool alternateTab();
    VEditTab *getTab(int tabIndex) const;

    // Ask tab @p_index to update its status and propogate.
    // The status here means tab status, outline, current header.
    // If @p_index is -1, it is current tab.
    void updateTabStatus(int p_index = -1);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

signals:
    // Status of current VEditTab has update.
    void tabStatusUpdated(const VEditTabInfo &p_info);

    // Requst VEditArea to split this window.
    void requestSplitWindow(VEditWindow *p_window, bool p_right = true);

    void requestRemoveSplit(VEditWindow *curWindow);
    // This widget or its children get the focus
    void getFocused();
    void outlineChanged(const VToc &toc);
    void curHeaderChanged(const VAnchor &anchor);

    // Emit when want to show message in status bar.
    void statusMessage(const QString &p_msg);

    // Emit when Vim mode status changed.
    void vimStatusUpdated(const VVim *p_vim);

private slots:
    // Close tab @p_index.
    bool closeTab(int p_index);

    // Split this window on the right/left.
    void splitWindow(bool p_right = true);

    void removeSplit();
    void handleTabbarClicked(int p_index);
    void handleCurrentIndexChanged(int p_index);
    void contextMenuRequested(QPoint pos);
    void tabListJump(VFile *p_file);
    void handleOutlineChanged(const VToc &p_toc);
    void handleCurHeaderChanged(const VAnchor &p_anchor);
    void updateSplitMenu();
    void tabbarContextMenuRequested(QPoint p_pos);
    void handleLocateAct();
    void handleMoveLeftAct();
    void handleMoveRightAct();

    // Handle the statusMessage signal of VEditTab.
    void handleTabStatusMessage(const QString &p_msg);

    // Handle the vimStatusUpdated() signal of VEditTab.
    void handleTabVimStatusUpdated(const VVim *p_vim);

    // Handle the statusUpdated signal of VEditTab.
    void handleTabStatusUpdated(const VEditTabInfo &p_info);

private:
    void initTabActions();
    void setupCornerWidget();
    void removeEditTab(int p_index);
    int insertEditTab(int p_index, VFile *p_file, QWidget *p_page);
    int appendEditTab(VFile *p_file, QWidget *p_page);
    int openFileInTab(VFile *p_file, OpenFileMode p_mode);
    inline QString generateTooltip(const VFile *p_file) const;
    inline QString generateTabText(int p_index, const QString &p_name,
                                   bool p_modified, bool p_modifiable) const;
    bool canRemoveSplit();

    // Move tab at @p_tabIdx one split window.
    // @p_right: move right or left.
    // If there is only one split window, it will request to split current window
    // and move the tab to the new split.
    void moveTabOneSplit(int p_tabIdx, bool p_right);

    void updateTabInfo(int p_idx);

    // Update the sequence number of all the tabs.
    void updateAllTabsSequence();

    // Connect the signals of VEditTab to this VEditWindow.
    void connectEditTab(const VEditTab *p_tab);

    VNote *vnote;
    VEditArea *m_editArea;

    // These two members are only used for alternateTab().
    QWidget *m_curTabWidget;
    QWidget *m_lastTabWidget;

    // Button in the right corner
    QPushButton *rightBtn;
    // Button in the left corner
    QPushButton *leftBtn;

    // Actions
    QAction *splitAct;
    QAction *removeSplitAct;
    // Locate current note in the directory and file list
    QAction *m_locateAct;
    QAction *m_moveLeftAct;
    QAction *m_moveRightAct;

    // Close current tab action in tab menu.
    QAction *m_closeTabAct;

    // Close other tabs action in tab menu.
    QAction *m_closeOthersAct;

    // Close tabs to the right in tab menu.
    QAction *m_closeRightAct;

    // View and edit info about this note.
    QAction *m_noteInfoAct;

    // Open the location (the folder containing this file) of this note.
    QAction *m_openLocationAct;
};

inline QString VEditWindow::generateTooltip(const VFile *p_file) const
{
    if (!p_file) {
        return "";
    }
    // [Notebook]path
    return QString("[%1] %2").arg(p_file->getNotebookName()).arg(p_file->retrivePath());
}

inline QString VEditWindow::generateTabText(int p_index, const QString &p_name,
                                            bool p_modified, bool p_modifiable) const
{
    QString seq = QString::number(p_index + c_tabSequenceBase, 10);
    return seq + ". " + p_name + (p_modifiable ? (p_modified ? "*" : "") : "#");
}

#endif // VEDITWINDOW_H
