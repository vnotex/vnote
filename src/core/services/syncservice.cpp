#include "syncservice.h"

#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncworker.h>

using namespace vnotex;

namespace {

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
  const bool ok = m_thread->wait(30000);
  if (!ok) {
    qWarning() << "SyncService shutdown timed out after 30s; terminating worker thread";
    m_thread->terminate();
    m_thread->wait(1000);
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
  qDebug() << "SyncService::enableSyncForNotebook: notebookId:" << p_notebookId;

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
  qDebug() << "SyncService::disableSyncForNotebook: notebookId:" << p_notebookId;

  const QString notebookId = p_notebookId;

  // After the worker reports disableFinished for THIS notebook, delete the
  // keychain entry. One-shot connection.
  auto *conn = new QMetaObject::Connection;
  *conn = connect(this, &SyncService::disableFinished, this,
                  [this, notebookId, conn](const QString &p_finishedNotebookId, VxCoreError) {
                    if (p_finishedNotebookId != notebookId) {
                      return;
                    }
                    QObject::disconnect(*conn);
                    delete conn;
                    m_credentialsStore->deleteCredentials(notebookId);
                  });

  QMetaObject::invokeMethod(m_worker, "disableSync", Qt::QueuedConnection,
                            Q_ARG(QString, notebookId));
}

void SyncService::triggerSyncNow(const QString &p_notebookId) {
  if (m_shutDown) {
    qWarning() << "SyncService::triggerSyncNow: ignored after shutdown";
    return;
  }
  qDebug() << "SyncService::triggerSyncNow: notebookId:" << p_notebookId;
  QMetaObject::invokeMethod(m_worker, "triggerSync", Qt::QueuedConnection,
                            Q_ARG(QString, p_notebookId));
}

void SyncService::updateCredentials(const QString &p_notebookId, const QString &p_newPat) {
  if (m_shutDown) {
    qWarning() << "SyncService::updateCredentials: ignored after shutdown";
    return;
  }
  qDebug() << "SyncService::updateCredentials: notebookId:" << p_notebookId;

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
  qDebug() << "SyncService::resolveConflicts: notebookId:" << p_notebookId
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
  return cfg.value(QStringLiteral("syncEnabled")).toBool();
}

QString SyncService::lastSyncTime(const QString &p_notebookId) const {
  if (!m_notebookCoreService) {
    return QString();
  }
  const QJsonObject cfg = m_notebookCoreService->getNotebookConfig(p_notebookId);
  return cfg.value(QStringLiteral("lastSyncIso")).toString();
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
  setInProgress(p_notebookId, false);
  emit syncFinished(p_notebookId, p_result);
}

void SyncService::onWorkerSyncFailed(const QString &p_notebookId, VxCoreError p_code,
                                     const QString &p_message) {
  emit syncFailed(p_notebookId, p_code, p_message);
}

void SyncService::onWorkerConflictsDetected(const QString &p_notebookId,
                                            const QStringList &p_conflictFiles) {
  emit conflictsDetected(p_notebookId, p_conflictFiles);
}

void SyncService::onWorkerEnableFinished(const QString &p_notebookId, VxCoreError p_result,
                                         const QString &p_message) {
  qDebug() << "SyncService::onWorkerEnableFinished: notebookId:" << p_notebookId
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
