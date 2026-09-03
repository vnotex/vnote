#include "syncworkqueuemanager.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <vector>

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(lcQueue, "vnote.sync.workqueue")
} // namespace

struct SyncWorkQueueManager::LeaseControl {
  QMutex mutex;
  SyncWorkQueueManager *manager = nullptr;
};

SyncWorkQueueManager::MaintenanceLease::MaintenanceLease(
    const std::shared_ptr<LeaseControl> &p_control, quint64 p_token)
    : m_control(p_control), m_token(p_token) {}

SyncWorkQueueManager::MaintenanceLease::~MaintenanceLease() { release(); }

SyncWorkQueueManager::MaintenanceLease::MaintenanceLease(MaintenanceLease &&p_other) noexcept
    : m_control(std::move(p_other.m_control)), m_token(p_other.m_token) {
  p_other.m_token = 0;
}

SyncWorkQueueManager::MaintenanceLease &
SyncWorkQueueManager::MaintenanceLease::operator=(MaintenanceLease &&p_other) noexcept {
  if (this != &p_other) {
    release();
    m_control = std::move(p_other.m_control);
    m_token = p_other.m_token;
    p_other.m_token = 0;
  }
  return *this;
}

bool SyncWorkQueueManager::MaintenanceLease::isValid() const {
  if (!m_control || m_token == 0) {
    return false;
  }

  QMutexLocker locker(&m_control->mutex);
  return m_control->manager != nullptr;
}

void SyncWorkQueueManager::MaintenanceLease::release() {
  if (!m_control || m_token == 0) {
    return;
  }

  {
    QMutexLocker locker(&m_control->mutex);
    if (m_control->manager) {
      m_control->manager->releaseMaintenance(m_token);
    }
  }
  m_token = 0;
  m_control.reset();
}

SyncWorkQueueManager::SyncWorkQueueManager(QObject *p_parent)
    : QObject(p_parent), m_leaseControl(std::make_shared<LeaseControl>()),
      m_pool(new QThreadPool(this)) {
  m_leaseControl->manager = this;
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
  return enqueue(p_notebookId, std::move(p_work), nullptr, QString());
}

SyncWorkQueueManager::EnqueueResult
SyncWorkQueueManager::enqueue(const QString &p_notebookId, Work p_work,
                              std::function<void()> p_onCancelled) {
  return enqueue(p_notebookId, std::move(p_work), std::move(p_onCancelled), QString());
}

SyncWorkQueueManager::EnqueueResult SyncWorkQueueManager::enqueue(const QString &p_notebookId,
                                                                  Work p_work,
                                                                  const QString &p_coalesceKey) {
  return enqueue(p_notebookId, std::move(p_work), nullptr, p_coalesceKey);
}

SyncWorkQueueManager::EnqueueResult
SyncWorkQueueManager::enqueue(const QString &p_notebookId, Work p_work,
                              std::function<void()> p_onCancelled, const QString &p_coalesceKey) {
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

    // PRECEDENCE (under m_mutex):
    // 1. Shutdown/empty check → done above, Rejected returned.
    // 2. Coalesce check: if non-empty coalesceKey and matching pending item exists,
    //    drop and return Coalesced.
    // 3. Cap check: if queue.size() >= m_maxDepth, return QueueFull.
    // 4. Otherwise: enqueue and return Accepted.

    // (2) Coalesce check: search pending queue for matching coalesceKey.
    //     Does NOT check the running item.
    if (!p_coalesceKey.isEmpty()) {
      for (const auto &item : slot.queue) {
        if (item.coalesceKey == p_coalesceKey) {
          qDebug(lcQueue) << "Coalescing enqueue for" << p_notebookId << "with key"
                          << p_coalesceKey;
          return EnqueueResult::Coalesced;
        }
      }
    }

    // (3) Cap check: pending queue size >= m_maxDepth.
    if (slot.queue.size() >= m_maxDepth) {
      qCDebug(lcQueue) << "Queue full for" << p_notebookId << "(size=" << slot.queue.size()
                       << ", cap=" << m_maxDepth << ")";
      return EnqueueResult::QueueFull;
    }

    // (4) Enqueue: append new work item.
    slot.queue.enqueue(WorkItem{std::move(p_work), std::move(p_onCancelled), p_coalesceKey});

    // Update hasPending: true if queue non-empty (which it now is after append).
    slot.hasPending = !slot.queue.isEmpty();

    if (!slot.running && slot.maintenanceToken == 0) {
      slot.running = true;
      needLaunch = true;
    }
  }

  if (needLaunch) {
    // Serialize post-m_mutex submission against shutdown so waitForDone()
    // cannot miss a runner that has been marked active but not yet submitted.
    QMutexLocker controlLocker(&m_leaseControl->mutex);
    if (m_leaseControl->manager) {
      startRunners({p_notebookId});
    } else {
      QMutexLocker locker(&m_mutex);
      auto it = m_perNotebook.find(p_notebookId);
      if (it != m_perNotebook.end()) {
        it->running = false;
        it->hasPending = false;
      }
    }
  }

  return EnqueueResult::Accepted;
}

int SyncWorkQueueManager::cancelPending(const QString &p_notebookId) {
  std::vector<WorkItem> dropped;
  {
    QMutexLocker locker(&m_mutex);
    auto it = m_perNotebook.find(p_notebookId);
    if (it == m_perNotebook.end() || it->queue.isEmpty()) {
      return 0;
    }
    dropped.reserve(static_cast<size_t>(it->queue.size()));
    while (!it->queue.isEmpty()) {
      dropped.push_back(std::move(it->queue.front()));
      it->queue.dequeue();
    }
    // Pending queue now empty; hasPending reflects only the in-flight item.
    it->hasPending = it->running;
  }

  // Invoke cancellation callbacks OUTSIDE m_mutex (Wave 0.5 contract).
  for (auto &item : dropped) {
    if (!item.onCancelled) {
      continue;
    }
    try {
      item.onCancelled();
    } catch (const std::exception &e) {
      qCWarning(lcQueue) << "onCancelled threw exception for" << p_notebookId << ":" << e.what();
    } catch (...) {
      qCWarning(lcQueue) << "onCancelled threw unknown exception for" << p_notebookId;
    }
  }

  const int count = static_cast<int>(dropped.size());
  if (count > 0) {
    emit pendingCancelled(p_notebookId, count);
  }
  return count;
}

void SyncWorkQueueManager::runLoop(const QString &p_notebookId) {
  // Drain queue for this notebook. Saves a pool dispatch per item.
  for (;;) {
    WorkItem next;
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
      next.body();
    } catch (const std::exception &e) {
      qCWarning(lcQueue) << "work threw exception for" << p_notebookId << ":" << e.what();
    } catch (...) {
      qCWarning(lcQueue) << "work threw unknown exception for" << p_notebookId;
    }
  }
}

bool SyncWorkQueueManager::shutdown(int p_timeoutMs) {
  {
    QMutexLocker controlLocker(&m_leaseControl->mutex);
    m_leaseControl->manager = nullptr;

    QMutexLocker locker(&m_mutex);
    if (!m_shutdown) {
      m_shutdown = true;
      m_maintenanceLeases.clear();
      // Drop pending work. In-flight items will observe m_shutdown on next
      // loop iteration and exit.
      for (auto it = m_perNotebook.begin(); it != m_perNotebook.end(); ++it) {
        it->queue.clear();
        it->maintenanceToken = 0;
      }
    }
  }

  return m_pool->waitForDone(p_timeoutMs);
}

SyncWorkQueueManager::MaintenanceLease
SyncWorkQueueManager::tryAcquireMaintenance(const QStringList &p_notebookIds) {
  QMutexLocker locker(&m_mutex);
  if (m_shutdown) {
    return MaintenanceLease();
  }

  QStringList notebookIds = p_notebookIds;
  std::sort(notebookIds.begin(), notebookIds.end());

  QStringList normalizedIds;
  normalizedIds.reserve(notebookIds.size());
  for (const auto &id : notebookIds) {
    if (normalizedIds.isEmpty() || normalizedIds.back() != id) {
      normalizedIds.append(id);
    }
  }
  if (normalizedIds.isEmpty()) {
    return MaintenanceLease();
  }

  for (const auto &id : normalizedIds) {
    auto it = m_perNotebook.constFind(id);
    if (it != m_perNotebook.constEnd() &&
        (it->running || !it->queue.isEmpty() || it->maintenanceToken != 0)) {
      return MaintenanceLease();
    }
  }

  quint64 token = m_nextMaintenanceToken++;
  while (token == 0 || m_maintenanceLeases.contains(token)) {
    token = m_nextMaintenanceToken++;
  }
  for (const auto &id : normalizedIds) {
    m_perNotebook[id].maintenanceToken = token;
  }
  m_maintenanceLeases.insert(token, normalizedIds);
  return MaintenanceLease(m_leaseControl, token);
}

void SyncWorkQueueManager::releaseMaintenance(quint64 p_token) {
  QStringList runners;
  {
    QMutexLocker locker(&m_mutex);
    auto leaseIt = m_maintenanceLeases.find(p_token);
    if (leaseIt == m_maintenanceLeases.end()) {
      return;
    }

    const QStringList notebookIds = leaseIt.value();
    m_maintenanceLeases.erase(leaseIt);
    for (const auto &id : notebookIds) {
      auto slotIt = m_perNotebook.find(id);
      if (slotIt == m_perNotebook.end() || slotIt->maintenanceToken != p_token) {
        continue;
      }
      slotIt->maintenanceToken = 0;
      if (!m_shutdown && !slotIt->running && !slotIt->queue.isEmpty()) {
        slotIt->running = true;
        runners.append(id);
      }
    }
  }

  startRunners(runners);
}

void SyncWorkQueueManager::startRunners(const QStringList &p_notebookIds) {
  // Caller holds m_leaseControl->mutex, but never m_mutex. This makes runner
  // submission atomic with respect to shutdown without invoking work under
  // the queue-state lock.
  for (const auto &id : p_notebookIds) {
    m_pool->start([this, id]() { this->runLoop(id); });
  }
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

void SyncWorkQueueManager::setMaxDepth(int p_depth) {
  QMutexLocker locker(&m_mutex);
  m_maxDepth = p_depth;
}

int SyncWorkQueueManager::maxDepth() const {
  QMutexLocker locker(&m_mutex);
  return m_maxDepth;
}

void SyncWorkQueueManager::testForceInFlight(const QString &p_notebookId, bool p_value) {
  QMutexLocker locker(&m_mutex);
  if (p_value) {
    auto &slot = m_perNotebook[p_notebookId];
    slot.running = true;
    slot.hasPending = true;
  } else {
    auto it = m_perNotebook.find(p_notebookId);
    if (it != m_perNotebook.end()) {
      it->running = false;
      it->hasPending = !it->queue.isEmpty();
    }
  }
}
