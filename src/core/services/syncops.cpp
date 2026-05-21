#include "syncops.h"

#include <utility>

#include <core/services/notebookcoreservice.h>

namespace vnotex {
namespace {

// Brief human-readable mapping for the error code carried by enableSync's
// completion callback. Mirrors SyncWorker's local vxErrorToString helper.
QString vxErrorToString(VxCoreError p_code) {
  switch (p_code) {
  case VXCORE_OK:
    return QStringLiteral("OK");
  case VXCORE_ERR_NULL_POINTER:
    return QStringLiteral("null service");
  case VXCORE_ERR_NOT_FOUND:
    return QStringLiteral("not found");
  case VXCORE_ERR_UNSUPPORTED:
    return QStringLiteral("unsupported");
  case VXCORE_ERR_NOT_INITIALIZED:
    return QStringLiteral("not initialized");
  case VXCORE_ERR_SYNC_IN_PROGRESS:
    return QStringLiteral("sync in progress");
  case VXCORE_ERR_SYNC_CONFLICT:
    return QStringLiteral("sync conflict");
  case VXCORE_ERR_SYNC_AUTH_FAILED:
    return QStringLiteral("sync auth failed");
  case VXCORE_ERR_SYNC_NETWORK:
    return QStringLiteral("sync network error");
  case VXCORE_ERR_SYNC_NOT_ENABLED:
    return QStringLiteral("sync not enabled");
  case VXCORE_ERR_UNKNOWN:
    return QStringLiteral("unknown error");
  default:
    return QStringLiteral("vxcore error %1").arg(static_cast<int>(p_code));
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
  if (!p_svc) {
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  const VxCoreError code = p_svc->setSyncCredentials(p_notebookId, p_credentialsJson);
  if (p_onFinished) {
    p_onFinished(code);
  }
}

void enableSync(NotebookCoreService *p_svc, QString p_notebookId, QString p_configJson,
                QString p_credentialsJson, std::function<void(VxCoreError, QString)> p_onFinished) {
  if (!p_svc) {
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER, vxErrorToString(VXCORE_ERR_NULL_POINTER));
    }
    return;
  }

  VxCoreError code = VXCORE_OK;
  if (p_credentialsJson.isEmpty()) {
    code = p_svc->enableSync(p_notebookId, p_configJson);
  } else {
    code = p_svc->enableSync(p_notebookId, p_configJson, p_credentialsJson);
  }

  if (p_onFinished) {
    p_onFinished(code, code == VXCORE_OK ? QString() : vxErrorToString(code));
  }
}

void triggerSync(NotebookCoreService *p_svc, QString p_notebookId, VxCoreSyncCancellation *p_cancel,
                 std::function<void(VxCoreError)> p_onFinished) {
  if (!p_svc) {
    if (p_onFinished) {
      p_onFinished(VXCORE_ERR_NULL_POINTER);
    }
    return;
  }

  VxCoreError code = VXCORE_OK;
  if (p_cancel) {
    code = p_svc->triggerSyncCancellable(p_notebookId, p_cancel);
  } else {
    code = p_svc->triggerSync(p_notebookId);
  }

  // NOTE: p_cancel is owned by the caller (SyncService). Do NOT free.
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
