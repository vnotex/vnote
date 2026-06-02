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
 *   - Both BufferSaveQueue workers and SyncOps workers acquire this gate.
 *   - It is NEVER held by the UI thread.
 *
 * Why per-notebook (not global):
 *   Unrelated notebooks must sync/save in parallel. Per-notebook keying
 *   permits maximum concurrency while protecting the only resource that
 *   actually contends — a single working tree.
 *
 * Why no timeout in v1:
 *   Simpler model; the worker QThreadPool absorbs back-pressure. We can
 *   add a try-acquire later if a deadlock-recovery story becomes necessary.
 *
 * Lock implementation:
 *   QMutex (not QReadWriteLock): every holder is a writer (both save and
 *   stage MUTATE the working tree). Reader/writer distinction adds no value
 *   and increases lock complexity.
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

private:
  friend class ScopedLock;

  // Acquires (and lazily creates) the per-notebook mutex, then locks it.
  // Blocks until lock acquired.
  void acquire(const QString &p_notebookId);

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
