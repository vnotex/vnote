#ifndef SYNCCREDENTIALSSTORE_H
#define SYNCCREDENTIALSSTORE_H

#include <QObject>
#include <QSet>
#include <QString>

#include "core/noncopyable.h"

namespace vnotex {

class ServiceLocator;

// SyncCredentialsStore is a thin async wrapper around QtKeychain for storing
// per-notebook git sync Personal Access Tokens (PATs).
//
// Per ADR-9, this store has NO plaintext fallback. When QtKeychain is
// unavailable (built with VNOTE_USE_KEYCHAIN=OFF, or runtime keychain init
// failure), every public method emits credentialsError(notebookId,
// "secure-keychain-unavailable") instead of falling back to plaintext storage.
//
// All public methods are asynchronous: they return immediately and emit a
// completion signal (credentialsStored / credentialsRetrieved /
// credentialsDeleted) or credentialsError on the caller's thread on the next
// event-loop tick.
//
// PAT values are never held in memory. Each async call routes directly to the
// OS keychain. Only a lightweight existence cache (notebook IDs only, no PATs)
// is maintained to support synchronous hasCredentials() probes by the UI.
class SyncCredentialsStore : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // @p_services: Reference to ServiceLocator (held but not currently used;
  //   reserved for future use, e.g., logging policy or test seam).
  // @p_parent: Optional QObject parent.
  explicit SyncCredentialsStore(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Compose the per-notebook keychain key. Public for test introspection.
  static QString keychainKey(const QString &p_notebookId);

  // Service name used as the QtKeychain "service" namespace.
  static const QString &serviceName();

  // SYNCHRONOUS existence check intended for UI classifier paint paths
  // (W2.T0 / W4.T1 of the sync-completion-flow-overhaul plan).
  //
  // Returns last-known cached existence; the cache is updated by the store's
  // own completion signals:
  //   - credentialsStored      -> add to cache
  //   - credentialsRetrieved   -> add to cache (successful probe)
  //   - credentialsDeleted     -> remove from cache
  //   - credentialsError whose error string contains "not found"
  //                            -> remove from cache (best effort)
  //
  // For an authoritative check, call retrieveCredentials() and observe the
  // credentialsRetrieved / credentialsError-with-"not found" signals. Use
  // hasCredentials() ONLY for UI hints (e.g., classifier badges) where a
  // stale-but-recent answer is acceptable.
  bool hasCredentials(const QString &p_notebookId) const;

public slots:
  // Asynchronously store a PAT for the given notebook.
  // Emits credentialsStored on success, credentialsError on failure.
  void storeCredentials(const QString &p_notebookId, const QString &p_pat);

  // Asynchronously retrieve the PAT for the given notebook.
  // Emits credentialsRetrieved on success (with the PAT string), or
  // credentialsError on failure (including not-found and backend errors).
  void retrieveCredentials(const QString &p_notebookId);

  // Asynchronously delete the PAT for the given notebook.
  // Emits credentialsDeleted on success, credentialsError on failure.
  void deleteCredentials(const QString &p_notebookId);

  // Best-effort refresh of the in-memory existence cache.
  //
  // QtKeychain exposes NO enumerate API, so this method cannot rebuild the
  // cache from disk by itself. It is provided as a hook for future keychain
  // backends that support enumeration, and as a no-op safety call sites can
  // invoke after suspected external mutation. The cache continues to update
  // automatically through the completion signals listed in hasCredentials().
  void refreshKnownIds();

signals:
  void credentialsStored(const QString &p_notebookId);
  void credentialsRetrieved(const QString &p_notebookId, const QString &p_pat);
  void credentialsDeleted(const QString &p_notebookId);
  void credentialsError(const QString &p_notebookId, const QString &p_errorString);

private:
  ServiceLocator &m_services;

  // Lightweight existence cache (notebook IDs only; no PAT values).
  // mutable so the const hasCredentials() method may return references in
  // future overloads without losing the option to lazily compact the set.
  mutable QSet<QString> m_knownCredentialIds;
};

} // namespace vnotex

#endif // SYNCCREDENTIALSSTORE_H
