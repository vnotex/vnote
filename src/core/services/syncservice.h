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

class QThread;

namespace vnotex {

class ServiceLocator;
class NotebookCoreService;
class SyncCredentialsStore;
class SyncWorker;
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
  // dispatched to SyncWorker::resolveConflict via QueuedConnection. After all
  // resolutions are queued, SyncWorker::triggerSync is invoked to push the
  // resolved state.
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

  // Test-only access to the underlying SyncWorker. Production code goes through
  // the SyncService API. Documented as test-only per ADR-6 test seam access.
  SyncWorker *worker() const noexcept { return m_worker; }

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
  // so they execute on the GUI thread.
  void onWorkerSyncStarted(const QString &p_notebookId);
  void onWorkerSyncFinished(const QString &p_notebookId, VxCoreError p_result);
  void onWorkerSyncFailed(const QString &p_notebookId, VxCoreError p_code,
                          const QString &p_message);
  void onWorkerConflictsDetected(const QString &p_notebookId, const QStringList &p_conflictFiles);
  void onWorkerEnableFinished(const QString &p_notebookId, VxCoreError p_result,
                              const QString &p_message);
  void onWorkerDisableFinished(const QString &p_notebookId, VxCoreError p_result);
  void onWorkerCredentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

  // Hooks driving reconcile-on-open.
  void onNotebookAfterOpen(const NotebookOpenEvent &p_event);
  void onMainWindowAfterStart();

  // Auto-sync event forwarders (from EventBridge, already on GUI thread).
  void onAutoSyncStarted(const QString &p_notebookId);
  void onAutoSyncFinished(const QString &p_notebookId, VxCoreError p_result);
  void onAutoSyncConflict(const QString &p_notebookId);

private:
  void setInProgress(const QString &p_notebookId, bool p_value);

  // Build a credentials JSON for the worker out of a PAT string.
  static QString buildCredentialsJson(const QString &p_pat);
  // Build a config JSON (backend=git, remoteUrl=...) for the worker.
  static QString buildConfigJson(const QString &p_remoteUrl);

  // Reconcile vxcore SyncManager runtime state for a notebook whose on-disk
  // config says syncEnabled=true but whose SyncManager::configs_ is empty.
  // Best-effort, idempotent per process via m_reconcileAttempted.
  void reconcileSyncForNotebook(const QString &p_notebookId);

  ServiceLocator &m_services;
  NotebookCoreService *m_notebookCoreService = nullptr;
  SyncCredentialsStore *m_credentialsStore = nullptr;
  EventBridge *m_eventBridge = nullptr;

  QThread *m_thread = nullptr;
  SyncWorker *m_worker = nullptr;

  // T20: per-notebook serialized executor for enable / disable / bootstrap
  // work. Resolved from ServiceLocator (production main.cpp registers one
  // with bounded shutdown via aboutToQuit). Tests that do not register a
  // SyncWorkQueueManager fall back to a SyncService-owned instance so the
  // queued operations still run; shutdown() drains the owned instance.
  std::unique_ptr<SyncWorkQueueManager> m_ownedWorkQueue;
  SyncWorkQueueManager *m_workQueue = nullptr;

  // Per-notebook in-flight flag. Guarded by m_inProgressMutex. Mutex is the
  // only synchronization needed because all writes happen on the GUI thread
  // (from the queued worker callbacks); the mutex is for safe reads from any
  // thread that might call isSyncInProgress.
  mutable QMutex m_inProgressMutex;
  QHash<QString, bool> m_inProgress;

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

  // Task 13.4 test seams (one-shot).
  bool m_testForceNextPersistFailure = false;
  QString m_testForceNextPersistFailureMsg;
  bool m_testForceNextRollbackFailure = false;
};

} // namespace vnotex

#endif // SYNCSERVICE_H
