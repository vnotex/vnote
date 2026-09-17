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

#include <QCryptographicHash>
#include <QFile>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QSet>
#include <QTextCodec>
#include <QThread>
#include <QThreadPool>

#include "buffer2.h"
#include "buffercoreservice.h"

#include "ibuffercoreservice.h"
#include "notebookiogate.h"

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(bufferSaveQueueLog, "vnote.buffer.savequeue")

// Encode text with the named codec, falling back to UTF-8 when the name is
// empty or unknown. Mirrors BufferService::encodeContent so the async save
// path and the inline sync paths stay byte-symmetric.
QByteArray encodeWith(const QString &p_encoding, const QString &p_text) {
  if (!p_encoding.isEmpty()) {
    if (QTextCodec *codec = QTextCodec::codecForName(p_encoding.toUtf8())) {
      return codec->fromUnicode(p_text);
    }
  }
  return p_text.toUtf8();
}
} // namespace

struct BufferSaveQueue::ProtectedQueue {
  struct Job {
    BufferCoreService *coreService = nullptr;
    QString notebookId;
    std::shared_ptr<ProtectedBufferLease> lease;
    QString content;
    quint64 revision = 0;
    QString encoding;
    bool backup = false;
    QByteArray rawContent;
    bool raw = false;
    std::function<void(int)> finished;
    int gateTimeoutMs = -1;
  };
  mutable QMutex mutex;
  QWaitCondition drained;
  QHash<QString, QQueue<Job>> pending;
  QSet<QString> running;
};

BufferSaveQueue::BufferSaveQueue(IBufferCoreService &p_coreService, NotebookIoGate &p_gate,
                                 QObject *p_parent)
    : QObject(p_parent), m_coreService(p_coreService), m_gate(p_gate) {}

BufferSaveQueue::~BufferSaveQueue() {
  // No worker may outlive the queue, gate, or core/context, even after a prior
  // bounded shutdown timed out. This joins ordinary, exact, and protected jobs.
  shutdown(-1);
}

QString BufferSaveQueue::compositeKey(const QString &p_notebookId, const QString &p_bufferId) {
  return p_notebookId + QStringLiteral("::") + p_bufferId;
}

void BufferSaveQueue::enqueue(const QString &p_notebookId, const QString &p_bufferId,
                              const QString &p_content, quint64 p_revision,
                              const QString &p_encoding) {
  // Guard: refuse to write to a read-only buffer (T16).
  // Checked BEFORE any mutex acquisition, queue insertion, or worker dispatch
  // so the disk file is NEVER touched. isBufferReadOnly is a stable per-buffer
  // fact resolved when the buffer was opened (BufferService), so the
  // enqueue-time check is sufficient — no worker-thread re-check is needed.
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

    if (m_running.value(key, JobType::Ordinary) == JobType::Replacement) {
      qCWarning(bufferSaveQueueLog) << "enqueue rejected: replacement owns" << key;
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
    job.encoding = p_encoding;
    m_pending.insert(key, job);

    if (!m_running.contains(key)) {
      m_running.insert(key, JobType::Ordinary);
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

bool BufferSaveQueue::enqueueReplacement(const QString &p_notebookId, const QString &p_bufferId,
                                         const QString &p_resolvedPath,
                                         const QByteArray &p_expectedFileSha256,
                                         const QByteArray &p_content, quint64 p_revision,
                                         const std::shared_ptr<std::atomic_bool> &p_cancelled) {
  if (m_coreService.isBufferReadOnly(p_bufferId)) {
    return false;
  }
  const QString key = compositeKey(p_notebookId, p_bufferId);
  {
    QMutexLocker locker(&m_mutex);
    if (m_stopping || m_running.contains(key) || m_pending.contains(key)) {
      return false;
    }
    SaveJob job;
    job.type = JobType::Replacement;
    job.notebookId = p_notebookId;
    job.bufferId = p_bufferId;
    job.rawContent = p_content;
    job.resolvedPath = p_resolvedPath;
    job.expectedFileSha256 = p_expectedFileSha256;
    job.revision = p_revision;
    job.cancelled = p_cancelled;
    m_pending.insert(key, std::move(job));
    m_running.insert(key, JobType::Replacement);
    ++m_inFlightCount;
  }
  QThreadPool::globalInstance()->start([this, key]() { runWorker(key); });
  return true;
}

void BufferSaveQueue::runWorker(const QString &p_key) {
  for (;;) {
    SaveJob job;
    {
      QMutexLocker locker(&m_mutex);
      auto it = m_pending.find(p_key);
      if (it == m_pending.end()) {
        m_running.remove(p_key);
        if (m_inFlightCount > 0) {
          --m_inFlightCount;
        }
        if (m_inFlightCount == 0) {
          m_drained.wakeAll();
        }
        return;
      }
      job = std::move(it.value());
      m_pending.erase(it);
    }

    QString errMsg;
    bool ok = false;
    bool contentChanged = false;

    try {
      // Acquire gate OUTSIDE m_mutex. Worker thread only.
      NotebookIoGate::ScopedLock lock(m_gate, job.notebookId);

      auto persist = [&](const QByteArray &p_bytes) {
        if (!m_coreService.setContentRaw(job.bufferId, p_bytes)) {
          errMsg = QStringLiteral("setContentRaw failed");
          return;
        }
        contentChanged = true;
        // Once memory has changed, cancellation must not abandon the save.
        if (!m_coreService.saveBuffer(job.bufferId)) {
          errMsg = QStringLiteral("saveBuffer failed");
        } else {
          ok = true;
        }
      };
      if (job.type == JobType::Replacement) {
        auto cancelled = [&]() { return job.cancelled && job.cancelled->load(); };
        auto matchesDisk = [&]() {
          QFile file(job.resolvedPath);
          QCryptographicHash hash(QCryptographicHash::Sha256);
          return file.open(QIODevice::ReadOnly) && hash.addData(&file) &&
                 file.error() == QFileDevice::NoError && hash.result() == job.expectedFileSha256;
        };
        if (cancelled()) {
          errMsg = QStringLiteral("Replacement cancelled.");
        } else if (!matchesDisk()) {
          errMsg = QStringLiteral("The note changed on disk. Search again before replacing.");
        } else if (cancelled()) {
          // Hashing can take time; cancellation must still prevent installation.
          errMsg = QStringLiteral("Replacement cancelled.");
        } else {
          persist(job.rawContent);
        }
      } else {
        persist(encodeWith(job.encoding, job.content));
      }
    } catch (const std::exception &e) {
      errMsg = QStringLiteral("exception: ") + QString::fromUtf8(e.what());
    } catch (...) {
      errMsg = QStringLiteral("unknown exception");
    }

    if (job.type == JobType::Replacement) {
      QMutexLocker locker(&m_mutex);
      m_running.remove(p_key);
      // Post before dropping worker accounting, so shutdown cannot destroy this
      // object between releasing the key and posting its terminal notification.
      QMetaObject::invokeMethod(
          this,
          [this, bufferId = job.bufferId, revision = job.revision, contentChanged, ok, errMsg]() {
            emit replacementFinished(bufferId, revision, contentChanged, ok, errMsg);
          },
          Qt::QueuedConnection);
      --m_inFlightCount;
      if (m_inFlightCount == 0) {
        m_drained.wakeAll();
      }
      return;
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

void BufferSaveQueue::prepareProtected() {
  if (!m_protectedQueue) {
    m_protectedQueue.reset(new ProtectedQueue);
  }
}

bool BufferSaveQueue::enqueueProtected(BufferCoreService &p_coreService,
                                       const QString &p_notebookId,
                                       const std::shared_ptr<ProtectedBufferLease> &p_lease,
                                       const QString &p_content, quint64 p_revision,
                                       const QString &p_encoding, bool p_backup,
                                       QByteArray *p_rawContent,
                                       std::function<void(int)> p_finished, int p_gateTimeoutMs) {
  if (!p_lease || !p_lease->isCurrent()) {
    return false;
  }
  const QString bufferId = p_lease->bufferId();
  bool launch = false;
  {
    QMutexLocker queueLock(&m_mutex);
    if (m_stopping) {
      return false;
    }
    // Publish the job before shutdown can stop acceptance and start draining.
    // BufferService also prepares this before any protected handle escapes.
    prepareProtected();
    QMutexLocker lock(&m_protectedQueue->mutex);
    ProtectedQueue::Job job;
    job.coreService = &p_coreService;
    job.notebookId = p_notebookId;
    job.lease = p_lease;
    job.content = p_content;
    job.revision = p_revision;
    job.encoding = p_encoding;
    job.backup = p_backup;
    if (p_rawContent) {
      job.rawContent = std::move(*p_rawContent);
      job.raw = true;
    }
    job.finished = std::move(p_finished);
    job.gateTimeoutMs = p_gateTimeoutMs;
    m_protectedQueue->pending[bufferId].enqueue(std::move(job));
    if (!m_protectedQueue->running.contains(bufferId)) {
      m_protectedQueue->running.insert(bufferId);
      launch = true;
    }
  }
  if (launch) {
    QThreadPool::globalInstance()->start([this, bufferId]() { runProtectedWorker(bufferId); });
  }
  return true;
}

void BufferSaveQueue::runProtectedWorker(const QString &p_bufferId) {
  for (;;) {
    ProtectedQueue::Job job;
    {
      QMutexLocker lock(&m_protectedQueue->mutex);
      auto it = m_protectedQueue->pending.find(p_bufferId);
      if (it == m_protectedQueue->pending.end() || it->isEmpty()) {
        if (it != m_protectedQueue->pending.end()) {
          m_protectedQueue->pending.erase(it);
        }
        m_protectedQueue->running.remove(p_bufferId);
        m_protectedQueue->drained.wakeAll();
        return;
      }
      job = it->dequeue();
    }
    VxCoreError error = VXCORE_ERR_INVALID_STATE;
    QByteArray bytes = std::move(job.rawContent);
    struct WipeBytes {
      QByteArray &bytes;
      ~WipeBytes() {
        volatile char *data = bytes.data();
        for (int i = 0; i < bytes.size(); ++i)
          data[i] = 0;
      }
    } wipe{bytes};
    try {
      auto persist = [&]() {
        if (!job.lease->isCurrent())
          return;
        if (!job.raw)
          bytes = encodeWith(job.encoding, job.content);
        if (job.coreService->setContentRaw(p_bufferId, bytes, &error)) {
          if (job.backup) {
            job.coreService->writeBackup(p_bufferId, &error);
          } else {
            job.coreService->saveBuffer(p_bufferId, &error);
          }
        }
      };
      if (job.gateTimeoutMs >= 0) {
        NotebookIoGate::ScopedTryLock gate(m_gate, job.notebookId, job.gateTimeoutMs);
        if (gate.isLocked())
          persist();
        else
          error = VXCORE_ERR_SYNC_IN_PROGRESS;
      } else {
        NotebookIoGate::ScopedLock gate(m_gate, job.notebookId);
        persist();
      }
    } catch (const std::bad_alloc &) {
      error = VXCORE_ERR_OUT_OF_MEMORY;
    } catch (...) {
      error = VXCORE_ERR_UNKNOWN;
    }
    // The lease covers delivery too: close/Lock All cannot discard this
    // generation before its actual persistence result has been accounted for.
    QMetaObject::invokeMethod(
        this,
        [this, lease = std::move(job.lease), revision = job.revision, backup = job.backup,
         finished = std::move(job.finished), error]() {
          const auto result = lease->isCurrent() ? error : VXCORE_ERR_INVALID_STATE;
          emit protectedSaveFinished(lease->bufferId(), lease->generation(), revision, backup,
                                     static_cast<int>(result));
          if (finished)
            finished(static_cast<int>(result));
        },
        Qt::QueuedConnection);
  }
}

bool BufferSaveQueue::isProtectedBusy(const QString &p_bufferId) const {
  if (!m_protectedQueue) {
    return false;
  }
  QMutexLocker lock(&m_protectedQueue->mutex);
  return m_protectedQueue->running.contains(p_bufferId);
}

bool BufferSaveQueue::drainProtected(int p_timeoutMs) {
  if (!m_protectedQueue) {
    return true;
  }
  QDeadlineTimer deadline(p_timeoutMs);
  QMutexLocker lock(&m_protectedQueue->mutex);
  while (!m_protectedQueue->running.isEmpty()) {
    if (!m_protectedQueue->drained.wait(&m_protectedQueue->mutex, deadline)) {
      return m_protectedQueue->running.isEmpty();
    }
  }
  return true;
}

bool BufferSaveQueue::isBusy(const QString &p_notebookId, const QString &p_bufferId) const {
  const QString key = compositeKey(p_notebookId, p_bufferId);
  QMutexLocker locker(&m_mutex);
  return m_pending.contains(key) || m_running.contains(key);
}

bool BufferSaveQueue::shutdown(int p_timeoutMs) {
  QDeadlineTimer deadline(p_timeoutMs);
  {
    QMutexLocker locker(&m_mutex);
    m_stopping = true;
    // Keep accepted jobs: each must report its actual persistence outcome.
    while (m_inFlightCount > 0) {
      if (!m_drained.wait(&m_mutex, deadline) && m_inFlightCount > 0) {
        return false;
      }
    }
  }
  return drainProtected(static_cast<int>(deadline.remainingTime()));
}
