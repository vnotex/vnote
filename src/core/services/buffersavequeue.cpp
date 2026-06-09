// BufferSaveQueue
//
// Thread-safety notes:
//   libs/vxcore/src/core/buffer.cpp does NOT carry a per-buffer mutex — concurrent
//   setContentRaw/saveBuffer calls on the same vxcore buffer would race. Adding a
//   mutex inside vxcore was explicitly rejected (T5 plan): it would change vxcore
//   semantics and risk deadlock with EventManager::Emit listeners running under
//   that lock.
//
//   Mitigation owned here: a TWO-LEVEL serialisation scheme.
//     - FIFO key: composite (notebookId, bufferId). Per-buffer serialisation
//       guarantees no two workers ever run setContentRaw/saveBuffer on the same
//       vxcore buffer concurrently — even when multiple buffers live inside one
//       notebook.
//     - Gate key: notebookId (NotebookIoGate). Preserves the cross-actor
//       working-tree contract (save vs. git-stage) without over-serialising
//       sibling buffers.

#include "buffersavequeue.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QThread>
#include <QThreadPool>

#include "ibuffercoreservice.h"
#include "notebookiogate.h"

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(bufferSaveQueueLog, "vnote.buffer.savequeue")
} // namespace

BufferSaveQueue::BufferSaveQueue(IBufferCoreService &p_coreService, NotebookIoGate &p_gate,
                                 QObject *p_parent)
    : QObject(p_parent), m_coreService(p_coreService), m_gate(p_gate) {}

BufferSaveQueue::~BufferSaveQueue() { shutdown(5000); }

QString BufferSaveQueue::compositeKey(const QString &p_notebookId, const QString &p_bufferId) {
  return p_notebookId + QStringLiteral("::") + p_bufferId;
}

void BufferSaveQueue::enqueue(const QString &p_notebookId, const QString &p_bufferId,
                              const QString &p_content, quint64 p_revision) {
  // Guard: refuse to write to a read-only notebook's buffer (T16).
  // Checked BEFORE any mutex acquisition, queue insertion, or worker dispatch
  // so the disk file is NEVER touched. Notebook RO state is immutable for
  // the notebook's lifetime under the current design, so the enqueue-time
  // check is sufficient — no worker-thread re-check is needed.
  // Emitted directly on the caller (UI) thread; listeners (T28) warn the user.
  if (m_coreService.isBufferReadOnly(p_bufferId)) {
    qCWarning(bufferSaveQueueLog) << "enqueue rejected: buffer is read-only" << p_bufferId
                                  << "notebook" << p_notebookId << "revision" << p_revision;
    emit saveRejectedReadOnly(p_bufferId);
    return;
  }

  const QString key = compositeKey(p_notebookId, p_bufferId);

  SaveJob superseded;
  bool hasSuperseded = false;
  bool needLaunch = false;

  {
    QMutexLocker locker(&m_mutex);
    if (m_stopping) {
      qCWarning(bufferSaveQueueLog) << "enqueue after shutdown ignored for" << key;
      return;
    }

    // Coalesce: an existing PENDING (not started) job for this key is dropped
    // in favour of the newest snapshot. We must still notify the caller that
    // its older save resolved, so capture it for an out-of-lock signal.
    auto it = m_pending.find(key);
    if (it != m_pending.end()) {
      superseded = it.value();
      hasSuperseded = true;
    }

    SaveJob job;
    job.notebookId = p_notebookId;
    job.bufferId = p_bufferId;
    job.content = p_content;
    job.revision = p_revision;
    m_pending.insert(key, job);

    if (!m_running.value(key, false)) {
      m_running.insert(key, true);
      ++m_inFlightCount;
      needLaunch = true;
    }
  }

  if (hasSuperseded) {
    // Superseded jobs report success with their original revision so waiters
    // resolve. The newer job (just inserted) carries the durable write.
    emitFinishedQueued(superseded.bufferId, superseded.revision, true, QString());
  }

  if (needLaunch) {
    const QString keyCopy = key;
    QThreadPool::globalInstance()->start([this, keyCopy]() { runWorker(keyCopy); });
  }
}

void BufferSaveQueue::runWorker(const QString &p_key) {
  for (;;) {
    SaveJob job;
    {
      QMutexLocker locker(&m_mutex);
      auto it = m_pending.find(p_key);
      if (it == m_pending.end()) {
        m_running.insert(p_key, false);
        if (m_inFlightCount > 0) {
          --m_inFlightCount;
        }
        if (m_inFlightCount == 0) {
          m_drained.wakeAll();
        }
        return;
      }
      job = it.value();
      m_pending.erase(it);
    }

    QString errMsg;
    bool ok = false;

    try {
      // Acquire gate OUTSIDE m_mutex. Worker thread only.
      NotebookIoGate::ScopedLock lock(m_gate, job.notebookId);

      const QByteArray bytes = job.content.toUtf8();
      if (!m_coreService.setContentRaw(job.bufferId, bytes)) {
        errMsg = QStringLiteral("setContentRaw failed");
      } else if (!m_coreService.saveBuffer(job.bufferId)) {
        errMsg = QStringLiteral("saveBuffer failed");
      } else {
        ok = true;
      }
    } catch (const std::exception &e) {
      errMsg = QStringLiteral("exception: ") + QString::fromUtf8(e.what());
    } catch (...) {
      errMsg = QStringLiteral("unknown exception");
    }

    emitFinishedQueued(job.bufferId, job.revision, ok, errMsg);
  }
}

void BufferSaveQueue::emitFinishedQueued(const QString &p_bufferId, quint64 p_revision, bool p_ok,
                                         const QString &p_errorMsg) {
  // Bounce to the owning thread; receivers must not see worker-thread emissions.
  QMetaObject::invokeMethod(
      this,
      [this, p_bufferId, p_revision, p_ok, p_errorMsg]() {
        emit saveFinished(p_bufferId, p_revision, p_ok, p_errorMsg);
      },
      Qt::QueuedConnection);
}

bool BufferSaveQueue::shutdown(int p_timeoutMs) {
  QMutexLocker locker(&m_mutex);
  if (m_stopping && m_inFlightCount == 0) {
    return true;
  }
  m_stopping = true;
  // NOTE: do NOT clear m_pending here — workers already dispatched are counted
  // as in-flight and must drain (run their pending job) so callers see the
  // saveFinished signal. m_stopping blocks new enqueue() from adding more.

  if (m_inFlightCount == 0) {
    return true;
  }

  QDeadlineTimer deadline(p_timeoutMs);
  while (m_inFlightCount > 0) {
    if (!m_drained.wait(&m_mutex, deadline)) {
      return m_inFlightCount == 0;
    }
  }
  return true;
}
