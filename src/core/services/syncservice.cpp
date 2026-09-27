#include "syncservice.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include <atomic>
#include <memory>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/eventbridge.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncerrorpresenter.h>
#include <core/services/synclog.h>
#include <core/services/syncops.h>
#include <core/services/syncworkqueuemanager.h>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

#include "sync/sync_json_keys.h"

Q_LOGGING_CATEGORY(syncCategory, "vnote.sync")

using namespace vnotex;

namespace {

// Bounded wait for the per-notebook work queue to drain on shutdown before
// we proceed with destruction. 30s comfortably accommodates a slow git push.
static constexpr int kShutdownTimeoutMs = 30000;

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

struct SyncService::SyncOperation {
  enum class Phase {
    Network,
    Requested,
    Reserving,
    Draining,
    Ready,
    Applying,
    Applied,
    Retiring,
    Retired
  };
  QString notebookId;
  quint64 generation = 0;
  std::unique_ptr<VxCoreSyncCancellation, decltype(&vxcore_sync_free_cancellation)> token{
      vxcore_sync_create_cancellation(), vxcore_sync_free_cancellation};
  QHash<QString, QString> resolutions;
  std::atomic_bool cancelled{false};
  std::atomic_bool stopping{false};
  QMutex mutex;
  QWaitCondition changed;
  Phase phase = Phase::Network;
  VxCoreError result = VXCORE_OK;
  QString error;
  QStringList protectedPaths;
  bool deferred = false;
  QStringList changedPaths;
  // GUI-owned only.
  bool buffersReserved = false;
  bool coreReserved = false;
  QPointer<QTimer> timer;
  QElapsedTimer drainTime;
};

SyncService::SyncService(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  m_notebookCoreService = m_services.get<NotebookCoreService>();
  m_credentialsStore = m_services.get<SyncCredentialsStore>();
  Q_ASSERT(m_notebookCoreService);
  Q_ASSERT(m_credentialsStore);

  // T20: resolve the per-notebook serialized executor. Production main.cpp
  // registers one with bounded shutdown on QCoreApplication::aboutToQuit;
  // tests typically do not. Own one locally in the fallback so the queued
  // enable / disable / bootstrap paths still execute. shutdown() below
  // drains the owned instance.
  m_workQueue = m_services.get<SyncWorkQueueManager>();
  if (!m_workQueue) {
    m_ownedWorkQueue.reset(new SyncWorkQueueManager());
    m_workQueue = m_ownedWorkQueue.get();
  }

  // T24: SyncWorker QObject + private QThread are gone. All async dispatch
  // now flows through SyncWorkQueueManager + SyncOps. Enable / disable /
  // setCredentials completion callbacks bounce back to the GUI thread via
  // QMetaObject::invokeMethod(this, ..., QueuedConnection) and land in the
  // onWorkerXxxFinished slots (kept as the bounce target so the public
  // enableFinished / disableFinished / credentialsSetFinished signal contracts
  // are preserved).

  // Wire EventBridge for all vxcore sync lifecycle events (already on GUI
  // thread; EventBridge delivers via QueuedConnection from its vxcore callback).
  m_eventBridge = m_services.get<EventBridge>();
  if (m_eventBridge) {
    connect(m_eventBridge, &EventBridge::syncStarted, this, &SyncService::onSyncStarted);
    connect(m_eventBridge, &EventBridge::syncFinished, this, &SyncService::onSyncFinished);
    connect(m_eventBridge, &EventBridge::syncConflictFiles, this,
            &SyncService::onSyncConflictFiles);
    // T31: close the auto-sync loop. vxcore SyncManager::MaybeEnqueueSync emits
    // sync.should_run when a buffered file event passes the debounce gate;
    // EventBridge translates that to syncShouldRun(QString). Without this
    // connection, auto-sync silently dies after the event hop.
    connect(m_eventBridge, &EventBridge::syncShouldRun, this, &SyncService::onSyncShouldRun,
            Qt::QueuedConnection);
  }

  // T17: subscribe to NotebookBeforeClose. Refuse the close while a sync is
  // in progress for the same notebook. Per ADR-9 we use HookContext::cancel()
  // (no-arg) and stash a user-visible reason via setMetadata("syncCancelReason", ...).
  // That metadata is the LIVE production channel: NotebookCoreService::closeNotebook
  // copies it out of the HookContext into its p_errorMessage out-param, and
  // ManageNotebooksController surfaces it as the dialog's information banner.
  // Do NOT pop a QMessageBox from here - core_services is deliberately
  // Qt-Widgets-free (see src/core/services/CMakeLists.txt).
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    hookMgr->addAction<NotebookCloseEvent>(
        HookNames::NotebookBeforeClose,
        [this](HookContext &p_ctx, const NotebookCloseEvent &p_event) {
          // T27: refuse close when sync is in progress OR when sync work is
          // queued-but-not-yet-running. Auto-flush is NOT performed — the user
          // must explicitly cancel via the sync UI to drop pending items.
          const bool inProgress = isSyncInProgress(p_event.notebookId);
          int pendingCount = 0;
          if (m_workQueue) {
            const auto snap = m_workQueue->inFlightState(p_event.notebookId);
            // hasPending is true while running OR pending items exist. Derive
            // pure pending count from queueDepth so the payload distinguishes
            // running-only (pending=0) from queued (pending>0).
            pendingCount = m_workQueue->queueDepth(p_event.notebookId);
            (void)snap;
          }
          // Expose the snapshot to downstream subscribers regardless of decision.
          p_ctx.setMetadata(QStringLiteral("pendingCount"), pendingCount);
          if (!inProgress && pendingCount == 0) {
            return;
          }
          p_ctx.cancel();
          const QString reason =
              inProgress ? tr("Sync is in progress for this notebook. Please wait for sync to "
                              "complete before closing.")
                         : tr("Sync work is queued for this notebook (%1 item(s)). Cancel the "
                              "queued sync from the toolbar before closing.")
                               .arg(pendingCount);
          // First non-empty reason wins: all handlers for a hook share one
          // context, so an earlier (lower-priority-number) handler's reason is
          // not clobbered here.
          if (p_ctx.getMetadata(QStringLiteral("syncCancelReason")).toString().isEmpty()) {
            p_ctx.setMetadata(QStringLiteral("syncCancelReason"), reason);
          }
        },
        /*priority=*/10);

    hookMgr->addAction<NotebookOpenEvent>(
        HookNames::NotebookAfterOpen,
        [this](HookContext &, const NotebookOpenEvent &p_event) { onNotebookAfterOpen(p_event); },
        /*priority=*/10);

    // T2 (fix-qtkeychain-win32-error-8): wipe any stored PAT when a notebook
    // is removed from VNote. NotebookAfterClose fires only on VXCORE_OK, i.e.
    // the notebook has truly left listNotebooks() and the startup S6 sweep
    // can no longer reach it. Centralizing here covers every current and
    // future caller of NotebookCoreService::closeNotebook (ManageNotebooks
    // close, NewNotebook rollback, VNote3 migration, etc.). deleteCredentials
    // is idempotent so notebooks that never had sync enabled are a no-op.
    //
    // T-fix-pack-handle-leak: ALSO release the vxcore sync runtime so
    // libgit2's git_repository* (and the mmapped .pack files it owns on
    // Windows) is freed immediately. Without this the user cannot delete
    // the notebook folder from Windows Explorer until vnote.exe exits.
    // Order matters: release runtime FIRST while the notebookId is still
    // meaningful and before the keychain delete races; deleteCredentials
    // runs second so any future caller's expectations about PAT cleanup
    // are unchanged. NotebookBeforeClose already refuses close while a
    // sync is in flight (syncservice.cpp:128 handler), so by the time we
    // get here the sync worker is guaranteed not to be touching the
    // backend pointer we're about to destroy.
    hookMgr->addAction<NotebookCloseEvent>(
        HookNames::NotebookAfterClose,
        [this](HookContext &, const NotebookCloseEvent &p_event) {
          if (p_event.notebookId.isEmpty()) {
            return;
          }
          const auto retire = [this, id = p_event.notebookId]() {
            dropDebounceTimer(id);
            unregisterSyncRuntime(id);
            if (m_credentialsStore)
              deleteStoredCredentials(id, {});
          };
          if (QThread::currentThread() == thread())
            retire();
          else
            QMetaObject::invokeMethod(this, retire, Qt::QueuedConnection);
        },
        /*priority=*/10);

    hookMgr->addAction(
        HookNames::MainWindowAfterStart,
        [this](HookContext &, const QVariantMap &) { onMainWindowAfterStart(); },
        /*priority=*/10);
  }
}

void SyncService::shutdown() {
  if (m_shutDown)
    return;
  m_shutDown = true;
  for (QTimer *timer : qAsConst(m_debounceTimers)) {
    timer->stop();
    timer->deleteLater();
  }
  m_debounceTimers.clear();
  const auto operations = m_syncOperations;
  // Wake every worker before joining: the GUI event loop need not run again.
  for (const auto &operation : operations)
    cancelSyncOperation(operation, true);
  if (m_workQueue && !m_workQueue->shutdown(kShutdownTimeoutMs)) {
    qCWarning(syncCategory) << "Waiting for cancelled sync workers before releasing services";
    // Workers hold service/context pointers. Never free them after a timed-out join.
    m_workQueue->shutdown(-1);
  }
  for (const auto &operation : operations)
    finishSyncApply(operation);
  m_syncOperations.clear();
}

SyncService::~SyncService() {
  // Idempotent: if shutdown() was already called via aboutToQuit, this is a
  // no-op. Otherwise it performs a bounded quit/wait/terminate.
  shutdown();
  qDeleteAll(m_debounceTimers);
  m_debounceTimers.clear();
}

QString SyncService::buildConfigJson(const QString &p_notebookId,
                                     const SyncSettings &p_settings) const {
  QJsonObject obj;
  obj[QLatin1String(vxcore::kJsonKeyBackend)] = p_settings.m_backend;
  obj[QLatin1String(vxcore::kJsonKeyRemoteUrl)] = canonicalSyncRemoteUrl(p_settings);
  const auto cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  obj[QLatin1String(vxcore::kJsonKeyAutoSyncEnabled)] =
      cfg.value(QLatin1String(vxcore::kJsonKeyAutoSyncEnabled)).toBool(true);
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool SyncService::hasInterruptedRetirement(const QString &p_notebookId) const {
  const auto path = m_notebookCoreService->buildAbsolutePath(
      p_notebookId, QStringLiteral("vx_notebook/vx_sync/webdav/retirement.json"));
  if (path.isEmpty())
    return false;
  const QFileInfo info(path);
  return info.exists() || info.isSymLink();
}

void SyncService::enableSyncForNotebook(const QString &p_notebookId,
                                        const SyncSettings &p_settings) {
  if (m_shutDown) {
    qWarning() << "SyncService::enableSyncForNotebook: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::enableSyncForNotebook: notebookId:" << p_notebookId;

  const auto validationError = validateSyncSettings(p_settings, true);
  if (!validationError.isEmpty()) {
    const auto code = isSupportedSyncBackend(p_settings.m_backend) ? VXCORE_ERR_INVALID_PARAM
                                                                   : VXCORE_ERR_UNKNOWN_BACKEND;
    QMetaObject::invokeMethod(
        this,
        [this, p_notebookId, validationError, code]() {
          emit enableFinished(p_notebookId, code, validationError);
        },
        Qt::QueuedConnection);
    return;
  }
  if (hasInterruptedRetirement(p_notebookId)) {
    QMetaObject::invokeMethod(
        this,
        [this, p_notebookId]() {
          emit enableFinished(
              p_notebookId, VXCORE_ERR_INVALID_STATE,
              tr("Open Sync Info to restore the interrupted sync configuration change."));
        },
        Qt::QueuedConnection);
    return;
  }
  if (m_deletingCredentials.contains(p_notebookId)) {
    // Clone closes its staging notebook before reopening the same UUID at the
    // final root. Let that close's vault delete finish before storing anew.
    auto deleted = std::make_shared<QMetaObject::Connection>();
    auto failed = std::make_shared<QMetaObject::Connection>();
    const auto resume = [this, p_notebookId, p_settings, deleted, failed]() {
      QObject::disconnect(*deleted);
      QObject::disconnect(*failed);
      enableSyncForNotebook(p_notebookId, p_settings);
    };
    *deleted = connect(m_credentialsStore, &SyncCredentialsStore::credentialsDeleted, this,
                       [p_notebookId, resume](const QString &id) {
                         if (id == p_notebookId)
                           resume();
                       });
    *failed = connect(m_credentialsStore, &SyncCredentialsStore::credentialsError, this,
                      [p_notebookId, resume](const QString &id, const QString &) {
                        if (id == p_notebookId)
                          resume();
                      });
    return;
  }
  if (m_credentialOperations.contains(p_notebookId) ||
      (isSyncRegistered(p_notebookId) && isSyncInProgress(p_notebookId))) {
    QMetaObject::invokeMethod(
        this,
        [this, p_notebookId]() {
          emit enableFinished(p_notebookId, VXCORE_ERR_SYNC_IN_PROGRESS,
                              tr("Sync is in progress. Try again when it finishes."));
        },
        Qt::QueuedConnection);
    return;
  }
  m_credentialOperations.insert(p_notebookId);

  // F4.5: fire vnote.sync.before_enable BEFORE any lock acquisition or worker
  // dispatch. Observe-only: HookContext::cancel() is intentionally ignored —
  // sync hooks may NOT abort the underlying op (per plan F4.5). PAT is NEVER
  // included in the payload.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    QVariantMap args;
    args[QLatin1String(vxcore::kJsonKeyNotebookId)] = p_notebookId;
    args[QLatin1String(vxcore::kJsonKeyRemoteUrl)] = canonicalSyncRemoteUrl(p_settings);
    hookMgr->doAction(HookNames::SyncBeforeEnable, args);
  }

  const QString configJson = buildConfigJson(p_notebookId, p_settings);
  // Secrets remain only in the store job and these transient operation captures.
  const QString credsJson = syncCredentialsJson(p_settings.m_credentials);
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

  *storedConn =
      connect(
          m_credentialsStore, &SyncCredentialsStore::credentialsStored, this,
          [this, notebookId, configJson, credsJson, cleanup](const QString &p_storedNotebookId) {
            if (p_storedNotebookId != notebookId) {
              return;
            }
            cleanup();
            if (m_shutDown) {
              m_credentialOperations.remove(notebookId);
              return;
            }
            // T20: route enable through SyncWorkQueueManager (per-notebook FIFO)
            // instead of the legacy SyncWorker QMetaObject::invokeMethod path.
            // SyncOps::enableSync invokes the completion callback on the pool
            // thread; we bounce back to the GUI thread via QueuedConnection so
            // onWorkerEnableFinished (and thus enableFinished signal) is emitted
            // on the GUI thread, preserving the public contract. The PAT lives
            // ONLY in this lambda capture and the inner enqueue capture; it is
            // released once SyncOps::enableSync returns.
            auto *workQueue = m_workQueue;
            NotebookCoreService *notebookSvc = m_notebookCoreService;
            if (!workQueue) {
              qCWarning(syncCategory)
                  << "SyncService::enableSyncForNotebook: SyncWorkQueueManager unavailable for"
                  << notebookId;
              QMetaObject::invokeMethod(
                  this,
                  [this, notebookId]() {
                    onWorkerEnableFinished(notebookId, VXCORE_ERR_UNKNOWN,
                                           QStringLiteral("SyncWorkQueueManager unavailable"));
                  },
                  Qt::QueuedConnection);
              return;
            }
            const auto queued = workQueue->enqueue(
                notebookId,
                [this, notebookId, configJson, credsJson, notebookSvc]() {
                  SyncOps::enableSync(notebookSvc, notebookId, configJson, credsJson,
                                      [this, notebookId](VxCoreError p_code, QString p_msg) {
                                        QMetaObject::invokeMethod(
                                            this,
                                            [this, notebookId, p_code, p_msg]() {
                                              onWorkerEnableFinished(notebookId, p_code, p_msg);
                                            },
                                            Qt::QueuedConnection);
                                      });
                },
                [this, notebookId]() {
                  QMetaObject::invokeMethod(
                      this,
                      [this, notebookId]() {
                        onWorkerEnableFinished(notebookId, VXCORE_ERR_CANCELLED,
                                               tr("Sync cancelled."));
                      },
                      Qt::QueuedConnection);
                });
            if (queued != SyncWorkQueueManager::EnqueueResult::Accepted)
              onWorkerEnableFinished(notebookId, VXCORE_ERR_SYNC_IN_PROGRESS,
                                     tr("The sync queue is busy. Try again."));
          });

  *errorConn =
      connect(m_credentialsStore, &SyncCredentialsStore::credentialsStoreError, this,
              [this, notebookId, cleanup](const QString &p_errNotebookId, const QString &p_errMsg) {
                if (p_errNotebookId != notebookId) {
                  return;
                }
                cleanup();
                qWarning() << "SyncService::enableSyncForNotebook: keychain store failed";
                onWorkerEnableFinished(notebookId, VXCORE_ERR_UNKNOWN, p_errMsg);
              });

  m_credentialsStore->storeCredentials(notebookId, p_settings.m_credentials);
}

void SyncService::disableSyncForNotebook(const QString &p_notebookId) {
  if (m_shutDown)
    return;
  if (m_credentialOperations.contains(p_notebookId)) {
    QMetaObject::invokeMethod(
        this,
        [this, p_notebookId]() { emit disableFinished(p_notebookId, VXCORE_ERR_SYNC_IN_PROGRESS); },
        Qt::QueuedConnection);
    return;
  }
  m_credentialOperations.insert(p_notebookId);
  const auto finish = [this, p_notebookId](VxCoreError code) {
    QMetaObject::invokeMethod(
        this, [this, p_notebookId, code]() { onWorkerDisableFinished(p_notebookId, code); },
        Qt::QueuedConnection);
  };
  const auto queued = m_workQueue->enqueue(
      p_notebookId,
      [this, p_notebookId, finish]() {
        SyncOps::disableSync(m_notebookCoreService, p_notebookId, finish);
      },
      [finish]() { finish(VXCORE_ERR_CANCELLED); });
  if (queued != SyncWorkQueueManager::EnqueueResult::Accepted)
    finish(VXCORE_ERR_SYNC_IN_PROGRESS);
}

void SyncService::unregisterSyncRuntime(const QString &p_notebookId) {
  if (p_notebookId.isEmpty()) {
    return;
  }
  dropDebounceTimer(p_notebookId);
  m_keepBothUnsupported.remove(p_notebookId);
  if (!m_notebookCoreService) {
    qCWarning(syncCategory)
        << "SyncService::unregisterSyncRuntime: NotebookCoreService unavailable for"
        << p_notebookId;
    return;
  }
  // Synchronous, lock-free against libgit2 — SyncManager::UnregisterBackend
  // moves the unique_ptr<ISyncBackend> out of backends_ under state_mutex_
  // and lets it destruct AFTER the lock is released. The destructor runs
  // ~GitSyncBackend → git_repository_free → unmap pack files. No worker
  // queue needed; this is short enough to run on the GUI thread (the close
  // path is already on the GUI thread).
  const VxCoreError err = m_notebookCoreService->unregisterSyncRuntime(p_notebookId);
  if (err != VXCORE_OK) {
    qCWarning(syncCategory) << "SyncService::unregisterSyncRuntime: failed for" << p_notebookId
                            << ":" << vxErrorToString(err);
  } else {
    qCDebug(syncCategory) << "SyncService::unregisterSyncRuntime: released runtime for"
                          << p_notebookId;
  }
}

void SyncService::triggerSyncNow(const QString &p_notebookId) {
  enqueueSync(p_notebookId, {}, false);
}

void SyncService::enqueueSync(const QString &p_notebookId,
                              const QHash<QString, QString> &p_resolutions, bool p_automatic) {
  if (m_shutDown)
    return;
  if (hasInterruptedRetirement(p_notebookId)) {
    if (!p_automatic) {
      m_syncErrorMessages.insert(
          p_notebookId, tr("Open Sync Info to restore the interrupted sync configuration change."));
      onSyncFinished(p_notebookId, VXCORE_ERR_INVALID_STATE);
    }
    return;
  }
  qCInfo(syncCategory) << "SyncService::enqueueSync: notebookId:" << p_notebookId;

  if (!m_workQueue) {
    qCWarning(syncCategory) << "SyncService::enqueueSync: work queue unavailable for"
                            << p_notebookId;
    if (!p_automatic)
      emit syncFailed(p_notebookId, VXCORE_ERR_NOT_INITIALIZED,
                      tr("The sync work queue is unavailable."));
    return;
  }
  auto operation = std::make_shared<SyncOperation>();
  operation->notebookId = p_notebookId;
  operation->generation = ++m_nextSyncGeneration;
  operation->resolutions = p_resolutions;
  if (!operation->token) {
    emit syncFailed(p_notebookId, VXCORE_ERR_OUT_OF_MEMORY,
                    QString::fromUtf8(vxcore_error_message(VXCORE_ERR_OUT_OF_MEMORY)));
    return;
  }
  m_syncOperations.insert(operation->generation, operation);
  const auto result = m_workQueue->enqueue(
      p_notebookId, [this, operation]() { runSyncOperation(operation); },
      [this, operation]() {
        operation->cancelled.store(true);
        vxcore_sync_cancel(operation->token.get());
        QMetaObject::invokeMethod(
            this, [this, operation]() { m_syncOperations.remove(operation->generation); },
            Qt::QueuedConnection);
      },
      p_resolutions.isEmpty() ? QStringLiteral("trigger") : QString());
  if (result == SyncWorkQueueManager::EnqueueResult::Accepted) {
    qCInfo(syncCategory) << "SyncService::enqueueSync: enqueued for" << p_notebookId;
  } else {
    m_syncOperations.remove(operation->generation);
    qCInfo(syncCategory) << "Sync enqueue not accepted for" << p_notebookId << int(result);
    // Trigger overflow/coalescing remains silent. A rejected resolution batch
    // must be visible: no choice from this batch was accepted.
    if (!p_resolutions.isEmpty()) {
      m_syncErrorMessages.insert(p_notebookId,
                                 tr("The sync queue is full. Conflict choices were not applied."));
      onSyncFinished(p_notebookId, VXCORE_ERR_SYNC_IN_PROGRESS);
    }
  }
}

bool SyncService::isCurrentOperation(const std::shared_ptr<SyncOperation> &p_operation) const {
  return m_syncOperations.value(p_operation->generation) == p_operation;
}

void SyncService::runSyncOperation(const std::shared_ptr<SyncOperation> &p_operation) {
  const auto &id = p_operation->notebookId;
  QMetaObject::invokeMethod(
      this,
      [this, p_operation]() {
        if (isCurrentOperation(p_operation))
          onSyncStarted(p_operation->notebookId);
      },
      Qt::QueuedConnection);
  VxCoreError result = VXCORE_OK;
  QString error;
  try {
    // One queue item owns the whole batch: no cap can silently drop a choice,
    // and a later successful resolution cannot erase an earlier error.
    for (auto it = p_operation->resolutions.constBegin(); it != p_operation->resolutions.constEnd();
         ++it) {
      if (p_operation->cancelled.load()) {
        result = VXCORE_ERR_CANCELLED;
        break;
      }
      SyncOps::resolveConflict(
          m_notebookCoreService, id, it.key(), it.value(), [&](VxCoreError code) {
            if (code != VXCORE_OK) {
              const auto detail = m_notebookCoreService->syncErrorMessage(code);
              if (result == VXCORE_OK)
                result = code;
              if (!error.isEmpty())
                error += QLatin1Char('\n');
              error += it.key() + QStringLiteral(": ") + detail;
            }
          });
    }
    if (p_operation->cancelled.load() && result == VXCORE_OK)
      result = VXCORE_ERR_CANCELLED;
    if (result == VXCORE_OK) {
      SyncOps::triggerSync(
          m_notebookCoreService, id, p_operation->token.get(),
          [&](VxCoreError code) {
            result = code;
            if (code != VXCORE_OK) {
              // The apply callback may already have copied a more specific
              // failure before GUI refresh touched the context's last error.
              QMutexLocker locker(&p_operation->mutex);
              error = p_operation->error;
              locker.unlock();
              if (error.isEmpty())
                error = m_notebookCoreService->syncErrorMessage(code);
            }
          },
          m_services.get<NotebookIoGate>(),
          [this, p_operation]() { return applySyncOperation(p_operation); });
    }
  } catch (...) {
    result = VXCORE_ERR_UNKNOWN;
    error = tr("Sync could not complete.");
  }
  // Capture diagnostics before any subsequent C call can overwrite last_error.
  if (result != VXCORE_OK && error.isEmpty())
    error = m_notebookCoreService->syncErrorMessage(result);
  QString conflictJson;
  QJsonArray conflicts;
  if (!p_operation->stopping.load() &&
      m_notebookCoreService->getSyncConflicts(id, conflictJson) == VXCORE_OK) {
    conflicts = QJsonDocument::fromJson(conflictJson.toUtf8())
                    .object()
                    .value(QStringLiteral("conflicts"))
                    .toArray();
  }
  if (result == VXCORE_OK && !conflicts.isEmpty()) {
    result = VXCORE_ERR_SYNC_CONFLICT;
    error = QString::fromUtf8(vxcore_error_message(result));
  }
  QMetaObject::invokeMethod(
      this,
      [this, p_operation, result, error, conflicts]() {
        if (m_shutDown || !isCurrentOperation(p_operation))
          return;
        QStringList paths;
        QSet<QString> unsupported;
        for (const auto &value : conflicts) {
          const auto conflict = value.toObject();
          const auto path = conflict.value(QLatin1String(vxcore::kJsonKeyPath)).toString();
          paths.append(path);
          if (!conflict.value(QLatin1String(vxcore::kJsonKeyCanKeepBoth)).toBool(true))
            unsupported.insert(path);
        }
        m_keepBothUnsupported.insert(p_operation->notebookId, unsupported);
        if (!paths.isEmpty())
          onSyncConflictFiles(p_operation->notebookId, paths);
        m_syncErrorMessages.insert(p_operation->notebookId, error);
        m_syncOperations.remove(p_operation->generation);
        onSyncFinished(p_operation->notebookId, result == VXCORE_OK && p_operation->cancelled.load()
                                                    ? VXCORE_ERR_CANCELLED
                                                    : result);
      },
      Qt::QueuedConnection);
}

VxCoreError SyncService::applySyncOperation(const std::shared_ptr<SyncOperation> &p_operation) {
  {
    QMutexLocker locker(&p_operation->mutex);
    p_operation->deferred = true;
    p_operation->phase = SyncOperation::Phase::Requested;
  }
  // No IO gate is held while asking the GUI to reserve/drain open buffers.
  QMetaObject::invokeMethod(
      this, [this, p_operation]() { beginSyncApply(p_operation); }, Qt::QueuedConnection);
  {
    QMutexLocker locker(&p_operation->mutex);
    while (p_operation->phase != SyncOperation::Phase::Ready &&
           p_operation->phase != SyncOperation::Phase::Retired && !p_operation->stopping.load())
      p_operation->changed.wait(&p_operation->mutex);
    if (p_operation->stopping.load())
      return VXCORE_ERR_CANCELLED;
    if (p_operation->phase == SyncOperation::Phase::Retired)
      return p_operation->result;
    p_operation->phase = SyncOperation::Phase::Applying;
  }
  QStringList changedPaths;
  QString error;
  VxCoreError result = VXCORE_ERR_CANCELLED;
  try {
    auto *gate = m_services.get<NotebookIoGate>();
    if (!gate) {
      result = VXCORE_ERR_NOT_INITIALIZED;
      error = tr("The notebook IO gate is unavailable.");
    } else {
      NotebookIoGate::ScopedLock lock(*gate, p_operation->notebookId);
      if (!p_operation->cancelled.load()) {
        result =
            m_notebookCoreService->syncApplyPhase(p_operation->notebookId, p_operation->token.get(),
                                                  p_operation->protectedPaths, &changedPaths);
        if (result != VXCORE_OK)
          error = m_notebookCoreService->syncErrorMessage(result);
      }
    }
  } catch (...) {
    result = VXCORE_ERR_UNKNOWN;
    error = tr("Sync could not install the downloaded files.");
  }
  {
    QMutexLocker locker(&p_operation->mutex);
    p_operation->result = result;
    p_operation->error = error;
    p_operation->changedPaths = changedPaths;
    p_operation->phase = SyncOperation::Phase::Applied;
  }
  // Publication is over and the gate released before GUI refresh or callbacks.
  QMetaObject::invokeMethod(
      this, [this, p_operation]() { finishSyncApply(p_operation); }, Qt::QueuedConnection);
  QMutexLocker locker(&p_operation->mutex);
  while (p_operation->phase != SyncOperation::Phase::Retired && !p_operation->stopping.load())
    p_operation->changed.wait(&p_operation->mutex);
  return p_operation->stopping.load() ? VXCORE_ERR_CANCELLED : p_operation->result;
}

void SyncService::beginSyncApply(const std::shared_ptr<SyncOperation> &p_operation) {
  if (!isCurrentOperation(p_operation))
    return;
  if (m_shutDown || p_operation->cancelled.load()) {
    cancelSyncOperation(p_operation, m_shutDown);
    return;
  }
  {
    QMutexLocker locker(&p_operation->mutex);
    if (p_operation->phase != SyncOperation::Phase::Requested)
      return;
    p_operation->phase = SyncOperation::Phase::Reserving;
  }
  auto *buffers = m_services.get<BufferService>();
  QStringList protectedPaths;
  bool reserved = false;
  try {
    reserved = buffers && buffers->beginSyncApply(p_operation->notebookId, &protectedPaths);
  } catch (...) {
    // No worker can publish until this GUI request has signalled Ready.
  }
  if (!isCurrentOperation(p_operation)) {
    if (reserved)
      buffers->endSyncApply(p_operation->notebookId, {});
    return;
  }
  p_operation->buffersReserved = reserved;
  {
    QMutexLocker locker(&p_operation->mutex);
    p_operation->protectedPaths = protectedPaths;
    p_operation->phase = SyncOperation::Phase::Draining;
    if (!reserved && !p_operation->cancelled.load()) {
      p_operation->result = VXCORE_ERR_SYNC_IN_PROGRESS;
      p_operation->error = tr("An open note is being replaced or converted. Try sync again.");
    }
  }
  if (!reserved || m_shutDown || p_operation->cancelled.load()) {
    finishSyncApply(p_operation);
    return;
  }
  p_operation->drainTime.start();
  p_operation->timer = new QTimer(this);
  p_operation->timer->setSingleShot(true);
  p_operation->timer->setInterval(25);
  connect(p_operation->timer.data(), &QTimer::timeout, this,
          [this, p_operation]() { pollSyncApply(p_operation); });
  pollSyncApply(p_operation);
}

void SyncService::pollSyncApply(const std::shared_ptr<SyncOperation> &p_operation) {
  if (!isCurrentOperation(p_operation))
    return;
  {
    QMutexLocker locker(&p_operation->mutex);
    if (p_operation->phase != SyncOperation::Phase::Draining)
      return;
  }
  if (m_shutDown || p_operation->cancelled.load()) {
    cancelSyncOperation(p_operation, m_shutDown);
    return;
  }
  auto *buffers = m_services.get<BufferService>();
  if (!buffers || !buffers->isSyncApplyReady(p_operation->notebookId)) {
    if (p_operation->drainTime.elapsed() < 5000) {
      p_operation->timer->start();
      return;
    }
    {
      QMutexLocker locker(&p_operation->mutex);
      p_operation->result = VXCORE_ERR_SYNC_IN_PROGRESS;
      p_operation->error = tr("An open note is still being saved. Try sync again.");
    }
    finishSyncApply(p_operation);
    return;
  }
  if (auto *comments = m_services.get<CommentService>())
    p_operation->protectedPaths.append(comments->syncProtectedPaths(p_operation->notebookId));
  p_operation->protectedPaths.removeDuplicates();
  const auto result = m_notebookCoreService->setSyncApplyInProgress(p_operation->notebookId, true);
  const auto error =
      result == VXCORE_OK ? QString() : m_notebookCoreService->syncErrorMessage(result);
  p_operation->coreReserved = result == VXCORE_OK;
  {
    QMutexLocker locker(&p_operation->mutex);
    p_operation->result = result;
    p_operation->error = error;
    if (result == VXCORE_OK && !p_operation->cancelled.load()) {
      p_operation->phase = SyncOperation::Phase::Ready;
      p_operation->changed.wakeAll();
      return;
    }
  }
  finishSyncApply(p_operation);
}

void SyncService::finishSyncApply(const std::shared_ptr<SyncOperation> &p_operation) {
  if (!isCurrentOperation(p_operation))
    return;
  VxCoreError result;
  QString error;
  QStringList changedPaths;
  bool deferred;
  {
    QMutexLocker locker(&p_operation->mutex);
    if (p_operation->phase == SyncOperation::Phase::Applying ||
        p_operation->phase == SyncOperation::Phase::Reserving ||
        p_operation->phase == SyncOperation::Phase::Retiring ||
        p_operation->phase == SyncOperation::Phase::Retired)
      return;
    result = p_operation->result;
    error = p_operation->error;
    changedPaths = p_operation->changedPaths;
    deferred = p_operation->deferred;
    p_operation->phase = SyncOperation::Phase::Retiring;
  }
  if (result == VXCORE_OK && p_operation->cancelled.load())
    result = VXCORE_ERR_CANCELLED;
  if (p_operation->timer) {
    p_operation->timer->stop();
    p_operation->timer->deleteLater();
    p_operation->timer = nullptr;
  }
  if (deferred) {
    const auto refreshed = m_notebookCoreService->refreshAfterSync(p_operation->notebookId);
    if (result == VXCORE_OK && refreshed != VXCORE_OK) {
      result = refreshed;
      error = m_notebookCoreService->syncErrorMessage(refreshed);
    }
  }
  // Refresh/reload while still reserved; clear the core flag only after the
  // GUI has reconciled its caches. Never reload dirty buffer text.
  if (p_operation->buffersReserved) {
    if (auto *buffers = m_services.get<BufferService>())
      buffers->endSyncApply(p_operation->notebookId, changedPaths);
    p_operation->buffersReserved = false;
  }
  if (p_operation->coreReserved) {
    const auto cleared =
        m_notebookCoreService->setSyncApplyInProgress(p_operation->notebookId, false);
    p_operation->coreReserved = false;
    if (result == VXCORE_OK && cleared != VXCORE_OK) {
      result = cleared;
      error = m_notebookCoreService->syncErrorMessage(cleared);
    }
  }
  if (result == VXCORE_OK && p_operation->cancelled.load())
    result = VXCORE_ERR_CANCELLED;
  if (!changedPaths.isEmpty()) {
    try {
      emit workingTreeChanged(p_operation->notebookId, changedPaths);
    } catch (...) {
      if (result == VXCORE_OK) {
        result = VXCORE_ERR_UNKNOWN;
        error = tr("An open view could not refresh after sync.");
      }
    }
  }
  if (result == VXCORE_OK && p_operation->cancelled.load())
    result = VXCORE_ERR_CANCELLED;
  {
    QMutexLocker locker(&p_operation->mutex);
    p_operation->result = result;
    p_operation->error = error;
    p_operation->phase = SyncOperation::Phase::Retired;
    p_operation->changed.wakeAll();
  }
}

void SyncService::cancelSyncOperation(const std::shared_ptr<SyncOperation> &p_operation,
                                      bool p_shutdown) {
  p_operation->cancelled.store(true);
  if (p_shutdown)
    p_operation->stopping.store(true);
  vxcore_sync_cancel(p_operation->token.get());
  bool canRetire;
  bool reserving;
  {
    QMutexLocker locker(&p_operation->mutex);
    reserving = p_operation->phase == SyncOperation::Phase::Reserving;
    canRetire = p_operation->phase != SyncOperation::Phase::Applying &&
                p_operation->phase != SyncOperation::Phase::Reserving &&
                p_operation->phase != SyncOperation::Phase::Retiring &&
                p_operation->phase != SyncOperation::Phase::Network;
    if (canRetire && p_operation->result == VXCORE_OK)
      p_operation->result = VXCORE_ERR_CANCELLED;
    p_operation->changed.wakeAll();
  }
  // Cancellation can be reentrant from a clean-view freeze signal. The buffer
  // service installs its whole reservation before emitting, so retire it now
  // and let beginSyncApply observe that it no longer owns the reservation.
  if (reserving) {
    if (auto *buffers = m_services.get<BufferService>())
      buffers->endSyncApply(p_operation->notebookId, {});
  }
  if (canRetire)
    finishSyncApply(p_operation);
}

void SyncService::cancelSync(const QString &p_notebookId) {
  if (m_shutDown)
    return;
  const int dropped = m_workQueue ? m_workQueue->cancelPending(p_notebookId) : 0;
  const auto operations = m_syncOperations;
  bool inFlight = false;
  for (const auto &operation : operations) {
    if (operation->notebookId == p_notebookId && !operation->cancelled.load()) {
      cancelSyncOperation(operation, false);
      inFlight = true;
    }
  }
  if (dropped > 0)
    emit syncCancelled(p_notebookId, true);
  if (inFlight)
    emit syncCancelled(p_notebookId, false);
  if (auto *hookMgr = m_services.get<HookManager>()) {
    SyncCancelledEvent event;
    event.notebookId = p_notebookId;
    event.wasQueued = dropped > 0;
    hookMgr->doAction(HookNames::SyncCancelled, event);
  }
}

// See AGENTS.md "bootstrapAndPersist Rollback x Reconcile" for rollback semantics
// and how rollback success/failure interacts with reconcileSyncForNotebook.
//
// T20: enable, persist, initial-sync, and rollback all route through
// SyncWorkQueueManager on the SAME notebookId so FIFO ordering is preserved.
// enableSyncForNotebook already enqueues the enable work; the bridge below
// observes enableFinished (GUI-thread signal emitted after the queued enable
// completes) and enqueues the persist work. On persist success a third work
// item is enqueued for the initial triggerSync. On persist failure a rollback
// work item is enqueued that drives SyncOps::disableSync. The atomic
// bootstrapAndPersistFinished contract (exactly one emission per call) is
// preserved by routing all completion edges through a single emit at the
// final step of whichever branch runs.
void SyncService::bootstrapAndPersist(const QString &p_notebookId, const SyncSettings &p_settings) {
  if (m_shutDown) {
    qWarning() << "SyncService::bootstrapAndPersist: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::bootstrapAndPersist: notebookId:" << p_notebookId;

  const QString notebookId = p_notebookId;
  const QString remoteUrl = canonicalSyncRemoteUrl(p_settings);
  const QString backend = p_settings.m_backend;

  // One-shot bridge on enableFinished. Filters by notebookId, self-disconnects,
  // then enqueues the persist work (or short-circuits on enable failure).
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn = connect(
      this, &SyncService::enableFinished, this,
      [this, conn, notebookId, remoteUrl, backend](const QString &p_finishedId,
                                                   VxCoreError p_enResult, const QString &p_enMsg) {
        if (p_finishedId != notebookId) {
          return;
        }
        QObject::disconnect(*conn);

        qCDebug(syncCategory) << "SyncService::bootstrapAndPersist: enableFinished result="
                              << static_cast<int>(p_enResult) << "id=" << notebookId;

        if (p_enResult != VXCORE_OK) {
          // Enable failed; notebook stays in original state. Nothing to roll back.
          emit bootstrapAndPersistFinished(notebookId, p_enResult, p_enMsg);
          return;
        }

        auto *workQueue = m_workQueue;
        if (!workQueue) {
          qCWarning(syncCategory)
              << "SyncService::bootstrapAndPersist: SyncWorkQueueManager unavailable for"
              << notebookId;
          emit bootstrapAndPersistFinished(notebookId, VXCORE_ERR_UNKNOWN,
                                           QStringLiteral("SyncWorkQueueManager unavailable"));
          return;
        }

        // T20: enqueue persist work. The pool-thread body bounces to the GUI
        // thread via QueuedConnection because NotebookCoreService::* mutates
        // shared state that the rest of SyncService also touches on the GUI
        // thread. FIFO ordering with the prior enable item is guaranteed by
        // enqueue-ing on the same notebookId.
        workQueue->enqueue(notebookId, [this, notebookId, remoteUrl, backend]() {
          QMetaObject::invokeMethod(
              this,
              [this, notebookId, remoteUrl, backend]() {
                bool persistOk = false;
                QString persistErr;
                if (m_testForceNextPersistFailure) {
                  // Test seam: simulate persist failure without writing JSON.
                  persistErr = m_testForceNextPersistFailureMsg;
                  m_testForceNextPersistFailure = false;
                  m_testForceNextPersistFailureMsg.clear();
                } else if (m_notebookCoreService) {
                  QJsonObject cfg = m_notebookCoreService->getNotebookConfig(notebookId);
                  cfg[QLatin1String(vxcore::kJsonKeySyncEnabled)] = true;
                  cfg[QLatin1String(vxcore::kJsonKeySyncBackend)] = backend;
                  cfg[QLatin1String(vxcore::kJsonKeySyncRemoteUrl)] = remoteUrl;
                  const QString cfgJson =
                      QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
                  persistOk = m_notebookCoreService->updateNotebookConfig(notebookId, cfgJson);
                  if (!persistOk) {
                    persistErr =
                        tr("Failed to persist sync configuration to notebook after enable.");
                  }
                } else {
                  persistErr = tr("Notebook service not available.");
                }

                auto *wq = m_workQueue;
                if (persistOk) {
                  // Bootstrap persists routing first; its initial sync uses
                  // the same cancellable, acknowledged staged operation.
                  if (wq)
                    triggerSyncNow(notebookId);
                  emit bootstrapAndPersistFinished(notebookId, VXCORE_OK, QString());
                  return;
                }

                // Persist failed AFTER enable succeeded. T20: enqueue rollback
                // (SyncOps::disableSync) on the same notebookId so it FIFO-
                // orders after the enable. Original persist error is preserved
                // in the emitted signal regardless of rollback outcome.
                qCritical() << "SyncService::bootstrapAndPersist: persist failed after enable; "
                               "rolling back. id="
                            << notebookId << "persistError:" << persistErr;
                const QString origPersistErr = persistErr;

                const bool forceRollbackFail = m_testForceNextRollbackFailure;
                if (forceRollbackFail) {
                  m_testForceNextRollbackFailure = false;
                }

                if (!wq) {
                  qCWarning(syncCategory)
                      << "SyncService::bootstrapAndPersist: SyncWorkQueueManager unavailable for"
                         " rollback; emitting persist error directly"
                      << notebookId;
                  emit bootstrapAndPersistFinished(notebookId, VXCORE_ERR_UNKNOWN, origPersistErr);
                  return;
                }

                // Disable's public completion is after JSON cleanup and the
                // vault delete has settled, so a retry cannot race that delete.
                auto rollback = std::make_shared<QMetaObject::Connection>();
                *rollback = connect(
                    this, &SyncService::disableFinished, this,
                    [this, notebookId, origPersistErr, rollback](const QString &id,
                                                                 VxCoreError code) {
                      if (id != notebookId)
                        return;
                      QObject::disconnect(*rollback);
                      if (code != VXCORE_OK)
                        qCritical() << "SyncService::bootstrapAndPersist: ROLLBACK FAILED for" << id
                                    << vxErrorToString(code);
                      emit bootstrapAndPersistFinished(id, VXCORE_ERR_UNKNOWN, origPersistErr);
                    });
                if (forceRollbackFail)
                  onWorkerDisableFinished(notebookId, VXCORE_ERR_UNKNOWN);
                else
                  disableSyncForNotebook(notebookId);
              },
              Qt::QueuedConnection);
        });
      });

  enableSyncForNotebook(notebookId, p_settings);
}

void SyncService::updateCredentials(const QString &p_notebookId,
                                    const SyncCredential &p_credentials) {
  if (m_shutDown)
    return;
  if (hasInterruptedRetirement(p_notebookId)) {
    emit credentialsSetFinished(p_notebookId, VXCORE_ERR_INVALID_STATE);
    return;
  }
  const auto cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  SyncSettings settings;
  settings.m_backend = cfg.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString();
  settings.m_remoteUrl = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
  settings.m_credentials = p_credentials;
  if (!isSyncEnabled(p_notebookId) || !validateSyncSettings(settings, true).isEmpty()) {
    emit credentialsSetFinished(p_notebookId, isSupportedSyncBackend(settings.m_backend)
                                                  ? VXCORE_ERR_INVALID_PARAM
                                                  : VXCORE_ERR_UNKNOWN_BACKEND);
    return;
  }
  if (isSyncInProgress(p_notebookId)) {
    emit credentialsSetFinished(p_notebookId, VXCORE_ERR_SYNC_IN_PROGRESS);
    return;
  }
  m_authFailureCount.remove(p_notebookId);

  // A new WebDAV backend authenticates and verifies the remote UUID before
  // atomically rotating usernameHash. Failed initialization keeps the old runtime.
  // No state deletion or transfer is part of credential rotation.
  if (!isSyncRegistered(p_notebookId) || settings.m_backend == QLatin1String("webdav")) {
    auto bridge = std::make_shared<QMetaObject::Connection>();
    *bridge =
        connect(this, &SyncService::enableFinished, this,
                [this, p_notebookId, bridge](const QString &id, VxCoreError code, const QString &) {
                  if (id != p_notebookId)
                    return;
                  QObject::disconnect(*bridge);
                  emit credentialsSetFinished(id, code);
                });
    enableSyncForNotebook(p_notebookId, settings);
    return;
  }

  m_credentialOperations.insert(p_notebookId);
  const auto credentialsJson = syncCredentialsJson(p_credentials);
  auto stored = std::make_shared<QMetaObject::Connection>();
  auto failed = std::make_shared<QMetaObject::Connection>();
  const auto disconnect = [stored, failed]() {
    QObject::disconnect(*stored);
    QObject::disconnect(*failed);
  };
  *stored =
      connect(m_credentialsStore, &SyncCredentialsStore::credentialsStored, this,
              [this, p_notebookId, credentialsJson, disconnect](const QString &id) {
                if (id != p_notebookId)
                  return;
                disconnect();
                if (m_shutDown) {
                  m_credentialOperations.remove(id);
                  return;
                }
                const auto finish = [this, id](VxCoreError code) {
                  QMetaObject::invokeMethod(
                      this, [this, id, code]() { onWorkerCredentialsSetFinished(id, code); },
                      Qt::QueuedConnection);
                };
                const auto queued = m_workQueue->enqueue(
                    id,
                    [this, id, credentialsJson, finish]() {
                      SyncOps::setCredentials(m_notebookCoreService, id, credentialsJson, finish);
                    },
                    [finish]() { finish(VXCORE_ERR_CANCELLED); });
                if (queued != SyncWorkQueueManager::EnqueueResult::Accepted)
                  onWorkerCredentialsSetFinished(id, VXCORE_ERR_SYNC_IN_PROGRESS);
              });
  *failed = connect(m_credentialsStore, &SyncCredentialsStore::credentialsStoreError, this,
                    [this, p_notebookId, disconnect](const QString &id, const QString &) {
                      if (id != p_notebookId)
                        return;
                      disconnect();
                      qWarning() << "SyncService::updateCredentials: keychain store failed";
                      onWorkerCredentialsSetFinished(id, VXCORE_ERR_UNKNOWN);
                    });
  m_credentialsStore->storeCredentials(p_notebookId, p_credentials);
}

void SyncService::resolveConflicts(const QString &p_notebookId,
                                   const QHash<QString, QString> &p_resolutions) {
  enqueueSync(p_notebookId, p_resolutions, false);
}

QSet<QString> SyncService::keepBothUnsupportedPaths(const QString &p_notebookId) const {
  return m_keepBothUnsupported.value(p_notebookId);
}

bool SyncService::isSyncInProgress(const QString &p_notebookId) const {
  return m_credentialOperations.contains(p_notebookId) ||
         (m_workQueue && m_workQueue->hasPending(p_notebookId));
}

bool SyncService::isSyncEnabled(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return false;
  }
  const QJsonObject cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  const bool enabled = cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool();
  qCDebug(syncCategory) << "SyncService::isSyncEnabled: query notebookId:" << p_notebookId
                        << "syncEnabled:" << enabled;
  return enabled;
}

bool SyncService::isSyncReady(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return false;
  }
  const auto cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  SyncSettings settings;
  settings.m_backend = cfg.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString();
  settings.m_remoteUrl = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
  const bool ready = cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool() &&
                     validateSyncSettings(settings, false).isEmpty();
  qCDebug(syncCategory) << "SyncService::isSyncReady: query notebookId:" << p_notebookId
                        << "syncReady:" << ready;
  return ready;
}

bool SyncService::isSyncRegistered(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return false;
  }
  // Route through NotebookCoreService::isSyncRegistered
  // (vxcore_sync_is_registered -> SyncManager::IsRegistered) which only
  // acquires state_mutex_, NEVER the per-backend op_mutex_. The previous
  // implementation called getSyncStatus -> GitSyncBackend::GetStatus and
  // acquired op_mutex_ blockingly, racing against worker-thread
  // StageAndCommit/FetchRebasePush into persistent VXCORE_ERR_SYNC_IN_PROGRESS
  // (21) on every Sync Now click.
  const bool registered = m_notebookCoreService->isSyncRegistered(p_notebookId);
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
  if (m_workQueue) {
    m_workQueue->testForceInFlight(p_notebookId, p_value);
  }
}

void SyncService::testForceNextPersistFailure(const QString &p_message) {
  m_testForceNextPersistFailure = true;
  m_testForceNextPersistFailureMsg =
      p_message.isEmpty() ? QStringLiteral("injected persist failure") : p_message;
}

void SyncService::testForceNextRollbackFailure() { m_testForceNextRollbackFailure = true; }

void SyncService::testForceLastSyncUtc(const QString &p_notebookId, qint64 p_ms) {
  if (p_ms < 0) {
    m_testLastSyncUtcOverrides.remove(p_notebookId);
  } else {
    m_testLastSyncUtcOverrides.insert(p_notebookId, p_ms);
  }
}

void SyncService::testInvokeMaybeTriggerPostReconcile(const QString &p_notebookId) {
  maybeTriggerPostReconcile(p_notebookId);
}

void SyncService::testSetMaybeTriggerBypassReadinessCheck(bool p_bypass) {
  m_testBypassReadinessCheck = p_bypass;
}

void SyncService::testSetDebounceOverrideSeconds(int p_seconds) {
  m_testDebounceOverrideSeconds = p_seconds;
}

bool SyncService::testIsDebounceTimerActive(const QString &p_notebookId) const {
  const QTimer *timer = m_debounceTimers.value(p_notebookId, nullptr);
  return timer && timer->isActive();
}

int SyncService::testDebounceRemainingMs(const QString &p_notebookId) const {
  const QTimer *timer = m_debounceTimers.value(p_notebookId, nullptr);
  return timer ? timer->remainingTime() : -1;
}

void SyncService::testFireDebounceNow(const QString &p_notebookId) {
  onDebounceTimeout(p_notebookId);
}

// Post-reconcile freshness-gated auto-trigger. Called from reconcileSyncForNotebook
// after the enable work item completes with VXCORE_OK, on the GUI thread (via
// the QueuedConnection bounce from the SyncOps::enableSync completion lambda).
// See header comment on maybeTriggerPostReconcile for the full rationale.
void SyncService::maybeTriggerPostReconcile(const QString &p_notebookId) {
  if (m_shutDown) {
    return;
  }
  // Defensive: reconcile may have raced with a disable / unregister; do not
  // try to trigger a sync we cannot run. Tests may opt out of this check via
  // testSetMaybeTriggerBypassReadinessCheck so the gate logic can be
  // exercised without a real vxcore sync registration.
  if (!m_testBypassReadinessCheck &&
      (!isSyncEnabled(p_notebookId) || !isSyncRegistered(p_notebookId))) {
    qCInfo(syncCategory) << "SyncService::maybeTriggerPostReconcile: skip (not ready) for"
                         << p_notebookId;
    return;
  }
  // If a sync is already in flight, do nothing. The work queue's coalesceKey
  // would collapse a duplicate enqueue anyway; skipping here just keeps the
  // queue clean.
  if (isSyncInProgress(p_notebookId)) {
    qCInfo(syncCategory) << "SyncService::maybeTriggerPostReconcile: skip (sync in progress) for"
                         << p_notebookId;
    return;
  }
  // Freshness gate: covers rapid open/close cycles without thrashing. Tests
  // may override via testForceLastSyncUtc.
  qint64 lastSyncMs = 0;
  const auto overrideIt = m_testLastSyncUtcOverrides.constFind(p_notebookId);
  if (overrideIt != m_testLastSyncUtcOverrides.constEnd()) {
    lastSyncMs = overrideIt.value();
  } else if (m_notebookCoreService) {
    lastSyncMs = m_notebookCoreService->getLastSyncUtc(p_notebookId);
  }
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (lastSyncMs > 0 && (nowMs - lastSyncMs) < kPostReconcileFreshnessMs) {
    qCInfo(syncCategory) << "SyncService::maybeTriggerPostReconcile: skip (fresh) for"
                         << p_notebookId << "ageMs:" << (nowMs - lastSyncMs)
                         << "thresholdMs:" << kPostReconcileFreshnessMs;
    return;
  }
  qCInfo(syncCategory) << "SyncService::maybeTriggerPostReconcile: enqueueing for" << p_notebookId
                       << "lastSyncMs:" << lastSyncMs << "nowMs:" << nowMs;
  triggerSyncNow(p_notebookId);
}

// ---- Worker -> SyncService forwarders --------------------------------------
// T23: syncStarted / syncFinished / syncFailed / conflictsDetected forwarders
// were removed; EventBridge is now the single source for those signals.
// The enable / disable / credentials forwarders remain (vxcore does not emit
// events for those today; T24 will remove them with the worker).

void SyncService::onWorkerEnableFinished(const QString &p_notebookId, VxCoreError p_result,
                                         const QString &p_message) {
  m_credentialOperations.remove(p_notebookId);
  qCDebug(syncCategory) << "SyncService::onWorkerEnableFinished: notebookId:" << p_notebookId
                        << "result:" << vxErrorToString(p_result);
  emit enableFinished(p_notebookId, p_result, p_message);
}

void SyncService::onWorkerDisableFinished(const QString &p_notebookId, VxCoreError p_result) {
  if (p_result != VXCORE_OK) {
    // A failed disable retains both routing and credentials for an explicit retry.
    m_credentialOperations.remove(p_notebookId);
    emit disableFinished(p_notebookId, p_result);
    return;
  }
  dropDebounceTimer(p_notebookId);
  m_authFailureCount.remove(p_notebookId);
  m_keepBothUnsupported.remove(p_notebookId);
  m_reconcileAttempted.remove(p_notebookId);
  auto cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  cfg.remove(QLatin1String(vxcore::kJsonKeySyncEnabled));
  cfg.remove(QLatin1String(vxcore::kJsonKeySyncBackend));
  cfg.remove(QLatin1String(vxcore::kJsonKeySyncRemoteUrl));
  if (!m_notebookCoreService->updateNotebookConfig(
          p_notebookId, QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)))) {
    qCWarning(syncCategory) << "Could not clear disabled sync routing for" << p_notebookId;
  }
  deleteStoredCredentials(p_notebookId, [this, p_notebookId]() {
    if (auto *hooks = m_services.get<HookManager>()) {
      QVariantMap args;
      args[QLatin1String(vxcore::kJsonKeyNotebookId)] = p_notebookId;
      hooks->doAction(HookNames::SyncAfterDisable, args);
    }
    emit disableFinished(p_notebookId, VXCORE_OK);
  });
}

void SyncService::deleteStoredCredentials(const QString &p_notebookId,
                                          std::function<void()> p_finished) {
  m_credentialOperations.insert(p_notebookId);
  m_deletingCredentials.insert(p_notebookId);
  auto deleted = std::make_shared<QMetaObject::Connection>();
  auto failed = std::make_shared<QMetaObject::Connection>();
  const auto complete = [this, p_notebookId, deleted, failed, p_finished]() {
    QObject::disconnect(*deleted);
    QObject::disconnect(*failed);
    m_credentialOperations.remove(p_notebookId);
    m_deletingCredentials.remove(p_notebookId);
    if (p_finished)
      p_finished();
  };
  *deleted = connect(m_credentialsStore, &SyncCredentialsStore::credentialsDeleted, this,
                     [p_notebookId, complete](const QString &id) {
                       if (id == p_notebookId)
                         complete();
                     });
  *failed = connect(m_credentialsStore, &SyncCredentialsStore::credentialsError, this,
                    [p_notebookId, complete](const QString &id, const QString &) {
                      if (id == p_notebookId) {
                        qWarning() << "SyncService: disabled credential cleanup failed";
                        complete();
                      }
                    });
  // The busy identity prevents a new store racing this still-running delete.
  m_credentialsStore->deleteCredentials(p_notebookId);
}

void SyncService::onWorkerCredentialsSetFinished(const QString &p_notebookId,
                                                 VxCoreError p_result) {
  m_credentialOperations.remove(p_notebookId);
  emit credentialsSetFinished(p_notebookId, p_result);
}

// ---- Sync lifecycle forwarders (from EventBridge) --------------------------
// T23: previously split into onWorkerSync* (worker source) and onAutoSync*
// (EventBridge source). After T7 made vxcore emit lifecycle events for BOTH
// manual and auto triggers, EventBridge is the only source — these slots
// handle every sync (manual + auto + initial-on-enable).

void SyncService::onSyncStarted(const QString &p_notebookId) {
  if (m_shutDown)
    return;
  qCInfo(syncCategory) << "SyncService::onSyncStarted: notebookId:" << p_notebookId;
  // T26: in-flight state is tracked by SyncWorkQueueManager via its work
  // item lifecycle; no separate flag to flip here.
  emit syncStarted(p_notebookId);
}

void SyncService::onSyncFinished(const QString &p_notebookId, VxCoreError p_result) {
  if (m_shutDown)
    return;
  qCInfo(syncCategory) << "SyncService::onSyncFinished: notebookId:" << p_notebookId
                       << "result:" << static_cast<int>(p_result)
                       << "message:" << vxErrorToString(p_result);
  // T26: in-flight state is tracked by SyncWorkQueueManager via its work
  // item lifecycle; no separate flag to clear here.

  // Track consecutive auth failures for the auto-sync circuit-breaker.
  // Reset on any success so a single good sync clears the cooldown.
  if (p_result == VXCORE_OK) {
    if (m_authFailureCount.remove(p_notebookId)) {
      qCInfo(syncCategory) << "SyncService::onSyncFinished: auth failure counter cleared on success"
                           << "notebookId:" << p_notebookId;
    }
    // Persist the per-device "last successful sync" timestamp HERE, on the GUI
    // thread. The two-phase production sync path (StageOnly + NetworkPhaseOnly)
    // does NOT write last_sync_utc inside vxcore (the legacy bundled TriggerSync
    // did, but it has no production callers). We must NOT write it from the sync
    // worker thread: metadata.db is a non-thread-safe per-notebook sqlite
    // connection touched ONLY from the GUI thread in production (OpenBuffer,
    // tag/folder ops, getLastSyncUtc reads). This slot runs on the GUI thread
    // (bounced via Qt::QueuedConnection from every SyncOps::triggerSync
    // completion — manual, auto-sync, bootstrap-initial, post-conflict), so the
    // write serializes with all other metadata.db access. Written BEFORE
    // syncFinished is emitted so the dialog's onSyncFinished re-read sees a
    // fresh value. See VNote root AGENTS.md Save Path Threading Contract.
    if (m_notebookCoreService) {
      m_notebookCoreService->setLastSyncUtc(p_notebookId, QDateTime::currentMSecsSinceEpoch());
    }
  } else if (p_result == VXCORE_ERR_SYNC_AUTH_FAILED) {
    const int count = ++m_authFailureCount[p_notebookId];
    qCWarning(syncCategory) << "SyncService::onSyncFinished: auth failure"
                            << "notebookId:" << p_notebookId << "consecutiveCount:" << count
                            << "circuitThreshold:" << kAuthFailureCircuitThreshold;
  }

  if (p_result != VXCORE_OK) {
    const auto message = m_syncErrorMessages.take(p_notebookId);
    emit syncFailed(p_notebookId, p_result,
                    message.isEmpty() ? vxErrorToString(p_result) : message);
  }
  m_syncErrorMessages.remove(p_notebookId);
  emit syncFinished(p_notebookId, p_result);
}

void SyncService::onSyncConflictFiles(const QString &p_notebookId, const QStringList &p_files) {
  if (m_shutDown)
    return;
  // F4.5: fire vnote.sync.conflict_detected on the GUI thread (this slot is
  // wired to EventBridge via QueuedConnection, so we are post-bounce and hold
  // no SyncManager / worker locks). Observe-only.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    QVariantMap args;
    args[QLatin1String(vxcore::kJsonKeyNotebookId)] = p_notebookId;
    args[QStringLiteral("conflictCount")] = p_files.size();
    hookMgr->doAction(HookNames::SyncConflictDetected, args);
  }
  emit conflictsDetected(p_notebookId, p_files);
}

int SyncService::debounceSeconds() const {
  if (m_testDebounceOverrideSeconds >= 0) {
    return m_testDebounceOverrideSeconds;
  }
  auto *configSvc = m_services.get<ConfigCoreService>();
  return configSvc ? configSvc->getAutoSyncDebounceSeconds() : 0;
}

qint64 SyncService::lastSyncTimeMs(const QString &p_notebookId) const {
  const auto overrideIt = m_testLastSyncUtcOverrides.constFind(p_notebookId);
  if (overrideIt != m_testLastSyncUtcOverrides.constEnd()) {
    return overrideIt.value();
  }
  return m_notebookCoreService ? m_notebookCoreService->getLastSyncUtc(p_notebookId) : 0;
}

void SyncService::armOrIgnoreDebounce(const QString &p_notebookId) {
  QTimer *timer = m_debounceTimers.value(p_notebookId, nullptr);
  if (timer && timer->isActive()) {
    qCInfo(syncCategory) << "SyncService::armOrIgnoreDebounce: active timer kept for"
                         << p_notebookId << "remainingMs:" << timer->remainingTime();
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 lastMs = lastSyncTimeMs(p_notebookId);
  const qint64 intervalMs = static_cast<qint64>(debounceSeconds()) * 1000;
  const qint64 delayMs = qMax<qint64>(0, (lastMs + intervalMs) - nowMs);

  if (!timer) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    m_debounceTimers.insert(p_notebookId, timer);
    connect(timer, &QTimer::timeout, this,
            [this, p_notebookId]() { onDebounceTimeout(p_notebookId); });
  }

  qCInfo(syncCategory) << "SyncService::armOrIgnoreDebounce: timer armed for" << p_notebookId
                       << "delayMs:" << delayMs << "lastSyncMs:" << lastMs;
  timer->start(static_cast<int>(delayMs));
}

void SyncService::dropDebounceTimer(const QString &p_notebookId) {
  QTimer *timer = m_debounceTimers.take(p_notebookId);
  if (!timer) {
    return;
  }
  timer->stop();
  timer->deleteLater();
}

void SyncService::enqueueAutoSync(const QString &p_notebookId) {
  enqueueSync(p_notebookId, {}, true);
}

void SyncService::onDebounceTimeout(const QString &p_notebookId) {
  if (QTimer *timer = m_debounceTimers.value(p_notebookId, nullptr)) {
    timer->stop();
  }
  if (m_shutDown) {
    dropDebounceTimer(p_notebookId);
    return;
  }
  if (!m_testBypassReadinessCheck &&
      (!isSyncEnabled(p_notebookId) || !isSyncRegistered(p_notebookId))) {
    qCInfo(syncCategory) << "SyncService::onDebounceTimeout: skip (not ready) for" << p_notebookId;
    dropDebounceTimer(p_notebookId);
    return;
  }
  const int authFailures = m_authFailureCount.value(p_notebookId, 0);
  if (authFailures >= kAuthFailureCircuitThreshold) {
    qCInfo(syncCategory) << "SyncService::onDebounceTimeout: skip (auth circuit-breaker) for"
                         << p_notebookId << "consecutiveFailures:" << authFailures;
    dropDebounceTimer(p_notebookId);
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 n = debounceSeconds();
  const qint64 lastMs = lastSyncTimeMs(p_notebookId);
  const qint64 intervalMs = n * 1000;
  if (n > 0 && lastMs > 0 && (nowMs - lastMs) < intervalMs) {
    QTimer *timer = m_debounceTimers.value(p_notebookId, nullptr);
    const qint64 remainingMs = qMax<qint64>(0, (lastMs + intervalMs) - nowMs);
    if (!timer) {
      armOrIgnoreDebounce(p_notebookId);
      return;
    }
    qCInfo(syncCategory) << "SyncService::onDebounceTimeout: re-arming fresh timer for"
                         << p_notebookId << "remainingMs:" << remainingMs;
    timer->start(static_cast<int>(remainingMs));
    return;
  }

  enqueueAutoSync(p_notebookId);
}

// T31: auto-sync route. EventBridge::syncShouldRun -> here -> enqueue
// SyncOps::triggerSync (with NULL cancellation token; auto path is
// fire-and-forget). Best-effort: queue overflow / coalesce are silent —
// do NOT emit syncFailed (auto-sync is opportunistic).
void SyncService::notifyWorkingTreeDirty(const QString &p_notebookId) {
  // Deliberately routed through the auto-sync path rather than triggerSyncNow:
  // a sidecar write is not user intent to sync, so it must be debounced and
  // coalesced like every other automatic trigger.
  qCInfo(syncCategory) << "SyncService::notifyWorkingTreeDirty:" << p_notebookId;
  onSyncShouldRun(p_notebookId);
}

void SyncService::onSyncShouldRun(const QString &p_notebookId) {
  qCInfo(syncCategory) << "SyncService::onSyncShouldRun: auto-sync triggered for" << p_notebookId;
  if (m_shutDown) {
    qCInfo(syncCategory) << "SyncService::onSyncShouldRun: auto-sync skipped: shutting down for"
                         << p_notebookId;
    return;
  }
  if (!isSyncEnabled(p_notebookId) || !isSyncRegistered(p_notebookId)) {
    qCInfo(syncCategory) << "SyncService::onSyncShouldRun: auto-sync skipped: notebook not ready"
                         << p_notebookId;
    return;
  }
  // Auth-failure circuit-breaker: after N consecutive auth failures, stop
  // auto-syncing to avoid hammering the remote with known-bad credentials on
  // every save tick. Manual triggerSyncNow remains allowed (user intent
  // overrides). Counter resets on successful sync or updateCredentials.
  const int authFailures = m_authFailureCount.value(p_notebookId, 0);
  if (authFailures >= kAuthFailureCircuitThreshold) {
    qCInfo(syncCategory) << "SyncService::onSyncShouldRun: auto-sync skipped: "
                            "auth-failure circuit-breaker open"
                         << p_notebookId << "consecutiveFailures:" << authFailures;
    return;
  }

  const int n = debounceSeconds();
  if (n > 0) {
    armOrIgnoreDebounce(p_notebookId);
    return;
  }

  enqueueAutoSync(p_notebookId);
}

// ---- Reconcile-on-open ------------------------------------------------------

void SyncService::onNotebookAfterOpen(const NotebookOpenEvent &p_event) {
  qCDebug(syncCategory) << "SyncService::onNotebookAfterOpen: notebookId:" << p_event.notebookId;
  reconcileSyncForNotebook(p_event.notebookId);
}

// See AGENTS.md "Startup S6 Sweep" for orphan-keychain cleanup rationale.
void SyncService::onMainWindowAfterStart() {
  if (!m_notebookCoreService) {
    return;
  }
  const QJsonArray notebooks = m_notebookCoreService->listNotebooks();
  qCDebug(syncCategory) << "SyncService::onMainWindowAfterStart: scanning" << notebooks.size()
                        << "notebooks for reconcile";
  for (const QJsonValue &v : notebooks) {
    const QJsonObject nb = v.toObject();
    const QString nbId = nb.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (nbId.isEmpty()) {
      continue;
    }

    // Probe-free idempotent deletion also reaches entries left by a prior
    // process: the existence cache deliberately starts empty on restart.
    if (m_credentialsStore && !isSyncEnabled(nbId) && !isSyncInProgress(nbId)) {
      qCDebug(syncCategory)
          << "SyncService::onMainWindowAfterStart: disabled credential cleanup for" << nbId;
      deleteStoredCredentials(nbId, {});
    }

    reconcileSyncForNotebook(nbId);
  }
}

// See AGENTS.md "Reconcile Semantics" for invariant rationale on the
// precondition-check-after-insert ordering and m_reconcileAttempted lifecycle.
void SyncService::reconcileSyncForNotebook(const QString &p_notebookId) {
  if (m_shutDown || !m_notebookCoreService || !m_credentialsStore ||
      m_reconcileAttempted.contains(p_notebookId) || isSyncInProgress(p_notebookId))
    return;
  const auto cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  if (!cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool())
    return;
  if (hasInterruptedRetirement(p_notebookId)) {
    emit syncFailed(p_notebookId, VXCORE_ERR_INVALID_STATE,
                    tr("Open Sync Info to restore the interrupted sync configuration change."));
    emit reconcileFinished(p_notebookId, VXCORE_ERR_INVALID_STATE);
    return;
  }
  SyncSettings settings;
  settings.m_backend = cfg.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString();
  settings.m_remoteUrl = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
  const auto validationError = validateSyncSettings(settings, false);
  if (!validationError.isEmpty()) {
    const auto code = settings.m_backend.isEmpty() || isSupportedSyncBackend(settings.m_backend)
                          ? VXCORE_ERR_INVALID_PARAM
                          : VXCORE_ERR_UNKNOWN_BACKEND;
    emit syncFailed(p_notebookId, code, validationError);
    emit reconcileFinished(p_notebookId, code);
    return;
  }
  if (isSyncRegistered(p_notebookId))
    return;
  m_reconcileAttempted.insert(p_notebookId);
  m_credentialOperations.insert(p_notebookId);
  auto retrieved = std::make_shared<QMetaObject::Connection>();
  auto failed = std::make_shared<QMetaObject::Connection>();
  const auto disconnect = [retrieved, failed]() {
    QObject::disconnect(*retrieved);
    QObject::disconnect(*failed);
  };
  const auto finish = [this, p_notebookId](VxCoreError code) {
    m_credentialOperations.remove(p_notebookId);
    if (code != VXCORE_OK)
      m_reconcileAttempted.remove(p_notebookId);
    emit reconcileFinished(p_notebookId, code);
    if (code == VXCORE_OK && !m_shutDown)
      maybeTriggerPostReconcile(p_notebookId);
  };
  *retrieved = connect(
      m_credentialsStore, &SyncCredentialsStore::credentialsRetrieved, this,
      [this, p_notebookId, settings, disconnect,
       finish](const QString &id, const SyncCredential &credentials) mutable {
        if (id != p_notebookId)
          return;
        disconnect();
        if (m_shutDown) {
          finish(VXCORE_ERR_CANCELLED);
          return;
        }
        settings.m_credentials = credentials;
        if (!validateSyncSettings(settings, true).isEmpty()) {
          finish(VXCORE_ERR_SYNC_AUTH_FAILED);
          return;
        }
        const auto configJson = buildConfigJson(id, settings);
        const auto credentialsJson = syncCredentialsJson(credentials);
        const auto complete = [this, finish](VxCoreError code) {
          QMetaObject::invokeMethod(this, [finish, code]() { finish(code); }, Qt::QueuedConnection);
        };
        const auto queued = m_workQueue->enqueue(
            id,
            [this, id, configJson, credentialsJson, complete]() {
              SyncOps::enableSync(m_notebookCoreService, id, configJson, credentialsJson,
                                  [complete](VxCoreError code, QString) { complete(code); });
            },
            [complete]() { complete(VXCORE_ERR_CANCELLED); });
        if (queued != SyncWorkQueueManager::EnqueueResult::Accepted)
          finish(VXCORE_ERR_SYNC_IN_PROGRESS);
      });
  *failed = connect(m_credentialsStore, &SyncCredentialsStore::credentialsError, this,
                    [p_notebookId, disconnect, finish](const QString &id, const QString &) {
                      if (id != p_notebookId)
                        return;
                      disconnect();
                      finish(VXCORE_ERR_SYNC_AUTH_FAILED);
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
