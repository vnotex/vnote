#include "synccredentialsstore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QStringLiteral>
#include <QThread>
#include <QtGlobal>

#include <sync/sync_json_keys.h>

#ifdef VNOTE_KEYCHAIN_AVAILABLE
#include <keychain.h>
#endif

namespace vnotex {

namespace {

// Static error string emitted when QtKeychain is not available at build or
// runtime. The string is part of the public contract: T14 (bootstrap path) and
// upstream UI catch this exact token to surface a user-facing message.
const char *const c_keychainUnavailableError = "secure-keychain-unavailable";
const char *const c_invalidCredentialsError = "Invalid stored sync credentials.";
const char *const c_credentialEnvelopePrefix = "vnote-sync-credentials-v1\n";

bool validCredentials(const SyncCredential &p_credentials) {
  return isSupportedSyncBackend(p_credentials.m_backend) && !p_credentials.m_secret.isEmpty() &&
         (!isPasswordSyncBackend(p_credentials.m_backend) || !p_credentials.m_username.isEmpty());
}

#ifdef VNOTE_KEYCHAIN_AVAILABLE
QString encodeCredentials(const SyncCredential &p_credentials) {
  if (p_credentials.m_backend == QLatin1String("git"))
    return p_credentials.m_secret;
  QJsonObject envelope;
  envelope[QLatin1String(vxcore::kJsonKeyBackend)] = p_credentials.m_backend;
  envelope[QLatin1String(vxcore::kJsonKeyUsername)] = p_credentials.m_username;
  envelope[QStringLiteral("secret")] = p_credentials.m_secret;
  return QString::fromLatin1(c_credentialEnvelopePrefix) +
         QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

bool decodeCredentials(const QString &p_value, SyncCredential &p_credentials) {
  const auto prefix = QString::fromLatin1(c_credentialEnvelopePrefix);
  if (!p_value.startsWith(QLatin1String("vnote-sync-credentials-"))) {
    p_credentials = {QStringLiteral("git"), QString(), p_value};
    return validCredentials(p_credentials);
  }
  if (!p_value.startsWith(prefix))
    return false;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(p_value.mid(prefix.size()).toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    return false;
  const auto envelope = document.object();
  const auto backend = envelope.value(QLatin1String(vxcore::kJsonKeyBackend));
  const auto username = envelope.value(QLatin1String(vxcore::kJsonKeyUsername));
  const auto secret = envelope.value(QStringLiteral("secret"));
  if (envelope.size() != 3 || !backend.isString() || !username.isString() || !secret.isString() ||
      !isPasswordSyncBackend(backend.toString()))
    return false;
  p_credentials = {backend.toString(), username.toString(), secret.toString()};
  return validCredentials(p_credentials);
}
#endif

#ifndef VNOTE_KEYCHAIN_AVAILABLE
// Log the unavailability warning ONCE per process to avoid log spam when
// multiple notebooks attempt sync.
void logKeychainUnavailableOnce() {
  static bool s_logged = false;
  if (!s_logged) {
    s_logged = true;
    qWarning() << "QtKeychain unavailable; sync features will be disabled "
                  "until keychain backend is installed";
  }
}
#endif

} // namespace

SyncCredentialsStore::SyncCredentialsStore(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  qRegisterMetaType<SyncCredential>("SyncCredential");
  qRegisterMetaType<SyncCredential>("vnotex::SyncCredential");
  // Maintain the in-memory existence cache by listening to the store's own
  // completion signals. Connections are made in the constructor, BEFORE any
  // caller can attach QSignalSpy / external slots, so cache updates always
  // run before observer slots in single-threaded scenarios (signal/slot
  // delivery order follows connection order).
  //
  // The cache holds notebook IDs only — never PAT values.
  connect(this, &SyncCredentialsStore::credentialsStored, this,
          [this](const QString &p_notebookId) { m_knownCredentialIds.insert(p_notebookId); });
  connect(this, &SyncCredentialsStore::credentialsRetrieved, this,
          [this](const QString &p_notebookId, const SyncCredential &) {
            m_knownCredentialIds.insert(p_notebookId);
          });
  connect(this, &SyncCredentialsStore::credentialsDeleted, this,
          [this](const QString &p_notebookId) { m_knownCredentialIds.remove(p_notebookId); });
  connect(this, &SyncCredentialsStore::credentialsError, this,
          [this](const QString &p_notebookId, const QString &p_errorString) {
            // QtKeychain reports missing entries with platform-specific strings
            // that consistently contain the phrase "not found" (libsecret,
            // Windows Credential Manager, and macOS Security framework all do).
            // Treat that as authoritative proof the entry is gone; leave the
            // cache untouched for any other error (transient backend issues
            // must not produce false negatives).
            if (p_errorString.contains(QLatin1String("not found"), Qt::CaseInsensitive)) {
              m_knownCredentialIds.remove(p_notebookId);
            }
          });
}

bool SyncCredentialsStore::hasCredentials(const QString &p_notebookId) const {
  return m_knownCredentialIds.contains(p_notebookId);
}

void SyncCredentialsStore::refreshKnownIds() {
  // QtKeychain has no enumerate API; this is intentionally a no-op kept as a
  // hook for future backends that support enumeration. The cache continues to
  // self-maintain through completion signals (see constructor).
}

const QString &SyncCredentialsStore::serviceName() {
  static const QString s_service = QStringLiteral("VNote");
  return s_service;
}

QString SyncCredentialsStore::keychainKey(const QString &p_notebookId) {
  return QStringLiteral("notebook_sync_pat_") + p_notebookId;
}

void SyncCredentialsStore::storeCredentials(const QString &p_notebookId,
                                            const SyncCredential &p_credentials) {
  // Create and start jobs on the store's thread, including calls from
  // NotebookAfterClose/AfterOpen hooks running on a QtConcurrent worker.
  if (thread() != QThread::currentThread()) {
    const QString notebookId = p_notebookId;
    const SyncCredential credentials = p_credentials;
    QMetaObject::invokeMethod(
        this, [this, notebookId, credentials]() { storeCredentials(notebookId, credentials); },
        Qt::QueuedConnection);
    return;
  }
  if (!validCredentials(p_credentials) ||
      (p_credentials.m_backend == QLatin1String("git") &&
       p_credentials.m_secret.startsWith(QLatin1String("vnote-sync-credentials-")))) {
    QMetaObject::invokeMethod(
        this,
        [this, p_notebookId]() {
          emit credentialsStoreError(p_notebookId, QString::fromLatin1(c_invalidCredentialsError));
        },
        Qt::QueuedConnection);
    return;
  }
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  // Native keychain operations cannot be cancelled by deleting their job:
  // Apple's backend borrows its data until the main-queue completion arrives.
  // Leave jobs unparented with QtKeychain's default auto-delete ownership so
  // they survive store destruction. The receiver context disconnects our
  // callback when the store dies; QtKeychain still finishes and deletes the job.
  auto *job = new QKeychain::WritePasswordJob(serviceName());
  job->setInsecureFallback(false);
  job->setKey(keychainKey(p_notebookId));
  job->setTextData(encodeCredentials(p_credentials));

  const QString notebookId = p_notebookId;
  connect(job, &QKeychain::Job::finished, this, [this, job, notebookId](QKeychain::Job *) {
    if (job->error() == QKeychain::NoError) {
      emit credentialsStored(notebookId);
    } else {
      emit credentialsStoreError(notebookId, job->errorString());
    }
  });

  job->start();
#else
  logKeychainUnavailableOnce();
  const QString notebookId = p_notebookId;
  QMetaObject::invokeMethod(
      this,
      [this, notebookId]() {
        emit credentialsStoreError(notebookId, QString::fromLatin1(c_keychainUnavailableError));
      },
      Qt::QueuedConnection);
#endif
}

void SyncCredentialsStore::retrieveCredentials(const QString &p_notebookId) {
  // See storeCredentials for the thread-safety rationale.
  if (thread() != QThread::currentThread()) {
    const QString notebookId = p_notebookId;
    QMetaObject::invokeMethod(
        this, [this, notebookId]() { retrieveCredentials(notebookId); }, Qt::QueuedConnection);
    return;
  }
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  auto *job = new QKeychain::ReadPasswordJob(serviceName());
  job->setInsecureFallback(false);
  job->setKey(keychainKey(p_notebookId));

  const QString notebookId = p_notebookId;
  connect(job, &QKeychain::Job::finished, this, [this, job, notebookId](QKeychain::Job *) {
    if (job->error() == QKeychain::NoError) {
      SyncCredential credentials;
      if (decodeCredentials(job->textData(), credentials)) {
        emit credentialsRetrieved(notebookId, credentials);
      } else {
        m_knownCredentialIds.remove(notebookId);
        emit credentialsError(notebookId, QString::fromLatin1(c_invalidCredentialsError));
      }
    } else {
      if (job->error() == QKeychain::EntryNotFound)
        m_knownCredentialIds.remove(notebookId);
      emit credentialsError(notebookId, job->errorString());
    }
  });

  job->start();
#else
  logKeychainUnavailableOnce();
  const QString notebookId = p_notebookId;
  QMetaObject::invokeMethod(
      this,
      [this, notebookId]() {
        emit credentialsError(notebookId, QString::fromLatin1(c_keychainUnavailableError));
      },
      Qt::QueuedConnection);
#endif
}

void SyncCredentialsStore::deleteCredentials(const QString &p_notebookId) {
  // See storeCredentials for the thread-safety rationale.
  if (thread() != QThread::currentThread()) {
    const QString notebookId = p_notebookId;
    QMetaObject::invokeMethod(
        this, [this, notebookId]() { deleteCredentials(notebookId); }, Qt::QueuedConnection);
    return;
  }
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  auto *job = new QKeychain::DeletePasswordJob(serviceName());
  job->setInsecureFallback(false);
  job->setKey(keychainKey(p_notebookId));

  const QString notebookId = p_notebookId;
  connect(job, &QKeychain::Job::finished, this, [this, job, notebookId](QKeychain::Job *) {
    // A missing entry is the desired end-state of a delete, so treat
    // EntryNotFound as success. Apple's DeletePasswordJob reports errSecItemNotFound
    // as an error (see keychain_apple.mm StartDeletePassword); Windows/libsecret
    // return NoError. Normalizing here makes deletion idempotent on all platforms
    // (issue #2718).
    if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound) {
      emit credentialsDeleted(notebookId);
    } else {
      emit credentialsError(notebookId, job->errorString());
    }
  });

  job->start();
#else
  logKeychainUnavailableOnce();
  const QString notebookId = p_notebookId;
  QMetaObject::invokeMethod(
      this,
      [this, notebookId]() {
        emit credentialsError(notebookId, QString::fromLatin1(c_keychainUnavailableError));
      },
      Qt::QueuedConnection);
#endif
}

} // namespace vnotex
