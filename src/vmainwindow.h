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
class VVimIndicator;
class VTabIndicator;
class VSingleInstanceGuard;
class QTimer;
class QSystemTrayIcon;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    friend class VCaptain;

    VMainWindow(VSingleInstanceGuard *p_guard, QWidget *p_parent = 0);
    const QVector<QPair<QString, QString> > &getPalette() const;

    // Returns true if the location succeeds.
    bool locateFile(VFile *p_file);

    // Returns true if the location succeeds.
    bool locateCurrentFile();

    VFileList *getFileList() const;

    // View and edit the information of @p_file, which is an orphan file.
    void editOrphanFileInfo(VFile *p_file);

    // Open external files @p_files as orphan files.
    // If @p_forceOrphan is false, for each file, VNote will try to find out if
    // it is a note inside VNote. If yes, VNote will open it as internal file.
    void openExternalFiles(const QStringList &p_files, bool p_forceOrphan = false);

    // Try to open @p_filePath as internal note.
    bool tryOpenInternalFile(const QString &p_filePath);

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
    void changeHighlightTrailingSapce(bool p_checked);
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
    void changeVimMode(bool p_checked);
    void enableCodeBlockHighlight(bool p_checked);
    void enableImagePreview(bool p_checked);
    void enableImagePreviewConstraint(bool p_checked);
    void enableImageConstraint(bool p_checked);
    void enableImageCaption(bool p_checked);
    void printNote();
    void exportAsPDF();

    // Show a temporary message in status bar.
    void showStatusMessage(const QString &p_msg);

    // Handle Vim status updated.
    void handleVimStatusUpdated(const VVim *p_vim);

    // Handle the status update of the current tab of VEditArea.
    void handleAreaTabStatusUpdated(const VEditTabInfo &p_info);

    // Check the shared memory between different instances to see if we have
    // files to open.
    void checkSharedMemory();

    void quitApp();

    // Restore main window.
    void showMainWindow();

protected:
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

    void changeEvent(QEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI();
    QWidget *setupDirectoryPanel();

    void initToolBar();
    void initFileToolBar(QSize p_iconSize = QSize());
    void initViewToolBar(QSize p_iconSize = QSize());

    // Init the Edit toolbar.
    void initEditToolBar(QSize p_iconSize = QSize());

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

    // Init the Line Number submenu in Edit menu.
    void initEditorLineNumberMenu(QMenu *p_menu);

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

    // Update status bar information.
    void updateStatusInfo(const VEditTabInfo &p_info);

    // Wrapper to create a QAction.
    QAction *newAction(const QIcon &p_icon,
                       const QString &p_text,
                       QObject *p_parent = nullptr);

    // Init a timer to watch the change of the shared memory between instances of
    // VNote.
    void initSharedMemoryWatcher();

    // Init system tray icon and correspondign context menu.
    void initTrayIcon();

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
    VVimIndicator *m_vimIndicator;
    VTabIndicator *m_tabIndicator;

    // Whether it is one panel or two panles.
    bool m_onePanel;

    // Actions
    QAction *newRootDirAct;
    QAction *newNoteAct;
    QAction *noteInfoAct;
    QAction *deleteNoteAct;
    QAction *m_closeNoteAct;
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

    // Edit Toolbar.
    QToolBar *m_editToolBar;

    QVector<QPixmap> predefinedColorPixmaps;

    // Single instance guard.
    VSingleInstanceGuard *m_guard;

    // Timer to check the shared memory between instances of VNote.
    QTimer *m_sharedMemTimer;

    // Tray icon.
    QSystemTrayIcon *m_trayIcon;

    // The old state of window.
    Qt::WindowStates m_windowOldState;

    // Whether user request VNote to quit.
    bool m_requestQuit;

    // Interval of the shared memory timer in ms.
    static const int c_sharedMemTimerInterval;
};

inline VFileList *VMainWindow::getFileList() const
{
    return fileList;
}

#endif // VMAINWINDOW_H
