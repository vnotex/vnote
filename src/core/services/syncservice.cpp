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
#include <core/services/syncops.h>
#include <core/services/syncworkqueuemanager.h>

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
  // The handler also pops a QMessageBox directly because HookManager guarantees
  // single-threaded (GUI-thread) handler execution.
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
  // T24: SyncWorker thread teardown is gone. Drain any locally-owned
  // SyncWorkQueueManager with a bounded budget. When the ServiceLocator
  // provides a shared instance, main.cpp's aboutToQuit handler shuts it
  // down with the same bounded budget — SyncWorkQueueManager::shutdown is
  // idempotent so the double-call is safe.
  if (m_ownedWorkQueue) {
    m_ownedWorkQueue->shutdown(kShutdownTimeoutMs);
  }

  // Wave 12.2 / F5.9: release any leftover cancellation tokens. After the
  // work queue has drained, no SyncOps callback can be in flight, so freeing
  // is safe.
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

  *storedConn =
      connect(
          m_credentialsStore, &SyncCredentialsStore::credentialsStored, this,
          [this, notebookId, configJson, credsJson, cleanup](const QString &p_storedNotebookId) {
            if (p_storedNotebookId != notebookId) {
              return;
            }
            cleanup();
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
            workQueue->enqueue(
                notebookId, [this, notebookId, configJson, credsJson, notebookSvc]() {
                  SyncOps::enableSync(notebookSvc, notebookId, configJson, credsJson,
                                      [this, notebookId](VxCoreError p_code, QString p_msg) {
                                        QMetaObject::invokeMethod(
                                            this,
                                            [this, notebookId, p_code, p_msg]() {
                                              onWorkerEnableFinished(notebookId, p_code, p_msg);
                                            },
                                            Qt::QueuedConnection);
                                      });
                });
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
          cfg.remove(QLatin1String(vxcore::kJsonKeySyncEnabled));
          cfg.remove(QLatin1String(vxcore::kJsonKeySyncBackend));
          cfg.remove(QLatin1String(vxcore::kJsonKeySyncRemoteUrl));
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

  // T18: route disable through SyncWorkQueueManager (per-notebook serialized
  // executor) instead of the legacy SyncWorker QMetaObject::invokeMethod path.
  // The work body runs on a QThreadPool worker; SyncOps::disableSync invokes
  // the completion callback on that same thread, so we bounce back to the GUI
  // thread via QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) to
  // invoke onWorkerDisableFinished, which preserves the disableFinished signal
  // contract (emitted on the GUI thread, exactly once per call).
  auto *workQueue = m_workQueue;
  NotebookCoreService *notebookSvc = m_notebookCoreService;
  if (!workQueue) {
    qCWarning(syncCategory)
        << "SyncService::disableSyncForNotebook: SyncWorkQueueManager unavailable; falling back"
        << "to synchronous failure for notebookId:" << notebookId;
    QMetaObject::invokeMethod(
        this, [this, notebookId]() { onWorkerDisableFinished(notebookId, VXCORE_ERR_UNKNOWN); },
        Qt::QueuedConnection);
    return;
  }
  workQueue->enqueue(notebookId, [this, notebookId, notebookSvc]() {
    SyncOps::disableSync(notebookSvc, notebookId, [this, notebookId](VxCoreError p_err) {
      QMetaObject::invokeMethod(
          this, [this, notebookId, p_err]() { onWorkerDisableFinished(notebookId, p_err); },
          Qt::QueuedConnection);
    });
  });
}

void SyncService::triggerSyncNow(const QString &p_notebookId) {
  if (m_shutDown) {
    qWarning() << "SyncService::triggerSyncNow: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::triggerSyncNow: notebookId:" << p_notebookId;

  auto *workQueue = m_workQueue;
  NotebookCoreService *notebookSvc = m_notebookCoreService;
  if (!workQueue) {
    qCWarning(syncCategory) << "SyncService::triggerSyncNow: SyncWorkQueueManager unavailable for"
                            << p_notebookId;
    emit syncFailed(p_notebookId, VXCORE_ERR_UNKNOWN,
                    QStringLiteral("SyncWorkQueueManager unavailable"));
    return;
  }

  // T21: per-call cancellation token. Created BEFORE enqueue (so the work
  // lambda can capture it), but only inserted into m_cancellations on
  // EnqueueResult::Accepted. On Coalesced / QueueFull / Rejected the token is
  // freed inline (never enters the map). On Accepted, the token is freed by
  // either SyncService::onSyncFinished (T23: single-source EventBridge path)
  // or the onCancelled callback registered with the queue (drop-from-pending
  // path).
  VxCoreSyncCancellation *token = vxcore_sync_create_cancellation();
  const QString notebookId = p_notebookId;

  auto work = [this, notebookId, notebookSvc, token]() {
    SyncOps::triggerSync(notebookSvc, notebookId, token, [this, notebookId](VxCoreError p_code) {
      // Bounce back to GUI thread to release the
      // cancellation token. SyncService does NOT emit
      // syncFinished from here — EventBridge already
      // observes vxcore's sync.finished event and routes
      // it through onSyncFinished (T23: single-source
      // forwarder for both auto AND manual triggers).
      QMetaObject::invokeMethod(
          this,
          [this, notebookId, p_code]() {
            Q_UNUSED(p_code);
            // T26: in-flight state lives on SyncWorkQueueManager — no
            // local flag to clear. Release the cancellation token.
            VxCoreSyncCancellation *tok = nullptr;
            {
              QMutexLocker locker(&m_cancellationMutex);
              auto it = m_cancellations.find(notebookId);
              if (it != m_cancellations.end()) {
                tok = static_cast<VxCoreSyncCancellation *>(it.value());
                m_cancellations.erase(it);
              }
            }
            if (tok) {
              vxcore_sync_free_cancellation(tok);
            }
          },
          Qt::QueuedConnection);
    });
  };

  auto onCancelled = [this, notebookId, token]() {
    // Fires OUTSIDE the queue mutex when the pending item is dropped via
    // cancelPending(). Free our token and remove the matching map entry.
    {
      QMutexLocker locker(&m_cancellationMutex);
      auto it = m_cancellations.find(notebookId);
      if (it != m_cancellations.end() && it.value() == token) {
        m_cancellations.erase(it);
      }
    }
    if (token) {
      vxcore_sync_free_cancellation(token);
    }
  };

  const auto result = workQueue->enqueue(notebookId, work, onCancelled, QStringLiteral("trigger"));
  switch (result) {
  case SyncWorkQueueManager::EnqueueResult::Accepted: {
    if (token) {
      QMutexLocker locker(&m_cancellationMutex);
      auto it = m_cancellations.find(notebookId);
      if (it != m_cancellations.end() && it.value() != nullptr && it.value() != token) {
        qCWarning(syncCategory)
            << "SyncService::triggerSyncNow: overwriting stale cancellation token for" << notebookId
            << "(prior sync likely still running)";
      }
      m_cancellations.insert(notebookId, token);
    }
    // T21: do NOT emit syncStarted here — EventBridge observes vxcore's
    // sync.started event for both auto and manual triggers (post-T7) and
    // routes it through onSyncStarted (T23). setInProgress is also handled
    // there. We only mark the queue as accepted.
    return;
  }
  case SyncWorkQueueManager::EnqueueResult::Coalesced:
    qCDebug(syncCategory) << "SyncService::triggerSyncNow: trigger coalesced with pending sync for"
                          << notebookId;
    if (token) {
      vxcore_sync_free_cancellation(token);
    }
    // No syncStarted emit — the existing pending sync already emitted.
    return;
  case SyncWorkQueueManager::EnqueueResult::QueueFull:
    qCWarning(syncCategory) << "SyncService::triggerSyncNow: queue full for" << notebookId;
    if (token) {
      vxcore_sync_free_cancellation(token);
    }
    emit syncFailed(p_notebookId, VXCORE_ERR_SYNC_IN_PROGRESS, QStringLiteral("sync queue full"));
    return;
  case SyncWorkQueueManager::EnqueueResult::Rejected:
    qCWarning(syncCategory) << "SyncService::triggerSyncNow: enqueue rejected for" << notebookId;
    if (token) {
      vxcore_sync_free_cancellation(token);
    }
    emit syncFailed(p_notebookId, VXCORE_ERR_SYNC_IN_PROGRESS,
                    QStringLiteral("sync queue rejected"));
    return;
  }
}

void SyncService::cancelSync(const QString &p_notebookId) {
  if (m_shutDown) {
    qWarning() << "SyncService::cancelSync: ignored after shutdown";
    return;
  }

  // T21: first drop any pending (queued, not-yet-running) sync items. Their
  // onCancelled callbacks (registered in triggerSyncNow) free the associated
  // tokens and remove their map entries.
  int dropped = 0;
  if (m_workQueue) {
    dropped = m_workQueue->cancelPending(p_notebookId);
  }
  if (dropped > 0) {
    qCDebug(syncCategory) << "SyncService::cancelSync: dropped" << dropped << "pending sync(s) for"
                          << p_notebookId;
    emit syncCancelled(p_notebookId, /*wasQueued=*/true);
    if (auto *hookMgr = m_services.get<HookManager>()) {
      SyncCancelledEvent event;
      event.notebookId = p_notebookId;
      event.wasQueued = true;
      hookMgr->doAction(HookNames::SyncCancelled, event);
    }
    return;
  }

  // Nothing pending dropped — fall back to cooperative cancel of the in-flight
  // op via its cancellation token (existing path).
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
      SyncCancelledEvent event;
      event.notebookId = p_notebookId;
      event.wasQueued = false;
      hookMgr->doAction(HookNames::SyncCancelled, event);
    }
    return;
  }
  qCDebug(syncCategory) << "SyncService::cancelSync: signalling token for" << p_notebookId;
  // vxcore_sync_cancel is thread-safe (atomic flag inside the token).
  vxcore_sync_cancel(token);

  emit syncCancelled(p_notebookId, /*wasQueued=*/false);

  // F4.5: fire vnote.sync.cancelled AFTER vxcore_sync_cancel returns. No
  // SyncService mutex is held here (snapshot/release done above). Observe-only.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    SyncCancelledEvent event;
    event.notebookId = p_notebookId;
    event.wasQueued = false;
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
void SyncService::bootstrapAndPersist(const QString &p_notebookId, const QString &p_remoteUrl,
                                      const QString &p_pat) {
  if (m_shutDown) {
    qWarning() << "SyncService::bootstrapAndPersist: ignored after shutdown";
    return;
  }
  qCDebug(syncCategory) << "SyncService::bootstrapAndPersist: notebookId:" << p_notebookId;

  const QString notebookId = p_notebookId;
  const QString remoteUrl = p_remoteUrl;

  // One-shot bridge on enableFinished. Filters by notebookId, self-disconnects,
  // then enqueues the persist work (or short-circuits on enable failure).
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn = connect(
      this, &SyncService::enableFinished, this,
      [this, conn, notebookId, remoteUrl](const QString &p_finishedId, VxCoreError p_enResult,
                                          const QString &p_enMsg) {
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
        workQueue->enqueue(notebookId, [this, notebookId, remoteUrl]() {
          QMetaObject::invokeMethod(
              this,
              [this, notebookId, remoteUrl]() {
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
                  cfg[QLatin1String(vxcore::kJsonKeySyncBackend)] = QStringLiteral("git");
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
                  // T20: enqueue initial sync as a third FIFO work item.
                  // Best-effort; ignore its result code (we already committed
                  // success at the bootstrap level).
                  if (wq) {
                    NotebookCoreService *notebookSvc = m_notebookCoreService;
                    wq->enqueue(notebookId, [notebookSvc, notebookId]() {
                      SyncOps::triggerSync(notebookSvc, notebookId, /*p_cancel=*/nullptr,
                                           [](VxCoreError) {});
                    });
                  }
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

                NotebookCoreService *notebookSvc = m_notebookCoreService;
                wq->enqueue(notebookId, [this, notebookId, notebookSvc, origPersistErr,
                                         forceRollbackFail]() {
                  auto onDone = [this, notebookId, origPersistErr](VxCoreError p_disResult) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, notebookId, origPersistErr, p_disResult]() {
                          if (p_disResult != VXCORE_OK) {
                            qCritical()
                                << "SyncService::bootstrapAndPersist: ROLLBACK FAILED for "
                                   "notebook"
                                << notebookId << ":" << vxErrorToString(p_disResult)
                                << "- notebook may be in inconsistent state (vxcore enabled "
                                   "but JSON not persisted). Original persist error "
                                   "preserved.";
                          } else {
                            qCWarning(syncCategory)
                                << "SyncService::bootstrapAndPersist: rollback succeeded for"
                                << notebookId;
                            // PAT was just stored for the enable; the
                            // notebook is being rolled back to clean S0,
                            // so drop the orphan keychain entry too.
                            if (m_credentialsStore) {
                              m_credentialsStore->deleteCredentials(notebookId);
                            }
                          }
                          emit bootstrapAndPersistFinished(notebookId, VXCORE_ERR_UNKNOWN,
                                                           origPersistErr);
                        },
                        Qt::QueuedConnection);
                  };

                  if (forceRollbackFail) {
                    // Test seam: synthesize a disable failure without
                    // touching vxcore.
                    onDone(VXCORE_ERR_UNKNOWN);
                    return;
                  }
                  SyncOps::disableSync(notebookSvc, notebookId, onDone);
                });
              },
              Qt::QueuedConnection);
        });
      });

  enableSyncForNotebook(notebookId, remoteUrl, p_pat);
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
    const QString persistedUrl = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
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
                // T19: route setCredentials through SyncWorkQueueManager (per-notebook
                // serialized executor). The PAT-bearing credsJson is captured by-value
                // in the enqueued lambda and again in the SyncOps::setCredentials call;
                // both copies are destroyed when their respective scopes end. NEVER
                // log credsJson. Completion bounces back to the GUI thread via
                // QueuedConnection so onWorkerCredentialsSetFinished preserves the
                // credentialsSetFinished signal contract (GUI-thread, exactly once).
                auto *workQueue = m_services.get<SyncWorkQueueManager>();
                NotebookCoreService *notebookSvc = m_notebookCoreService;
                if (!workQueue) {
                  qCWarning(syncCategory)
                      << "SyncService::updateCredentials: SyncWorkQueueManager unavailable;"
                      << "falling back to synchronous failure for notebookId:" << notebookId;
                  QMetaObject::invokeMethod(
                      this,
                      [this, notebookId]() {
                        onWorkerCredentialsSetFinished(notebookId, VXCORE_ERR_UNKNOWN);
                      },
                      Qt::QueuedConnection);
                  return;
                }
                workQueue->enqueue(notebookId, [this, notebookId, credsJson, notebookSvc]() {
                  SyncOps::setCredentials(notebookSvc, notebookId, credsJson,
                                          [this, notebookId](VxCoreError p_err) {
                                            QMetaObject::invokeMethod(
                                                this,
                                                [this, notebookId, p_err]() {
                                                  onWorkerCredentialsSetFinished(notebookId, p_err);
                                                },
                                                Qt::QueuedConnection);
                                          });
                });
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

  auto *workQueue = m_workQueue;
  auto *notebookSvc = m_notebookCoreService;
  if (!workQueue) {
    qCWarning(syncCategory) << "SyncService::resolveConflicts: SyncWorkQueueManager unavailable for"
                            << p_notebookId;
    return;
  }

  // T22: enqueue each resolution as its own work item on the per-notebook
  // FIFO queue. coalesceKey "resolve-<filePath>" collapses duplicate clicks
  // for the same path but keeps distinct paths independent.
  for (auto it = p_resolutions.constBegin(); it != p_resolutions.constEnd(); ++it) {
    const QString filePath = it.key();
    const QString resolution = it.value();
    const QString notebookId = p_notebookId;
    workQueue->enqueue(
        notebookId,
        [notebookSvc, notebookId, filePath, resolution]() {
          SyncOps::resolveConflict(notebookSvc, notebookId, filePath, resolution,
                                   [notebookId, filePath](VxCoreError p_code) {
                                     if (p_code != VXCORE_OK) {
                                       qCWarning(syncCategory)
                                           << "SyncService::resolveConflicts: resolve failed for"
                                           << notebookId << filePath << "code:" << p_code;
                                     }
                                   });
        },
        /*onCancelled=*/nullptr, QStringLiteral("resolve-") + filePath);
  }

  // T22: trailing triggerSync enqueued AFTER all resolves. Same coalesceKey
  // "trigger" as SyncService::triggerSyncNow so a concurrent manual click
  // collapses into this trailing run. Per-notebook FIFO guarantees this
  // trigger executes only after every queued resolve has completed.
  const QString notebookId = p_notebookId;
  workQueue->enqueue(
      notebookId,
      [notebookSvc, notebookId]() {
        SyncOps::triggerSync(
            notebookSvc, notebookId, /*p_cancel=*/nullptr, [notebookId](VxCoreError p_code) {
              if (p_code != VXCORE_OK) {
                qCDebug(syncCategory) << "SyncService::resolveConflicts: trailing trigger result"
                                      << notebookId << "code:" << p_code;
              }
            });
      },
      /*onCancelled=*/nullptr, QStringLiteral("trigger"));
}

bool SyncService::isSyncInProgress(const QString &p_notebookId) const {
  return m_workQueue ? m_workQueue->inFlightState(p_notebookId).running : false;
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

// ---- Worker -> SyncService forwarders --------------------------------------
// T23: syncStarted / syncFinished / syncFailed / conflictsDetected forwarders
// were removed; EventBridge is now the single source for those signals.
// The enable / disable / credentials forwarders remain (vxcore does not emit
// events for those today; T24 will remove them with the worker).

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

// ---- Sync lifecycle forwarders (from EventBridge) --------------------------
// T23: previously split into onWorkerSync* (worker source) and onAutoSync*
// (EventBridge source). After T7 made vxcore emit lifecycle events for BOTH
// manual and auto triggers, EventBridge is the only source — these slots
// handle every sync (manual + auto + initial-on-enable).

void SyncService::onSyncStarted(const QString &p_notebookId) {
  if (m_shutDown)
    return;
  // T26: in-flight state is tracked by SyncWorkQueueManager via its work
  // item lifecycle; no separate flag to flip here.
  emit syncStarted(p_notebookId);
}

void SyncService::onSyncFinished(const QString &p_notebookId, VxCoreError p_result) {
  if (m_shutDown)
    return;
  qCDebug(syncCategory) << "SyncService::onSyncFinished: notebookId:" << p_notebookId
                        << "result:" << static_cast<int>(p_result)
                        << "message:" << vxErrorToString(p_result);
  // T26: in-flight state is tracked by SyncWorkQueueManager via its work
  // item lifecycle; no separate flag to clear here.

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

  if (p_result != VXCORE_OK) {
    emit syncFailed(p_notebookId, p_result, vxErrorToString(p_result));
  }
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
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("conflictCount")] = p_files.size();
    hookMgr->doAction(HookNames::SyncConflictDetected, args);
  }
  emit conflictsDetected(p_notebookId, p_files);
}

// T31: auto-sync route. EventBridge::syncShouldRun -> here -> enqueue
// SyncOps::triggerSync (with NULL cancellation token; auto path is
// fire-and-forget). Best-effort: queue overflow / coalesce are silent —
// do NOT emit syncFailed (auto-sync is opportunistic).
void SyncService::onSyncShouldRun(const QString &p_notebookId) {
  if (m_shutDown) {
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: auto-sync skipped: shutting down for"
                          << p_notebookId;
    return;
  }
  if (!isSyncEnabled(p_notebookId) || !isSyncRegistered(p_notebookId)) {
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: auto-sync skipped: notebook not ready"
                          << p_notebookId;
    return;
  }
  auto *workQueue = m_workQueue;
  if (!workQueue) {
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: SyncWorkQueueManager unavailable for"
                          << p_notebookId;
    return;
  }

  NotebookCoreService *notebookSvc = m_notebookCoreService;
  const QString notebookId = p_notebookId;
  auto work = [notebookSvc, notebookId]() {
    SyncOps::triggerSync(notebookSvc, notebookId, /*p_cancel=*/nullptr,
                         /*onFinished=*/[](VxCoreError) {
                           // No-op: EventBridge delivers lifecycle signals
                           // (sync.started / sync.finished) to onSyncStarted /
                           // onSyncFinished, which re-emit the public signals.
                         });
  };

  const auto result =
      workQueue->enqueue(notebookId, work, /*onCancelled=*/nullptr, QStringLiteral("trigger"));
  switch (result) {
  case SyncWorkQueueManager::EnqueueResult::Accepted:
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: auto-sync enqueued for" << notebookId;
    return;
  case SyncWorkQueueManager::EnqueueResult::Coalesced:
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: auto-sync coalesced for" << notebookId;
    return;
  case SyncWorkQueueManager::EnqueueResult::QueueFull:
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: auto-sync dropped (queue full) for"
                          << notebookId;
    return;
  case SyncWorkQueueManager::EnqueueResult::Rejected:
    qCDebug(syncCategory) << "SyncService::onSyncShouldRun: auto-sync rejected for" << notebookId;
    return;
  }
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

// See AGENTS.md "Reconcile Semantics" for invariant rationale on the
// precondition-check-after-insert ordering and m_reconcileAttempted lifecycle.
void SyncService::reconcileSyncForNotebook(const QString &p_notebookId) {
  qCDebug(syncCategory) << "SyncService::reconcileSyncForNotebook: ENTRY notebookId:"
                        << p_notebookId << "shutDown:" << m_shutDown
                        << "hasNotebookCoreService:" << (m_notebookCoreService != nullptr)
                        << "hasCredentialsStore:" << (m_credentialsStore != nullptr);
  if (m_shutDown || !m_notebookCoreService || !m_credentialsStore) {
    return;
  }

  const QJsonObject cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  const bool diskEnabled = cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool();
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

  const QString backend = cfg.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString();
  const QString remoteUrl = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
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

                // T24: route reconcile enable through SyncWorkQueueManager +
                // SyncOps instead of the removed SyncWorker. Best-effort:
                // completion result is folded into reconcileFinished below
                // (we still report VXCORE_OK to indicate dispatch succeeded,
                // mirroring the pre-T24 behavior).
                auto *workQueue = m_workQueue;
                NotebookCoreService *notebookSvc = m_notebookCoreService;
                if (workQueue) {
                  const QString nbId = p_notebookId;
                  workQueue->enqueue(nbId, [notebookSvc, nbId, configJson, credentialsJson]() {
                    SyncOps::enableSync(notebookSvc, nbId, configJson, credentialsJson,
                                        [](VxCoreError, QString) {});
                  });
                } else {
                  qCWarning(syncCategory) << "SyncService::reconcileSyncForNotebook: "
                                             "SyncWorkQueueManager unavailable for"
                                          << p_notebookId;
                }
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
