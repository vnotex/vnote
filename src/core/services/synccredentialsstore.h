#ifndef SYNCCREDENTIALSSTORE_H
#define SYNCCREDENTIALSSTORE_H

#include <QObject>
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
// The store is stateless: it holds NO PAT values in memory. Each call routes
// directly to the OS keychain.
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

signals:
  void credentialsStored(const QString &p_notebookId);
  void credentialsRetrieved(const QString &p_notebookId, const QString &p_pat);
  void credentialsDeleted(const QString &p_notebookId);
  void credentialsError(const QString &p_notebookId, const QString &p_errorString);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // SYNCCREDENTIALSSTORE_H
