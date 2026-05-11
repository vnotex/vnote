#include "notebooksyncinfocontroller.h"

#include <QJsonDocument>
#include <QJsonObject>

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
  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  return cfg.value(QStringLiteral("lastSyncIso")).toString();
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
  const QString lastSync = cfg.value(QStringLiteral("lastSyncIso")).toString();

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
