#include "synccredentialsstore.h"

#include <QMetaObject>
#include <QStringLiteral>
#include <QtGlobal>

#ifdef VNOTE_KEYCHAIN_AVAILABLE
#include <keychain.h>
#endif

namespace vnotex {

namespace {

// Static error string emitted when QtKeychain is not available at build or
// runtime. The string is part of the public contract: T14 (bootstrap path) and
// upstream UI catch this exact token to surface a user-facing message.
const char *const c_keychainUnavailableError = "secure-keychain-unavailable";

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
    : QObject(p_parent), m_services(p_services) {}

const QString &SyncCredentialsStore::serviceName() {
  static const QString s_service = QStringLiteral("VNote");
  return s_service;
}

QString SyncCredentialsStore::keychainKey(const QString &p_notebookId) {
  return QStringLiteral("notebook_sync_pat_") + p_notebookId;
}

void SyncCredentialsStore::storeCredentials(const QString &p_notebookId, const QString &p_pat) {
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  auto *job = new QKeychain::WritePasswordJob(serviceName(), this);
  job->setAutoDelete(false);
  job->setInsecureFallback(false);
  job->setKey(keychainKey(p_notebookId));
  job->setTextData(p_pat);

  const QString notebookId = p_notebookId;
  connect(job, &QKeychain::Job::finished, this, [this, job, notebookId](QKeychain::Job *) {
    if (job->error() == QKeychain::NoError) {
      emit credentialsStored(notebookId);
    } else {
      emit credentialsError(notebookId, job->errorString());
    }
    job->deleteLater();
  });

  job->start();
#else
  Q_UNUSED(p_pat);
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

void SyncCredentialsStore::retrieveCredentials(const QString &p_notebookId) {
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  auto *job = new QKeychain::ReadPasswordJob(serviceName(), this);
  job->setAutoDelete(false);
  job->setInsecureFallback(false);
  job->setKey(keychainKey(p_notebookId));

  const QString notebookId = p_notebookId;
  connect(job, &QKeychain::Job::finished, this, [this, job, notebookId](QKeychain::Job *) {
    if (job->error() == QKeychain::NoError) {
      // textData() is the PAT; pass via signal but never log.
      emit credentialsRetrieved(notebookId, job->textData());
    } else {
      emit credentialsError(notebookId, job->errorString());
    }
    job->deleteLater();
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
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  auto *job = new QKeychain::DeletePasswordJob(serviceName(), this);
  job->setAutoDelete(false);
  job->setInsecureFallback(false);
  job->setKey(keychainKey(p_notebookId));

  const QString notebookId = p_notebookId;
  connect(job, &QKeychain::Job::finished, this, [this, job, notebookId](QKeychain::Job *) {
    if (job->error() == QKeychain::NoError) {
      emit credentialsDeleted(notebookId);
    } else {
      emit credentialsError(notebookId, job->errorString());
    }
    job->deleteLater();
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
