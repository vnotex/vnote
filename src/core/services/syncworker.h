#ifndef SYNCWORKER_H
#define SYNCWORKER_H

#include <atomic>

#include <QObject>
#include <QString>
#include <QStringList>

#include <core/noncopyable.h>

#include <vxcore/vxcore_types.h>

// Forward-declare the opaque C cancellation handle (defined in vxcore.h).
// Worker slots receive it as a `quintptr` over QueuedConnection so we don't
// need full vxcore.h inclusion in this header. The casted pointer is the
// raw `VxCoreSyncCancellation*` produced by vxcore_sync_create_cancellation.
struct VxCoreSyncCancellation_;

namespace vnotex {

class NotebookCoreService;

// SyncWorker is the threading core of the sync stack. It is an internal QObject
// that lives on a dedicated QThread (owned by SyncService, NOT by this class).
// All public slots dispatch to NotebookCoreService (which wraps the blocking
// vxcore C sync API) and emit typed signals back to the GUI thread.
//
// Per ADR-1: this class NEVER includes sync/sync_manager.h; only
// NotebookCoreService is consumed.
//
// Per ADR-6: test seams are unconditional public methods documented as
// test-only. They were originally intended to be `#ifdef VNOTE_TESTING`-gated,
// but gating would require dual-compiling syncworker.cpp into both
// core_services and the test binary, producing duplicate symbols on link.
// Keeping the seam members unconditional adds two `std::atomic<int>`s and two
// public setters; the runtime cost is one relaxed atomic load per slot
// invocation. The VNOTE_TESTING compile definition remains reserved on the
// test target for forward compatibility (mirroring the T4 SyncCredentialsStore
// pattern).
class SyncWorker : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit SyncWorker(NotebookCoreService *p_notebookCoreService, QObject *p_parent = nullptr);
  ~SyncWorker() override;

  // Atomic, lock-free read of the global "sync in progress" flag. Safe to call
  // from any thread.
  bool isSyncInProgressGlobal() const noexcept;

  // ---- Test seams (public; test-only consumers) -----------------------------
  // Cause the next slot invocation to QThread::msleep(p_sleepMs) BEFORE doing
  // real work. Consumes-and-clears: a second slot invocation runs normally
  // unless the seam is re-armed.
  void testHangNextOperation(int p_sleepMs);

  // Cause the next slot invocation to skip the vxcore call entirely and emit
  // the failed/finished signal with the forced VxCoreError. Consume-and-clear.
  // Pass a value < 0 (or never call) to disable.
  void testForceError(int p_code);

public slots:
  // All slots assume QueuedConnection delivery from the GUI thread. They debug
  // assert that they execute on the worker thread (this->thread()).
  // Arguments are taken by value so QueuedConnection marshalling is safe.
  void enableSync(QString p_notebookId, QString p_configJson, QString p_credentialsJson);
  void disableSync(QString p_notebookId);
  void triggerSync(QString p_notebookId);
  // Wave 12.2 / F5.9: cancellable variant. @p_cancellationHandle is a
  // quintptr-cast pointer to a VxCoreSyncCancellation_ owned by SyncService.
  // SyncService retains ownership of the handle for the duration of the
  // sync (releasing only after syncFinished arrives back on the GUI thread)
  // so the worker may safely treat the raw pointer as live. Zero means "no
  // cancellation wired" — semantically identical to triggerSync(id).
  void triggerSyncCancellable(QString p_notebookId, quintptr p_cancellationHandle);
  void getStatus(QString p_notebookId);
  void getConflicts(QString p_notebookId);
  void resolveConflict(QString p_notebookId, QString p_filePath, QString p_resolution);
  void setCredentials(QString p_notebookId, QString p_credentialsJson);

signals:
  // Emitted at the start of triggerSync (worker thread).
  void syncStarted(QString p_notebookId);

  // Emitted after triggerSync completes successfully (or with a VxCoreError).
  void syncFinished(QString p_notebookId, VxCoreError p_result);

  // Emitted in addition to syncFinished when triggerSync result != VXCORE_OK.
  void syncFailed(QString p_notebookId, VxCoreError p_code, QString p_message);

  // Emitted when getConflicts returns at least one conflict; carries the
  // parsed file paths for typed consumers.
  void conflictsDetected(QString p_notebookId, QStringList p_conflictFiles);

  void statusReady(QString p_notebookId, QString p_statusJson);
  void conflictsReady(QString p_notebookId, QString p_conflictsJson);

  void enableFinished(QString p_notebookId, VxCoreError p_result, QString p_message);
  void disableFinished(QString p_notebookId, VxCoreError p_result);
  void credentialsSetFinished(QString p_notebookId, VxCoreError p_result);
  void resolveConflictFinished(QString p_notebookId, QString p_filePath, VxCoreError p_result);

private:
  // Returns true if a forced error was consumed; sets p_outCode in that case.
  bool consumeForcedError(VxCoreError &p_outCode);

  // If a hang seam is armed, sleeps for that many ms and clears the seam.
  void maybeHang();

  NotebookCoreService *m_notebookCoreService = nullptr;

  // Global "is a triggerSync in flight" flag. Set by triggerSync only.
  std::atomic<bool> m_syncInProgressGlobal{false};

  // Test seam state. Sentinel: -1 means disabled.
  std::atomic<int> m_testHangMs{-1};
  std::atomic<int> m_testForceError{-1};
};

} // namespace vnotex

#endif // SYNCWORKER_H
