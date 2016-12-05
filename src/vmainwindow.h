#ifndef VMAINWINDOW_H
#define VMAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QPair>
#include <QPointer>
#include <QString>
#include "vfile.h"

class QLabel;
class QComboBox;
class VDirectoryTree;
class QSplitter;
class QListWidget;
class QTabWidget;
class QToolBar;
class VNote;
class QAction;
class QPushButton;
class VNotebook;
class QActionGroup;
class VFileList;
class VEditArea;
class QToolBox;
class VOutline;
class VNotebookSelector;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(QWidget *parent = 0);
    const QVector<QPair<QString, QString> > &getPalette() const;

private slots:
    void importNoteFromFile();
    void changeMarkdownConverter(QAction *action);
    void aboutMessage();
    void changeExpandTab(bool checked);
    void setTabStopWidth(QAction *action);
    void setEditorBackgroundColor(QAction *action);
    void setRenderBackgroundColor(QAction *action);
    void handleCurTabStatusChanged(const VFile *p_file, bool p_editMode);
    void changePanelView(QAction *action);
    void curEditFileInfo();
    void deleteCurNote();
    void handleCurrentDirectoryChanged(const VDirectory *p_dir);
    void handleCurrentNotebookChanged(const VNotebook *p_notebook);

protected:
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;

private:
    void setupUI();
    QWidget *setupDirectoryPanel();
    void initActions();
    void initToolBar();
    void initMenuBar();
    void initDockWindows();
    void initPredefinedColorPixmaps();
    void initRenderBackgroundMenu(QMenu *menu);
    void initEditorBackgroundMenu(QMenu *menu);
    void changeSplitterView(int nrPanel);
    void updateWindowTitle(const QString &str);
    void updateToolbarFromTabChage(const VFile *p_file, bool p_editMode);
    void saveStateAndGeometry();
    void restoreStateAndGeometry();

    VNote *vnote;
    QPointer<VFile> m_curFile;

    QLabel *notebookLabel;
    QLabel *directoryLabel;
    VNotebookSelector *notebookSelector;
    VFileList *fileList;
    VDirectoryTree *directoryTree;
    QSplitter *mainSplitter;
    VEditArea *editArea;
    QDockWidget *toolDock;
    QToolBox *toolBox;
    VOutline *outline;

    // Actions
    QAction *newRootDirAct;
    QAction *newNoteAct;
    QAction *noteInfoAct;
    QAction *deleteNoteAct;
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

    // Menus
    QMenu *viewMenu;

    QVector<QPixmap> predefinedColorPixmaps;
};

#endif // VMAINWINDOW_H
