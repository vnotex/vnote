#ifndef SYNCSTATECLASSIFIER_H
#define SYNCSTATECLASSIFIER_H

#include <QString>

#include "core/noncopyable.h"

namespace vnotex {

class ServiceLocator;

// Canonical sync state for a notebook. The state is the tuple of:
//   * on-disk JSON sync fields (syncEnabled, syncBackend, syncRemoteUrl)
//   * PAT presence in the OS keychain (via SyncCredentialsStore)
//   * runtime registration in vxcore's states_ map (via SyncService)
//
// See the "Sync State Model" section in the repo root AGENTS.md for the
// authoritative predicate table.
enum class SyncState {
  S0, // Cleanly disabled: !syncEnabled, no backend/url, no PAT, not registered.
  S1, // Partial: enabled, backend=git, URL empty, not registered (PAT optional).
  S2, // Partial: enabled, backend=git, URL set, NO PAT, not registered.
  S3, // Partial: enabled, backend empty, not registered (PAT optional).
  S4, // Partial: enabled, backend=git, URL set, PAT present, NOT registered.
  S5, // Ready: enabled, backend=git, URL set, PAT present, registered.
  S6, // Orphan PAT: !syncEnabled but keychain still has a PAT for this id.
  S7, // S5 + active in-flight sync. (Currently mapped to S5; see classify().)
};

// SyncStateClassifier is a stateless, read-through service that maps a
// notebook id to its current SyncState by querying NotebookCoreService,
// SyncCredentialsStore, and SyncService at call time.
//
// Per Task 8.1 contract: the classifier MUST NOT cache classifications and
// MUST have no side effects. It is NOT a QObject by design.
class SyncStateClassifier : private Noncopyable {
public:
  explicit SyncStateClassifier(ServiceLocator &p_services);

  // Map @p_notebookId to its current SyncState by gathering live predicates
  // from the underlying services. Safe to call from the GUI thread; performs
  // only short metadata queries (no vxcore_sync_* long-running ops).
  SyncState classify(const QString &p_notebookId) const;

  // Pure mapping from a tuple of predicates to a SyncState. Exposed so unit
  // tests can exhaustively cover all 8 states without spinning up vxcore
  // and a keychain. classify() is a thin wrapper that gathers the live
  // predicates then delegates here.
  static SyncState classifyFromPredicates(bool p_syncEnabled, bool p_hasPat, bool p_registered,
                                          const QString &p_backend, const QString &p_remoteUrl);

  // Human-readable short tooltip describing @p_state. Always non-empty.
  QString tooltipFor(SyncState p_state) const;

  // True for S1, S2, S3, S4 (partial / inconsistent configuration).
  bool isPartial(SyncState p_state) const;

  // True for S0, S5, S6 (states that map to a user-actionable button:
  // enable / sync-now / re-enable, respectively).
  bool isActionable(SyncState p_state) const;

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // SYNCSTATECLASSIFIER_H
