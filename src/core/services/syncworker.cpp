#include "syncworker.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QThread>

#include <core/services/notebookcoreservice.h>
#include <core/services/synclog.h>

using namespace vnotex;

namespace {

// Convert a VxCoreError code to a brief human-readable string for logs and
// signal payloads. Intentionally minimal; UI-facing strings are produced by
// SyncService / dialogs, not here.
QString vxErrorToString(VxCoreError p_code) {
  switch (p_code) {
  case VXCORE_OK:
    return QStringLiteral("OK");
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

// Parse the {"conflicts": [{"path": ...}, ...]} shape returned by
// vxcore_sync_get_conflicts and extract just the "path" strings.
QStringList parseConflictPaths(const QString &p_conflictsJson) {
  QStringList paths;
  if (p_conflictsJson.isEmpty()) {
    return paths;
  }
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(p_conflictsJson.toUtf8(), &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    return paths;
  }
  const QJsonArray arr = doc.object().value(QStringLiteral("conflicts")).toArray();
  paths.reserve(arr.size());
  for (const QJsonValue &v : arr) {
    if (!v.isObject()) {
      continue;
    }
    const QString path = v.toObject().value(QStringLiteral("path")).toString();
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  return paths;
}

} // namespace

SyncWorker::SyncWorker(NotebookCoreService *p_notebookCoreService, QObject *p_parent)
    : QObject(p_parent), m_notebookCoreService(p_notebookCoreService) {
  Q_ASSERT(m_notebookCoreService);
  // Required for QueuedConnection delivery of signals carrying these types.
  qRegisterMetaType<VxCoreError>("VxCoreError");
  qRegisterMetaType<QStringList>("QStringList");
}

SyncWorker::~SyncWorker() = default;

bool SyncWorker::isSyncInProgressGlobal() const noexcept {
  return m_syncInProgressGlobal.load(std::memory_order_acquire);
}

void SyncWorker::testHangNextOperation(int p_sleepMs) {
  m_testHangMs.store(p_sleepMs, std::memory_order_release);
}

void SyncWorker::testForceError(int p_code) {
  m_testForceError.store(p_code, std::memory_order_release);
}

bool SyncWorker::consumeForcedError(VxCoreError &p_outCode) {
  const int forced = m_testForceError.exchange(-1, std::memory_order_acq_rel);
  if (forced < 0) {
    return false;
  }
  p_outCode = static_cast<VxCoreError>(forced);
  return true;
}

void SyncWorker::maybeHang() {
  const int hangMs = m_testHangMs.exchange(-1, std::memory_order_acq_rel);
  if (hangMs > 0) {
    QThread::msleep(static_cast<unsigned long>(hangMs));
  }
}

// ---- Slots -----------------------------------------------------------------

void SyncWorker::enableSync(QString p_notebookId, QString p_configJson, QString p_credentialsJson) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::enableSync: [worker] notebookId:" << p_notebookId;

  maybeHang();

  VxCoreError code = VXCORE_OK;
  if (consumeForcedError(code)) {
    emit enableFinished(p_notebookId, code, vxErrorToString(code));
    return;
  }

  if (p_credentialsJson.isEmpty()) {
    code = m_notebookCoreService->enableSync(p_notebookId, p_configJson);
  } else {
    code = m_notebookCoreService->enableSyncWithCredentials(p_notebookId, p_configJson,
                                                            p_credentialsJson);
  }
  emit enableFinished(p_notebookId, code, vxErrorToString(code));
}

void SyncWorker::disableSync(QString p_notebookId) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::disableSync: [worker] notebookId:" << p_notebookId;

  maybeHang();

  VxCoreError code = VXCORE_OK;
  if (consumeForcedError(code)) {
    emit disableFinished(p_notebookId, code);
    return;
  }

  code = m_notebookCoreService->disableSync(p_notebookId);
  emit disableFinished(p_notebookId, code);
}

void SyncWorker::triggerSync(QString p_notebookId) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::triggerSync: [worker] notebookId:" << p_notebookId;

  m_syncInProgressGlobal.store(true, std::memory_order_release);
  emit syncStarted(p_notebookId);

  maybeHang();

  VxCoreError code = VXCORE_OK;
  if (!consumeForcedError(code)) {
    code = m_notebookCoreService->triggerSync(p_notebookId);
  }

  m_syncInProgressGlobal.store(false, std::memory_order_release);

  if (code != VXCORE_OK) {
    emit syncFailed(p_notebookId, code, vxErrorToString(code));
  }
  emit syncFinished(p_notebookId, code);
}

void SyncWorker::getStatus(QString p_notebookId) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::getStatus: [worker] notebookId:" << p_notebookId;

  maybeHang();

  VxCoreError code = VXCORE_OK;
  QString statusJson;
  if (consumeForcedError(code)) {
    emit statusReady(p_notebookId, statusJson);
    return;
  }

  code = m_notebookCoreService->getSyncStatus(p_notebookId, statusJson);
  if (code != VXCORE_OK) {
    qCDebug(syncCategory) << "SyncWorker::getStatus: failed with code" << code;
  }
  emit statusReady(p_notebookId, statusJson);
}

void SyncWorker::getConflicts(QString p_notebookId) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::getConflicts: [worker] notebookId:" << p_notebookId;

  maybeHang();

  VxCoreError code = VXCORE_OK;
  QString conflictsJson;
  if (consumeForcedError(code)) {
    emit conflictsReady(p_notebookId, conflictsJson);
    return;
  }

  code = m_notebookCoreService->getSyncConflicts(p_notebookId, conflictsJson);
  emit conflictsReady(p_notebookId, conflictsJson);

  if (code == VXCORE_OK) {
    const QStringList paths = parseConflictPaths(conflictsJson);
    if (!paths.isEmpty()) {
      emit conflictsDetected(p_notebookId, paths);
    }
  }
}

void SyncWorker::resolveConflict(QString p_notebookId, QString p_filePath, QString p_resolution) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::resolveConflict: [worker] notebookId:" << p_notebookId
                        << "file:" << p_filePath;

  maybeHang();

  VxCoreError code = VXCORE_OK;
  if (consumeForcedError(code)) {
    emit resolveConflictFinished(p_notebookId, p_filePath, code);
    return;
  }

  code = m_notebookCoreService->resolveSyncConflict(p_notebookId, p_filePath, p_resolution);
  emit resolveConflictFinished(p_notebookId, p_filePath, code);
}

void SyncWorker::setCredentials(QString p_notebookId, QString p_credentialsJson) {
  Q_ASSERT(QThread::currentThread() == this->thread());
  qCDebug(syncCategory) << "SyncWorker::setCredentials: [worker] notebookId:" << p_notebookId;

  maybeHang();

  VxCoreError code = VXCORE_OK;
  if (consumeForcedError(code)) {
    emit credentialsSetFinished(p_notebookId, code);
    return;
  }

  code = m_notebookCoreService->setSyncCredentials(p_notebookId, p_credentialsJson);
  emit credentialsSetFinished(p_notebookId, code);
}
