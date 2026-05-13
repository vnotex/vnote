#ifndef SYNCSERVICE_H
#define SYNCSERVICE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include <core/noncopyable.h>

#include <vxcore/vxcore_types.h>

class QThread;

namespace vnotex {

class ServiceLocator;
class NotebookCoreService;
class SyncCredentialsStore;
class SyncWorker;
class EventBridge;

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

  // Returns the persisted ISO-8601 last-sync time from the notebook config
  // ("lastSyncIso" key), or empty if not set. T14 will populate this; T7 just
  // reads it for completeness.
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
  void conflictsDetected(const QString &p_notebookId, const QStringList &p_conflictFiles);
  void enableFinished(const QString &p_notebookId, VxCoreError p_result, const QString &p_message);
  void disableFinished(const QString &p_notebookId, VxCoreError p_result);
  void credentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

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

  ServiceLocator &m_services;
  NotebookCoreService *m_notebookCoreService = nullptr;
  SyncCredentialsStore *m_credentialsStore = nullptr;
  EventBridge *m_eventBridge = nullptr;

  QThread *m_thread = nullptr;
  SyncWorker *m_worker = nullptr;

  // Per-notebook in-flight flag. Guarded by m_inProgressMutex. Mutex is the
  // only synchronization needed because all writes happen on the GUI thread
  // (from the queued worker callbacks); the mutex is for safe reads from any
  // thread that might call isSyncInProgress.
  mutable QMutex m_inProgressMutex;
  QHash<QString, bool> m_inProgress;

  // Set true by shutdown(); subsequent public-API operations early-return.
  bool m_shutDown = false;
};

} // namespace vnotex

#endif // SYNCSERVICE_H
