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

class VEditWindow : public QTabWidget
{
    Q_OBJECT
public:
    explicit VEditWindow(VNote *vnote, QWidget *parent = 0);
    int findTabByFile(const QString &notebook, const QString &relativePath) const;
    int openFile(const QString &notebook, const QString &relativePath,
                 int mode);
    bool closeFile(const QString &notebook, const QString &relativePath, bool isForced);
    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();
    void handleNotebookRenamed(const QVector<VNotebook> &notebooks, const QString &oldName,
                               const QString &newName);
    bool closeAllFiles();
    void setRemoveSplitEnable(bool enabled);
    void requestUpdateTabStatus();
    void requestUpdateOutline();
    void requestUpdateCurHeader();
    // Focus to current tab's editor
    void focusWindow();
    void scrollCurTab(const VAnchor &anchor);
    void handleDirectoryRenamed(const QString &notebook,
                                const QString &oldRelativePath, const QString &newRelativePath);
    void handleFileRenamed(const QString &notebook,
                           const QString &oldRelativePath, const QString &newRelativePath);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

signals:
    void tabStatusChanged(const QString &notebook, const QString &relativePath,
                          bool editMode, bool modifiable, bool modified);
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
    void handleOutlineChanged(const VToc &toc);
    void handleCurHeaderChanged(const VAnchor &anchor);
    void handleTabStatusChanged();

private:
    void setupCornerWidget();
    void openWelcomePage();
    int insertTabWithData(int index, QWidget *page, const QJsonObject &tabData);
    int appendTabWithData(QWidget *page, const QJsonObject &tabData);
    int openFileInTab(const QString &notebook, const QString &relativePath, bool modifiable);
    inline QString getFileName(const QString &relativePath) const;
    inline VEditTab *getTab(int tabIndex) const;
    void noticeTabStatus(int index);
    void updateTabListMenu();
    void noticeStatus(int index);
    inline QString generateTooltip(const QJsonObject &tabData) const;

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

inline QString VEditWindow::getFileName(const QString &path) const
{
    return QFileInfo(QDir::cleanPath(path)).fileName();
}

inline VEditTab* VEditWindow::getTab(int tabIndex) const
{
    return dynamic_cast<VEditTab *>(widget(tabIndex));
}

inline QString VEditWindow::generateTooltip(const QJsonObject &tabData) const
{
    // [Notebook]relativePath
    return QString("[%1] %2").arg(tabData["notebook"].toString()).arg(tabData["relative_path"].toString());
}

#endif // VEDITWINDOW_H
