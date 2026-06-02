#ifndef TESTS_KEYCHAIN_GUARD_H
#define TESTS_KEYCHAIN_GUARD_H

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QtTest/QtTest>

#include <core/services/synccredentialsstore.h>

namespace tests {

// Tracks notebook IDs written to a real SyncCredentialsStore during a test
// and synchronously deletes them on cleanup() to prevent OS keychain pollution
// (e.g. Windows Credential Manager entries `notebook_sync_pat_<id>`).
//
// Inherits QObject WITHOUT Q_OBJECT: only used as a connection receiver via
// lambdas (no own signals/slots). tests/helpers is an INTERFACE library; headers
// here are NOT processed by AUTOMOC. Q_OBJECT here would silently miss its moc.
//
// NEVER touches PAT values. Only observes credentialsStored(QString) which
// carries the notebook ID, NOT the PAT.
class KeychainGuard : public QObject {
public:
  explicit KeychainGuard(vnotex::SyncCredentialsStore *p_store, QObject *p_parent = nullptr)
      : QObject(p_parent), m_store(p_store) {
    if (m_store) {
      QObject::connect(m_store.data(), &vnotex::SyncCredentialsStore::credentialsStored, this,
                       [this](const QString &p_id) { m_trackedIds.insert(p_id); });
    }
  }

  ~KeychainGuard() override {
    if (!m_trackedIds.isEmpty() && !m_store.isNull()) {
      qWarning() << "KeychainGuard destroyed with" << m_trackedIds.size()
                 << "pending IDs — call cleanup() explicitly before service teardown";
      cleanup();
    }
  }

  // Manually register a notebook ID to be cleaned up.
  // Use when a test writes indirectly (e.g. via SyncService::enableSyncForNotebook)
  // and the credentialsStored signal observation may race.
  void track(const QString &p_notebookId) { m_trackedIds.insert(p_notebookId); }

  // Synchronously delete all tracked notebook IDs from the OS keychain.
  // Each delete waits at most p_perDeleteTimeoutMs. Treats "not found" and
  // "secure-keychain-unavailable" errors as terminal success outcomes.
  // Tolerant on timeout and CI unavailability; emits only qDebug for diagnostics.
  void cleanup(int p_perDeleteTimeoutMs = 5000) {
    if (m_store.isNull()) {
      m_trackedIds.clear();
      return;
    }
    const auto ids = m_trackedIds; // snapshot
    for (const QString &id : ids) {
      waitForDelete(id, p_perDeleteTimeoutMs);
    }
    m_trackedIds.clear();
  }

private:
  // Returns true on confirmed cleanup outcome (deleted, not-found, or
  // keychain-unavailable). False on timeout — logged as qDebug but not fatal.
  bool waitForDelete(const QString &p_id, int p_timeoutMs) {
    QSignalSpy deletedSpy(m_store.data(), &vnotex::SyncCredentialsStore::credentialsDeleted);
    QSignalSpy errorSpy(m_store.data(), &vnotex::SyncCredentialsStore::credentialsError);
    m_store->deleteCredentials(p_id);

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < p_timeoutMs) {
      // Check if any matching completion has arrived.
      if (matched(deletedSpy, errorSpy, p_id)) {
        return true;
      }
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QTest::qWait(10);
    }
    // Final drain
    if (matched(deletedSpy, errorSpy, p_id)) {
      return true;
    }
    qDebug() << "KeychainGuard: cleanup timeout for id" << p_id;
    return false;
  }

  // Checks if either spy contains a matching completion signal for p_id.
  // Treats both credentialsDeleted and certain credentialsError outcomes as success.
  static bool matched(QSignalSpy &p_deleted, QSignalSpy &p_error, const QString &p_id) {
    for (const auto &args : p_deleted) {
      if (!args.isEmpty() && args.at(0).toString() == p_id) {
        return true;
      }
    }
    for (const auto &args : p_error) {
      if (args.size() < 2 || args.at(0).toString() != p_id) {
        continue;
      }
      const QString errStr = args.at(1).toString();
      // Treat "not found" as a successful cleanup outcome (key was absent).
      if (errStr.contains(QLatin1String("not found"), Qt::CaseInsensitive)) {
        return true;
      }
      // Treat keychain-unavailable as a successful cleanup outcome (backend offline).
      if (errStr == QLatin1String("secure-keychain-unavailable")) {
        return true;
      }
      // Other errors: keep waiting in case the real delete still arrives.
    }
    return false;
  }

  QPointer<vnotex::SyncCredentialsStore> m_store;
  QSet<QString> m_trackedIds;
};

} // namespace tests

#endif // TESTS_KEYCHAIN_GUARD_H
