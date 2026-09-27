#ifndef SYNCSERVICE_H
#define SYNCSERVICE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "syncsettings.h"
#include <core/noncopyable.h>

#include <vxcore/vxcore_types.h>

class QTimer;

namespace vnotex {

class ServiceLocator;
class NotebookCoreService;
class SyncCredentialsStore;
class SyncWorkQueueManager;
class EventBridge;
struct NotebookOpenEvent;

// GUI-thread facade over SyncWorkQueueManager. Each staged sync owns its token
// and deferred-apply handshake through GUI refresh acknowledgement; workers
// never access metadata.db or wait for GUI work while holding NotebookIoGate.
//
// Per ADR-1: SyncService NEVER pulls in sync/sync_manager.h directly; all
// vxcore sync calls go through NotebookCoreService.
//
// Credentials are routed through SyncCredentialsStore and never cached in
// service members. Secrets live only in transient operation captures.
//
// Test seams are unconditional to avoid duplicate-symbol problems in direct
// compile tests. NotebookBeforeClose refuses queued or running sync; shutdown
// is wired to aboutToQuit and does not depend on further GUI event delivery.
class SyncService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit SyncService(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~SyncService() override;

  // Validate settings, save credentials to the OS vault, then enqueue enable.
  // A vault failure stops before any backend/network work.
  void enableSyncForNotebook(const QString &p_notebookId, const SyncSettings &p_settings);

  // Disable sync for a notebook. Invokes SyncWorker::disableSync; on
  // disableFinished, deletes the keychain entry via SyncCredentialsStore.
  void disableSyncForNotebook(const QString &p_notebookId);

  // Release the vxcore sync runtime for a notebook (frees the libgit2 repo
  // handle so Windows unmaps mmapped .pack files and closes their file
  // descriptors) WITHOUT touching on-disk sync config or the keychain PAT.
  // Safe to call on a notebook that was never sync-registered (idempotent).
  // Logs and swallows vxcore errors via qCWarning(syncCategory); the close
  // path MUST always proceed. NEVER logs PAT or remote URL values.
  //
  // Fired by the NotebookAfterClose hook handler in the SyncService ctor
  // BEFORE the existing keychain credential deletion, so the libgit2 handle
  // is released while the notebook ID is still meaningful and before any
  // other cleanup races.
  void unregisterSyncRuntime(const QString &p_notebookId);

  // Trigger a one-shot sync for a notebook. Invokes SyncWorker::triggerSync.
  void triggerSyncNow(const QString &p_notebookId);

  // Report that VNote wrote something into @p_notebookId's working tree WITHOUT
  // going through vxcore, so no `file.saved` event (and therefore no
  // `sync.should_run`) was emitted for it.
  //
  // Currently the only such writer is CommentService, which commits
  // `comments.json` with a plain QSaveFile. Without this the sidecar would sit
  // uncommitted in a synced notebook until some unrelated edit happened to
  // trigger a sync.
  //
  // This is a FACT, not "sync now": it enters the ordinary auto-sync path, so it
  // inherits every guard (shutdown, readiness, auth circuit-breaker), the
  // per-notebook trailing-throttle debounce and the "trigger" coalesce key. A
  // burst of comment edits therefore produces at most one network round-trip
  // per debounce window, never one per keystroke.
  void notifyWorkingTreeDirty(const QString &p_notebookId);

  // Wave 12.2 / F5.9: request cancellation of an in-flight triggerSyncNow for
  // @p_notebookId. No-op if no sync is in flight or the notebook has no
  // active cancellation token. Cancellation is cooperative: the libgit2
  // operation polls the token via SyncCancellation; once observed it returns
  // GIT_EUSER which the backend maps to GitOpError::Cancelled / a
  // VXCORE_ERR_* code that surfaces through the normal syncFinished path.
  void cancelSync(const QString &p_notebookId);

  // Ensure runtime sync state is populated for a notebook whose on-disk
  // config is now complete (sync_enabled=true, non-empty backend +
  // remoteUrl). Forces a reconcile attempt even if a prior attempt was
  // already made in this process (clears the notebookId from
  // m_reconcileAttempted first).
  //
  // Called by NotebookSyncInfoController after the user completes a
  // previously-partial sync config via the Sync Info dialog. Idempotent
  // for already-runtime-enabled notebooks (the reconcile check returns
  // early). Results surface via the existing reconcileFinished signal.
  void ensureSyncEnabled(const QString &p_notebookId);

  // F1.6 / Task 13.4 — Atomically enable sync for an existing partial notebook
  // AND persist the flat ADR-8 sync keys (syncEnabled / syncBackend /
  // syncRemoteUrl) to the notebook JSON. Replaces the prior two-step
  // enable-then-persist pattern in NotebookSyncInfoController::bootstrapApply
  // that left the notebook in S5 on disk-but-missing-syncRemoteUrl when the
  // persist step failed (next reconcile would observe S4 and bail).
  //
  // Sequence:
  //   1. enableSyncForNotebook(id, settings) — async via worker.
  //   2. On enableFinished VXCORE_OK: write the three flat sync keys to
  //      notebook JSON via NotebookCoreService::updateNotebookConfig.
  //        - On persist success: trigger initial sync, emit
  //          bootstrapAndPersistFinished(id, VXCORE_OK, "").
  //        - On persist failure: ROLLBACK by calling
  //          disableSyncForNotebook(id). Original persist error is preserved
  //          and reported as bootstrapAndPersistFinished(id,
  //          VXCORE_ERR_UNKNOWN, persistErrorMsg) regardless of whether the
  //          rollback succeeds. Rollback failures are logged at qCritical.
  //   3. On enableFinished failure: notebook stays in original (pre-call)
  //      state. Emit bootstrapAndPersistFinished(id, enableResult,
  //      enableMsg). No rollback needed (nothing to undo).
  //
  // Credentials are forwarded to enableSyncForNotebook, never cached here.
  void bootstrapAndPersist(const QString &p_notebookId, const SyncSettings &p_settings);

  // WebDAV reauthenticates and verifies the remote UUID before updating the
  // username binding. Git keeps its provider-only credential refresh.
  void updateCredentials(const QString &p_notebookId, const SyncCredential &p_credentials);

  // Resolve one FIFO batch, preserving per-file failures. Only a successful
  // batch runs the trailing staged sync, within the same queue item.
  void resolveConflicts(const QString &p_notebookId, const QHash<QString, QString> &p_resolutions);
  QSet<QString> keepBothUnsupportedPaths(const QString &p_notebookId) const;

  // Includes queued/running sync and pending credential lifecycle operations.
  bool isSyncInProgress(const QString &p_notebookId) const;

  // Returns true if the notebook config marks sync as enabled (ADR-8 flat keys:
  // reads "syncEnabled" from getNotebookConfig).
  bool isSyncEnabled(const QString &p_notebookId) const;

  // Returns true if the notebook's sync configuration is complete (enabled,
  // backend set, remote URL set). A notebook can be sync-enabled but not ready
  // if the user hasn't finished the bootstrap flow.
  bool isSyncReady(const QString &p_notebookId) const;

  // Returns true if the notebook is registered in vxcore's runtime sync state
  // (i.e., vxcore_sync_get_status returns VXCORE_OK for this notebook).
  // This answers "is sync currently active at runtime" — distinct from
  // isSyncReady which answers "is sync configured on disk".
  //
  // A notebook can be sync-enabled on disk (isSyncReady=true) but not yet
  // registered at runtime (isSyncRegistered=false) if the user hasn't called
  // enableSyncForNotebook yet in this process, or if the enable operation
  // failed.
  //
  // MUST be called on the main thread (SyncManager is not thread-safe).
  // Does NOT cache the result; state changes asynchronously via worker
  // operations.
  bool isSyncRegistered(const QString &p_notebookId) const;

  // Returns the per-device last successful sync timestamp formatted as a
  // locale-aware short string (via NotebookCoreService::getLastSyncUtc, which
  // reads metadata.db). Empty if the notebook has never been successfully
  // synced on this device.
  QString lastSyncTime(const QString &p_notebookId) const;

  // Test-only: directly mutate the in-progress map. Required by T17's
  // BlockClose test (simulating "sync running" for the close-hook check).
  // Per the T6 deviation: kept unconditional to avoid duplicate-symbol
  // problems that would arise from dual-compiling syncservice.cpp into both
  // core_services and test binaries.
  void testSetInProgress(const QString &p_notebookId, bool p_value);

  // Test-only seams for Task 13.4 bootstrapAndPersist. Force the persist
  // step and/or the rollback disable step to behave as if they failed,
  // without needing a mock NotebookCoreService. Unconditional per ADR-6.
  //   - testForceNextPersistFailure: next bootstrapAndPersist persist step
  //     will skip the actual JSON write and report failure with the given
  //     message (default "injected persist failure"). One-shot: auto-clears
  //     after being consumed.
  //   - testForceNextRollbackFailure: next rollback disableSync inside
  //     bootstrapAndPersist will skip the worker dispatch and synthesize a
  //     disableFinished(VXCORE_ERR_UNKNOWN) on the GUI thread. One-shot.
  void testForceNextPersistFailure(const QString &p_message = QString());
  void testForceNextRollbackFailure();

  // Test-only seams for the post-reconcile freshness gate (auto-sync on open).
  // Unconditional per ADR-6.
  //   - testForceLastSyncUtc: override the value
  //     NotebookCoreService::getLastSyncUtc would return for the freshness
  //     comparison inside maybeTriggerPostReconcile. A non-negative override
  //     wins over the real metadata.db read. Pass -1 to clear an override.
  //   - testInvokeMaybeTriggerPostReconcile: invoke the helper directly,
  //     bypassing the full reconcile/enable lambda chain (which otherwise
  //     would require keychain + notebook + bare-repo setup). Public solely
  //     so tests can exercise the gate without staging a real reconcile.
  //   - testSetMaybeTriggerBypassReadinessCheck: when true, the
  //     isSyncEnabled / isSyncRegistered defense inside
  //     maybeTriggerPostReconcile is skipped so tests can drive the
  //     freshness / in-progress gates without a real vxcore sync
  //     registration (which would require a real bare-repo enable flow).
  //     Default false; production code never flips this.
  void testForceLastSyncUtc(const QString &p_notebookId, qint64 p_ms);
  void testInvokeMaybeTriggerPostReconcile(const QString &p_notebookId);
  void testSetMaybeTriggerBypassReadinessCheck(bool p_bypass);

  // Test-only seams for auto-sync debounce. Unconditional per ADR-6.
  void testSetDebounceOverrideSeconds(int p_seconds);
  bool testIsDebounceTimerActive(const QString &p_notebookId) const;
  int testDebounceRemainingMs(const QString &p_notebookId) const;
  void testFireDebounceNow(const QString &p_notebookId);

  // Public accessor for the credentials store. Used by T1 bootstrapSync
  // rollback path to delete orphan keychain PAT on enable failure.
  SyncCredentialsStore *credentialsStore() const { return m_credentialsStore; }
  SyncWorkQueueManager *workQueueManager() const { return m_workQueue; }

  // Cancel/wake handshakes before joining workers, then retire reservations on
  // this thread. Idempotent and safe when the GUI event loop has stopped.
  void shutdown();

signals:
  // Re-emit signals from the underlying worker. All are delivered on the GUI
  // thread (worker -> queued slot -> emit).
  void syncStarted(const QString &p_notebookId);
  void syncFinished(const QString &p_notebookId, VxCoreError p_result);
  void syncFailed(const QString &p_notebookId, VxCoreError p_code, const QString &p_message);

  // T21: emitted by cancelSync(). p_wasQueued is true when the cancel dropped
  // one or more PENDING (not-yet-running) sync items from the queue; false
  // when the cancel signalled an in-flight cancellation token (or when no
  // active sync was found — best-effort).
  void syncCancelled(const QString &p_notebookId, bool p_wasQueued);
  void conflictsDetected(const QString &p_notebookId, const QStringList &p_conflictFiles);
  void workingTreeChanged(const QString &p_notebookId, const QStringList &p_changedPaths);
  void enableFinished(const QString &p_notebookId, VxCoreError p_result, const QString &p_message);
  void disableFinished(const QString &p_notebookId, VxCoreError p_result);
  void credentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

  // Emitted after a notebook reconcile attempt completes.
  // Reports the actual enable result, credential failure, or invalid configuration.
  void reconcileFinished(const QString &p_notebookId, VxCoreError p_result);

  // F1.6 / Task 13.4 — Final outcome of bootstrapAndPersist().
  //   p_result == VXCORE_OK on full success (enable + persist + trigger sync).
  //   p_result != VXCORE_OK on either enable failure (original code) or
  //     persist failure (VXCORE_ERR_UNKNOWN; original persist error in
  //     p_message even if rollback later failed).
  void bootstrapAndPersistFinished(const QString &p_notebookId, VxCoreError p_result,
                                   const QString &p_message);

private slots:
  // Worker completions are posted to these GUI-thread forwarders.
  void onWorkerEnableFinished(const QString &p_notebookId, VxCoreError p_result,
                              const QString &p_message);
  void onWorkerDisableFinished(const QString &p_notebookId, VxCoreError p_result);
  void onWorkerCredentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

  // Hooks driving reconcile-on-open.
  void onNotebookAfterOpen(const NotebookOpenEvent &p_event);
  void onMainWindowAfterStart();

  // Sole GUI lifecycle/timestamp surface for both staged queue work and
  // externally initiated vxcore lifecycle events.
  void onSyncStarted(const QString &p_notebookId);
  void onSyncFinished(const QString &p_notebookId, VxCoreError p_result);
  void onSyncConflictFiles(const QString &p_notebookId, const QStringList &p_files);

  // Route sync.should_run through the ordinary debounce/coalesce policy.
  // Automatic operations have the same cancellable lifetime as manual sync.
  void onSyncShouldRun(const QString &p_notebookId);

  void onDebounceTimeout(const QString &p_notebookId);

private:
  struct SyncOperation;
  void enqueueSync(const QString &p_notebookId, const QHash<QString, QString> &p_resolutions,
                   bool p_automatic);
  void runSyncOperation(const std::shared_ptr<SyncOperation> &p_operation);
  VxCoreError applySyncOperation(const std::shared_ptr<SyncOperation> &p_operation);
  void beginSyncApply(const std::shared_ptr<SyncOperation> &p_operation);
  void pollSyncApply(const std::shared_ptr<SyncOperation> &p_operation);
  void finishSyncApply(const std::shared_ptr<SyncOperation> &p_operation);
  void cancelSyncOperation(const std::shared_ptr<SyncOperation> &p_operation, bool p_shutdown);
  bool isCurrentOperation(const std::shared_ptr<SyncOperation> &p_operation) const;

  // Actual backend/URL plus this notebook's existing auto-sync preference.
  QString buildConfigJson(const QString &p_notebookId, const SyncSettings &p_settings) const;
  bool hasInterruptedRetirement(const QString &p_notebookId) const;
  void deleteStoredCredentials(const QString &p_notebookId, std::function<void()> p_finished);

  // Reconcile vxcore SyncManager runtime state for a notebook whose on-disk
  // config says syncEnabled=true but whose SyncManager::configs_ is empty.
  // Best-effort, idempotent per process via m_reconcileAttempted.
  void reconcileSyncForNotebook(const QString &p_notebookId);

  // After reconcile has registered a notebook with vxcore, optionally enqueue
  // a `triggerSyncNow` IF the notebook is "stale" (last successful sync was
  // more than kPostReconcileFreshnessMs ago, or never synced on this device).
  //
  // Closes the UX gap from the reconcile-only path: opening a notebook (or
  // app start) was only enqueuing enableSync, so the first actual FetchOrigin
  // had to wait for the next save or manual "Sync Now" to fire mark_dirty.
  // For multi-device users this surfaced as stale content after sleep/wake.
  //
  // Skips silently when any of these is true:
  //   * service is shutting down
  //   * notebook is no longer sync-enabled or sync-registered (defense)
  //   * a sync is already in flight for this notebook (queue would coalesce
  //     anyway; this just avoids spurious queue churn)
  //   * last successful sync was within kPostReconcileFreshnessMs
  //
  // Re-uses the existing triggerSyncNow path, so SyncWorkQueueManager
  // coalescing (coalesceKey="trigger") still de-dupes against concurrent
  // user-initiated or auto-sync triggers.
  void maybeTriggerPostReconcile(const QString &p_notebookId);

  int debounceSeconds() const;
  qint64 lastSyncTimeMs(const QString &p_notebookId) const;
  void armOrIgnoreDebounce(const QString &p_notebookId);
  void dropDebounceTimer(const QString &p_notebookId);
  void enqueueAutoSync(const QString &p_notebookId);

  ServiceLocator &m_services;
  NotebookCoreService *m_notebookCoreService = nullptr;
  SyncCredentialsStore *m_credentialsStore = nullptr;
  EventBridge *m_eventBridge = nullptr;

  // T20: per-notebook serialized executor for enable / disable / bootstrap
  // work. Resolved from ServiceLocator (production main.cpp registers one
  // with bounded shutdown via aboutToQuit). Tests that do not register a
  // SyncWorkQueueManager fall back to a SyncService-owned instance so the
  // queued operations still run; shutdown() drains either instance.
  std::unique_ptr<SyncWorkQueueManager> m_ownedWorkQueue;
  SyncWorkQueueManager *m_workQueue = nullptr;

  // Per-notebook in-flight state lives on SyncWorkQueueManager (T26). Query
  // via m_workQueue->inFlightState(id).running; mutate in tests via
  // SyncWorkQueueManager::testForceInFlight (called from testSetInProgress).

  // GUI-owned generation registry; queued callbacks must still own this entry
  // before changing reservations. Tokens live in their shared operation state.
  QHash<quint64, std::shared_ptr<SyncOperation>> m_syncOperations;
  quint64 m_nextSyncGeneration = 0;
  QHash<QString, QSet<QString>> m_keepBothUnsupported;
  QHash<QString, QString> m_syncErrorMessages;

  // Set true by shutdown(); subsequent public-API operations early-return.
  bool m_shutDown = false;

  // Prevents double reconcile when both MainWindowAfterStart and a subsequent
  // user-initiated NotebookAfterOpen fire for the same notebook in one session.
  QSet<QString> m_reconcileAttempted;

  // GUI-owned operation identities only; credentials remain in short-lived captures.
  QSet<QString> m_credentialOperations;
  QSet<QString> m_deletingCredentials;

  // Per-notebook consecutive auth-failure counter (Wave: silent-sync fix).
  // Incremented on every VXCORE_ERR_SYNC_AUTH_FAILED in onSyncFinished.
  // Reset on any successful syncFinished(VXCORE_OK), on updateCredentials
  // (user supplied a new PAT, give it a fresh chance), and on disable.
  // When the count reaches kAuthFailureCircuitThreshold the auto-sync path
  // (onSyncShouldRun) suppresses further auto-triggered syncs for the
  // notebook so we stop hammering the remote with known-bad credentials on
  // every keystroke. Manual triggerSyncNow is NEVER suppressed (user intent
  // overrides the circuit-breaker).
  QHash<QString, int> m_authFailureCount;
  static constexpr int kAuthFailureCircuitThreshold = 3;

  // Per-notebook trailing-throttle timers for the auto-sync path. Timers live
  // on the SyncService/GUI thread and are parented to this.
  QHash<QString, QTimer *> m_debounceTimers;

  // Task 13.4 test seams (one-shot).
  bool m_testForceNextPersistFailure = false;
  QString m_testForceNextPersistFailureMsg;
  bool m_testForceNextRollbackFailure = false;

  // Test-only overrides for last-sync UTC, consumed by maybeTriggerPostReconcile.
  // Per-notebook; presence wins over the real NotebookCoreService::getLastSyncUtc
  // read. Empty in production.
  QHash<QString, qint64> m_testLastSyncUtcOverrides;

  // Test-only flag: when true, maybeTriggerPostReconcile skips its
  // isSyncEnabled / isSyncRegistered defense check. Default (production)
  // is false.
  bool m_testBypassReadinessCheck = false;

  // Test-only override for debounceSeconds(). -1 means read ConfigCoreService.
  int m_testDebounceOverrideSeconds = -1;

  // Post-reconcile freshness window: skip auto-trigger if the per-device last
  // successful sync timestamp is newer than (now - this). 2 minutes is long
  // enough to coalesce rapid close-open cycles (workspace switching, window
  // refocus) but short enough that a real sleep/wake cycle is treated as
  // stale and gets a fresh sync. Tunable per future telemetry.
  static constexpr qint64 kPostReconcileFreshnessMs = 2 * 60 * 1000;
};

} // namespace vnotex

#endif // SYNCSERVICE_H
