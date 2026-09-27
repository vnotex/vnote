#include "syncstateclassifier.h"

#include <QCoreApplication>
#include <QJsonObject>

#include "core/servicelocator.h"
#include "notebookcoreservice.h"
#include "sync/sync_json_keys.h"
#include "synccredentialsstore.h"
#include "syncservice.h"

namespace vnotex {

SyncStateClassifier::SyncStateClassifier(ServiceLocator &p_services) : m_services(p_services) {}

SyncState SyncStateClassifier::classifyFromPredicates(bool p_syncEnabled, bool p_hasCredentials,
                                                      bool p_registered, const QString &p_backend,
                                                      const QString &p_remoteUrl) {
  if (!p_syncEnabled)
    return p_hasCredentials ? SyncState::S6 : SyncState::S0;
  if (!isSupportedSyncBackend(p_backend))
    return SyncState::S3;
  if (p_remoteUrl.isEmpty())
    return SyncState::S1;
  if (!p_hasCredentials)
    return SyncState::S2;
  return p_registered ? SyncState::S5 : SyncState::S4;
}

SyncState SyncStateClassifier::classify(const QString &p_notebookId) const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  auto *syncSvc = m_services.get<SyncService>();
  auto *credentials = m_services.get<SyncCredentialsStore>();

  // Defensive default if a service is missing: report S0 so callers don't
  // crash. In production, all three services are registered by main.cpp.
  if (!notebookSvc || !syncSvc || !credentials) {
    return SyncState::S0;
  }

  const QJsonObject cfg = notebookSvc->getNotebookConfig(p_notebookId);
  const bool syncEnabled = cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool();
  const QString backend = cfg.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString();
  const QString remoteUrl = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();

  const bool hasCredentials = credentials->hasCredentials(p_notebookId);
  const bool registered = syncSvc->isSyncRegistered(p_notebookId);
  const auto state =
      classifyFromPredicates(syncEnabled, hasCredentials, registered, backend, remoteUrl);
  return state == SyncState::S5 && syncSvc->isSyncInProgress(p_notebookId) ? SyncState::S7 : state;
}

QString SyncStateClassifier::tooltipFor(SyncState p_state) const {
#define VX_TR(text) QCoreApplication::translate("SyncStateClassifier", text)
  switch (p_state) {
  case SyncState::S0:
    return VX_TR("Sync is disabled for this notebook");
  case SyncState::S1:
    return VX_TR("Sync is partially configured: remote URL is missing");
  case SyncState::S2:
    return VX_TR("Sync is partially configured: missing credentials");
  case SyncState::S3:
    return VX_TR("Sync is partially configured: backend is missing or unsupported");
  case SyncState::S4:
    return VX_TR("Sync is configured on disk but not yet active. Reopen the "
                 "notebook to activate");
  case SyncState::S5:
    return VX_TR("Sync is ready");
  case SyncState::S6:
    return VX_TR("Orphan credentials detected: sync is disabled but stored "
                 "credentials remain. Re-enable sync or remove the credentials");
  case SyncState::S7:
    return VX_TR("Sync is in progress");
  }
  return VX_TR("Unknown sync state");
#undef VX_TR
}

bool SyncStateClassifier::isPartial(SyncState p_state) const {
  switch (p_state) {
  case SyncState::S1:
  case SyncState::S2:
  case SyncState::S3:
  case SyncState::S4:
    return true;
  default:
    return false;
  }
}

bool SyncStateClassifier::isActionable(SyncState p_state) const {
  switch (p_state) {
  case SyncState::S0: // enable
  case SyncState::S5: // sync-now
  case SyncState::S6: // re-enable
    return true;
  default:
    return false;
  }
}

} // namespace vnotex
