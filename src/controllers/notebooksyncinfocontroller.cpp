#include "notebooksyncinfocontroller.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
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
