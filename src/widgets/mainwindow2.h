#ifndef MAINWINDOW2_H
#define MAINWINDOW2_H

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include <core/noncopyable.h>
#include <widgets/dockwidgethelper.h>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QDockWidget;
class QToolBar;
class QWebEngineView;
class QProgressDialog;
class QSystemTrayIcon;

namespace QWK {
class WidgetWindowAgent;
}

namespace vnotex {

class ServiceLocator;
class NotebookExplorer2;
class OutlineViewer;
class TagExplorer2;
class SnippetPanel2;
class SearchPanel2;
class LocationList2;
class ViewArea2;
class ExportDialog2;
class TaskPanel2;
class ConsoleViewer;
class MainWindowTaskContext;

class ToolBarHelper2;
class SyncConflictController;
class FirstRunController;
class UpdateController;
class NotificationRouter;
class NotificationToast;

// MainWindow2 is a minimal QMainWindow shell for the new clean architecture.
// Receives ServiceLocator via constructor for dependency injection.
// Framework only - NO toolbar, dock widgets, menu bar, or status bar.
// Widgets will be added incrementally during migration.
class MainWindow2 : public QMainWindow, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives ServiceLocator reference via DI.
  // @p_serviceLocator: Reference to ServiceLocator (non-owning). Must outlive MainWindow2.
  // @p_parent: Optional QWidget parent.
  explicit MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent = nullptr);

  ~MainWindow2();

  // Access to ServiceLocator for child widgets that need services.
  ServiceLocator &getServiceLocator();

  // Access NotebookExplorer2.
  NotebookExplorer2 *getNotebookExplorer() const;

  // Access ViewArea2.
  ViewArea2 *getViewArea() const;

  QWidget *getDockWidget(DockWidgetHelper::DockType p_dockType) const;

  void kickOffPostInit(const QStringList &p_pathsToOpen, bool p_detached = false);

  // Open the given files/folders (e.g. command-line paths or files forwarded
  // from a second instance). Files are opened as external buffers so they can
  // be viewed/edited without a notebook. If post-initialization has not yet
  // completed (workspace/core propagation still disabled during session
  // restore), the paths are queued and opened once startup finishes.
  // @p_detached: open the files in a single detached view split.
  void openFiles(const QStringList &p_paths, bool p_detached = false);

  void setupNavigationMode();

  // Content area expansion.
  bool isContentAreaExpanded() const;
  void setContentAreaExpanded(bool p_expanded);

  // Window state.
  void setStayOnTop(bool p_enabled);

  bool isFrameless() const;

  // Access dock widgets.
  const QVector<QDockWidget *> &getDocks() const;

  // Reset window state and geometry.
  void resetStateAndGeometry();

  void restart();

  // Menu entry point. Delegates to UpdateController, which decides between the
  // in-app update flow and simply opening the releases page.
  void checkForUpdates();

  // Quits with kExitToApplyUpdate so main() applies the staged incremental
  // update (after every service and Application are destroyed) and then spawns
  // the replacement. Sets only m_requestQuit; the update lease is owned by
  // main() and is never touched from here.
  void restartForUpdate();

  void showMainWindow();

  void quitApp();

signals:
  void windowStateChanged(Qt::WindowStates p_state);

  void layoutChanged();

  void minimizedToSystemTray();

  // File operations.
  void newNoteRequested();
  void newFolderRequested();
  void importFileRequested();
  void importFolderRequested();
  void exportRequested();

protected:
  void closeEvent(QCloseEvent *p_event) override;

  void changeEvent(QEvent *p_event) Q_DECL_OVERRIDE;

  void dragEnterEvent(QDragEnterEvent *p_event) override;
  void dropEvent(QDropEvent *p_event) override;

private:
  // Setup basic window properties (title, size, central widget).
  void setupUI();

  // Setup NotebookExplorer2 as dock widget.
  void setupNotebookExplorer();

  // Setup OutlineViewer as dock widget.
  void setupOutlineViewer();

  // Setup TagExplorer2 as dock widget.
  void setupTagExplorer();

  // Setup SnippetPanel2 as dock widget.
  void setupSnippetExplorer();

  // Setup TaskPanel2 as dock widget.
  void setupTaskPanel();

  // Setup ConsoleViewer as dock widget.
  void setupConsoleViewer();

  // Setup SearchPanel2 as dock widget.
  void setupSearchPanel();

  // Setup LocationList2 as dock widget.
  void setupLocationList();

  // Setup ViewArea2 as central widget.
  void setupViewArea();

  // Setup dock widgets.
  void setupDocks();

  // Setup tool bar.
  void setupToolBar();

  // Setup qwindowkit window agent for frameless mode.
  void setupWindowAgent();

  // Update the OS window title to "<current note name> - VNote".
  // No-op when the custom (frameless) title bar is in use.
  void updateWindowTitle();

  void exportNotes();

  // Actually open the resolved paths as external buffers. Assumes the view
  // area is ready to receive buffers.
  // @p_detached: open the files in a single detached view split.
  void doOpenFiles(const QStringList &p_paths, bool p_detached = false);

  // Drain m_pendingOpenBatches in arrival order once startup finishes. Detached
  // batches reset the controller's per-batch detached workspace synchronously
  // after each batch, so every queued --detached-view invocation opens into its
  // own detached window without relying on timer-ordering.
  void drainPendingOpenBatches();
  void setupSystemTray();

  // Create the notification toast (transient surface for Attention::Interrupt)
  // and the NotificationRouter (producer side), and connect the widget-owned
  // sources the router cannot reach on its own.
  void setupNotifications();

  // Register the global (system-wide) wake-up hotkey (Global_WakeUp) to show the
  // main window. No-op if the configured shortcut is empty or registration fails.
  void setupGlobalHotkey();

  // Register the single window-level CloseFocus shortcut. Its handler inspects
  // QApplication::focusWidget(): if focus is inside a dock, that dock is hidden;
  // otherwise the current view window (tab) is closed. A single registration is
  // used deliberately: two same-sequence Qt::WindowShortcut shortcuts in one
  // window would trigger activatedAmbiguously and neither would fire.
  void setupCloseFocusedShortcut();

  // Restore only window geometry and dock state (safe to call before event loop).
  void restoreWindowGeometry();

  // Restore explorer state and view area layout (must be called after show()).
  void loadStateAndGeometry();

  void saveStateAndGeometry();

  // Clamp dock widths/heights that fall outside acceptable bounds after state restore.
  void validateDockProportions();

  // Theme switch orchestration slot.
  void onThemeChanged();

  // Non-owning reference to ServiceLocator.
  ServiceLocator &m_serviceLocator;

  DockWidgetHelper m_dockWidgetHelper{this, m_serviceLocator};

  // NotebookExplorer2 dock widget.
  NotebookExplorer2 *m_notebookExplorer = nullptr;

  // OutlineViewer dock widget.
  OutlineViewer *m_outlineViewer = nullptr;

  // TagExplorer2 dock widget.
  TagExplorer2 *m_tagExplorer = nullptr;

  // SnippetPanel2 dock widget.
  SnippetPanel2 *m_snippetPanel = nullptr;

  // TaskPanel2 dock widget.
  TaskPanel2 *m_taskPanel = nullptr;

  // ConsoleViewer dock widget (bottom ConsoleDock).
  ConsoleViewer *m_consoleViewer = nullptr;

  // Production ITaskContext injected into TaskService. Owned by MainWindow2;
  // reset to nullptr on TaskService before destruction.
  MainWindowTaskContext *m_taskContext = nullptr;

  // SearchPanel2 dock widget.
  SearchPanel2 *m_searchPanel = nullptr;

  // LocationList2 dock widget.
  LocationList2 *m_locationList = nullptr;

  // ViewArea2 central widget.
  ViewArea2 *m_viewArea = nullptr;

  // Toolbar helper.
  ToolBarHelper2 *m_toolBarHelper = nullptr;

  // Long-lived conflict-resolution orchestrator (T13). Owned by MainWindow2.
  // Wired in setupUI() to SyncService::conflictsDetected.
  SyncConflictController *m_syncConflictController = nullptr;

  // First-run experience controller (creates a default notebook on version
  // change when zero notebooks exist). Owned by MainWindow2; surfacing handled
  // by the defaultNotebookCreated connection in setupUI().
  FirstRunController *m_firstRunController = nullptr;

  // Owns all incremental-update policy (throttle, skip, prompts, notifications).
  UpdateController *m_updateController = nullptr;

  // Transient surface for Attention::Interrupt notifications. A child widget,
  // so it can never take window activation.
  NotificationToast *m_notificationToast = nullptr;

  // Turns subsystem failure signals into notifications.
  NotificationRouter *m_notificationRouter = nullptr;

  // Per-notebook retry counter for sync conflict resolution. Incremented each
  // time conflictsDetected fires for a notebook. When the count exceeds 3, the
  // conflict dialog is suppressed and a QMessageBox::warning is shown instead
  // (prevents an infinite resolve/re-conflict loop). Reset on a clean
  // syncFinished or when the user abandons the conflict dialog.
  QHash<QString, int> m_syncRetryCount;

  ExportDialog2 *m_exportDialog = nullptr;

  QSystemTrayIcon *m_trayIcon = nullptr;

  // Theme switch progress dialog.
  QProgressDialog *m_progressDialog = nullptr;

  // Content area expanded state.
  bool m_contentAreaExpanded = false;

  // Dock visibility saved before content area expansion, for restoring later.
  QStringList m_visibleDocksBeforeExpand;

  bool m_layoutReset = false;

  // True once kickOffPostInit's deferred startup work has completed and the
  // view area has re-enabled core propagation. Until then, openFiles() queues
  // its paths in m_pendingOpenBatches instead of opening immediately.
  bool m_postInitComplete = false;

  // Command-line / forwarded paths queued before post-init completed; drained
  // in arrival order once startup finishes. Each batch is one invocation so the
  // normal/detached ordering between separate invocations is preserved, and a
  // detached batch opens into its own detached window.
  struct PendingOpenBatch {
    QStringList m_paths;
    bool m_detached = false;
  };
  QVector<PendingOpenBatch> m_pendingOpenBatches;

  // -1: do not request to quit;
  // 0 and above: exit code.
  int m_requestQuit = -1;

  Qt::WindowStates m_windowOldState = Qt::WindowMinimized;

  QWK::WidgetWindowAgent *m_windowAgent = nullptr;

  bool m_frameless = false;

  // Tracks the current ViewWindow2::nameChanged connection so the title updates on rename.
  QMetaObject::Connection m_currentWindowNameConn;

#if defined(Q_OS_WIN)
  QWebEngineView *m_dummyWebView = nullptr;
#endif
};

} // namespace vnotex

#endif // MAINWINDOW2_H

