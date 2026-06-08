#ifndef SYNCSERVICE_H
#define SYNCSERVICE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

#include <core/noncopyable.h>

#include <vxcore/vxcore_types.h>

namespace vnotex {

class ServiceLocator;
class NotebookCoreService;
class SyncCredentialsStore;
class SyncWorkQueueManager;
class EventBridge;
struct NotebookOpenEvent;

// SyncService is the GUI-thread facade for the per-notebook git sync stack.
//
// Internally it owns a SyncWorker (T6) on a private QThread and wraps the
// SyncCredentialsStore (T4). All UI consumers (T10/T11/T13/T14/T15/T16/T17)
// interact ONLY with this facade; they never touch the worker, the credentials
// store, or vxcore directly.
//
// Threading model:
//   * The SyncService instance lives on the GUI thread. All public methods and
//     signals are GUI-thread.
//   * SyncWorker lives on a private QThread owned by SyncService. Worker
//     signals are connected to internal re-emit slots via Qt::QueuedConnection
//     so that they hop back onto the GUI thread before forwarding to consumers.
//   * Service -> worker calls go through QMetaObject::invokeMethod with
//     Qt::QueuedConnection.
//
// Per ADR-1: SyncService NEVER pulls in sync/sync_manager.h directly; all
// vxcore sync calls go through NotebookCoreService.
//
// Per ADR-9: PATs are routed through SyncCredentialsStore. SyncService never
// caches PAT values in member variables. PATs received via slot args are
// forwarded to the credentials store and to the worker (in JSON form, captured
// only by short-lived lambdas that release the data once the worker has been
// invoked).
//
// Per ADR-6: SyncWorker exposes test seams as unconditional public methods to
// avoid duplicate-symbol issues from dual-compiling. SyncService follows the
// same pattern: testSetInProgress is unconditional. The VNOTE_TESTING compile
// definition remains reserved on the test target.
//
// T17 wires the NotebookBeforeClose hook (refusing to close while a sync is
// in progress for that notebook) inside the constructor, and exposes a public
// shutdown() method that quits the worker thread with a bounded 30s wait and
// a terminate() fallback. shutdown() is also wired from main.cpp via
// QCoreApplication::aboutToQuit (DirectConnection) so that QApplication tear-
// down does not race with in-flight worker operations.
class SyncService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit SyncService(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~SyncService() override;

  // Enable git sync for a notebook.
  //
  // Sequence:
  //   1. Store @p_pat in the OS keychain via SyncCredentialsStore.
  //   2. On credentialsStored, build the credentials JSON and invoke
  //      SyncWorker::enableSync via QueuedConnection.
  //   3. On credentialsError, emit enableFinished with VXCORE_ERR_UNKNOWN
  //      and the keychain error message; the worker is NOT called.
  //
  // The PAT lives only in the lambda capture between (1) and (2); it is never
  // assigned to a SyncService member.
  void enableSyncForNotebook(const QString &p_notebookId, const QString &p_remoteUrl,
                             const QString &p_pat);

  // Disable sync for a notebook. Invokes SyncWorker::disableSync; on
  // disableFinished, deletes the keychain entry via SyncCredentialsStore.
  void disableSyncForNotebook(const QString &p_notebookId);

  // Trigger a one-shot sync for a notebook. Invokes SyncWorker::triggerSync.
  void triggerSyncNow(const QString &p_notebookId);

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
  //   1. enableSyncForNotebook(id, url, pat) — async via worker.
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
  // PAT is forwarded to enableSyncForNotebook and NEVER cached on the
  // service. Per Wave 0.5: no SyncService mutex is held while emitting the
  // finished signal.
  void bootstrapAndPersist(const QString &p_notebookId, const QString &p_remoteUrl,
                           const QString &p_pat);

  // Replace the stored PAT for a notebook. Sequence mirrors enableSyncForNotebook
  // but invokes SyncWorker::setCredentials instead of enableSync.
  void updateCredentials(const QString &p_notebookId, const QString &p_newPat);

  // Resolve a batch of conflicts. Each (filePath -> resolution) entry is
  // enqueued on the SyncWorkQueueManager for this notebookId with coalesceKey
  // "resolve-<filePath>" (so duplicate resolutions for the same path collapse,
  // while different paths run in FIFO order). After all resolves are queued,
  // a final triggerSync is enqueued with coalesceKey "trigger" (same key
  // SyncService::triggerSyncNow uses) so any concurrent manual trigger is
  // absorbed into this trailing run.
  void resolveConflicts(const QString &p_notebookId, const QHash<QString, QString> &p_resolutions);

  // Returns true if a sync (or enable/disable etc. operation that maps to
  // syncStarted/syncFinished) is currently in flight for @p_notebookId.
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

  // Public accessor for the credentials store. Used by T1 bootstrapSync
  // rollback path to delete orphan keychain PAT on enable failure.
  SyncCredentialsStore *credentialsStore() const { return m_credentialsStore; }

  // Bounded shutdown of the underlying SyncWorker thread.
  // Sequence:
  //   1. Set the m_shutDown flag so subsequent public-API calls are no-ops.
  //   2. Call m_thread->quit() to ask the worker thread's event loop to exit.
  //   3. Wait up to 30 seconds for the thread to finish.
  //   4. If the wait times out, log a qWarning and call terminate() + a short
  //      bounded wait as a last resort.
  // Idempotent: calling shutdown() more than once is safe (subsequent calls
  // observe the flag and return immediately).
  // Wired from main.cpp's QCoreApplication::aboutToQuit handler with
  // Qt::DirectConnection so that the GUI event loop teardown does not skip
  // the call.
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
  void enableFinished(const QString &p_notebookId, VxCoreError p_result, const QString &p_message);
  void disableFinished(const QString &p_notebookId, VxCoreError p_result);
  void credentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

  // Emitted after a notebook reconcile attempt completes.
  // p_result: VXCORE_OK if enableSync dispatched, VXCORE_ERR_SYNC_AUTH_FAILED
  // if PAT lookup failed, VXCORE_ERR_INVALID_PARAM if config incomplete.
  void reconcileFinished(const QString &p_notebookId, VxCoreError p_result);

  // F1.6 / Task 13.4 — Final outcome of bootstrapAndPersist().
  //   p_result == VXCORE_OK on full success (enable + persist + trigger sync).
  //   p_result != VXCORE_OK on either enable failure (original code) or
  //     persist failure (VXCORE_ERR_UNKNOWN; original persist error in
  //     p_message even if rollback later failed).
  void bootstrapAndPersistFinished(const QString &p_notebookId, VxCoreError p_result,
                                   const QString &p_message);

private slots:
  // Internal forwarders. All connected to worker signals via QueuedConnection
  // so they execute on the GUI thread. T23: only enable / disable / credentials
  // remain wired to SyncWorker — vxcore does not emit lifecycle events for
  // those. T24 will retire these along with the worker.
  void onWorkerEnableFinished(const QString &p_notebookId, VxCoreError p_result,
                              const QString &p_message);
  void onWorkerDisableFinished(const QString &p_notebookId, VxCoreError p_result);
  void onWorkerCredentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

  // Hooks driving reconcile-on-open.
  void onNotebookAfterOpen(const NotebookOpenEvent &p_event);
  void onMainWindowAfterStart();

  // T23: single-source sync lifecycle forwarders. EventBridge translates
  // vxcore sync.started / sync.finished / sync.conflict events into Qt
  // signals on the GUI thread; SyncService re-emits as its own public
  // signals. Covers manual + auto + initial-on-enable triggers uniformly.
  void onSyncStarted(const QString &p_notebookId);
  void onSyncFinished(const QString &p_notebookId, VxCoreError p_result);
  void onSyncConflictFiles(const QString &p_notebookId, const QStringList &p_files);

  // T31: vxcore's MaybeEnqueueSync emits sync.should_run; EventBridge re-emits
  // it as syncShouldRun(QString). This slot routes the auto-sync request onto
  // SyncWorkQueueManager via SyncOps::triggerSync (NO cancellation token —
  // auto path is fire-and-forget). Skips silently if the notebook has been
  // closed / disabled between vxcore's emit and our handler running.
  void onSyncShouldRun(const QString &p_notebookId);

private:
  // Build a credentials JSON for the worker out of a PAT string.
  static QString buildCredentialsJson(const QString &p_pat);
  // Build a config JSON (backend=git, remoteUrl=...) for the worker.
  static QString buildConfigJson(const QString &p_remoteUrl);

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

  ServiceLocator &m_services;
  NotebookCoreService *m_notebookCoreService = nullptr;
  SyncCredentialsStore *m_credentialsStore = nullptr;
  EventBridge *m_eventBridge = nullptr;

  // T20: per-notebook serialized executor for enable / disable / bootstrap
  // work. Resolved from ServiceLocator (production main.cpp registers one
  // with bounded shutdown via aboutToQuit). Tests that do not register a
  // SyncWorkQueueManager fall back to a SyncService-owned instance so the
  // queued operations still run; shutdown() drains the owned instance.
  std::unique_ptr<SyncWorkQueueManager> m_ownedWorkQueue;
  SyncWorkQueueManager *m_workQueue = nullptr;

  // Per-notebook in-flight state lives on SyncWorkQueueManager (T26). Query
  // via m_workQueue->inFlightState(id).running; mutate in tests via
  // SyncWorkQueueManager::testForceInFlight (called from testSetInProgress).

  // Wave 12.2 / F5.9: per-notebook cancellation tokens for in-flight
  // triggerSyncNow. Created in triggerSyncNow before dispatch, freed in the
  // syncFinished slot on the GUI thread. Guarded by m_cancellationMutex.
  // Value is the raw C handle (VxCoreSyncCancellation*) returned by
  // vxcore_sync_create_cancellation; we store it as void* so the header
  // doesn't need to include vxcore.h.
  mutable QMutex m_cancellationMutex;
  QHash<QString, void *> m_cancellations;

  // Set true by shutdown(); subsequent public-API operations early-return.
  bool m_shutDown = false;

  // Prevents double reconcile when both MainWindowAfterStart and a subsequent
  // user-initiated NotebookAfterOpen fire for the same notebook in one session.
  QSet<QString> m_reconcileAttempted;

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

  // Post-reconcile freshness window: skip auto-trigger if the per-device last
  // successful sync timestamp is newer than (now - this). 2 minutes is long
  // enough to coalesce rapid close-open cycles (workspace switching, window
  // refocus) but short enough that a real sleep/wake cycle is treated as
  // stale and gets a fresh sync. Tunable per future telemetry.
  static constexpr qint64 kPostReconcileFreshnessMs = 2 * 60 * 1000;
};

} // namespace vnotex

#endif // SYNCSERVICE_H
