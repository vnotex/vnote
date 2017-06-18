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
    void requestUpdateTabStatus();
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

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

signals:
    void tabStatusChanged(const VFile *p_file, const VEditTab *p_editTab, bool p_editMode);
    void requestSplitWindow(VEditWindow *curWindow);
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
    bool handleTabCloseRequest(int index);
    void splitWindow();
    void removeSplit();
    void handleTabbarClicked(int p_index);
    void handleCurrentIndexChanged(int p_index);
    void contextMenuRequested(QPoint pos);
    void tabListJump(VFile *p_file);
    void handleOutlineChanged(const VToc &p_toc);
    void handleCurHeaderChanged(const VAnchor &p_anchor);
    void handleTabStatusChanged();
    void updateSplitMenu();
    void tabbarContextMenuRequested(QPoint p_pos);
    void handleLocateAct();
    void handleMoveLeftAct();
    void handleMoveRightAct();

    // Handle the statusMessage signal of VEditTab.
    void handleTabStatusMessage(const QString &p_msg);

    // Handle the vimStatusUpdated() signal of VEditTab.
    void handleTabVimStatusUpdated(const VVim *p_vim);

private:
    void initTabActions();
    void setupCornerWidget();
    void removeEditTab(int p_index);
    int insertEditTab(int p_index, VFile *p_file, QWidget *p_page);
    int appendEditTab(VFile *p_file, QWidget *p_page);
    int openFileInTab(VFile *p_file, OpenFileMode p_mode);
    void noticeTabStatus(int p_index);
    void noticeStatus(int index);
    inline QString generateTooltip(const VFile *p_file) const;
    inline QString generateTabText(int p_index, const QString &p_name,
                                   bool p_modified, bool p_modifiable) const;
    bool canRemoveSplit();
    void moveTabOneSplit(int p_tabIdx, bool p_right);
    void updateTabInfo(int p_idx);
    // Update the sequence number of all the tabs.
    void updateAllTabsSequence();

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
