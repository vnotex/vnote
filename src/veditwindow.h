#ifndef VEDITWINDOW_H
#define VEDITWINDOW_H

#include <QTabWidget>
#include <QJsonObject>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include "vnotebook.h"
#include "vedittab.h"

class VNote;
class QPushButton;

class VEditWindow : public QTabWidget
{
    Q_OBJECT
public:
    explicit VEditWindow(VNote *vnote, QWidget *parent = 0);
    int findTabByFile(const QString &notebook, const QString &relativePath) const;
    int openFile(const QString &notebook, const QString &relativePath,
                 int mode);
    // Close the file forcely
    void closeFile(const QString &notebook, const QString &relativePath);
    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();
    void handleNotebookRenamed(const QVector<VNotebook> &notebooks, const QString &oldName,
                               const QString &newName);
    bool closeAllFiles();
    void setRemoveSplitEnable(bool enabled);
    void getTabStatus(QString &notebook, QString &relativePath,
                      bool &editMode, bool &modifiable) const;
    // Focus to current tab's editor
    void focusWindow();

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

signals:
    void tabStatusChanged(const QString &notebook, const QString &relativePath,
                          bool editMode, bool modifiable);
    void requestSplitWindow(VEditWindow *curWindow);
    void requestRemoveSplit(VEditWindow *curWindow);
    // This widget or its children get the focus
    void getFocused();

private slots:
    bool handleTabCloseRequest(int index);
    void splitWindow();
    void removeSplit();
    void handleTabbarClicked(int index);
    void contextMenuRequested(QPoint pos);

private:
    void setupCornerWidget();
    void openWelcomePage();
    int insertTabWithData(int index, QWidget *page, const QJsonObject &tabData);
    int appendTabWithData(QWidget *page, const QJsonObject &tabData);
    int openFileInTab(const QString &notebook, const QString &relativePath, bool modifiable);
    inline QString getFileName(const QString &relativePath) const;
    inline VEditTab *getTab(int tabIndex) const;
    void noticeTabStatus(int index);

    VNote *vnote;
    // Button in the right corner
    QPushButton *rightBtn;

    // Actions
    QAction *splitAct;
    QAction *removeSplitAct;
};

inline QString VEditWindow::getFileName(const QString &path) const
{
    return QFileInfo(QDir::cleanPath(path)).fileName();
}

inline VEditTab* VEditWindow::getTab(int tabIndex) const
{
    return dynamic_cast<VEditTab *>(widget(tabIndex));
}

#endif // VEDITWINDOW_H
