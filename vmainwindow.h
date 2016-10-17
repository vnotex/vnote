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
class QPushButton;
class VNotebook;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(QWidget *parent = 0);
    ~VMainWindow();

private slots:
    // Change current notebook index and update the directory tree
    void setCurNotebookIndex(int index);
    // Create a notebook
    void onNewNotebookBtnClicked();
    void onDeleteNotebookBtnClicked();
    void updateNotebookComboBox(const QVector<VNotebook> &notebooks);

signals:
    void curNotebookIndexChanged(const QString &path);

private:
    void setupUI();
    void initActions();
    void initToolBar();
    void initMenuBar();
    bool isConflictWithExistingNotebooks(const QString &name, const QString &path);

    QLabel *notebookLabel;
    QLabel *directoryLabel;
    QComboBox *notebookComboBox;
    QPushButton *newNotebookBtn;
    QPushButton *deleteNotebookBtn;
    QPushButton *notebookInfoBtn;
    VDirectoryTree *directoryTree;
    VFileList *fileList;
    VTabWidget *tabs;
    QSplitter *mainSplitter;
    VNote *vnote;

    // Actions
    QAction *editNoteAct;
    QAction *saveNoteAct;
    QAction *readNoteAct;
};

#endif // VMAINWINDOW_H
