#ifndef MAINWINDOW2_H
#define MAINWINDOW2_H

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

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
class CommentPanel;
class TagExplorer2;
class SnippetPanel2;
class SearchPanel2;
class LocationList2;
class ViewArea2;
class ViewWindow2;
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

// Non-UI state machine behind MainWindow2's macOS Service note capture: a FIFO
// of pending requests, a startup-readiness bit, and a reentrancy guard.
//
// It is deliberately independent of MainWindow2 (which merely owns one and
// supplies the modal handler) so the queueing/serialization contract can be
// unit-tested without constructing the full window. Defined inline because
// there is no widgets static library: a test must not have to compile
// mainwindow2.cpp (and its whole transitive widget graph) to exercise this.
//
// Semantics:
//   - Requests received before setReady() are queued, never handled.
//   - setReady() drains the queue in FIFO order and is idempotent.
//   - A request that arrives while the handler is running (i.e. from inside a
//     modal dialog's nested event loop) is appended and handled after the
//     current one returns, so handler depth never exceeds one.
class PendingCaptureDispatcher {
public:
  using Handler = std::function<void(const QString &)>;

  void setHandler(Handler p_handler) { m_handler = std::move(p_handler); }

  // Queue a capture request, draining immediately when already ready.
  void request(const QString &p_text) {
    m_pending.append(p_text);
    if (m_ready) {
      drain();
    }
  }

  // Mark startup complete and drain whatever accumulated. Idempotent.
  void setReady() {
    if (m_ready) {
      return;
    }
    m_ready = true;
    drain();
  }

  bool isReady() const { return m_ready; }

  int pendingCount() const { return m_pending.size(); }

private:
  void drain() {
    if (m_draining) {
      // A request arrived from inside the handler (the modal dialog's nested
      // event loop). It is already queued; the outer loop will pick it up.
      return;
    }

    m_draining = true;
    while (!m_pending.isEmpty()) {
      const QString text = m_pending.takeFirst();
      if (m_handler) {
        m_handler(text);
      }
    }
    m_draining = false;
  }

  Handler m_handler;

  QVector<QString> m_pending;

  bool m_ready = false;

  // True while drain() is running, so a nested request only appends.
  bool m_draining = false;
};

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

  // Menu entry point. Delegates to UpdateController, which checks for a newer
  // release and offers the release page; VNote downloads nothing itself.
  void checkForUpdates();

  void showMainWindow();

  // Entry point for a macOS "Create Note in VNote" Service request. Queues the
  // text until post-initialization completes, then serially opens one capture
  // dialog per request.
  void requestNoteCapture(const QString &p_text);

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

  void setupCommentPanel();

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

  // @p_source: the view window requesting the export. When null, the current
  // view window of the main area is used. A detached window must pass itself.
  void exportNotes(ViewWindow2 *p_source = nullptr);

  // Actually open the resolved paths as external buffers. Assumes the view
  // area is ready to receive buffers.
  // @p_detached: open the files in a single detached view split.
  void doOpenFiles(const QStringList &p_paths, bool p_detached = false);

  // Drain m_pendingOpenBatches in arrival order once startup finishes. Detached
  // batches reset the controller's per-batch detached workspace synchronously
  // after each batch, so every queued --detached-view invocation opens into its
  // own detached window without relying on timer-ordering.
  void drainPendingOpenBatches();

  // Handle one dequeued capture request: foreground the window and run the
  // explorer's capture flow (which is modal for the duration).
  void handleNoteCapture(const QString &p_text);

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

  CommentPanel *m_commentPanel = nullptr;

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

  // macOS Service capture requests: queued until post-init completes, then
  // handled one at a time.
  PendingCaptureDispatcher m_captureDispatcher;

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
