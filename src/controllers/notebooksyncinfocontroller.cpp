#include "notebooksyncinfocontroller.h"

#include <memory>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>

#include <vxcore/vxcore_types.h>

using namespace vnotex;

NotebookSyncInfoController::NotebookSyncInfoController(ServiceLocator &p_services,
                                                       const QString &p_notebookId,
                                                       QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_notebookId(p_notebookId) {
  // Connect once to SyncService signals; filtering by notebookId happens in
  // each slot. Both ends are GUI-thread (SyncService re-emits on the GUI
  // thread); default Qt::AutoConnection resolves to DirectConnection.
  if (auto *syncSvc = m_services.get<SyncService>()) {
    connect(syncSvc, &SyncService::credentialsSetFinished, this,
            &NotebookSyncInfoController::onCredentialsSetFinished);
    connect(syncSvc, &SyncService::disableFinished, this,
            &NotebookSyncInfoController::onDisableFinished);
  }
}

QString NotebookSyncInfoController::notebookName() const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return QString();
  }
  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  return cfg.value(QStringLiteral("name")).toString();
}

QString NotebookSyncInfoController::remoteUrl() const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return QString();
  }
  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  // ADR-8: use the FLAT key, not a nested "sync.remoteUrl".
  return cfg.value(QStringLiteral("syncRemoteUrl")).toString();
}

QString NotebookSyncInfoController::lastSyncTime() const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return QString();
  }
  const qint64 millis = notebookSvc->getLastSyncUtc(m_notebookId);
  if (millis <= 0) {
    return QString();
  }
  return QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(millis), QLocale::ShortFormat);
}

void NotebookSyncInfoController::loadInitialData() {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    emit error(tr("Notebook service not available."));
    return;
  }

  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  const QString name = cfg.value(QStringLiteral("name")).toString();
  const QString url = cfg.value(QStringLiteral("syncRemoteUrl")).toString();
  // Last sync timestamp is per-device and lives in metadata.db (NOT in the
  // notebook config JSON, which would self-sync). Read via vxcore C API.
  const qint64 lastSyncMillis = notebookSvc->getLastSyncUtc(m_notebookId);
  QString lastSync;
  if (lastSyncMillis > 0) {
    lastSync = QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(lastSyncMillis),
                                          QLocale::ShortFormat);
  }

  // Cache the URL so applyChanges() can detect whether the user changed it.
  m_currentRemoteUrl = url;

  emit dataLoaded(name, url, lastSync);
}

bool NotebookSyncInfoController::persistRemoteUrl(const QString &p_newRemoteUrl) {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    emit error(tr("Notebook service not available."));
    return false;
  }

  // Read-modify-write the notebook config. Per ADR-8 the FLAT
  // "syncRemoteUrl" key is the source of truth.
  QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  cfg[QStringLiteral("syncRemoteUrl")] = p_newRemoteUrl;
  const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
  if (!notebookSvc->updateNotebookConfig(m_notebookId, cfgJson)) {
    emit error(tr("Failed to update notebook configuration."));
    return false;
  }
  m_currentRemoteUrl = p_newRemoteUrl;
  return true;
}

void NotebookSyncInfoController::applyChanges(const QString &p_newRemoteUrl,
                                              const QString &p_newPat) {
  // Diff URL against cached value; only write when the user actually changed
  // it. Empty new URL is allowed (clears the field) only if it differs from
  // the cached value.
  bool urlChanged = (p_newRemoteUrl != m_currentRemoteUrl);
  bool urlOk = true;
  if (urlChanged) {
    urlOk = persistRemoteUrl(p_newRemoteUrl);
  }

  // Snapshot the URL write outcome so the credentialsSetFinished slot can
  // combine it with the PAT result before emitting applyComplete.
  m_pendingUrlWriteOk = urlOk;

  if (!p_newPat.isEmpty()) {
    auto *syncSvc = m_services.get<SyncService>();
    if (!syncSvc) {
      emit error(tr("Sync service not available."));
      m_pendingPatUpdate = false;
      emit applyComplete(false);
      return;
    }
    m_pendingPatUpdate = true;
    syncSvc->updateCredentials(m_notebookId, p_newPat);
    return;
  }

  // No PAT update requested; applyComplete fires synchronously based on the
  // URL-write outcome alone.
  m_pendingPatUpdate = false;
  if (urlOk) {
    // URL-only completion path: PAT unchanged, runtime state may be empty
    // for previously-partial notebooks. Trigger init now.
    if (auto *syncSvc = m_services.get<SyncService>()) {
      syncSvc->ensureSyncEnabled(m_notebookId);
    }
  }
  emit applyComplete(urlOk);
}

void NotebookSyncInfoController::disableSync() {
  auto *syncSvc = m_services.get<SyncService>();
  if (!syncSvc) {
    emit error(tr("Sync service not available."));
    return;
  }
  syncSvc->disableSyncForNotebook(m_notebookId);
}

void NotebookSyncInfoController::bootstrapApply(const QString &p_url, const QString &p_pat) {
  // W3.T1: atomic enable on an EXISTING partial notebook (S1/S2/S3/S4 -> S5).
  // Mirrors NewNotebookController::bootstrapSync but does NOT delete the
  // notebook on failure (the notebook already exists with user content; the
  // partial sync config is left in place so the user can retry).
  qCDebug(syncCategory) << "NotebookSyncInfoController::bootstrapApply: start id=" << m_notebookId;

  auto *syncSvc = m_services.get<SyncService>();
  if (!syncSvc) {
    qCWarning(syncCategory)
        << "NotebookSyncInfoController::bootstrapApply: SyncService not available; id="
        << m_notebookId;
    emit error(tr("Sync service not available."));
    emit applyComplete(false);
    return;
  }

  // Capture the notebookId by value so the lambda is independent of `this`
  // member lifetime ordering during Qt's queued-signal delivery.
  const QString notebookId = m_notebookId;

  // One-shot connection: SyncService::enableFinished can fire for any
  // notebook, so we filter by id inside the lambda and disconnect manually
  // after handling our id. Heap-stored Connection mirrors the convention
  // used in SyncService::updateCredentials / disableSyncForNotebook.
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn =
      connect(syncSvc, &SyncService::enableFinished, this,
              [this, conn, syncSvc, notebookId,
               p_url](const QString &p_resultId, VxCoreError p_result, const QString &p_message) {
                if (p_resultId != notebookId) {
                  return;
                }
                QObject::disconnect(*conn);

                qCDebug(syncCategory)
                    << "NotebookSyncInfoController::bootstrapApply: enableFinished result="
                    << static_cast<int>(p_result) << "id=" << notebookId;

                if (p_result == VXCORE_OK) {
                  // Persist the flat ADR-8 syncRemoteUrl key into the
                  // notebook config so a restart finds the notebook in
                  // S5 (sync-ready) instead of S4 (partial).
                  // persistRemoteUrl emits error() on failure but does
                  // NOT block applyComplete(true) — the vxcore enable
                  // step itself succeeded.
                  persistRemoteUrl(p_url);

                  // Idempotent initial sync push (best-effort; runtime
                  // state is now populated, so this won't hit the
                  // chicken-and-egg path).
                  syncSvc->triggerSyncNow(notebookId);

                  emit applyComplete(true);
                  return;
                }

                // Failure path: KEEP the notebook (no closeNotebook, no
                // removeRecursively). The user's notebook content stays
                // intact; only the sync-enable failed. The partial sync
                // config (syncEnabled / syncBackend / empty syncRemoteUrl)
                // remains so the user can retry from the Sync Info dialog.
                qCWarning(syncCategory)
                    << "NotebookSyncInfoController::bootstrapApply: enable failed id=" << notebookId
                    << "result:" << static_cast<int>(p_result) << "message:" << p_message;
                emit error(p_message.isEmpty() ? tr("Failed to enable sync for notebook.")
                                               : p_message);
                emit applyComplete(false);
              });

  // Fire enableSync LAST so the connect is in place before the signal could
  // fire (mirrors NewNotebookController::bootstrapSync line ordering). The
  // PAT is forwarded to SyncService and NEVER logged here (per ADR-9).
  syncSvc->enableSyncForNotebook(notebookId, p_url, p_pat);
}

void NotebookSyncInfoController::onCredentialsSetFinished(const QString &p_notebookId,
                                                          VxCoreError p_result) {
  if (p_notebookId != m_notebookId || !m_pendingPatUpdate) {
    return;
  }
  m_pendingPatUpdate = false;
  const bool patOk = (p_result == VXCORE_OK);
  if (patOk) {
    // URL+PAT completion path: PAT is now in keychain. If runtime state
    // is empty for a previously-partial notebook, trigger init now that
    // reconcile can read the freshly-written PAT.
    if (auto *syncSvc = m_services.get<SyncService>()) {
      syncSvc->ensureSyncEnabled(m_notebookId);
    }
  }
  emit applyComplete(m_pendingUrlWriteOk && patOk);
}

void NotebookSyncInfoController::onDisableFinished(const QString &p_notebookId,
                                                   VxCoreError p_result) {
  if (p_notebookId != m_notebookId) {
    return;
  }
  Q_UNUSED(p_result);
  emit disableComplete(p_notebookId);
}
