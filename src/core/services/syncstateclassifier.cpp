#include "syncstateclassifier.h"

#include <QCoreApplication>
#include <QJsonObject>

#include "core/servicelocator.h"
#include "notebookcoreservice.h"
#include "synccredentialsstore.h"
#include "syncservice.h"

namespace vnotex {

namespace {
constexpr const char *kSyncEnabled = "syncEnabled";
constexpr const char *kSyncBackend = "syncBackend";
constexpr const char *kSyncRemoteUrl = "syncRemoteUrl";
constexpr const char *kBackendGit = "git";
} // namespace

SyncStateClassifier::SyncStateClassifier(ServiceLocator &p_services) : m_services(p_services) {}

SyncState SyncStateClassifier::classifyFromPredicates(bool p_syncEnabled, bool p_hasPat,
                                                      bool p_registered, const QString &p_backend,
                                                      const QString &p_remoteUrl) {
  // S0 — cleanly disabled. No PAT, not registered, no flat sync fields.
  if (!p_syncEnabled && !p_hasPat && !p_registered) {
    return SyncState::S0;
  }

  // S6 — orphan PAT: disabled on disk but keychain still has a credential.
  if (!p_syncEnabled && p_hasPat) {
    return SyncState::S6;
  }

  // From here on, p_syncEnabled is true (the cleanly-disabled / orphan branches
  // above are the only non-enabled outcomes).

  // S7 / S5 — fully configured. S7 ("active sync") additionally requires a
  // runtime in-flight probe which is intentionally NOT performed here; the
  // classifier reports S5 in that case and a higher-level consumer can
  // overlay the "in-progress" badge using SyncService::isSyncInProgress().
  // See Task 8.6 doc work for the badge composition policy.
  if (p_registered && p_backend == QLatin1String(kBackendGit) && !p_remoteUrl.isEmpty() &&
      p_hasPat) {
    return SyncState::S5;
  }

  // S3 — enabled but backend is empty (config never specified a backend).
  if (p_backend.isEmpty()) {
    return SyncState::S3;
  }

  // From here on, backend == "git" and we are not yet registered.

  // S1 — enabled, backend=git, but URL is empty.
  if (p_remoteUrl.isEmpty()) {
    return SyncState::S1;
  }

  // S2 — enabled, backend=git, URL set, NO PAT.
  if (!p_hasPat) {
    return SyncState::S2;
  }

  // S4 — enabled, backend=git, URL set, PAT present, NOT registered.
  return SyncState::S4;
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
  const bool syncEnabled = cfg.value(QLatin1String(kSyncEnabled)).toBool();
  const QString backend = cfg.value(QLatin1String(kSyncBackend)).toString();
  const QString remoteUrl = cfg.value(QLatin1String(kSyncRemoteUrl)).toString();

  const bool hasPat = credentials->hasCredentials(p_notebookId);
  const bool registered = syncSvc->isSyncRegistered(p_notebookId);

  return classifyFromPredicates(syncEnabled, hasPat, registered, backend, remoteUrl);
}

QString SyncStateClassifier::tooltipFor(SyncState p_state) const {
#define VX_TR(text) QCoreApplication::translate("SyncStateClassifier", text)
  switch (p_state) {
  case SyncState::S0:
    return VX_TR("Sync is disabled for this notebook.");
  case SyncState::S1:
    return VX_TR("Sync is partially configured: remote URL is missing.");
  case SyncState::S2:
    return VX_TR("Sync is partially configured: missing credentials.");
  case SyncState::S3:
    return VX_TR("Sync is partially configured: no backend selected.");
  case SyncState::S4:
    return VX_TR("Sync is configured on disk but not yet active. Reopen the "
                 "notebook to activate.");
  case SyncState::S5:
    return VX_TR("Sync is ready.");
  case SyncState::S6:
    return VX_TR("Orphan credentials detected: sync is disabled but a stored "
                 "token remains. Re-enable sync or wipe the token.");
  case SyncState::S7:
    return VX_TR("Sync is in progress.");
  }
  return VX_TR("Unknown sync state.");
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
