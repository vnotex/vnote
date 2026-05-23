#include "syncservice.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

#include <memory>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/eventbridge.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/synclog.h>
#include <core/services/syncworker.h>

#include <vxcore/vxcore.h>

Q_LOGGING_CATEGORY(syncCategory, "vnote.sync")

using namespace vnotex;

namespace {

// Bounded wait for the worker thread to finish a clean quit before we resort
// to terminate(). 30s comfortably accommodates a slow git push.
static constexpr int kShutdownTimeoutMs = 30000;

// After terminate(), give the OS a brief grace period to actually reap the
// thread before we proceed with destruction.
static constexpr int kPostTerminateJoinMs = 1000;

// Brief log-friendly description for VxCoreError. Mirrors syncworker.cpp's
// helper but is kept local to avoid coupling.
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

} // namespace

SyncService::SyncService(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  m_notebookCoreService = m_services.get<NotebookCoreService>();
  m_credentialsStore = m_services.get<SyncCredentialsStore>();
  Q_ASSERT(m_notebookCoreService);
  Q_ASSERT(m_credentialsStore);

  // Create thread and worker. The worker is constructed on the GUI thread,
  // then moveToThread'd onto the dedicated thread BEFORE the thread starts.
  // Standard Qt worker pattern.
  m_thread = new QThread(this);
  m_thread->setObjectName(QStringLiteral("VNoteSyncWorker"));
  m_worker = new SyncWorker(m_notebookCoreService);
  m_worker->moveToThread(m_thread);
  // Ensure the worker is destroyed on its own thread when the thread finishes.
  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  // Wire worker -> service forwarders. ALL must be QueuedConnection so the
  // signal payload is marshalled across threads and the slot runs on the GUI
  // thread (where SyncService lives).
  connect(m_worker, &SyncWorker::syncStarted, this, &SyncService::onWorkerSyncStarted,
          Qt::QueuedConnection);
  connect(m_worker, &SyncWorker::syncFinished, this, &SyncService::onWorkerSyncFinished,
          Qt::QueuedConnection);
  connect(m_worker, &SyncWorker::syncFailed, this, &SyncService::onWorkerSyncFailed,
          Qt::QueuedConnection);
  connect(m_worker, &SyncWorker::conflictsDetected, this, &SyncService::onWorkerConflictsDetected,
          Qt::QueuedConnection);
  connect(m_worker, &SyncWorker::enableFinished, this, &SyncService::onWorkerEnableFinished,
          Qt::QueuedConnection);
  connect(m_worker, &SyncWorker::disableFinished, this, &SyncService::onWorkerDisableFinished,
          Qt::QueuedConnection);
  connect(m_worker, &SyncWorker::credentialsSetFinished, this,
          &SyncService::onWorkerCredentialsSetFinished, Qt::QueuedConnection);

  // Wire EventBridge for vxcore auto-sync events (already on GUI thread).
  m_eventBridge = m_services.get<EventBridge>();
  if (m_eventBridge) {
    connect(m_eventBridge, &EventBridge::syncStarted, this, &SyncService::onAutoSyncStarted);
    connect(m_eventBridge, &EventBridge::syncFinished, this, &SyncService::onAutoSyncFinished);
    connect(m_eventBridge, &EventBridge::syncConflict, this, &SyncService::onAutoSyncConflict);
  }

  m_thread->start();

  // T17: subscribe to NotebookBeforeClose. Refuse the close while a sync is
  // in progress for the same notebook. Per ADR-9 we use HookContext::cancel()
  // (no-arg) and stash a user-visible reason via setMetadata("syncCancelReason", ...).
  // The handler also pops a QMessageBox directly because HookManager guarantees
  // single-threaded (GUI-thread) handler execution.
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    hookMgr->addAction<NotebookCloseEvent>(
        HookNames::NotebookBeforeClose,
        [this](HookContext &p_ctx, const NotebookCloseEvent &p_event) {
          if (!isSyncInProgress(p_event.notebookId)) {
            return;
          }
          p_ctx.cancel();
          const QString reason =
              tr("Sync is in progress for this notebook. Please wait for sync to "
                 "complete before closing.");
          p_ctx.setMetadata(QStringLiteral("syncCancelReason"), reason);
          QMessageBox::warning(qApp ? qApp->activeWindow() : nullptr, tr("Cannot close notebook"),
                               reason);
        },
        /*priority=*/10);

    hookMgr->addAction<NotebookOpenEvent>(
        HookNames::NotebookAfterOpen,
        [this](HookContext &, const NotebookOpenEvent &p_event) { onNotebookAfterOpen(p_event); },
        /*priority=*/10);

    hookMgr->addAction(
        HookNames::MainWindowAfterStart,
        [this](HookContext &, const QVariantMap &) { onMainWindowAfterStart(); },
        /*priority=*/10);
  }
}

void SyncService::shutdown() {
  if (m_shutDown) {
    return;
  }
  m_shutDown = true;
  if (!m_thread) {
    return;
  }
  m_thread->quit();
  const bool ok = m_thread->wait(kShutdownTimeoutMs);
  if (!ok) {
    qWarning() << "SyncService shutdown timed out after 30s; terminating worker thread";
    m_thread->terminate();
    m_thread->wait(kPostTerminateJoinMs);
  }

  // Wave 12.2 / F5.9: release any leftover cancellation tokens. After the
  // worker thread has joined, no slot can be in flight, so freeing is safe.
  QHash<QString, void *> leftover;
  {
    QMutexLocker locker(&m_cancellationMutex);
    leftover.swap(m_cancellations);
  }
  for (auto it = leftover.begin(); it != leftover.end(); ++it) {
    if (it.value()) {
      vxcore_sync_free_cancellation(static_cast<VxCoreSyncCancellation *>(it.value()));
    }
  }
}

SyncService::~SyncService() {
  // Idempotent: if shutdown() was already called via aboutToQuit, this is a
  // no-op. Otherwise it performs a bounded quit/wait/terminate.
  shutdown();
}

QString SyncService::buildCredentialsJson(const QString &p_pat) {
  QJsonObject obj;
  obj[QStringLiteral("pat")] = p_pat;
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString SyncService::buildConfigJson(const QString &p_remoteUrl) {
  QJsonObject obj;
  obj[QStringLiteral("backend")] = QStringLiteral("git");
  obj[QStringLiteral("remoteUrl")] = p_remoteUrl;
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void SyncService::enableSyncForNotebook(const QString &p_notebookId, const QString &p_remoteUrl,
                                        const QString &p_pat) {
  if (m_shutDown) {
    qWarning() << "SyncService::enableSyncForNotebook: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::enableSyncForNotebook: notebookId:" << p_notebookId;

  // F4.5: fire vnote.sync.before_enable BEFORE any lock acquisition or worker
  // dispatch. Observe-only: HookContext::cancel() is intentionally ignored —
  // sync hooks may NOT abort the underlying op (per plan F4.5). PAT is NEVER
  // included in the payload.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("remoteUrl")] = p_remoteUrl;
    hookMgr->doAction(HookNames::SyncBeforeEnable, args);
  }

  const QString configJson = buildConfigJson(p_remoteUrl);

  // PAT is captured ONLY in the lambda below; it is never assigned to a
  // SyncService member. The lambda destructs (releasing the capture) after
  // the worker has been invoked.
  const QString credsJson = buildCredentialsJson(p_pat);
  const QString notebookId = p_notebookId;

  // Create heap-stored connection handles so the lambda can disconnect itself
  // (one-shot semantics). Both success and error paths share the same
  // disconnect/free flow.
  auto *storedConn = new QMetaObject::Connection;
  auto *errorConn = new QMetaObject::Connection;

  auto cleanup = [storedConn, errorConn]() {
    QObject::disconnect(*storedConn);
    QObject::disconnect(*errorConn);
    delete storedConn;
    delete errorConn;
  };

  *storedConn = connect(
      m_credentialsStore, &SyncCredentialsStore::credentialsStored, this,
      [this, notebookId, configJson, credsJson, cleanup](const QString &p_storedNotebookId) {
        if (p_storedNotebookId != notebookId) {
          return;
        }
        cleanup();
        QMetaObject::invokeMethod(m_worker, "enableSync", Qt::QueuedConnection,
                                  Q_ARG(QString, notebookId), Q_ARG(QString, configJson),
                                  Q_ARG(QString, credsJson));
      });

  *errorConn =
      connect(m_credentialsStore, &SyncCredentialsStore::credentialsError, this,
              [this, notebookId, cleanup](const QString &p_errNotebookId, const QString &p_errMsg) {
                if (p_errNotebookId != notebookId) {
                  return;
                }
                cleanup();
                qWarning() << "SyncService::enableSyncForNotebook: keychain store failed:"
                           << p_errMsg;
                emit enableFinished(notebookId, VXCORE_ERR_UNKNOWN, p_errMsg);
              });

  m_credentialsStore->storeCredentials(notebookId, p_pat);
}

void SyncService::disableSyncForNotebook(const QString &p_notebookId) {
  if (m_shutDown) {
    qWarning() << "SyncService::disableSyncForNotebook: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::disableSyncForNotebook: notebookId:" << p_notebookId;

  const QString notebookId = p_notebookId;

  // After the worker reports disableFinished for THIS notebook:
  //   1. If vxcore disable_sync succeeded (VXCORE_OK), clear the on-disk JSON
  //      sync fields (syncEnabled / syncBackend / syncRemoteUrl). vxcore's
  //      DisableSync only clears in-memory maps (W1.T2 evidence) — without
  //      this Qt-side reset the notebook would still look sync-enabled on next
  //      app launch and reconcileSyncForNotebook would resurrect it (B8).
  //      Failure to update the JSON is logged but does NOT block the keychain
  //      cleanup (best-effort to avoid orphan PAT in S6).
  //   2. If vxcore disable_sync failed, PRESERVE the JSON fields so the user
  //      can retry without losing config state.
  //   3. Always delete the keychain entry afterwards.
  // One-shot connection.
  auto *conn = new QMetaObject::Connection;
  *conn = connect(
      this, &SyncService::disableFinished, this,
      [this, notebookId, conn](const QString &p_finishedNotebookId, VxCoreError p_result) {
        if (p_finishedNotebookId != notebookId) {
          return;
        }
        QObject::disconnect(*conn);
        delete conn;

        // W2.T5: clear JSON sync fields only on disable success.
        if (p_result == VXCORE_OK && m_notebookCoreService) {
          QJsonObject cfg = m_notebookCoreService->getNotebookConfig(notebookId);
          cfg.remove(QStringLiteral("syncEnabled"));
          cfg.remove(QStringLiteral("syncBackend"));
          cfg.remove(QStringLiteral("syncRemoteUrl"));
          const QString newJson =
              QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
          const bool ok = m_notebookCoreService->updateNotebookConfig(notebookId, newJson);
          if (!ok) {
            qCWarning(syncCategory)
                << "SyncService::disableSyncForNotebook: failed to clear JSON sync fields for"
                << "notebookId:" << notebookId
                << "(continuing with keychain cleanup as best-effort)";
          }
        }

        m_credentialsStore->deleteCredentials(notebookId);

        // F4.5: fire vnote.sync.after_disable on success only. Fired AFTER the
        // JSON sync fields are cleared and AFTER scheduling keychain delete,
        // outside any SyncService mutex. Observe-only.
        if (p_result == VXCORE_OK) {
          if (auto *hookMgr = m_services.get<HookManager>()) {
            QVariantMap args;
            args[QStringLiteral("notebookId")] = notebookId;
            hookMgr->doAction(HookNames::SyncAfterDisable, args);
          }
        }
      });

  QMetaObject::invokeMethod(m_worker, "disableSync", Qt::QueuedConnection,
                            Q_ARG(QString, notebookId));
}

void SyncService::triggerSyncNow(const QString &p_notebookId) {
  if (m_shutDown) {
    qWarning() << "SyncService::triggerSyncNow: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::triggerSyncNow: notebookId:" << p_notebookId;

  // Wave 12.2 / F5.9: create a per-call cancellation token. Owned by
  // SyncService until onWorkerSyncFinished arrives on the GUI thread.
  // If an old token is still registered (shouldn't normally happen — would
  // mean a previous syncFinished was lost), free it first to avoid leaks.
  VxCoreSyncCancellation *token = vxcore_sync_create_cancellation();
  if (token) {
    QMutexLocker locker(&m_cancellationMutex);
    auto it = m_cancellations.find(p_notebookId);
    if (it != m_cancellations.end() && it.value() != nullptr) {
      qCWarning(syncCategory) << "SyncService::triggerSyncNow: stale cancellation token for"
                              << p_notebookId << "freeing before replacement";
      vxcore_sync_free_cancellation(static_cast<VxCoreSyncCancellation *>(it.value()));
    }
    m_cancellations.insert(p_notebookId, token);
  }

  QMetaObject::invokeMethod(m_worker, "triggerSyncCancellable", Qt::QueuedConnection,
                            Q_ARG(QString, p_notebookId),
                            Q_ARG(quintptr, reinterpret_cast<quintptr>(token)));
}

void SyncService::cancelSync(const QString &p_notebookId) {
  if (m_shutDown) {
    qWarning() << "SyncService::cancelSync: ignored after shutdown";
    return;
  }
  VxCoreSyncCancellation *token = nullptr;
  {
    QMutexLocker locker(&m_cancellationMutex);
    auto it = m_cancellations.find(p_notebookId);
    if (it != m_cancellations.end()) {
      token = static_cast<VxCoreSyncCancellation *>(it.value());
    }
  }
  if (!token) {
    qCDebug(syncCategory) << "SyncService::cancelSync: no active sync for" << p_notebookId;
    // F4.5: still fire vnote.sync.cancelled best-effort so observers can
    // record cancel intent even if no in-flight op was found.
    if (auto *hookMgr = m_services.get<HookManager>()) {
      QVariantMap args;
      args[QStringLiteral("notebookId")] = p_notebookId;
      args[QStringLiteral("hadActiveSync")] = false;
      hookMgr->doAction(HookNames::SyncCancelled, args);
    }
    return;
  }
  qCDebug(syncCategory) << "SyncService::cancelSync: signalling token for" << p_notebookId;
  // vxcore_sync_cancel is thread-safe (atomic flag inside the token).
  vxcore_sync_cancel(token);

  // F4.5: fire vnote.sync.cancelled AFTER vxcore_sync_cancel returns. No
  // SyncService mutex is held here (snapshot/release done above). Observe-only.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("hadActiveSync")] = true;
    hookMgr->doAction(HookNames::SyncCancelled, args);
  }
}

void SyncService::updateCredentials(const QString &p_notebookId, const QString &p_newPat) {
  if (m_shutDown) {
    qWarning() << "SyncService::updateCredentials: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::updateCredentials: notebookId:" << p_notebookId;

  // F4 chicken-and-egg fix: if the notebook is NOT registered in vxcore's
  // runtime states_ map but its on-disk config says sync is enabled, the
  // worker's setCredentials path would fail with VXCORE_ERR_SYNC_NOT_ENABLED
  // because vxcore_sync_set_credentials requires a states_ entry. Route to
  // the full enableSyncForNotebook flow (which registers the notebook AND
  // applies credentials atomically), then re-emit its enableFinished as
  // credentialsSetFinished so the public API contract is preserved for
  // callers that only listen for credentialsSetFinished.
  if (!isSyncRegistered(p_notebookId) && isSyncEnabled(p_notebookId)) {
    qCDebug(syncCategory) << "SyncService::updateCredentials routing to enableSyncForNotebook "
                             "(notebook unregistered) id="
                          << p_notebookId;

    const QJsonObject cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
    const QString persistedUrl = cfg.value(QStringLiteral("syncRemoteUrl")).toString();
    if (persistedUrl.isEmpty()) {
      qCWarning(syncCategory)
          << "SyncService::updateCredentials: cannot route to enableSyncForNotebook; "
             "syncRemoteUrl is empty in notebook config for"
          << p_notebookId;
      emit credentialsSetFinished(p_notebookId, VXCORE_ERR_INVALID_PARAM);
      return;
    }

    // One-shot bridge: enableFinished -> credentialsSetFinished. Filter by
    // notebookId because enableFinished is a shared signal. The lambda
    // self-disconnects after firing for this notebook to avoid leaking
    // connections across multiple updateCredentials calls.
    const QString routedNotebookId = p_notebookId;
    auto *bridgeConn = new QMetaObject::Connection;
    *bridgeConn =
        connect(this, &SyncService::enableFinished, this,
                [this, routedNotebookId, bridgeConn](const QString &p_finishedNotebookId,
                                                     VxCoreError p_result, const QString &) {
                  if (p_finishedNotebookId != routedNotebookId) {
                    return;
                  }
                  QObject::disconnect(*bridgeConn);
                  delete bridgeConn;
                  emit credentialsSetFinished(routedNotebookId, p_result);
                });

    // Connect FIRST, then invoke. enableSyncForNotebook is async (keychain
    // store -> worker dispatch via QueuedConnection) so there's no race, but
    // we follow the convention used elsewhere in this file.
    enableSyncForNotebook(p_notebookId, persistedUrl, p_newPat);
    return;
  }

  const QString credsJson = buildCredentialsJson(p_newPat);
  const QString notebookId = p_notebookId;

  auto *storedConn = new QMetaObject::Connection;
  auto *errorConn = new QMetaObject::Connection;

  auto cleanup = [storedConn, errorConn]() {
    QObject::disconnect(*storedConn);
    QObject::disconnect(*errorConn);
    delete storedConn;
    delete errorConn;
  };

  *storedConn =
      connect(m_credentialsStore, &SyncCredentialsStore::credentialsStored, this,
              [this, notebookId, credsJson, cleanup](const QString &p_storedNotebookId) {
                if (p_storedNotebookId != notebookId) {
                  return;
                }
                cleanup();
                QMetaObject::invokeMethod(m_worker, "setCredentials", Qt::QueuedConnection,
                                          Q_ARG(QString, notebookId), Q_ARG(QString, credsJson));
              });

  *errorConn =
      connect(m_credentialsStore, &SyncCredentialsStore::credentialsError, this,
              [this, notebookId, cleanup](const QString &p_errNotebookId, const QString &p_errMsg) {
                if (p_errNotebookId != notebookId) {
                  return;
                }
                cleanup();
                qWarning() << "SyncService::updateCredentials: keychain store failed:" << p_errMsg;
                emit credentialsSetFinished(notebookId, VXCORE_ERR_UNKNOWN);
              });

  m_credentialsStore->storeCredentials(notebookId, p_newPat);
}

void SyncService::resolveConflicts(const QString &p_notebookId,
                                   const QHash<QString, QString> &p_resolutions) {
  if (m_shutDown) {
    qWarning() << "SyncService::resolveConflicts: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::resolveConflicts: notebookId:" << p_notebookId
                        << "count:" << p_resolutions.size();

  for (auto it = p_resolutions.constBegin(); it != p_resolutions.constEnd(); ++it) {
    QMetaObject::invokeMethod(m_worker, "resolveConflict", Qt::QueuedConnection,
                              Q_ARG(QString, p_notebookId), Q_ARG(QString, it.key()),
                              Q_ARG(QString, it.value()));
  }

  // After all resolutions are queued, follow with a triggerSync to push the
  // resolved state. The QueuedConnection ordering guarantees triggerSync runs
  // AFTER the resolveConflict slots above (FIFO event queue).
  QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                            Q_ARG(QString, p_notebookId));
}

bool SyncService::isSyncInProgress(const QString &p_notebookId) const {
  QMutexLocker locker(&m_inProgressMutex);
  return m_inProgress.value(p_notebookId, false);
}

bool SyncService::isSyncEnabled(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return false;
  }
  const QJsonObject cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  const bool enabled = cfg.value(QStringLiteral("syncEnabled")).toBool();
  qCDebug(syncCategory) << "SyncService::isSyncEnabled: query notebookId:" << p_notebookId
                        << "syncEnabled:" << enabled;
  return enabled;
}

bool SyncService::isSyncReady(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return false;
  }
  const bool ready = m_notebookCoreService->isSyncReady(p_notebookId);
  qCDebug(syncCategory) << "SyncService::isSyncReady: query notebookId:" << p_notebookId
                        << "syncReady:" << ready;
  return ready;
}

bool SyncService::isSyncRegistered(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return false;
  }
  // Query vxcore's runtime sync state via getSyncStatus wrapper.
  // Returns VXCORE_OK iff states_[notebookId] exists (notebook is registered).
  QString throwaway;
  const VxCoreError err = m_notebookCoreService->getSyncStatus(p_notebookId, throwaway);
  const bool registered = (err == VXCORE_OK);
  qCDebug(syncCategory) << "SyncService::isSyncRegistered: query notebookId:" << p_notebookId
                        << "registered:" << registered;
  return registered;
}

QString SyncService::lastSyncTime(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return QString();
  }
  // Per-device timestamp from metadata.db (NOT NotebookConfig JSON).
  const qint64 millis = m_notebookCoreService->getLastSyncUtc(p_notebookId);
  if (millis <= 0) {
    return QString();
  }
  return QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(millis), QLocale::ShortFormat);
}

void SyncService::testSetInProgress(const QString &p_notebookId, bool p_value) {
  setInProgress(p_notebookId, p_value);
}

void SyncService::setInProgress(const QString &p_notebookId, bool p_value) {
  QMutexLocker locker(&m_inProgressMutex);
  if (p_value) {
    m_inProgress.insert(p_notebookId, true);
  } else {
    m_inProgress.remove(p_notebookId);
  }
}

// ---- Worker -> SyncService forwarders --------------------------------------

void SyncService::onWorkerSyncStarted(const QString &p_notebookId) {
  setInProgress(p_notebookId, true);
  emit syncStarted(p_notebookId);
}

void SyncService::onWorkerSyncFinished(const QString &p_notebookId, VxCoreError p_result) {
  qCDebug(syncCategory) << "SyncService::onWorkerSyncFinished: notebookId:" << p_notebookId
                        << "result:" << static_cast<int>(p_result)
                        << "message:" << vxErrorToString(p_result);
  setInProgress(p_notebookId, false);

  // Wave 12.2 / F5.9: release the cancellation token (if any). Snapshot
  // under the mutex, then free outside — rule W0.5.
  VxCoreSyncCancellation *token = nullptr;
  {
    QMutexLocker locker(&m_cancellationMutex);
    auto it = m_cancellations.find(p_notebookId);
    if (it != m_cancellations.end()) {
      token = static_cast<VxCoreSyncCancellation *>(it.value());
      m_cancellations.erase(it);
    }
  }
  if (token) {
    vxcore_sync_free_cancellation(token);
  }

  emit syncFinished(p_notebookId, p_result);
}

void SyncService::onWorkerSyncFailed(const QString &p_notebookId, VxCoreError p_code,
                                     const QString &p_message) {
  emit syncFailed(p_notebookId, p_code, p_message);
}

void SyncService::onWorkerConflictsDetected(const QString &p_notebookId,
                                            const QStringList &p_conflictFiles) {
  // F4.5: fire vnote.sync.conflict_detected on the GUI thread (this slot is
  // wired to the worker signal via Qt::QueuedConnection, so we are post-bounce
  // and hold no SyncManager / worker locks). Observe-only.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("conflictCount")] = p_conflictFiles.size();
    hookMgr->doAction(HookNames::SyncConflictDetected, args);
  }
  emit conflictsDetected(p_notebookId, p_conflictFiles);
}

void SyncService::onWorkerEnableFinished(const QString &p_notebookId, VxCoreError p_result,
                                         const QString &p_message) {
  qCDebug(syncCategory) << "SyncService::onWorkerEnableFinished: notebookId:" << p_notebookId
                        << "result:" << vxErrorToString(p_result);
  emit enableFinished(p_notebookId, p_result, p_message);
}

void SyncService::onWorkerDisableFinished(const QString &p_notebookId, VxCoreError p_result) {
  emit disableFinished(p_notebookId, p_result);
}

void SyncService::onWorkerCredentialsSetFinished(const QString &p_notebookId,
                                                 VxCoreError p_result) {
  emit credentialsSetFinished(p_notebookId, p_result);
}

// ---- Auto-sync event forwarders (from EventBridge) --------------------------

void SyncService::onAutoSyncStarted(const QString &p_notebookId) {
  if (m_shutDown)
    return;
  setInProgress(p_notebookId, true);
  emit syncStarted(p_notebookId);
}

void SyncService::onAutoSyncFinished(const QString &p_notebookId, VxCoreError p_result) {
  if (m_shutDown)
    return;
  setInProgress(p_notebookId, false);
  if (p_result != VXCORE_OK) {
    emit syncFailed(p_notebookId, p_result, vxErrorToString(p_result));
  }
  emit syncFinished(p_notebookId, p_result);
}

void SyncService::onAutoSyncConflict(const QString &p_notebookId) {
  if (m_shutDown)
    return;
  // F4.5: fire vnote.sync.conflict_detected for auto-sync-originated
  // conflicts as well. Conflict file count is not known until getConflicts
  // returns; observers receive -1 to signal "count unknown at fire time".
  if (auto *hookMgr = m_services.get<HookManager>()) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("conflictCount")] = -1;
    hookMgr->doAction(HookNames::SyncConflictDetected, args);
  }
  QMetaObject::invokeMethod(m_worker, "getConflicts", Qt::QueuedConnection,
                            Q_ARG(QString, p_notebookId));
}

// ---- Reconcile-on-open ------------------------------------------------------

void SyncService::onNotebookAfterOpen(const NotebookOpenEvent &p_event) {
  qCDebug(syncCategory) << "SyncService::onNotebookAfterOpen: notebookId:" << p_event.notebookId;
  reconcileSyncForNotebook(p_event.notebookId);
}

void SyncService::onMainWindowAfterStart() {
  if (!m_notebookCoreService) {
    return;
  }
  const QJsonArray notebooks = m_notebookCoreService->listNotebooks();
  qCDebug(syncCategory) << "SyncService::onMainWindowAfterStart: scanning" << notebooks.size()
                        << "notebooks for reconcile";
  for (const QJsonValue &v : notebooks) {
    const QJsonObject nb = v.toObject();
    const QString nbId = nb.value(QStringLiteral("id")).toString();
    if (nbId.isEmpty()) {
      continue;
    }

    // W2.T5/S6: Orphan PAT cleanup. Legacy disable paths (prior to W2.T5) did
    // not consistently clear keychain credentials, leaving an orphan PAT for
    // notebooks whose JSON now says sync is disabled. Sweep here at app start
    // so the keychain stays in sync with disk truth.
    if (m_credentialsStore && !isSyncEnabled(nbId) && m_credentialsStore->hasCredentials(nbId)) {
      qCDebug(syncCategory)
          << "SyncService::onMainWindowAfterStart: S6 orphan PAT cleanup for notebookId=" << nbId;
      m_credentialsStore->deleteCredentials(nbId);
    }

    reconcileSyncForNotebook(nbId);
  }
}

void SyncService::reconcileSyncForNotebook(const QString &p_notebookId) {
  qCDebug(syncCategory) << "SyncService::reconcileSyncForNotebook: ENTRY notebookId:"
                        << p_notebookId << "shutDown:" << m_shutDown
                        << "hasNotebookCoreService:" << (m_notebookCoreService != nullptr)
                        << "hasCredentialsStore:" << (m_credentialsStore != nullptr);
  if (m_shutDown || !m_notebookCoreService || !m_credentialsStore) {
    return;
  }

  const QJsonObject cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  const bool diskEnabled = cfg.value(QStringLiteral("syncEnabled")).toBool();
  if (!diskEnabled) {
    qCDebug(syncCategory) << "SyncService::reconcileSyncForNotebook: skip - disk says not enabled"
                          << p_notebookId;
    return;
  }

  if (m_reconcileAttempted.contains(p_notebookId)) {
    qCDebug(syncCategory)
        << "SyncService::reconcileSyncForNotebook: already attempted in this process"
        << p_notebookId;
    return;
  }

  const QString backend = cfg.value(QStringLiteral("syncBackend")).toString();
  const QString remoteUrl = cfg.value(QStringLiteral("syncRemoteUrl")).toString();
  if (backend.isEmpty() || remoteUrl.isEmpty()) {
    qCWarning(syncCategory) << "SyncService::reconcileSyncForNotebook: incomplete config"
                            << "notebookId:" << p_notebookId << "backend:" << backend
                            << "remoteUrl:" << remoteUrl;
    emit reconcileFinished(p_notebookId, VXCORE_ERR_INVALID_PARAM);
    return;
  }

  // Mark as attempted AFTER precondition checks pass, so transient failures can retry
  m_reconcileAttempted.insert(p_notebookId);
  qCDebug(syncCategory) << "SyncService::reconcileSyncForNotebook: reconcile attempted set inserted"
                        << "notebookId:" << p_notebookId;

  qCDebug(syncCategory) << "SyncService::reconcileSyncForNotebook: requesting PAT"
                        << "notebookId:" << p_notebookId;

  auto connOk = std::make_shared<QMetaObject::Connection>();
  auto connErr = std::make_shared<QMetaObject::Connection>();

  *connOk =
      connect(m_credentialsStore, &SyncCredentialsStore::credentialsRetrieved, this,
              [this, p_notebookId, connOk, connErr, backend, remoteUrl](const QString &p_evNbId,
                                                                        const QString &p_pat) {
                if (p_evNbId != p_notebookId) {
                  return;
                }
                QObject::disconnect(*connOk);
                QObject::disconnect(*connErr);

                qCDebug(syncCategory)
                    << "SyncService::reconcileSyncForNotebook: got PAT, dispatching enableSync"
                    << "notebookId:" << p_notebookId;

                QJsonObject cfgObj;
                cfgObj.insert(QStringLiteral("backend"), backend);
                cfgObj.insert(QStringLiteral("remoteUrl"), remoteUrl);
                const QString configJson =
                    QString::fromUtf8(QJsonDocument(cfgObj).toJson(QJsonDocument::Compact));

                QJsonObject credsObj;
                credsObj.insert(QStringLiteral("pat"), p_pat);
                const QString credentialsJson =
                    QString::fromUtf8(QJsonDocument(credsObj).toJson(QJsonDocument::Compact));

                QMetaObject::invokeMethod(m_worker, "enableSync", Qt::QueuedConnection,
                                          Q_ARG(QString, p_notebookId), Q_ARG(QString, configJson),
                                          Q_ARG(QString, credentialsJson));
                emit reconcileFinished(p_notebookId, VXCORE_OK);
              });

  *connErr =
      connect(
          m_credentialsStore, &SyncCredentialsStore::credentialsError, this,
          [this, p_notebookId, connOk, connErr](const QString &p_evNbId, const QString &p_errMsg) {
            if (p_evNbId != p_notebookId) {
              return;
            }
            QObject::disconnect(*connOk);
            QObject::disconnect(*connErr);

            // Clear the "attempted" marker so a retry (e.g., after user re-enters PAT) can proceed
            m_reconcileAttempted.remove(p_notebookId);
            qCDebug(syncCategory) << "SyncService::reconcileSyncForNotebook: reconcile attempted "
                                     "set removed on PAT fetch failure"
                                  << "notebookId:" << p_notebookId;

            qCWarning(syncCategory)
                << "SyncService::reconcileSyncForNotebook: PAT fetch failed"
                << "notebookId:" << p_notebookId << "error:" << p_errMsg
                << "-> leaving notebook usable; Sync Now will fail until user re-enters PAT";
            emit reconcileFinished(p_notebookId, VXCORE_ERR_SYNC_AUTH_FAILED);
          });

  m_credentialsStore->retrieveCredentials(p_notebookId);
}

void SyncService::ensureSyncEnabled(const QString &p_notebookId) {
  qCDebug(syncCategory) << "SyncService::ensureSyncEnabled: notebookId:" << p_notebookId;
  if (m_shutDown) {
    return;
  }
  // Clear any prior "already-attempted" marker so reconcileSyncForNotebook
  // will actually run its checks and (if config is now complete) dispatch
  // enableSync. Without this, a partial notebook that reconcile bailed on
  // at startup would never get re-attempted within the same session.
  m_reconcileAttempted.remove(p_notebookId);
  reconcileSyncForNotebook(p_notebookId);
}
