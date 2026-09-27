#include "syncops.h"

#include <QDebug>
#include <QElapsedTimer>

#include <functional>
#include <memory>
#include <sync/sync_backend.h>
#include <thread>
#include <utility>

#include <core/logging.h>
#include <core/services/isyncnotebookservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>

namespace vnotex {
namespace {

void logSyncResult(const char *p_operation, const QString &p_notebookId,
                   unsigned long long p_worker, VxCoreError p_code, qint64 p_elapsedMs) {
  if (p_code == VXCORE_OK) {
    qCInfo(lcSync) << p_operation << "end notebookId:" << p_notebookId << "worker:" << p_worker
                   << "result:" << static_cast<int>(p_code) << "elapsed_ms:" << p_elapsedMs;
  } else {
    qCWarning(lcSync) << p_operation << "end notebookId:" << p_notebookId << "worker:" << p_worker
                      << "result:" << static_cast<int>(p_code) << "elapsed_ms:" << p_elapsedMs;
  }
}

} // namespace

namespace SyncOps {

void disableSync(NotebookCoreService *p_svc, QString p_notebookId,
                 std::function<void(VxCoreError)> p_onFinished) {
  if (!p_svc) {
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  const VxCoreError code = p_svc->disableSync(p_notebookId);
  if (p_onFinished) {
    p_onFinished(code);
  }
}

void setCredentials(NotebookCoreService *p_svc, QString p_notebookId, QString p_credentialsJson,
                    std::function<void(VxCoreError)> p_onFinished) {
  // NOTE: p_credentialsJson contains the PAT in plaintext. Never log it.
  // The QString is held by value here; it goes out of scope when this
  // function returns, so no copy outlives the call.
  QElapsedTimer timer;
  timer.start();
  const auto worker =
      static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  qCInfo(lcSync) << "SyncOps::setCredentials start notebookId:" << p_notebookId
                 << "worker:" << worker;
  if (!p_svc) {
    logSyncResult("SyncOps::setCredentials", p_notebookId, worker, VXCORE_ERR_NULL_POINTER,
                  timer.elapsed());
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  const VxCoreError code = p_svc->setSyncCredentials(p_notebookId, p_credentialsJson);
  logSyncResult("SyncOps::setCredentials", p_notebookId, worker, code, timer.elapsed());
  if (p_onFinished) {
    p_onFinished(code);
  }
}

void enableSync(NotebookCoreService *p_svc, QString p_notebookId, QString p_configJson,
                QString p_credentialsJson, std::function<void(VxCoreError, QString)> p_onFinished) {
  QElapsedTimer timer;
  timer.start();
  const auto worker =
      static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  qCInfo(lcSync) << "SyncOps::enableSync start notebookId:" << p_notebookId << "worker:" << worker;
  if (!p_svc) {
    logSyncResult("SyncOps::enableSync", p_notebookId, worker, VXCORE_ERR_NULL_POINTER,
                  timer.elapsed());
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER, QStringLiteral("null service"));
    }
    return;
  }

  VxCoreError code = VXCORE_OK;
  if (p_credentialsJson.isEmpty()) {
    code = p_svc->enableSync(p_notebookId, p_configJson);
  } else {
    code = p_svc->enableSync(p_notebookId, p_configJson, p_credentialsJson);
  }

  logSyncResult("SyncOps::enableSync", p_notebookId, worker, code, timer.elapsed());
  if (p_onFinished) {
    p_onFinished(code, p_svc->syncErrorMessage(code));
  }
}

void triggerSync(ISyncNotebookService *p_svc, QString p_notebookId,
                 VxCoreSyncCancellation *p_cancel, std::function<void(VxCoreError)> p_onFinished,
                 NotebookIoGate *p_gate, std::function<VxCoreError()> p_applyPhase) {
  QElapsedTimer timer;
  timer.start();
  const auto worker =
      static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  qCInfo(lcSync) << "SyncOps::triggerSync start notebookId:" << p_notebookId << "worker:" << worker;
  // Null-service early-return runs BEFORE gate acquisition. This both
  // preserves prior contract semantics and satisfies the T8 invariant
  // "cancellation/precondition checks before acquiring the gate", since
  // grabbing the per-notebook mutex on a doomed call would needlessly
  // block any concurrent BufferSaveQueue worker on the same notebook.
  if (!p_svc) {
    logSyncResult("SyncOps::triggerSync", p_notebookId, worker, VXCORE_ERR_NULL_POINTER,
                  timer.elapsed());
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  uint32_t capabilities = 0;
  auto code = p_svc->syncCapabilities(p_notebookId, &capabilities);
  const bool deferredApply =
      (capabilities & static_cast<uint32_t>(vxcore::SyncCapability::DeferredLocalApply)) != 0;
  if (code == VXCORE_OK && deferredApply && !p_applyPhase) {
    code = VXCORE_ERR_NOT_IMPLEMENTED;
  }

  // Snapshot/staging excludes saves; network work never holds the notebook gate.
  if (code == VXCORE_OK) {
    bool didCommit = false;
    std::unique_ptr<NotebookIoGate::ScopedLock> lock;
    if (p_gate) {
      QElapsedTimer gateTimer;
      gateTimer.start();
      qCInfo(lcSync) << "SyncOps::triggerSync gate wait notebookId:" << p_notebookId
                     << "worker:" << worker;
      lock.reset(new NotebookIoGate::ScopedLock(*p_gate, p_notebookId));
      qCInfo(lcSync) << "SyncOps::triggerSync gate acquired notebookId:" << p_notebookId
                     << "worker:" << worker << "wait_ms:" << gateTimer.elapsed();
    }
    QElapsedTimer stageTimer;
    stageTimer.start();
    qCInfo(lcSync) << "SyncOps::triggerSync stage_commit start notebookId:" << p_notebookId
                   << "worker:" << worker;
    code = p_svc->syncStageOnly(p_notebookId, p_cancel, &didCommit);
    if (code == VXCORE_OK) {
      qCInfo(lcSync) << "SyncOps::triggerSync stage_commit end notebookId:" << p_notebookId
                     << "worker:" << worker << "result:" << static_cast<int>(code)
                     << "didCommit:" << didCommit << "elapsed_ms:" << stageTimer.elapsed();
    } else {
      qCWarning(lcSync) << "SyncOps::triggerSync stage_commit end notebookId:" << p_notebookId
                        << "worker:" << worker << "result:" << static_cast<int>(code)
                        << "didCommit:" << didCommit << "elapsed_ms:" << stageTimer.elapsed();
    }
  }
  if (code == VXCORE_OK) {
    QElapsedTimer networkTimer;
    networkTimer.start();
    qCInfo(lcSync) << "SyncOps::triggerSync network start notebookId:" << p_notebookId
                   << "worker:" << worker;
    code = p_svc->syncNetworkPhase(p_notebookId, p_cancel);
    logSyncResult("SyncOps::triggerSync network", p_notebookId, worker, code,
                  networkTimer.elapsed());
  }
  if (code == VXCORE_OK && deferredApply) {
    code = p_applyPhase();
  }

  logSyncResult("SyncOps::triggerSync", p_notebookId, worker, code, timer.elapsed());
  // The caller owns the token through apply acknowledgement and completion.
  if (p_onFinished) {
    p_onFinished(code);
  }
}

void resolveConflict(NotebookCoreService *p_svc, QString p_notebookId, QString p_filePath,
                     QString p_resolution, std::function<void(VxCoreError)> p_onFinished) {
  if (!p_svc) {
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  const VxCoreError code = p_svc->resolveSyncConflict(p_notebookId, p_filePath, p_resolution);
  if (p_onFinished) {
    p_onFinished(code);
  }
}

} // namespace SyncOps
} // namespace vnotex
