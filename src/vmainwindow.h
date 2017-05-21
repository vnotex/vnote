#ifndef VMAINWINDOW_H
#define VMAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QPair>
#include <QPointer>
#include <QString>
#include "vfile.h"
#include "vedittab.h"

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
class VAvatar;
class VFindReplaceDialog;
class VCaptain;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    friend class VCaptain;

    VMainWindow(QWidget *parent = 0);
    const QVector<QPair<QString, QString> > &getPalette() const;
    void locateFile(VFile *p_file);
    void locateCurrentFile();

private slots:
    void importNoteFromFile();
    void viewSettings();
    void changeMarkdownConverter(QAction *action);
    void aboutMessage();
    void shortcutHelp();
    void changeExpandTab(bool checked);
    void setTabStopWidth(QAction *action);
    void setEditorBackgroundColor(QAction *action);
    void setRenderBackgroundColor(QAction *action);
    void setRenderStyle(QAction *p_action);
    void setEditorStyle(QAction *p_action);
    void updateRenderStyleMenu();
    void updateEditorStyleMenu();
    void changeHighlightCursorLine(bool p_checked);
    void changeHighlightSelectedWord(bool p_checked);
    void changeHighlightSearchedWord(bool p_checked);
    void handleCurTabStatusChanged(const VFile *p_file, const VEditTab *p_editTab, bool p_editMode);
    void onePanelView();
    void twoPanelView();
    void expandPanelView(bool p_checked);
    void curEditFileInfo();
    void deleteCurNote();
    void handleCurrentDirectoryChanged(const VDirectory *p_dir);
    void handleCurrentNotebookChanged(const VNotebook *p_notebook);
    void insertImage();
    void handleFindDialogTextChanged(const QString &p_text, uint p_options);
    void openFindDialog();
    void enableMermaid(bool p_checked);
    void enableMathjax(bool p_checked);
    void handleCaptainModeChanged(bool p_enabled);
    void changeAutoIndent(bool p_checked);
    void changeAutoList(bool p_checked);
    void enableCodeBlockHighlight(bool p_checked);
    void enableImagePreview(bool p_checked);
    void enableImagePreviewConstraint(bool p_checked);
    void enableImageConstraint(bool p_checked);
    void enableImageCaption(bool p_checked);
    void printNote();
    void exportAsPDF();

protected:
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

private:
    void setupUI();
    QWidget *setupDirectoryPanel();

    void initToolBar();
    void initFileToolBar();
    void initViewToolBar();

    void initMenuBar();
    void initFileMenu();
    void initEditMenu();
    void initViewMenu();
    void initMarkdownMenu();
    void initHelpMenu();

    void initDockWindows();
    void initAvatar();
    void initPredefinedColorPixmaps();
    void initRenderBackgroundMenu(QMenu *menu);
    void initRenderStyleMenu(QMenu *p_menu);
    void initEditorBackgroundMenu(QMenu *menu);
    void initEditorStyleMenu(QMenu *p_emnu);
    void changeSplitterView(int nrPanel);
    void updateWindowTitle(const QString &str);
    void updateActionStateFromTabStatusChange(const VFile *p_file,
                                              bool p_editMode);
    void saveStateAndGeometry();
    void restoreStateAndGeometry();
    void repositionAvatar();

    void initCaptain();
    void toggleOnePanelView();
    void closeCurrentFile();

    VNote *vnote;
    QPointer<VFile> m_curFile;
    QPointer<VEditTab> m_curTab;

    VCaptain *m_captain;

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
    VAvatar *m_avatar;
    VFindReplaceDialog *m_findReplaceDialog;

    // Whether it is one panel or two panles.
    bool m_onePanel;

    // Actions
    QAction *newRootDirAct;
    QAction *newNoteAct;
    QAction *noteInfoAct;
    QAction *deleteNoteAct;
    QAction *editNoteAct;
    QAction *saveNoteAct;
    QAction *saveExitAct;
    QAction *discardExitAct;
    QAction *expandViewAct;
    QAction *m_importNoteAct;
    QAction *m_printAct;
    QAction *m_exportAsPDFAct;

    QAction *m_insertImageAct;
    QAction *m_findReplaceAct;
    QAction *m_findNextAct;
    QAction *m_findPreviousAct;
    QAction *m_replaceAct;
    QAction *m_replaceFindAct;
    QAction *m_replaceAllAct;

    QAction *m_autoIndentAct;

    QActionGroup *m_renderStyleActs;
    QActionGroup *m_editorStyleActs;

    // Menus
    QMenu *viewMenu;

    QVector<QPixmap> predefinedColorPixmaps;
};

#endif // VMAINWINDOW_H
