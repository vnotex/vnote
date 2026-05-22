#include "syncworkqueuemanager.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QThread>
#include <QThreadPool>

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(lcQueue, "vnote.sync.workqueue")
} // namespace

SyncWorkQueueManager::SyncWorkQueueManager(QObject *p_parent)
    : QObject(p_parent), m_pool(new QThreadPool(this)) {
  int ideal = QThread::idealThreadCount();
  if (ideal <= 0) {
    ideal = 4;
  }
  m_pool->setMaxThreadCount(ideal);
  // Allow workers to finish before pool destruction; we explicitly drain in
  // shutdown() so this is just defensive.
  m_pool->setExpiryTimeout(30000);
}

SyncWorkQueueManager::~SyncWorkQueueManager() {
  // Best-effort drain if caller never invoked shutdown().
  shutdown(5000);
}

SyncWorkQueueManager::EnqueueResult SyncWorkQueueManager::enqueue(const QString &p_notebookId,
                                                                  Work p_work) {
  if (!p_work) {
    qCWarning(lcQueue) << "enqueue() called with empty work for" << p_notebookId;
    return EnqueueResult::Rejected;
  }

  bool needLaunch = false;
  {
    QMutexLocker locker(&m_mutex);
    if (m_shutdown) {
      qCWarning(lcQueue) << "enqueue() after shutdown ignored for" << p_notebookId;
      return EnqueueResult::Rejected;
    }
    PerNotebook &slot = m_perNotebook[p_notebookId];
    slot.queue.enqueue(std::move(p_work));

    // Update hasPending: true if queue non-empty (which it now is after append)
    slot.hasPending = !slot.queue.isEmpty();

    if (!slot.running) {
      slot.running = true;
      needLaunch = true;
    }
  }

  if (needLaunch) {
    // Dispatch worker loop to the pool. Work runs OUTSIDE m_mutex.
    // Qt 5.15+/6 supports std::function overload of QThreadPool::start.
    const QString idCopy = p_notebookId;
    m_pool->start([this, idCopy]() { this->runLoop(idCopy); });
  }

  return EnqueueResult::Accepted;
}

void SyncWorkQueueManager::runLoop(const QString &p_notebookId) {
  // Drain queue for this notebook. Saves a pool dispatch per item.
  for (;;) {
    Work next;
    {
      QMutexLocker locker(&m_mutex);
      auto it = m_perNotebook.find(p_notebookId);
      if (it == m_perNotebook.end()) {
        // Defensive: slot was wiped (shouldn't happen mid-run).
        return;
      }
      // On shutdown, queues are cleared by shutdown(); we stop.
      if (m_shutdown || it->queue.isEmpty()) {
        it->running = false;
        it->hasPending = false; // No items pending and not running
        return;
      }
      next = std::move(it->queue.front());
      it->queue.dequeue();

      // Update hasPending: true if queue still has items OR we're still running
      it->hasPending = !it->queue.isEmpty() || it->running;
    }

    // Invoke work OUTSIDE the mutex (Wave 0.5 contract).
    try {
      next();
    } catch (const std::exception &e) {
      qCWarning(lcQueue) << "work threw exception for" << p_notebookId << ":" << e.what();
    } catch (...) {
      qCWarning(lcQueue) << "work threw unknown exception for" << p_notebookId;
    }
  }
}

bool SyncWorkQueueManager::shutdown(int p_timeoutMs) {
  {
    QMutexLocker locker(&m_mutex);
    if (m_shutdown) {
      // Already shut down; pool was drained on first call.
      return true;
    }
    m_shutdown = true;
    // Drop pending work. In-flight items will observe m_shutdown on next
    // loop iteration and exit.
    for (auto it = m_perNotebook.begin(); it != m_perNotebook.end(); ++it) {
      it->queue.clear();
    }
  }

  return m_pool->waitForDone(p_timeoutMs);
}

int SyncWorkQueueManager::queueDepth(const QString &p_notebookId) const {
  QMutexLocker locker(&m_mutex);
  auto it = m_perNotebook.find(p_notebookId);
  if (it == m_perNotebook.end()) {
    return 0;
  }
  return it->queue.size();
}

bool SyncWorkQueueManager::isRunning(const QString &p_notebookId) const {
  QMutexLocker locker(&m_mutex);
  auto it = m_perNotebook.find(p_notebookId);
  if (it == m_perNotebook.end()) {
    return false;
  }
  return it->running;
}

bool SyncWorkQueueManager::hasPending(const QString &p_id) const {
  QMutexLocker locker(&m_mutex);
  auto it = m_perNotebook.find(p_id);
  if (it == m_perNotebook.end()) {
    return false;
  }
  return it->hasPending;
}

SyncWorkQueueManager::SyncInFlightState
SyncWorkQueueManager::inFlightState(const QString &p_id) const {
  QMutexLocker locker(&m_mutex);
  auto it = m_perNotebook.find(p_id);
  if (it == m_perNotebook.end()) {
    return SyncInFlightState{}; // Default: all false/nullptr
  }
  return SyncInFlightState{it->running, it->hasPending, nullptr};
}
