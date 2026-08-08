#ifndef NOTEBOOKIOGATE_H
#define NOTEBOOKIOGATE_H

#include <QHash>
#include <QMutex>
#include <QSharedPointer>
#include <QString>

#include <core/noncopyable.h>

namespace vnotex {

/**
 * NotebookIoGate
 *
 * Per-notebook serializer for filesystem operations that race on the same
 * working tree. Used to serialize buffer save (via BufferSaveQueue) and
 * git-stage (via SyncOps::triggerSync) for the SAME notebook, so they
 * cannot fight for `.git/index.lock` or exclusive file handles on Windows.
 *
 * Thread affinity:
 *   - ScopedLock (unbounded) is WORKER-ONLY: BufferSaveQueue workers and
 *     SyncOps workers acquire it that way. It must NEVER be taken on the UI
 *     thread, where an in-flight sync stage would freeze the window.
 *   - ScopedTryLock (bounded) MAY be used on the UI thread for one short,
 *     bounded write. BufferService::saveForSnapshot uses it so a
 *     share-triggered save is still serialized against sync staging without
 *     risking an unbounded GUI block; on timeout it gives up and reports
 *     "busy" instead of proceeding unserialized.
 *
 * Why per-notebook (not global):
 *   Unrelated notebooks must sync/save in parallel. Per-notebook keying
 *   permits maximum concurrency while protecting the only resource that
 *   actually contends — a single working tree.
 *
 * Lock implementation:
 *   QMutex (not QReadWriteLock): every holder is a writer (both save and
 *   stage MUTATE the working tree). Reader/writer distinction adds no value
 *   and increases lock complexity. It is NOT recursive, so a hook or callback
 *   invoked while the gate is held must not try to re-acquire it — fire hooks
 *   outside the locked region.
 */
class NotebookIoGate : private Noncopyable {
public:
  NotebookIoGate();
  ~NotebookIoGate();

  /**
   * RAII holder for the per-notebook lock. Non-copyable, movable.
   *
   * Construction blocks until the lock is acquired.
   * Destruction releases the lock.
   *
   * Must NEVER be constructed on the UI thread.
   */
  class ScopedLock {
  public:
    ScopedLock(NotebookIoGate &p_gate, const QString &p_notebookId);
    ~ScopedLock();

    ScopedLock(const ScopedLock &) = delete;
    ScopedLock &operator=(const ScopedLock &) = delete;

    ScopedLock(ScopedLock &&p_other) noexcept;
    ScopedLock &operator=(ScopedLock &&p_other) noexcept;

  private:
    NotebookIoGate *m_gate;
    QString m_notebookId;
  };

  /**
   * RAII holder that acquires the per-notebook lock with a TIMEOUT.
   *
   * Exists so a GUI-thread operation can serialize a single short write
   * against save / sync workers WITHOUT the unbounded block that plain
   * ScopedLock would impose (a sync stage can hold the gate for a while, and
   * freezing the UI on it is not acceptable).
   *
   * ALWAYS check isLocked(): on timeout the lock was NOT taken and the caller
   * must back off rather than proceed unserialized.
   *
   * Keep the held window short. This is not a licence to run long GUI-thread
   * I/O under the gate; it is for one bounded operation such as a single
   * buffer save.
   */
  class ScopedTryLock {
  public:
    ScopedTryLock(NotebookIoGate &p_gate, const QString &p_notebookId, int p_timeoutMs);
    ~ScopedTryLock();

    ScopedTryLock(const ScopedTryLock &) = delete;
    ScopedTryLock &operator=(const ScopedTryLock &) = delete;

    bool isLocked() const { return m_locked; }

  private:
    NotebookIoGate *m_gate;
    QString m_notebookId;
    bool m_locked = false;
  };

private:
  friend class ScopedLock;
  friend class ScopedTryLock;

  // Acquires (and lazily creates) the per-notebook mutex, then locks it.
  // Blocks until lock acquired.
  void acquire(const QString &p_notebookId);

  // Acquires (and lazily creates) the per-notebook mutex, waiting at most
  // @p_timeoutMs. Returns false when the lock was NOT taken.
  bool tryAcquire(const QString &p_notebookId, int p_timeoutMs);

  // Unlocks the per-notebook mutex.
  void release(const QString &p_notebookId);

  // Guards m_mutexes (the map of per-notebook mutexes).
  QMutex m_registryMutex;

  // Per-notebook mutex registry. Mutexes are held by QSharedPointer so
  // outstanding ScopedLocks remain safe even if a notebook entry is
  // removed (not currently supported — entries live for app lifetime).
  QHash<QString, QSharedPointer<QMutex>> m_mutexes;
};

} // namespace vnotex

#endif // NOTEBOOKIOGATE_H
