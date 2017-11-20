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
class VToolBox;
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
class VButtonWithWidget;
class VAttachmentList;
class VSnippetList;

enum class PanelViewState
{
    ExpandMode,
    SinglePanel,
    TwoPanels,
    CompactMode,
    Invalid
};

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(VSingleInstanceGuard *p_guard, QWidget *p_parent = 0);

    const QVector<QPair<QString, QString> > &getPalette() const;

    // Returns true if the location succeeds.
    bool locateFile(VFile *p_file);

    VFileList *getFileList() const;

    VEditArea *getEditArea() const;

    VSnippetList *getSnippetList() const;

    // View and edit the information of @p_file, which is an orphan file.
    void editOrphanFileInfo(VFile *p_file);

    // Open files @p_files as orphan files or internal note files.
    // If @p_forceOrphan is false, for each file, VNote will try to find out if
    // it is a note inside VNote. If yes, VNote will open it as internal file.
    void openFiles(const QStringList &p_files,
                   bool p_forceOrphan = false,
                   OpenFileMode p_mode = OpenFileMode::Read,
                   bool p_forceMode = false);

    // Try to open @p_filePath as internal note.
    bool tryOpenInternalFile(const QString &p_filePath);

    // Show a temporary message in status bar.
    void showStatusMessage(const QString &p_msg);

    // Open startup pages according to configuration.
    void openStartupPages();

    VCaptain *getCaptain() const;

    // Prompt user for new notebook if there is no notebook.
    void promptNewNotebookIfEmpty();

    VFile *getCurrentFile() const;

    VEditTab *getCurrentTab() const;

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

    void setRenderStyle(QAction *p_action);

    void setEditorStyle(QAction *p_action);

    // Set code block render style.
    void setCodeBlockStyle(QAction *p_action);

    // Update the render styles menu according to existing files.
    void updateRenderStyleMenu();

    void updateEditorStyleMenu();

    // Update the code block styles menu according to existing files.
    void updateCodeBlockStyleMenu();

    void changeHighlightCursorLine(bool p_checked);
    void changeHighlightSelectedWord(bool p_checked);
    void changeHighlightSearchedWord(bool p_checked);
    void changeHighlightTrailingSapce(bool p_checked);
    void onePanelView();
    void twoPanelView();
    void compactModeView();
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
    void changeVimMode(bool p_checked);
    void enableCodeBlockHighlight(bool p_checked);
    void enableImagePreview(bool p_checked);
    void enableImagePreviewConstraint(bool p_checked);
    void enableImageConstraint(bool p_checked);
    void enableImageCaption(bool p_checked);
    void printNote();
    void exportAsPDF();

    // Set the panel view properly.
    void enableCompactMode(bool p_enabled);

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

    void initNoteToolBar(QSize p_iconSize = QSize());

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

    void initCodeBlockStyleMenu(QMenu *p_menu);

    void initConverterMenu(QMenu *p_menu);
    void initMarkdownitOptionMenu(QMenu *p_menu);
    void initEditorBackgroundMenu(QMenu *menu);

    // Init the Line Number submenu in Edit menu.
    void initEditorLineNumberMenu(QMenu *p_menu);

    void initEditorStyleMenu(QMenu *p_emnu);
    void updateWindowTitle(const QString &str);

    // Update state of actions according to @p_tab.
    void updateActionsStateFromTab(const VEditTab *p_tab);

    void saveStateAndGeometry();
    void restoreStateAndGeometry();
    void repositionAvatar();

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

    // Init system tray icon and correspondign context menu.
    void initTrayIcon();

    // Change the panel view according to @p_state.
    // Will not change m_panelViewState.
    void changePanelView(PanelViewState p_state);

    // Whether heading sequence is applicable to current tab.
    // Only available for writable Markdown file.
    bool isHeadingSequenceApplicable() const;

    void initShortcuts();

    // Captain mode functions.

    // Popup the attachment list if it is enabled.
    static void showAttachmentListByCaptain(void *p_target, void *p_data);

    static void locateCurrentFileByCaptain(void *p_target, void *p_data);

    static void toggleExpandModeByCaptain(void *p_target, void *p_data);

    static void toggleOnePanelViewByCaptain(void *p_target, void *p_data);

    static void discardAndReadByCaptain(void *p_target, void *p_data);

    static void toggleToolsDockByCaptain(void *p_target, void *p_data);

    static void closeFileByCaptain(void *p_target, void *p_data);

    static void shortcutsHelpByCaptain(void *p_target, void *p_data);

    static void flushLogFileByCaptain(void *p_target, void *p_data);

    // End Captain mode functions.

    VNote *vnote;
    QPointer<VFile> m_curFile;
    QPointer<VEditTab> m_curTab;

    VCaptain *m_captain;

    QLabel *notebookLabel;
    QLabel *directoryLabel;
    VNotebookSelector *notebookSelector;
    VFileList *m_fileList;
    VDirectoryTree *directoryTree;

    // Splitter for directory | files | edit.
    QSplitter *m_mainSplitter;

    // Splitter for directory | files.
    // Move directory and file panel in one compact vertical split.
    QSplitter *m_naviSplitter;

    VEditArea *editArea;

    QDockWidget *toolDock;

    // Tool box in the dock widget.
    VToolBox *m_toolBox;

    VOutline *outline;

    // View and manage snippets.
    VSnippetList *m_snippetList;

    VAvatar *m_avatar;
    VFindReplaceDialog *m_findReplaceDialog;
    VVimIndicator *m_vimIndicator;
    VTabIndicator *m_tabIndicator;

    // SinglePanel, TwoPanels, CompactMode.
    PanelViewState m_panelViewState;

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

    QAction *m_findReplaceAct;
    QAction *m_findNextAct;
    QAction *m_findPreviousAct;
    QAction *m_replaceAct;
    QAction *m_replaceFindAct;
    QAction *m_replaceAllAct;

    QAction *m_autoIndentAct;

    // Enable heading sequence for current note.
    QAction *m_headingSequenceAct;

    // Act group for render styles.
    QActionGroup *m_renderStyleActs;

    QActionGroup *m_editorStyleActs;

    // Act group for code block render styles.
    QActionGroup *m_codeBlockStyleActs;

    // Act group for panel view actions.
    QActionGroup *m_viewActGroup;

    // Menus
    QMenu *viewMenu;

    // Edit Toolbar.
    QToolBar *m_editToolBar;

    // Attachment button.
    VButtonWithWidget *m_attachmentBtn;

    // Attachment list.
    VAttachmentList *m_attachmentList;

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
    return m_fileList;
}

inline VEditArea *VMainWindow::getEditArea() const
{
    return editArea;
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

#endif // VMAINWINDOW_H
