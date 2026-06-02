#include "notebookiogate.h"

#include <QMutex>
#include <QMutexLocker>

using namespace vnotex;

NotebookIoGate::NotebookIoGate() = default;

NotebookIoGate::~NotebookIoGate() = default;

void NotebookIoGate::acquire(const QString &p_notebookId) {
  QSharedPointer<QMutex> mutex;
  {
    QMutexLocker locker(&m_registryMutex);
    auto it = m_mutexes.find(p_notebookId);
    if (it == m_mutexes.end()) {
      mutex = QSharedPointer<QMutex>::create();
      m_mutexes[p_notebookId] = mutex;
    } else {
      mutex = it.value();
    }
  }
  // Acquire the per-notebook mutex outside the registry lock.
  mutex->lock();
}

void NotebookIoGate::release(const QString &p_notebookId) {
  QSharedPointer<QMutex> mutex;
  {
    QMutexLocker locker(&m_registryMutex);
    auto it = m_mutexes.find(p_notebookId);
    if (it != m_mutexes.end()) {
      mutex = it.value();
    }
  }
  // Unlock the per-notebook mutex outside the registry lock.
  if (mutex) {
    mutex->unlock();
  }
}

NotebookIoGate::ScopedLock::ScopedLock(NotebookIoGate &p_gate, const QString &p_notebookId)
    : m_gate(&p_gate), m_notebookId(p_notebookId) {
  m_gate->acquire(m_notebookId);
}

NotebookIoGate::ScopedLock::~ScopedLock() {
  if (m_gate) {
    m_gate->release(m_notebookId);
  }
}

NotebookIoGate::ScopedLock::ScopedLock(ScopedLock &&p_other) noexcept
    : m_gate(p_other.m_gate), m_notebookId(p_other.m_notebookId) {
  p_other.m_gate = nullptr;
}

NotebookIoGate::ScopedLock &NotebookIoGate::ScopedLock::operator=(ScopedLock &&p_other) noexcept {
  if (this != &p_other) {
    if (m_gate) {
      m_gate->release(m_notebookId);
    }
    m_gate = p_other.m_gate;
    m_notebookId = p_other.m_notebookId;
    p_other.m_gate = nullptr;
  }
  return *this;
}
