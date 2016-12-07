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

class VNote;
class QPushButton;
class QActionGroup;
class VFile;

class VEditWindow : public QTabWidget
{
    Q_OBJECT
public:
    explicit VEditWindow(VNote *vnote, QWidget *parent = 0);
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

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;

signals:
    void tabStatusChanged(const VFile *p_file, bool p_editMode);
    void requestSplitWindow(VEditWindow *curWindow);
    void requestRemoveSplit(VEditWindow *curWindow);
    // This widget or its children get the focus
    void getFocused();
    void outlineChanged(const VToc &toc);
    void curHeaderChanged(const VAnchor &anchor);

private slots:
    bool handleTabCloseRequest(int index);
    void splitWindow();
    void removeSplit();
    void handleTabbarClicked(int index);
    void contextMenuRequested(QPoint pos);
    void tabListJump(QAction *action);
    void handleOutlineChanged(const VToc &p_toc);
    void handleCurHeaderChanged(const VAnchor &p_anchor);
    void handleTabStatusChanged();
    void updateTabListMenu();
    void updateSplitMenu();

private:
    void setupCornerWidget();
    void removeEditTab(int p_index);
    int insertEditTab(int p_index, VFile *p_file, QWidget *p_page);
    int appendEditTab(VFile *p_file, QWidget *p_page);
    int openFileInTab(VFile *p_file, OpenFileMode p_mode);
    inline VEditTab *getTab(int tabIndex) const;
    void noticeTabStatus(int p_index);
    void noticeStatus(int index);
    inline QString generateTooltip(const VFile *p_file) const;
    inline QString generateTabText(const QString &p_name, bool p_modified) const;
    bool canRemoveSplit();
    // If the scroller of the tabBar() is visible.
    bool scrollerVisible() const;
    void setLeftCornerWidgetVisible(bool p_visible);

    VNote *vnote;
    // Button in the right corner
    QPushButton *rightBtn;
    // Button in the left corner
    QPushButton *leftBtn;

    // Actions
    QAction *splitAct;
    QAction *removeSplitAct;
    QActionGroup *tabListAct;
};

inline VEditTab* VEditWindow::getTab(int tabIndex) const
{
    return dynamic_cast<VEditTab *>(widget(tabIndex));
}

inline QString VEditWindow::generateTooltip(const VFile *p_file) const
{
    if (!p_file) {
        return "";
    }
    // [Notebook]path
    return QString("[%1] %2").arg(p_file->getNotebook()).arg(p_file->retrivePath());
}

inline QString VEditWindow::generateTabText(const QString &p_name, bool p_modified) const
{
    return p_modified ? (p_name + "*") : p_name;
}

#endif // VEDITWINDOW_H
