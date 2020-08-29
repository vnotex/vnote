#ifndef VMAINWINDOW_H
#define VMAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QPair>
#include <QPointer>
#include <QString>
#include "vfile.h"
#include "vedittab.h"
#include "utils/vwebutils.h"

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
class VToolBox;
class VOutline;
class VNotebookSelector;
class VFindReplaceDialog;
class VCaptain;
class VVimIndicator;
class VVimCmdLineEdit;
class VTabIndicator;
class VSingleInstanceGuard;
class QTimer;
class QSystemTrayIcon;
class VButtonWithWidget;
class VAttachmentList;
class VSnippetList;
class VCart;
class VSearcher;
class QPrinter;
class VUniversalEntry;
class VHistoryList;
class VExplorer;
class VTagExplorer;
class VSync;

#define RESTART_EXIT_CODE   1000

enum class PanelViewState
{
    ExpandMode = 0,
    HorizontalMode,
    VerticalMode,
    Invalid
};

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(VSingleInstanceGuard *p_guard, QWidget *p_parent = 0);

    // Returns true if the location succeeds.
    bool locateFile(VFile *p_file, bool p_focus = true, bool p_show = true);

    // Returns true if the location succeeds.
    bool locateDirectory(VDirectory *p_directory);

    // Returns true if the location succeeds.
    bool locateNotebook(VNotebook *p_notebook);

    VFileList *getFileList() const;

    VEditArea *getEditArea() const;

    VSnippetList *getSnippetList() const;

    VCart *getCart() const;

    VDirectoryTree *getDirectoryTree() const;

    VNotebookSelector *getNotebookSelector() const;

    VHistoryList *getHistoryList() const;

    // View and edit the information of @p_file, which is an orphan file.
    void editOrphanFileInfo(VFile *p_file);

    // Open files @p_files as orphan files or internal note files.
    // If @p_forceOrphan is false, for each file, VNote will try to find out if
    // it is a note inside VNote. If yes, VNote will open it as internal file.
    QVector<VFile *> openFiles(const QStringList &p_files,
                               bool p_forceOrphan = false,
                               OpenFileMode p_mode = OpenFileMode::Read,
                               bool p_forceMode = false,
                               bool p_oneByOne = false);

    // Try to open @p_filePath as internal note.
    bool tryOpenInternalFile(const QString &p_filePath);

    // Show a temporary message in status bar.
    void showStatusMessage(const QString &p_msg);

    // Open startup pages according to configuration.
    void openStartupPages();

    VCaptain *getCaptain() const;

    // Prompt user for new notebook if there is no notebook.
    void promptNewNotebookIfEmpty();

    // Check invalid notebooks. Set current notebook according to config.
    void checkNotebooks();

    VFile *getCurrentFile() const;

    VEditTab *getCurrentTab() const;

    VNotebook *getCurrentNotebook() const;

    // Kick off timer to do things after start.
    void kickOffStartUpTimer(const QStringList &p_files);

    void focusEditArea() const;

    void showExplorerPanel(bool p_focus = false);

    VExplorer *getExplorer() const;

    void setCaptainModeEnabled(bool p_enabled);

public slots:
    void restartVNote();

signals:
    // Emit when editor related configurations were changed by user.
    void editorConfigUpdated();

private slots:
    void importNoteFromFile();
    void viewSettings();
    void changeMarkdownConverter(QAction *action);
    void aboutMessage();

    // Display shortcuts help.
    void shortcutsHelp();

    void changeExpandTab(bool checked);
    void setTabStopWidth(QAction *action);
    void setEditorBackgroundColor(QAction *action);
    void setRenderBackgroundColor(QAction *action);

    void changeHighlightCursorLine(bool p_checked);
    void changeHighlightSelectedWord(bool p_checked);
    void changeHighlightSearchedWord(bool p_checked);
    void changeHighlightTrailingSapce(bool p_checked);
    void curEditFileInfo();
    void deleteCurNote();
    void handleCurrentDirectoryChanged(const VDirectory *p_dir);
    void handleCurrentNotebookChanged(const VNotebook *p_notebook);
    void handleFindDialogTextChanged(const QString &p_text, uint p_options);
    void openFindDialog();
    void enableMermaid(bool p_checked);
    void enableMathjax(bool p_checked);
    void changeAutoIndent(bool p_checked);
    void changeAutoList(bool p_checked);
    void enableCodeBlockHighlight(bool p_checked);
    void enableImagePreview(bool p_checked);
    void enableImagePreviewConstraint(bool p_checked);
    void enableImageConstraint(bool p_checked);
    void enableImageCaption(bool p_checked);
    void printNote();

    // Open export dialog.
    void handleExportAct();

    // Handle Vim status updated.
    void handleVimStatusUpdated(const VVim *p_vim);

    // Handle the status update of the current tab of VEditArea.
    // Will be called frequently.
    void handleAreaTabStatusUpdated(const VEditTabInfo &p_info);

    // Check the shared memory between different instances to see if we have
    // files to open.
    void checkSharedMemory();

    void quitApp();

    // Restore main window.
    void showMainWindow();

    // Close current note.
    void closeCurrentFile();

    // Open flash page in edit mode.
    void openFlashPage();

    void openQuickAccess();

    void customShortcut();

    void toggleEditReadMode();

    // Activate Universal Entry.
    void activateUniversalEntry();

    void stayOnTop(bool p_enabled);

    void splitFileListOut(bool p_enabled);

    void collectUserStat() const;

protected:
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;

    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

    void changeEvent(QEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI();

    void setupNaviBox();

    void setupNotebookPanel();

    void setupFileListSplitOut(bool p_enabled);

    void initToolBar();

    QToolBar *initFileToolBar(QSize p_iconSize = QSize());

    QToolBar *initViewToolBar(QSize p_iconSize = QSize());

    QToolBar *initNoteToolBar(QSize p_iconSize = QSize());

    QToolBar *initEditToolBar(QSize p_iconSize = QSize());

    void initMenuBar();
    void initFileMenu();
    void initEditMenu();
    void initViewMenu();
    void initMarkdownMenu();
    void initHelpMenu();
    void initSyncMenu();
    void initSync();
    void upload();
    void download();
    void onDownloadSuccess();
    void onUploadSuccess();

    void initDockWindows();

    void initToolsDock();

    void initSearchDock();

    void initRenderBackgroundMenu(QMenu *menu);

    void initRenderStyleMenu(QMenu *p_menu);

    void initCodeBlockStyleMenu(QMenu *p_menu);

    void initConverterMenu(QMenu *p_menu);

    void initMarkdownitOptionMenu(QMenu *p_menu);

    void initMarkdownExtensionMenu(QMenu *p_menu);

    void initEditorBackgroundMenu(QMenu *menu);

    // Init the Line Number submenu in Edit menu.
    void initEditorLineNumberMenu(QMenu *p_menu);

    void initEditorStyleMenu(QMenu *p_menu);

    void initAutoScrollCursorLineMenu(QMenu *p_menu);

    void updateWindowTitle(const QString &str);

    void initVimCmd();

    // Update state of actions according to @p_tab.
    void updateActionsStateFromTab(const VEditTab *p_tab);

    void saveStateAndGeometry();
    void restoreStateAndGeometry();

    // Should init VCaptain before setupUI().
    void initCaptain();

    void registerCaptainAndNavigationTargets();

    // Update status bar information.
    void updateStatusInfo(const VEditTabInfo &p_info);

    // Wrapper to create a QAction.
    QAction *newAction(const QIcon &p_icon,
                       const QString &p_text,
                       QObject *p_parent = nullptr);

    // Init a timer to watch the change of the shared memory between instances of
    // VNote.
    void initSharedMemoryWatcher();

    void initUpdateTimer();

    // Init system tray icon and correspondign context menu.
    void initTrayIcon();

    // Change the panel view according to @p_state.
    void changePanelView(PanelViewState p_state);

    // Whether heading sequence is applicable to current tab.
    // Only available for writable Markdown file.
    bool isHeadingSequenceApplicable() const;

    void initShortcuts();

    void initHeadingButton(QToolBar *p_tb);

    void initThemeMenu(QMenu *p_emnu);

    void updateEditReadAct(const VEditTab *p_tab);

    void initUniversalEntry();

    void setMenuBarVisible(bool p_visible);

    void setToolBarVisible(bool p_visible);

    void showNotebookPanel();

    void updateFontOfAllTabs();

    void promptForVNoteRestart();

    void checkIfNeedToShowWelcomePage();

    // Captain mode functions.

    // Popup the attachment list if it is enabled.
    static bool showAttachmentListByCaptain(void *p_target, void *p_data);

    static bool locateCurrentFileByCaptain(void *p_target, void *p_data);

    static bool toggleExpandModeByCaptain(void *p_target, void *p_data);

    static bool currentNoteInfoByCaptain(void *p_target, void *p_data);

    static bool discardAndReadByCaptain(void *p_target, void *p_data);

    static bool toggleToolBarByCaptain(void *p_target, void *p_data);

    static bool toggleToolsDockByCaptain(void *p_target, void *p_data);

    static bool toggleSearchDockByCaptain(void *p_target, void *p_data);

    static bool closeFileByCaptain(void *p_target, void *p_data);

    static bool shortcutsHelpByCaptain(void *p_target, void *p_data);

    static bool flushLogFileByCaptain(void *p_target, void *p_data);

    static bool exportByCaptain(void *p_target, void *p_data);

    static bool focusEditAreaByCaptain(void *p_target, void *p_data);

    // End Captain mode functions.

    VNote *vnote;
    QPointer<VFile> m_curFile;
    QPointer<VEditTab> m_curTab;

    VCaptain *m_captain;

    VNotebookSelector *m_notebookSelector;

    VFileList *m_fileList;

    VDirectoryTree *m_dirTree;

    VToolBox *m_naviBox;

    // Splitter for directory | files | edit.
    QSplitter *m_mainSplitter;

    // Splitter for folders/notes.
    QSplitter *m_nbSplitter;

    VEditArea *m_editArea;

    QDockWidget *m_toolDock;

    QDockWidget *m_searchDock;

    // Tool box in the dock widget.
    VToolBox *m_toolBox;

    VOutline *outline;

    // View and manage snippets.
    VSnippetList *m_snippetList;

    // View and manage cart.
    VCart *m_cart;

    // Advanced search.
    VSearcher *m_searcher;

    VFindReplaceDialog *m_findReplaceDialog;

    VVimCmdLineEdit *m_vimCmd;

    VVimIndicator *m_vimIndicator;

    VTabIndicator *m_tabIndicator;

    // Actions
    QAction *newRootDirAct;
    QAction *newNoteAct;
    QAction *noteInfoAct;

    QAction *deleteNoteAct;

    // Toggle read and edit note.
    QAction *m_editReadAct;

    QAction *saveNoteAct;

    QAction *m_discardExitAct;

    QAction *expandViewAct;

    QAction *m_importNoteAct;

    QAction *m_printAct;

    QAction *m_exportAct;

    QAction *m_findReplaceAct;

    QAction *m_findNextAct;
    QAction *m_findPreviousAct;
    QAction *m_replaceAct;
    QAction *m_replaceFindAct;
    QAction *m_replaceAllAct;

    QAction *m_autoIndentAct;

    // Enable heading sequence for current note.
    QAction *m_headingSequenceAct;

    QAction *m_toolBarAct;

    // Act group for render styles.
    QActionGroup *m_renderStyleActs;

    // Act group for code block render styles.
    QActionGroup *m_codeBlockStyleActs;

    // Menus
    QMenu *m_viewMenu;

    QToolBar *m_fileToolBar;

    QToolBar *m_viewToolBar;

    QToolBar *m_editToolBar;

    QToolBar *m_noteToolBar;

    // sync menu
    QMenu *m_syncMenu;

    // All the ToolBar.
    QVector<QToolBar *> m_toolBars;

    // Attachment button.
    VButtonWithWidget *m_attachmentBtn;

    // Attachment list.
    VAttachmentList *m_attachmentList;

    QPushButton *m_headingBtn;

    QPushButton *m_avatarBtn;

    // Single instance guard.
    VSingleInstanceGuard *m_guard;

    // Timer to check the shared memory between instances of VNote.
    QTimer *m_sharedMemTimer;

    // Timer to update gui.
    // Sometimes the toolbar buttons do not refresh themselves.
    QTimer *m_updateTimer;

    // Tray icon.
    QSystemTrayIcon *m_trayIcon;

    // The old state of window.
    Qt::WindowStates m_windowOldState;

    // Whether user request VNote to quit.
    bool m_requestQuit;

    VWebUtils m_webUtils;

    QPrinter *m_printer;

    VUniversalEntry *m_ue;

    VHistoryList *m_historyList;

    VExplorer *m_explorer;

    VTagExplorer *m_tagExplorer;

    VSync *m_git;

    // Whether sync note list to current tab.
    bool m_syncNoteListToCurrentTab;

    // Interval of the shared memory timer in ms.
    static const int c_sharedMemTimerInterval;
};

inline VFileList *VMainWindow::getFileList() const
{
    return m_fileList;
}

inline VEditArea *VMainWindow::getEditArea() const
{
    return m_editArea;
}

inline VCaptain *VMainWindow::getCaptain() const
{
    return m_captain;
}

inline VFile *VMainWindow::getCurrentFile() const
{
    return m_curFile;
}

inline VEditTab *VMainWindow::getCurrentTab() const
{
    return m_curTab;
}

inline VSnippetList *VMainWindow::getSnippetList() const
{
    return m_snippetList;
}

inline VCart *VMainWindow::getCart() const
{
    return m_cart;
}

inline VHistoryList *VMainWindow::getHistoryList() const
{
    return m_historyList;
}

inline VDirectoryTree *VMainWindow::getDirectoryTree() const
{
    return m_dirTree;
}

inline VNotebookSelector *VMainWindow::getNotebookSelector() const
{
    return m_notebookSelector;
}

inline VExplorer *VMainWindow::getExplorer() const
{
    return m_explorer;
}
#endif // VMAINWINDOW_H
