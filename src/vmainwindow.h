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
class VTabWidget;
class QAction;
class QPushButton;
class VNotebook;
class QActionGroup;
class VFileList;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(QWidget *parent = 0);
    ~VMainWindow();

private slots:
    void setCurNotebookIndex(int index);
    // Create a notebook
    void onNewNotebookBtnClicked();
    void onDeleteNotebookBtnClicked();
    void onNotebookInfoBtnClicked();
    void updateNotebookComboBox(const QVector<VNotebook> &notebooks);
    void notebookComboBoxAdded(const QVector<VNotebook> &notebooks, int idx);
    void notebookComboBoxDeleted(const QVector<VNotebook> &notebooks, const QString &deletedName);
    void notebookComboBoxRenamed(const QVector<VNotebook> &notebooks,
                                 const QString &oldName, const QString &newName);
    void importNoteFromFile();
    void changeMarkdownConverter(QAction *action);
    void aboutMessage();
    void changeExpandTab(bool checked);
    void setTabStopWidth(QAction *action);
    void setEditorBackgroundColor(QAction *action);
    void setRenderBackgroundColor(QAction *action);
    void updateToolbarFromTabChage(const QString &notebook, const QString &relativePath,
                                   bool editMode, bool modifiable);
    void changePanelView(QAction *action);

signals:
    void curNotebookChanged(const QString &notebookName);

private:
    void setupUI();
    void initActions();
    void initToolBar();
    void initMenuBar();
    bool isConflictWithExistingNotebooks(const QString &name);
    void initPredefinedColorPixmaps();
    void initRenderBackgroundMenu(QMenu *menu);
    void initEditorBackgroundMenu(QMenu *menu);
    void changeSplitterView(int nrPanel);

    // If true, comboBox changes will not trigger any signal out
    bool notebookComboMuted;

    QLabel *notebookLabel;
    QLabel *directoryLabel;
    QComboBox *notebookComboBox;
    QPushButton *newNotebookBtn;
    QPushButton *deleteNotebookBtn;
    QPushButton *notebookInfoBtn;
    QPushButton *newRootDirBtn;
    QPushButton *deleteDirBtn;
    QPushButton *dirInfoBtn;
    VFileList *fileList;
    VDirectoryTree *directoryTree;
    VTabWidget *tabs;
    QSplitter *mainSplitter;
    VNote *vnote;

    // Actions
    QAction *newNoteAct;
    QAction *editNoteAct;
    QAction *saveNoteAct;
    QAction *saveExitAct;
    QAction *discardExitAct;
    QActionGroup *viewAct;
    QAction *twoPanelViewAct;
    QAction *onePanelViewAct;
    QAction *expandViewAct;
    QAction *importNoteAct;
    QActionGroup *converterAct;
    QAction *markedAct;
    QAction *hoedownAct;
    QAction *aboutAct;
    QAction *aboutQtAct;
    QAction *expandTabAct;
    QActionGroup *tabStopWidthAct;
    QAction *twoSpaceTabAct;
    QAction *fourSpaceTabAct;
    QAction *eightSpaceTabAct;
    QActionGroup *backgroundColorAct;
    QActionGroup *renderBackgroundAct;

    QVector<QPixmap> predefinedColorPixmaps;
};

#endif // VMAINWINDOW_H
