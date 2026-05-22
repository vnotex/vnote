#ifndef SYNCWORKQUEUEMANAGER_H
#define SYNCWORKQUEUEMANAGER_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <functional>

class QThreadPool;

namespace vnotex {

// SyncWorkQueueManager — per-notebook serialized executor.
//
// Owns a QThreadPool plus a per-notebook FIFO queue. Work submitted via
// enqueue() for the same notebook id runs STRICTLY in order (one at a time);
// work for different notebook ids runs in parallel up to the pool capacity.
//
// Threading contract (Wave 0.5):
//   - The internal mutex protects ONLY the map structure + per-notebook
//     queue + running flag + shutdown flag.
//   - Work std::function bodies are ALWAYS invoked OUTSIDE the mutex.
//   - Re-entrant enqueue() from inside a work body is safe: it appends to
//     the queue, and the running worker picks it up after the current
//     item returns.
//
// Shutdown:
//   - shutdown(timeoutMs) clears all pending queues, waits for in-flight
//     work to finish (or timeout), returns true if drained cleanly.
//   - After shutdown, enqueue() is a no-op (warning logged).
//   - Destructor calls shutdown(5000) if not already shut down.
class SyncWorkQueueManager : public QObject {
  Q_OBJECT

public:
  using Work = std::function<void()>;

  /// Result of a SyncWorkQueueManager::enqueue() call.
  enum class EnqueueResult {
    /// Work was queued; the runner will pick it up.
    Accepted,
    /// Pending work with the same coalesce key already exists; the new
    /// item was dropped intentionally. (Coalesce logic added in T6;
    /// returned only by T6+ code paths.)
    Coalesced,
    /// Per-notebook queue is at cap; caller must retry or surrender.
    /// (Cap logic added in T6; returned only by T6+ code paths.)
    QueueFull,
    /// Generic refusal — e.g., manager has been shut down or work is empty.
    Rejected,
  };

  /// Per-notebook in-flight state snapshot.
  /// All fields are set atomically under m_mutex and returned as a value,
  /// ensuring callers read a consistent snapshot without holding the lock.
  struct SyncInFlightState {
    bool running = false;         // true if a work item is currently executing
    bool hasPending = false;      // true if items are queued (pending or running)
    void *cancellation = nullptr; // VxCoreSyncCancellation* (populated by T21)
  };

  explicit SyncWorkQueueManager(QObject *p_parent = nullptr);
  ~SyncWorkQueueManager() override;

  // Enqueue work for a given notebook id. Thread-safe. Returns immediately.
  EnqueueResult enqueue(const QString &p_notebookId, Work p_work);

  // Drain pending queues and wait for in-flight work to finish.
  // Returns true if pool drained within timeout.
  bool shutdown(int p_timeoutMs = 5000);

  // Test-facing inspectors.
  int queueDepth(const QString &p_notebookId) const;
  bool isRunning(const QString &p_notebookId) const;

  // Query per-notebook pending state. Returns snapshot (value type, not reference).
  // For unknown notebook id, returns default-constructed state (all false/nullptr).
  bool hasPending(const QString &p_id) const;
  SyncInFlightState inFlightState(const QString &p_id) const;

private:
  struct PerNotebook {
    QQueue<Work> queue;
    bool running = false;
    bool hasPending = false;
  };

  // Worker loop: pulls and runs items for p_notebookId until queue is empty.
  // Runs on a QThreadPool worker thread.
  void runLoop(const QString &p_notebookId);

  mutable QMutex m_mutex;
  QHash<QString, PerNotebook> m_perNotebook;
  QThreadPool *m_pool;
  bool m_shutdown = false;
};

} // namespace vnotex

#endif // SYNCWORKQUEUEMANAGER_H
