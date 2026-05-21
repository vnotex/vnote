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

  // Overload: enqueue work with an optional per-item cancellation callback.
  // The cancellation callback fires (outside m_mutex) if the item is dropped
  // from the pending queue via cancelPending(). It is NOT invoked when the
  // item runs normally or when the manager shuts down (shutdown drops items
  // silently — call cancelPending() first if cleanup is needed).
  EnqueueResult enqueue(const QString &p_notebookId, Work p_work,
                        std::function<void()> p_onCancelled);

  // Overload: enqueue work with a coalesce key (no cancellation callback).
  // If the coalesce key is non-empty and a pending item (same notebook id)
  // has the same key, this item is dropped and Coalesced is returned.
  // Default cancellation callback is nullptr.
  EnqueueResult enqueue(const QString &p_notebookId, Work p_work, const QString &p_coalesceKey);

  // Overload: enqueue work with all parameters including cancellation callback
  // and coalesce key. Precedence (under m_mutex):
  //   1. Shutdown or empty work → Rejected.
  //   2. Non-empty coalesceKey + match in queue → Coalesced (drop incoming).
  //   3. Queue size >= m_maxDepth → QueueFull.
  //   4. Otherwise → Accepted (enqueue).
  EnqueueResult enqueue(const QString &p_notebookId, Work p_work,
                        std::function<void()> p_onCancelled, const QString &p_coalesceKey);

  // Drop all pending (not-yet-started) work items for the given notebook id.
  // Returns the number of items dropped. The in-flight item (if any) is NOT
  // affected and will run to completion. Each dropped item's onCancelled
  // callback (if registered) fires OUTSIDE m_mutex. Emits pendingCancelled()
  // ONCE per call when count > 0.
  int cancelPending(const QString &p_notebookId);

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

  // Set the maximum queue depth per notebook. Default value is 4.
  // Thread-safe; affects future enqueue() calls. Existing queued items are not
  // affected retroactively. Recommended to call during initialization before any
  // enqueue() calls to avoid transient cap changes.
  void setMaxDepth(int p_depth);

  // Get the current maximum queue depth per notebook.
  int maxDepth() const;

signals:
  // Emitted once per cancelPending() call when count > 0. Fires OUTSIDE m_mutex.
  void pendingCancelled(const QString &p_id, int p_count);

private:
  struct WorkItem {
    Work body;
    std::function<void()> onCancelled;
    QString coalesceKey; // Used for deduplication: if non-empty and
                         // matches pending item key, new item is coalesced.
  };

  struct PerNotebook {
    QQueue<WorkItem> queue;
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
  int m_maxDepth = 4; // Default queue-depth cap per notebook
};

} // namespace vnotex

#endif // SYNCWORKQUEUEMANAGER_H
