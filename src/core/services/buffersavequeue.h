#ifndef BUFFERSAVEQUEUE_H
#define BUFFERSAVEQUEUE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QWaitCondition>

namespace vnotex {

class IBufferCoreService;
class NotebookIoGate;

// BufferSaveQueue
//
// Asynchronous, per-(notebookId, bufferId) FIFO save executor.
//
// Why this exists: auto-save on the UI thread was contending with the libgit2
// worker on the same working tree (Windows `.git/index.lock` + AV scans),
// freezing the UI. This queue dispatches the actual file write to
// QThreadPool::globalInstance() and serialises saves per buffer.
//
// Threading contract:
//   - enqueue() is non-blocking; safe to call from the UI thread.
//   - The worker body acquires NotebookIoGate, then calls
//     IBufferCoreService::setContentRaw + saveBuffer (NEVER on the UI thread).
//   - saveFinished is emitted via QMetaObject::invokeMethod with
//     Qt::QueuedConnection so receivers observe it on this object's owning
//     thread.
//
// Coalescing:
//   If multiple jobs are PENDING (not started) for the same
//   (notebookId, bufferId) composite key, only the newest snapshot is kept.
//   Each superseded job still emits saveFinished(..., ok=true, errorMsg="")
//   so any waiters resolve cleanly. The currently in-flight job is never
//   coalesced.
class BufferSaveQueue : public QObject {
  Q_OBJECT

public:
  BufferSaveQueue(IBufferCoreService &p_coreService, NotebookIoGate &p_gate,
                  QObject *p_parent = nullptr);
  ~BufferSaveQueue() override;

  // Schedule an async save. Returns immediately. Result delivered via
  // queued saveFinished signal.
  void enqueue(const QString &p_notebookId, const QString &p_bufferId, const QString &p_content,
               quint64 p_revision);

  // Stop accepting new jobs and wait up to @p_timeoutMs for in-flight workers
  // to drain. Returns true if drained; false on timeout.
  bool shutdown(int p_timeoutMs = 5000);

signals:
  // Emitted on this object's owning thread (queued from worker).
  void saveFinished(const QString &p_bufferId, quint64 p_revision, bool p_ok,
                    const QString &p_errorMsg);

private:
  struct SaveJob {
    QString notebookId;
    QString bufferId;
    QString content;
    quint64 revision = 0;
  };

  // Composite key "notebookId::bufferId" — pin buffer-level serialisation
  // while the gate still acquires per notebookId for cross-process semantics.
  static QString compositeKey(const QString &p_notebookId, const QString &p_bufferId);

  // Worker entry: pops and runs the one pending job for @p_key, drains until
  // the queue for @p_key is empty.
  void runWorker(const QString &p_key);

  // Helper: queue a saveFinished onto our owning thread.
  void emitFinishedQueued(const QString &p_bufferId, quint64 p_revision, bool p_ok,
                          const QString &p_errorMsg);

  IBufferCoreService &m_coreService;
  NotebookIoGate &m_gate;

  // m_mutex protects: m_pending, m_running, m_inFlightCount, m_stopping.
  mutable QMutex m_mutex;
  QWaitCondition m_drained; // Signalled when m_inFlightCount hits 0.

  // Per composite-key pending job (coalesced — newest wins).
  QHash<QString, SaveJob> m_pending;
  // Per composite-key in-flight flag (worker dispatched).
  QHash<QString, bool> m_running;

  int m_inFlightCount = 0;
  bool m_stopping = false;
};

} // namespace vnotex

#endif // BUFFERSAVEQUEUE_H
