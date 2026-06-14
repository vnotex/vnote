#ifndef NOTEBOOKEXPLORER2_H
#define NOTEBOOKEXPLORER2_H

#include <QFrame>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>

#include <core/global.h>
#include <core/noncopyable.h>
#include <nodeinfo.h>
#include <views/inodeexplorer.h>
#include <vxcore/vxcore_types.h>

class QAction;
class QFileSystemWatcher;
class QSplitter;
class QMenu;
class QActionGroup;
class QLabel;
class QTimer;
class QToolButton;

namespace vnotex {

class NotebookSelector2;
class NotebookSyncInfoDialog2;
class SyncService;
class TitleBar;
class INodeExplorer;
class TwoColumnsNodeExplorer;
class ServiceLocator;
struct FileOpenSettings;

// NotebookExplorer2 is a container widget that displays notebook nodes
// using proper MVC architecture.
// It supports two explore modes:
// - Combined: Single tree view showing all nodes
// - TwoColumns: Split view with folder tree on left, file list on right
//
// NOTE: This class uses NodeIdentifier/NodeInfo for all node references.
// Legacy Node* pointers are being phased out.
class NotebookExplorer2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  enum ExploreMode {
    Combined,  // Single tree with folders and files
    TwoColumns // Left: folders only, Right: files in selected folder
  };

  explicit NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~NotebookExplorer2() override;

  // Notebook management - using notebook ID instead of Notebook pointer
  void setCurrentNotebook(const QString &p_notebookId);
  QString currentNotebookId() const;

  // Navigation - using NodeIdentifier instead of Node*
  void setCurrentNode(const NodeIdentifier &p_nodeId);
  NodeIdentifier currentNodeId() const;

  NotebookSelector2 *getNotebookSelector() const;
  INodeExplorer *getNodeExplorer() const;

  // Mode switching
  void setExploreMode(ExploreMode p_mode);
  ExploreMode exploreMode() const;

  // Get current explored folder node (for newNote/newFolder operations)
  NodeIdentifier currentExploredFolderId() const;

  // Get current explored node
  NodeIdentifier currentExploredNodeId() const;

  // State saving/restoring for session
  QByteArray saveState() const;
  void restoreState(const QByteArray &p_data);
  void applyRestoredSessionState();

public slots:
  // Notebook operations - these launch dialogs
  void newNotebook();
  void newNotebookFromFolder();
  void importNotebook();
  void openVNote3Notebook();
  void manageNotebooks();

  // Reload operations
  void loadNotebooks();

  // Node creation - use current explored folder as parent
  void newFolder();
  void newNote();
  void newQuickNote();

  // Import operations - use current explored folder as parent
  void importFile();
  void importFolder();

  // Locate a node in the explorer (switch notebook if needed, expand, select, scroll).
  void locateNode(const NodeIdentifier &p_nodeId);

#ifdef VNOTE_TESTING
  // Test-only seam (per ADR-6) that simulates the post-newNotebook() auto-open
  // flow without driving the actual NewNotebookDialog2. Mirrors the same code
  // path used inside newNotebook() after dialog Accept: when @p_syncMethod
  // equals "git" it pops a NotebookSyncInfoDialog2 in bootstrap mode for
  // @p_notebookId.
  void testTriggerNewNotebookCreated(const QString &p_notebookId, const QString &p_syncMethod);
#endif

private slots:
  // Node activation — opens the file via BufferService
  void onNodeActivated(const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings);

  // GUI request handlers from controller signals
  void onNewNoteRequested(const NodeIdentifier &p_parentId);
  void onNewFolderRequested(const NodeIdentifier &p_parentId);
  void onRenameRequested(const NodeIdentifier &p_nodeId, const QString &p_currentName);
  void onDeleteRequested(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent);
  void onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds);
  void onPropertiesRequested(const NodeIdentifier &p_nodeId);
  void onMarkRequested(const QList<NodeIdentifier> &p_ids);
  void onIgnoreRequested(const NodeIdentifier &p_nodeId);
  void onManageTagsRequested(const NodeIdentifier &p_nodeId);
  void onErrorOccurred(const QString &p_title, const QString &p_message);
  void onInfoMessage(const QString &p_title, const QString &p_message);

  // T11 (notebook-explorer-drag-reorder): handles the sortRequested signal
  // forwarded from CombinedNodeExplorer / TwoColumnsNodeExplorer. Owns the
  // SortDialog2 instances (controllers MUST NOT show QDialog per
  // src/controllers/AGENTS.md). Shows two dialogs in sequence (folders then
  // files; strict separation) and routes the result through
  // INodeExplorer::requestReorderNodes.
  void onSortRequested(const NodeIdentifier &p_parentId);

  // Sync UI handlers (T15).
  void onSyncButtonClicked();
  void onSyncInfoActionTriggered();
  // Surface sync failures to the user. Wired to SyncService::syncFailed.
  // Routes by error code: auth/network → modal dialog with "Open Sync Info"
  // shortcut; conflict is handled elsewhere (MainWindow2). Anti-spam guards
  // (m_authFailureNotified / m_networkFailureNotified) ensure at most one
  // popup per notebook per "failure streak" — cleared on the next successful
  // sync or notebook switch.
  void onSyncFailedSurface(const QString &p_notebookId, VxCoreError p_code,
                           const QString &p_message);
  // Recompute enabled state and tooltip for the title-bar Sync button and
  // "Notebook Sync Info..." menu entry. Wired to SyncService lifecycle signals
  // and to currentNotebookChanged.
  void updateSyncButtonState();

signals:
  void currentNotebookChanged(const QString &p_notebookId);
  void currentExploredFolderChanged(const NodeIdentifier &p_folderId);
  void exportNodeRequested(const NodeIdentifier &p_nodeId);

private:
  void setupUI();
  void setupTitleBar();
  void setupTitleBarMenu();
  void setupRecycleBinMenu();
  void setupExploreModeMenu();
  void setupViewMenu(QMenu *p_menu, bool p_isNotebookView);
  void setupCombinedMode();
  void setupTwoColumnsMode();
  void connectSignals();
  void updateExploreMode();
  void updateFocusProxy();

  // Notebook database rebuild
  void rebuildDatabase();

  // Import file/folder helpers (called from toolbar actions)
  void onImportFilesRequested(const NodeIdentifier &p_targetFolderId);
  void onImportFolderRequested(const NodeIdentifier &p_targetFolderId);
  void setCurrentNotebookInternal(const QString &p_notebookId);
  void updateRecycleBinMenuState();
  void cacheCurrentExplorerState();
  void applyCachedExplorerState(const QString &p_notebookId);
  // Apply view order to all node views
  void setNodeViewOrder(ViewOrder p_order);

  // File system watcher methods
  void setupFileWatcher();
  void teardownFileWatcher();
  void onFileSystemChanged(const QString &p_path);
  void onFsReloadTimeout();
  void addWatchPath(const QString &p_path);
  void removeWatchPath(const QString &p_path);
  void syncWatchedPaths();

  // Register p_absolutePath as an app-initiated change whose next
  // directoryChanged event should be swallowed. Idempotent.
  void expectFsChange(const QString &p_absolutePath, int p_windowMs = 3000);

  // If p_absolutePath has a non-expired entry, remove it and return true.
  // Else return false. Purges expired entries opportunistically.
  bool consumeExpectedFsChange(const QString &p_absolutePath);

  // Services
  ServiceLocator &m_services;

  // Node explorer - polymorphic, recreated when mode changes
  INodeExplorer *m_nodeExplorer = nullptr;

  // UI Components
  TitleBar *m_titleBar = nullptr;
  NotebookSelector2 *m_notebookSelector = nullptr;
  // T26: visible lock badge that surfaces a notebook's read-only state next
  // to the selector. Hidden by default; toggled from setCurrentNotebookInternal
  // based on NotebookCoreService::isNotebookReadOnly.
  QLabel *m_readOnlyBadgeLabel = nullptr;
  QVBoxLayout *m_mainLayout = nullptr;

  // State
  ExploreMode m_exploreMode = Combined;
  QString m_currentNotebookId;
  QHash<QString, NodeExplorerState> m_notebookStateCache;
  bool m_hasRestoredSessionStatePending = false;
  QAction *m_openRecycleBinAction = nullptr;
  QAction *m_emptyRecycleBinAction = nullptr;

  // Sync UI elements (T15). Always-instantiated; visibility/enabled state is
  // driven by updateSyncButtonState().
  QToolButton *m_syncButton = nullptr;
  QAction *m_syncInfoAction = nullptr;

  // In-memory reconcile error tracking (W4.T3). Maps notebookId -> error code.
  // Cleared on sync success or notebook switch. Used to surface errors via tooltip.
  QHash<QString, int> m_lastReconcileError;

  // Anti-spam set for auth/network sync-failure modal popups. Once we have
  // shown the user an auth-failed dialog for a notebook, we suppress further
  // popups for the same notebook until the next successful sync (clears the
  // entry), the user switches notebooks, or sync is disabled. Without this
  // the auto-sync path would pop a modal on every 3 s save tick.
  QSet<QString> m_authFailureNotified;
  QSet<QString> m_networkFailureNotified;

  // Manual-sync feedback set. Populated in onSyncButtonClicked when a user
  // explicitly invokes Sync Now (state S5/S7 non-cancel branch). Consumed in
  // the syncFinished handler on VXCORE_OK to emit a brief status-bar
  // confirmation. Auto-sync success NEVER pushes a status message (would be
  // noisy on every save tick), so this set is the only way the syncFinished
  // OK lambda decides whether to notify the user.
  QSet<QString> m_pendingManualSyncFeedback;

  // Credential-update auto-retry arm. Populated in onSyncFailedSurface when an
  // auth-failure dialog is shown. Consumed in the credentialsSetFinished(OK)
  // handler: if the arm is set for the notebook, immediately call
  // SyncService::triggerSyncNow so the user sees the verdict of their new
  // PAT without an extra click. Tagged for manual feedback so the result
  // surfaces through the same Sync Now success/failure UX. Cleared on
  // notebook switch, disable, and on successful sync.
  QSet<QString> m_credentialUpdateRetryArm;

  // T3 (sync-in-progress-ux): defer-one-retry set for credential-update
  // race. When credentialsSetFinished(OK) arrives but a sync is already in
  // flight for the notebook (isSyncInProgress(id)==true), insert the
  // notebookId here instead of calling triggerSyncNow immediately. The
  // syncFinished(OK) handler consumes the entry and fires the deferred
  // trigger. ONE-SHOT per credentialsSetFinished — adding the same id twice
  // is idempotent (QSet semantics) so we never pile up retries.
  QSet<QString> m_deferredCredentialRetry;

  // File system watcher for detecting external changes
  QFileSystemWatcher *m_fsWatcher = nullptr;
  QTimer *m_fsReloadTimer = nullptr;
  QString m_lastChangedDir;

  // Map of absolute filesystem paths the explorer expects to receive a
  // directoryChanged() event for, with an epoch-ms deadline after which the
  // expectation auto-expires.
  QHash<QString, qint64> m_expectedFsChangesDeadline;

  // Hook handle for FileBeforeSave subscription; -1 when unregistered.
  // Stored so the destructor can call HookManager::removeAction symmetrically.
  int m_fileBeforeSaveHookId = -1;

  // Set of notebook IDs whose sync is in flight. While a notebook's ID is in
  // this set, fs events on its watched paths are dropped without arming the
  // debounce timer. Maintained by syncStarted/syncFinished/syncCancelled/
  // disableFinished/currentNotebookChanged handlers.
  QSet<QString> m_activeSyncFsSuppression;

  // Singleshot timer started after syncFinished. Keeps suppression active for
  // 2 s of grace so late fs deliveries from the post-stage network rebase are
  // still swallowed. Owned by Qt parent (this); do NOT delete manually.
  QTimer *m_syncGraceTimer = nullptr;
};

} // namespace vnotex

#endif // NOTEBOOKEXPLORER2_H
