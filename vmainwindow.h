#ifndef VMAINWINDOW_H
#define VMAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QComboBox;
class VDirectoryTree;
class QSplitter;
class QListWidget;
class QTabWidget;
class QToolBar;
class VNote;
class VFileList;
class VTabWidget;
class QAction;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(QWidget *parent = 0);
    ~VMainWindow();

private slots:
    // Change current notebook index and update the directory tree
    void setCurNotebookIndex(int index);

signals:
    void curNotebookIndexChanged(const QString &path);

private:
    void setupUI();
    // Update notebookComboBox according to vnote
    void updateNotebookComboBox();
    void initActions();
    void initToolBar();

    QLabel *notebookLabel;
    QLabel *directoryLabel;
    QComboBox *notebookComboBox;
    VDirectoryTree *directoryTree;
    VFileList *fileList;
    VTabWidget *tabs;
    QSplitter *mainSplitter;
    VNote *vnote;
    QToolBar *fileToolBar;

    // Actions
    QAction *editNoteAct;
    QAction *saveNoteAct;
    QAction *readNoteAct;
};

#endif // VMAINWINDOW_H
