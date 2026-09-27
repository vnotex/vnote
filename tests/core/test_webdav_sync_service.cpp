// SPDX-License-Identifier: LGPL-3.0-or-later
// Run through libs/vxcore/tests/run_webdav_test.py (TLS is required).
// This executable registers WebDAV locally; production registration belongs to step 6.

#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <sqlite3.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <csignal>
#include <sys/resource.h>
#endif

#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/commenttypes.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncstateclassifier.h>
#include <core/services/syncworkqueuemanager.h>
#include <temp_dir_fixture.h>
#include <test_helper.h>

#include "core/notebook.h"
#include "sync/sync_backend_registry.h"
#include "sync/sync_json_keys.h"
#include "sync/webdav/webdav_sync_backend.h"
#include "utils/file_utils.h"
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {

const QString c_config = QStringLiteral("vx_notebook/config.json");
const QString c_folderConfig = QStringLiteral("vx_notebook/contents/vx.json");
const QString c_state = QStringLiteral("vx_notebook/vx_sync/webdav/state.json");
const QString c_pending = QStringLiteral("vx_notebook/vx_sync/webdav/pending.json");
const QString c_envelope = QStringLiteral("vx_notebook/encryption.vne");

bool waitUntil(const std::function<bool()> &p_ready, int p_timeoutMs = 15000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!p_ready() && elapsed.elapsed() < p_timeoutMs) {
    QTest::qWait(5);
  }
  return p_ready();
}

QByteArray readBytes(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error("Could not read a test-owned file");
  }
  return file.readAll();
}

bool writeBytes(const QString &p_path, const QByteArray &p_bytes) {
  if (!QDir().mkpath(QFileInfo(p_path).absolutePath())) {
    return false;
  }
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_bytes) == p_bytes.size();
}

QByteArray jsonBytes(const QJsonObject &p_object) {
  return QJsonDocument(p_object).toJson(QJsonDocument::Compact);
}

QJsonObject readObject(const QString &p_path) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(readBytes(p_path), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw std::runtime_error("Expected a valid test-owned JSON object");
  }
  return document.object();
}

size_t collectControl(char *p_bytes, size_t p_size, size_t p_count, void *p_output) {
  auto &output = *static_cast<QByteArray *>(p_output);
  const size_t length = p_size * p_count;
  if (length > size_t(4 * 1024 * 1024 - output.size())) {
    return 0;
  }
  output.append(p_bytes, int(length));
  return length;
}

// The authenticated fixture control is separate from the real backend's DAV session.
// Keep certificate and hostname verification enabled on BOTH clients.
QJsonObject control(const QJsonObject &p_request) {
  const auto url = qgetenv("VXCORE_WEBDAV_TEST_CONTROL_URL");
  const auto ca = qgetenv("VXCORE_WEBDAV_TEST_CA_FILE");
  const auto authorization =
      QByteArray("Authorization: Bearer ") + qgetenv("VXCORE_WEBDAV_TEST_CONTROL_TOKEN");
  const auto body = jsonBytes(p_request);
  CURL *handle = curl_easy_init();
  if (!handle) {
    throw std::runtime_error("Could not create fixture control session");
  }
  curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
  headers = curl_slist_append(headers, authorization.constData());
  QByteArray response;
  curl_easy_setopt(handle, CURLOPT_URL, url.constData());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.constData());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body.size()));
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(handle, CURLOPT_CAINFO, ca.constData());
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collectControl);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  const auto result = curl_easy_perform(handle);
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(handle);
  const auto document = QJsonDocument::fromJson(response);
  if (result != CURLE_OK || status != 200 || !document.isObject() ||
      !document.object().value(QStringLiteral("ok")).toBool()) {
    throw std::runtime_error("Authenticated TLS fixture control failed (details redacted)");
  }
  return document.object();
}

void putRemote(const QString &p_path, const QByteArray &p_bytes) {
  control({{QStringLiteral("action"), QStringLiteral("put")},
           {QStringLiteral("path"), p_path},
           {QStringLiteral("content_base64"), QString::fromLatin1(p_bytes.toBase64())},
           {QStringLiteral("parents"), true}});
}

QByteArray remoteBytes(const QString &p_path) {
  const auto object = control({{QStringLiteral("action"), QStringLiteral("inspect")},
                               {QStringLiteral("path"), p_path},
                               {QStringLiteral("content"), true}});
  if (!object.value(QStringLiteral("exists")).toBool() ||
      object.value(QStringLiteral("kind")).toString() != QLatin1String("file")) {
    throw std::runtime_error("Expected an ordinary remote file");
  }
  return QByteArray::fromBase64(
      object.value(QStringLiteral("content_base64")).toString().toLatin1());
}

void armDownloadBarrier() {
  control({{QStringLiteral("action"), QStringLiteral("barrier")},
           {QStringLiteral("id"), QStringLiteral("download")},
           {QStringLiteral("method"), QStringLiteral("GET")},
           {QStringLiteral("path"), QStringLiteral("clean.md")},
           {QStringLiteral("phase"), QStringLiteral("after_headers")}});
}

bool waitDownloadBarrier() {
  return control({{QStringLiteral("action"), QStringLiteral("wait")},
                  {QStringLiteral("id"), QStringLiteral("download")},
                  {QStringLiteral("timeout_ms"), 10000}})
      .value(QStringLiteral("reached"))
      .toBool();
}

void releaseDownloadBarrier() {
  control({{QStringLiteral("action"), QStringLiteral("release")},
           {QStringLiteral("id"), QStringLiteral("download")}});
}

// Existing NotebookIoGate is the public serialization boundary. The GUI thread never
// takes its blocking lock: a separate owner thread makes queued saves deterministic.
class GateBarrier {
public:
  GateBarrier(NotebookIoGate &p_gate, const QString &p_id)
      : m_thread([this, &p_gate, p_id]() {
          NotebookIoGate::ScopedLock lock(p_gate, p_id);
          m_started.release();
          m_release.acquire();
        }) {
    m_started.acquire();
  }
  ~GateBarrier() { release(); }
  void release() {
    if (m_thread.joinable()) {
      m_release.release();
      m_thread.join();
    }
  }

private:
  QSemaphore m_started;
  QSemaphore m_release;
  std::thread m_thread;
};

// Staged apply tests never use the OS vault.
// Any accidental vault route fails instead of silently skipping an unavailable keychain.
class UnusedCredentialsStore final : public SyncCredentialsStore {
public:
  explicit UnusedCredentialsStore(ServiceLocator &p_services) : SyncCredentialsStore(p_services) {}
  bool hasCredentials(const QString &) const override { return false; }
  void storeCredentials(const QString &, const SyncCredential &) override {
    qFatal("The staged WebDAV fixture must not write the OS vault");
  }
  void retrieveCredentials(const QString &) override {
    qFatal("The staged WebDAV fixture must not read the OS vault");
  }
  void deleteCredentials(const QString &) override {
    qFatal("The staged WebDAV fixture must not delete OS vault entries");
  }
  void refreshKnownIds() override {}
};

// Fault injection is limited to the vault boundary. DAV remains the real TLS
// transport/backend, and the fixture request journal observes network effects.
class LifecycleCredentialsStore final : public SyncCredentialsStore {
public:
  explicit LifecycleCredentialsStore(ServiceLocator &p_services)
      : SyncCredentialsStore(p_services) {}
  bool rejectWrites = false;
  bool deferDeletion = false;
  QString pendingDeletion;
  void storeCredentials(const QString &p_id, const SyncCredential &) override {
    QMetaObject::invokeMethod(
        this,
        [this, p_id]() {
          if (rejectWrites)
            emit credentialsStoreError(p_id, QStringLiteral("secure-keychain-unavailable"));
          else
            emit credentialsStored(p_id);
        },
        Qt::QueuedConnection);
  }
  void retrieveCredentials(const QString &p_id) override {
    QMetaObject::invokeMethod(
        this,
        [this, p_id]() {
          emit credentialsRetrieved(
              p_id, {QStringLiteral("git"), QString(), QStringLiteral("legacy-pat")});
        },
        Qt::QueuedConnection);
  }
  void deleteCredentials(const QString &p_id) override {
    if (deferDeletion)
      pendingDeletion = p_id;
    else
      emit credentialsDeleted(p_id);
  }
};

// Preserve valid recovery state so the real guard/preflight can read it. Inject
// failure only in publication: Windows denies baseline replacement; POSIX limits
// the second incoming file after the first small file and journals are durable.
class ApplyPublicationFailure {
public:
  explicit ApplyPublicationFailure(QString p_statePath) : m_statePath(std::move(p_statePath)) {}
  ~ApplyPublicationFailure() { restore(); }

  bool install() {
#ifdef Q_OS_WIN
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(m_statePath.utf16()), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    return m_handle != INVALID_HANDLE_VALUE;
#else
    if (getrlimit(RLIMIT_FSIZE, &m_previousLimit) != 0 ||
        m_previousLimit.rlim_max < rlim_t(64 * 1024))
      return false;
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGXFSZ, &ignore, &m_previousSignal) != 0)
      return false;
    m_signalChanged = true;
    auto limit = m_previousLimit;
    limit.rlim_cur = 64 * 1024;
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
      restore();
      return false;
    }
    m_limitChanged = true;
    return true;
#endif
  }

  bool restore() {
#ifdef Q_OS_WIN
    if (m_handle == INVALID_HANDLE_VALUE)
      return true;
    if (!CloseHandle(m_handle))
      return false;
    m_handle = INVALID_HANDLE_VALUE;
    return true;
#else
    if (m_limitChanged) {
      if (setrlimit(RLIMIT_FSIZE, &m_previousLimit) != 0)
        return false;
      m_limitChanged = false;
    }
    if (m_signalChanged) {
      if (sigaction(SIGXFSZ, &m_previousSignal, nullptr) != 0)
        return false;
      m_signalChanged = false;
    }
    return true;
#endif
  }

private:
  QString m_statePath;
#ifdef Q_OS_WIN
  HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
  struct rlimit m_previousLimit{};
  struct sigaction m_previousSignal{};
  bool m_limitChanged = false;
  bool m_signalChanged = false;
#endif
};

class DatabaseWriteLock {
public:
  ~DatabaseWriteLock() {
    if (m_database) {
      sqlite3_exec(m_database, "ROLLBACK", nullptr, nullptr, nullptr);
      sqlite3_close(m_database);
    }
  }
  bool acquire(const QString &p_path) {
    return sqlite3_open_v2(p_path.toUtf8().constData(), &m_database, SQLITE_OPEN_READWRITE,
                           nullptr) == SQLITE_OK &&
           sqlite3_exec(m_database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK;
  }

private:
  sqlite3 *m_database = nullptr;
};

} // namespace

// BufferService privately inherits QObject; use the same named-slot boundary as views.
class SyncBufferObserver final : public QObject {
  Q_OBJECT
public:
  explicit SyncBufferObserver(BufferService &p_buffers) {
    connect(p_buffers.asQObject(),
            SIGNAL(contentReplacementStateChanged(QString, bool, bool, bool)), this,
            SLOT(onReplacement(QString, bool, bool, bool)));
  }
  QSet<QString> frozen;
  std::function<void(const QString &, bool)> replacement;

private slots:
  void onReplacement(const QString &p_id, bool p_active, bool, bool) {
    if (p_active) {
      frozen.insert(p_id);
    } else {
      frozen.remove(p_id);
    }
    if (replacement) {
      replacement(p_id, p_active);
    }
  }
};

class TestWebDavSyncService : public QObject {
  Q_OBJECT
public:
  ~TestWebDavSyncService() override { teardown(); }

private slots:
  void initTestCase();
  void init();
  void cleanup();
  void incomingChangesReloadCleanBuffersAndMetadata();
  void laterSaveAndEditorCommentDraftsProtectTheWholeCohort();
  void drainsQueuedSavesBeforeApply();
  void reservationBlocksMetadataButOtherNotebookSaves();
  void cancelBeforeGuiReservationLeavesIncomingFilesStaged();
  void cancelWhileDrainingPreservesTheQueuedSave();
  void drainTimeoutRetiresReservationWithoutDroppingSave();
  void shutdownWithPendingHandshakeCannotStartLateApply();
  void partialApplyFailureRefreshesInstalledPathsWithoutSuccess();
  void metadataRefreshFailureDoesNotStampSuccess();
  void conflictSnapshotCarriesKeepBothPolicy();
  void liveEncryptedLeaseRefusesEnvelopeReplacement();
  void unavailableVaultPreventsNetworkAndPreservesRegistration();
  void mismatchedCredentialCannotReconcile();
  void credentialRotationAuthenticatesBeforeChangingBinding();
  void interruptedRetirementBlocksCredentialIngress();
  void disableCompletionWaitsForVaultBeforeReenable();

private:
  bool teardown();
  QString path(const QString &p_relative) const { return QDir(m_root).filePath(p_relative); }
  Buffer2 open(const QString &p_relative) { return m_buffers->openBuffer({m_id, p_relative}); }
  void startSync();
  VxCoreError finishSync();
  VxCoreError sync() {
    startSync();
    return finishSync();
  }
  QStringList changedPaths() const;
  QStringList conflictPaths() const;
  std::unique_ptr<GateBarrier> startDrainWait(Buffer2 &p_save, QString &p_editor);

  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<TempDirFixture> m_temp;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<CommentService> m_comments;
  std::unique_ptr<SyncWorkQueueManager> m_queue;
  std::unique_ptr<UnusedCredentialsStore> m_credentials;
  std::unique_ptr<SyncService> m_sync;
  std::unique_ptr<SyncBufferObserver> m_observer;
  std::unique_ptr<QSignalSpy> m_finished;
  std::unique_ptr<QSignalSpy> m_changed;
  std::unique_ptr<QSignalSpy> m_conflicts;
  QString m_root;
  QString m_id;
  QString m_commentPath;
  qint64 m_lastSuccess = 0;
};

void TestWebDavSyncService::initTestCase() {
  vxcore_set_test_mode(1);
  QVERIFY2(QUrl(QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_URL"))).scheme() ==
               QLatin1String("https"),
           "Run this service test through the TLS fixture runner");
  QVERIFY(QFileInfo::exists(QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_CA_FILE"))));
  QVERIFY(!qgetenv("VXCORE_WEBDAV_TEST_CONTROL_TOKEN").isEmpty());
  QCOMPARE(curl_global_init(CURL_GLOBAL_DEFAULT), CURLE_OK);
  qRegisterMetaType<VxCoreError>("VxCoreError");
  qRegisterMetaType<NodeIdentifier>("NodeIdentifier");
  qRegisterMetaType<NodeIdentifier>("vnotex::NodeIdentifier");
}

void TestWebDavSyncService::unavailableVaultPreventsNetworkAndPreservesRegistration() {
  ServiceLocator services;
  LifecycleCredentialsStore credentials(services);
  credentials.rejectWrites = true;
  services.registerService<NotebookCoreService>(m_notebooks.get());
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService syncService(services);
  const auto before = control({{QStringLiteral("action"), QStringLiteral("requests")}})
                          .value(QStringLiteral("total"));
  const auto baseline = readBytes(path(c_state));
  QSignalSpy finished(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(m_id, {QStringLiteral("webdav"),
                                       QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_USERNAME")),
                                       QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_PASSWORD"))});
  QVERIFY(waitUntil([&]() { return !finished.isEmpty(); }));
  QCOMPARE(finished.first().at(1).toInt(), int(VXCORE_ERR_UNKNOWN));
  QVERIFY(syncService.isSyncRegistered(m_id));
  QCOMPARE(readBytes(path(c_state)), baseline);
  QCOMPARE(control({{QStringLiteral("action"), QStringLiteral("requests")}})
               .value(QStringLiteral("total")),
           before);
}

void TestWebDavSyncService::mismatchedCredentialCannotReconcile() {
  QCOMPARE(m_notebooks->unregisterSyncRuntime(m_id), VXCORE_OK);
  ServiceLocator services;
  LifecycleCredentialsStore credentials(services);
  services.registerService<NotebookCoreService>(m_notebooks.get());
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService syncService(services);
  const auto before = control({{QStringLiteral("action"), QStringLiteral("requests")}})
                          .value(QStringLiteral("total"));
  QSignalSpy finished(&syncService, &SyncService::reconcileFinished);
  syncService.ensureSyncEnabled(m_id);
  QVERIFY(waitUntil([&]() { return !finished.isEmpty(); }));
  QCOMPARE(finished.first().at(1).toInt(), int(VXCORE_ERR_SYNC_AUTH_FAILED));
  QVERIFY(!syncService.isSyncRegistered(m_id));
  QCOMPARE(control({{QStringLiteral("action"), QStringLiteral("requests")}})
               .value(QStringLiteral("total")),
           before);
}

void TestWebDavSyncService::credentialRotationAuthenticatesBeforeChangingBinding() {
  ServiceLocator services;
  LifecycleCredentialsStore credentials(services);
  services.registerService<NotebookCoreService>(m_notebooks.get());
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);
  const auto before = readObject(path(c_state));
  const auto configBefore = readBytes(path(c_config));
  const auto localBefore = readBytes(path(QStringLiteral("clean.md")));
  const auto remoteBefore = remoteBytes(QStringLiteral("clean.md"));
  const QString username = QStringLiteral("rotated-account");
  const QString password = QStringLiteral(" rotated-app-password ");
  control({{QStringLiteral("action"), QStringLiteral("configure")},
           {QStringLiteral("username"), username},
           {QStringLiteral("password"), password}});
  QSignalSpy finished(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(
      m_id, {QStringLiteral("webdav"), username, QStringLiteral("incorrect-password")});
  QVERIFY(waitUntil([&]() { return !finished.isEmpty(); }));
  QCOMPARE(finished.takeFirst().at(1).toInt(), int(VXCORE_ERR_SYNC_AUTH_FAILED));
  QVERIFY(syncService.isSyncRegistered(m_id));
  QCOMPARE(readObject(path(c_state)), before);
  syncService.updateCredentials(m_id, {QStringLiteral("webdav"), username, password});
  QVERIFY(waitUntil([&]() { return !finished.isEmpty(); }));
  QCOMPARE(finished.takeFirst().at(1).toInt(), int(VXCORE_OK));
  const auto after = readObject(path(c_state));
  QCOMPARE(after.value(QStringLiteral("entries")), before.value(QStringLiteral("entries")));
  QVERIFY(after.value(QStringLiteral("usernameHash")) !=
          before.value(QStringLiteral("usernameHash")));
  QCOMPARE(readBytes(path(c_config)), configBefore);
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), localBefore);
  QCOMPARE(remoteBytes(QStringLiteral("clean.md")), remoteBefore);
  QVERIFY(!readBytes(path(c_state)).contains(password.toUtf8()));
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  SyncStateClassifier classifier(services);
  QCOMPARE(classifier.classify(m_id), SyncState::S5);
  auto lease = syncService.workQueueManager()->tryAcquireMaintenance({m_id});
  QVERIFY(lease);
  syncService.triggerSyncNow(m_id);
  QCOMPARE(classifier.classify(m_id), SyncState::S7);
  syncService.cancelSync(m_id);
  QCOMPARE(classifier.classify(m_id), SyncState::S5);
  lease.release();
}

void TestWebDavSyncService::interruptedRetirementBlocksCredentialIngress() {
  ServiceLocator services;
  LifecycleCredentialsStore credentials(services);
  services.registerService<NotebookCoreService>(m_notebooks.get());
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService syncService(services);
  QVERIFY(writeBytes(path(QStringLiteral("vx_notebook/vx_sync/webdav/retirement.json")), "{}"));
  const auto before = control({{QStringLiteral("action"), QStringLiteral("requests")}})
                          .value(QStringLiteral("total"));
  const SyncSettings settings{QStringLiteral("webdav"),
                              QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_URL")),
                              {QStringLiteral("webdav"),
                               QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_USERNAME")),
                               QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_PASSWORD"))}};
  QSignalSpy stored(&credentials, &SyncCredentialsStore::credentialsStored);
  QSignalSpy enabled(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(m_id, settings);
  QVERIFY(waitUntil([&]() { return !enabled.isEmpty(); }));
  QCOMPARE(enabled.first().at(1).toInt(), int(VXCORE_ERR_INVALID_STATE));
  QSignalSpy updated(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(m_id, settings.m_credentials);
  QCOMPARE(updated.count(), 1);
  QCOMPARE(updated.first().at(1).toInt(), int(VXCORE_ERR_INVALID_STATE));
  QSignalSpy synced(&syncService, &SyncService::syncFinished);
  syncService.triggerSyncNow(m_id);
  QCOMPARE(synced.count(), 1);
  QCOMPARE(synced.first().at(1).toInt(), int(VXCORE_ERR_INVALID_STATE));
  QCOMPARE(m_notebooks->unregisterSyncRuntime(m_id), VXCORE_OK);
  QSignalSpy reconciled(&syncService, &SyncService::reconcileFinished);
  syncService.ensureSyncEnabled(m_id);
  QCOMPARE(reconciled.count(), 1);
  QCOMPARE(reconciled.first().at(1).toInt(), int(VXCORE_ERR_INVALID_STATE));
  QCOMPARE(stored.count(), 0);
  QCOMPARE(control({{QStringLiteral("action"), QStringLiteral("requests")}})
               .value(QStringLiteral("total")),
           before);
}

void TestWebDavSyncService::disableCompletionWaitsForVaultBeforeReenable() {
  ServiceLocator services;
  LifecycleCredentialsStore credentials(services);
  credentials.deferDeletion = true;
  services.registerService<NotebookCoreService>(m_notebooks.get());
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService syncService(services);
  const SyncSettings settings{QStringLiteral("webdav"),
                              QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_URL")),
                              {QStringLiteral("webdav"),
                               QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_USERNAME")),
                               QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_PASSWORD"))}};
  QSignalSpy stored(&credentials, &SyncCredentialsStore::credentialsStored);
  QSignalSpy disabled(&syncService, &SyncService::disableFinished);
  QSignalSpy enabled(&syncService, &SyncService::enableFinished);
  syncService.disableSyncForNotebook(m_id);
  QVERIFY(waitUntil([&]() { return credentials.pendingDeletion == m_id; }));
  QVERIFY(!syncService.isSyncRegistered(m_id));
  QVERIFY(syncService.isSyncInProgress(m_id));
  QCOMPARE(disabled.count(), 0);
  // A reopened clone with the same UUID may request enable before the old
  // close's native delete callback. It must wait, not lose the new credential.
  syncService.enableSyncForNotebook(m_id, settings);
  QCOMPARE(enabled.count(), 0);
  QCOMPARE(stored.count(), 0);
  credentials.credentialsDeleted(m_id);
  QVERIFY(waitUntil([&]() { return !enabled.isEmpty(); }));
  QCOMPARE(disabled.count(), 1);
  QCOMPARE(enabled.first().at(1).toInt(), int(VXCORE_OK));
  QVERIFY(syncService.isSyncRegistered(m_id));
  QCOMPARE(stored.count(), 1);
}

void TestWebDavSyncService::init() {
  // QtTest can skip cleanup() when init() fails. Never replace a dependency while
  // a service from the previous case still holds it, including on assertion return.
  QVERIFY(teardown());
  bool initialized = false;
  const auto rollback = qScopeGuard([&]() {
    if (!initialized)
      teardown();
  });
  control({{QStringLiteral("action"), QStringLiteral("reset")}});
  m_temp.reset(new TempDirFixture());
  QVERIFY(m_temp->isValid());
  // The runner gave this process its own TMP/TEMP before its first context.
  // Each case owns a fresh notebook, not a reopen of the previous case's registry.
  vxcore_clear_test_directory();
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_services.reset(new ServiceLocator());
  m_hooks.reset(new HookManager());
  m_gate.reset(new NotebookIoGate());
  m_notebooks.reset(new NotebookCoreService(m_context));
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_buffers.reset(new BufferService(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None));
  m_comments.reset(new CommentService(m_notebooks.get(), m_gate.get(), m_hooks.get()));
  m_queue.reset(new SyncWorkQueueManager());
  m_credentials.reset(new UnusedCredentialsStore(*m_services));
  m_services->registerService<NotebookCoreService>(m_notebooks.get());
  m_services->registerService<BufferService>(m_buffers.get());
  m_services->registerService<CommentService>(m_comments.get());
  m_services->registerService<NotebookIoGate>(m_gate.get());
  m_services->registerService<SyncWorkQueueManager>(m_queue.get());
  m_services->registerService<SyncCredentialsStore>(m_credentials.get());
  m_sync.reset(new SyncService(*m_services));
  m_observer.reset(new SyncBufferObserver(*m_buffers));
  m_finished.reset(new QSignalSpy(m_sync.get(), &SyncService::syncFinished));
  m_changed.reset(new QSignalSpy(m_sync.get(), &SyncService::workingTreeChanged));
  m_conflicts.reset(new QSignalSpy(m_sync.get(), &SyncService::conflictsDetected));
  QVERIFY(m_finished->isValid() && m_changed->isValid() && m_conflicts->isValid());

  m_root = m_temp->filePath(QStringLiteral("notebook"));
  m_id = m_notebooks->createNotebook(
      m_root,
      QStringLiteral(
          R"({"name":"Service DAV","assetsFolder":"custom-assets","recycleBinFolder":"recycle"})"),
      NotebookType::Bundled);
  QVERIFY(!m_id.isEmpty());
  for (const auto &name :
       {QStringLiteral("clean.md"), QStringLiteral("saved.md"), QStringLiteral("draft.md")}) {
    QVERIFY(!m_notebooks->createFile(m_id, QString(), name).isEmpty());
    QVERIFY(
        writeBytes(path(name), (QStringLiteral("baseline ") + name + QLatin1Char('\n')).toUtf8()));
  }
  const NodeIdentifier commented{m_id, QStringLiteral("draft.md")};
  const auto location = m_comments->resolveLocation(commented);
  QVERIFY(location.isValid());
  m_commentPath = QDir(m_root).relativeFilePath(location.m_storePath);
  CommentSet comments;
  comments.m_comments.append(Comment::create(
      PdfQuadsAnchor::make(1, {{0, 0, 20, 0, 20, 10, 0, 10}}, QStringLiteral("quote")),
      QStringLiteral("baseline comment"), QStringLiteral("green")));
  m_comments->scheduleSave(commented, comments, 1);
  QVERIFY(waitUntil([&]() { return !m_comments->isBusy(commented); }));
  QCOMPARE(m_comments->load(commented).m_status, CommentService::LoadResult::Status::Loaded);

  auto notebookConfig = m_notebooks->getNotebookConfig(m_id);
  const auto remote = QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_URL"));
  notebookConfig.insert(QLatin1String(vxcore::kJsonKeySyncEnabled), true);
  notebookConfig.insert(QLatin1String(vxcore::kJsonKeySyncBackend), QStringLiteral("webdav"));
  notebookConfig.insert(QLatin1String(vxcore::kJsonKeySyncRemoteUrl), remote);
  notebookConfig.insert(QLatin1String(vxcore::kJsonKeyAutoSyncEnabled), false);
  QVERIFY(m_notebooks->updateNotebookConfig(m_id, QString::fromUtf8(jsonBytes(notebookConfig))));
  const QJsonObject config{{QLatin1String(vxcore::kJsonKeyBackend), QStringLiteral("webdav")},
                           {QLatin1String(vxcore::kJsonKeyRemoteUrl), remote},
                           {QLatin1String(vxcore::kJsonKeyAutoSyncEnabled), false}};
  const QJsonObject extra{{QLatin1String(vxcore::kJsonKeyUsername),
                           QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_USERNAME"))},
                          {QLatin1String(vxcore::kJsonKeyPassword),
                           QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_PASSWORD"))}};
  const auto credentials = jsonBytes({{QLatin1String(vxcore::kJsonKeyExtra), extra}});
  auto enable = std::async(std::launch::async, [this, config, credentials]() {
    const auto result = m_notebooks->enableSync(m_id, QString::fromUtf8(jsonBytes(config)),
                                                QString::fromUtf8(credentials));
    return std::make_pair(result, m_notebooks->syncErrorMessage(result));
  });
  QVERIFY(waitUntil([&]() {
    return enable.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
  }));
  const auto enabled = enable.get();
  QVERIFY2(enabled.first == VXCORE_OK, qPrintable(QStringLiteral("WebDAV enable returned %1: %2")
                                                      .arg(int(enabled.first))
                                                      .arg(enabled.second)));
  QVERIFY(m_sync->isSyncReady(m_id));
  QCOMPARE(sync(), VXCORE_OK);
  m_lastSuccess = m_notebooks->getLastSyncUtc(m_id);
  QVERIFY(m_lastSuccess > 0);
  m_finished->clear();
  m_changed->clear();
  m_conflicts->clear();
  initialized = true;
}

void TestWebDavSyncService::cleanup() { QVERIFY(teardown()); }

bool TestWebDavSyncService::teardown() {
  bool drained = true;
  // Fault retirement must not short-circuit ownership cleanup if the fixture died.
  if (m_context) {
    try {
      control({{QStringLiteral("action"), QStringLiteral("clear_faults")}});
    } catch (...) {
      drained = false;
    }
  }
  if (m_observer)
    m_observer->replacement = {};
  if (m_sync)
    m_sync->shutdown();
  if (m_comments && !m_comments->shutdown())
    drained = false;
  if (m_buffers && !m_buffers->shutdown())
    drained = false;
  m_finished.reset();
  m_changed.reset();
  m_conflicts.reset();
  m_sync.reset();
  m_observer.reset();
  m_comments.reset();
  m_buffers.reset();
  m_queue.reset();
  m_credentials.reset();
  m_notebooks.reset();
  m_gate.reset();
  m_hooks.reset();
  m_services.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_temp.reset();
  m_root.clear();
  m_id.clear();
  m_commentPath.clear();
  m_lastSuccess = 0;
  return drained;
}

void TestWebDavSyncService::startSync() {
  m_finished->clear();
  m_changed->clear();
  m_conflicts->clear();
  m_sync->triggerSyncNow(m_id);
}

VxCoreError TestWebDavSyncService::finishSync() {
  if (!waitUntil([&]() { return !m_finished->isEmpty(); }, 20000)) {
    QTest::qFail("Staged WebDAV sync did not finish", __FILE__, __LINE__);
    return VXCORE_ERR_UNKNOWN;
  }
  if (!waitUntil([&]() { return !m_sync->isSyncInProgress(m_id); })) {
    QTest::qFail("Completed sync did not release its queue item", __FILE__, __LINE__);
    return VXCORE_ERR_UNKNOWN;
  }
  if (m_finished->size() != 1 || m_finished->first().at(0).toString() != m_id) {
    QTest::qFail("Expected one lifecycle completion for the notebook", __FILE__, __LINE__);
    return VXCORE_ERR_UNKNOWN;
  }
  return qvariant_cast<VxCoreError>(m_finished->first().at(1));
}

QStringList TestWebDavSyncService::changedPaths() const {
  QStringList paths;
  for (const auto &event : *m_changed) {
    if (event.at(0).toString() == m_id) {
      paths.append(event.at(1).toStringList());
    }
  }
  return paths;
}

QStringList TestWebDavSyncService::conflictPaths() const {
  QStringList paths;
  for (const auto &event : *m_conflicts) {
    if (event.at(0).toString() == m_id) {
      paths.append(event.at(1).toStringList());
    }
  }
  return paths;
}

void TestWebDavSyncService::incomingChangesReloadCleanBuffersAndMetadata() {
  auto clean = open(QStringLiteral("clean.md"));
  QVERIFY(clean.isValid());
  const auto before = m_notebooks->listFolderChildren(m_id, QString());
  QVERIFY(!before.value(QStringLiteral("files")).toArray().isEmpty());
  auto config = QJsonDocument::fromJson(remoteBytes(c_config)).object();
  config.insert(QStringLiteral("description"), QStringLiteral("remote metadata revision"));
  // Even a foreign client cannot change this device's routing through config projection.
  config.insert(QLatin1String(vxcore::kJsonKeySyncEnabled), false);
  config.insert(QLatin1String(vxcore::kJsonKeySyncBackend), QStringLiteral("git"));
  putRemote(c_config, jsonBytes(config));
  auto folder = QJsonDocument::fromJson(remoteBytes(c_folderConfig)).object();
  const auto files = folder.value(QStringLiteral("files")).toArray();
  QJsonArray reversed;
  for (int index = files.size() - 1; index >= 0; --index) {
    reversed.append(files.at(index));
  }
  folder.insert(QStringLiteral("files"), reversed);
  putRemote(c_folderConfig, jsonBytes(folder));
  const QByteArray incoming("external edit\r\nsecond line\n");
  putRemote(QStringLiteral("clean.md"), incoming);
  const QString external = QString::fromUtf8("\xe5\xa4\x96\xe9\x83\xa8 #%.md");
  putRemote(external, "unindexed external bytes\r\n");
  bool completionObservedInstalledState = false;
  connect(m_sync.get(), &SyncService::syncFinished, this,
          [&](const QString &p_id, VxCoreError p_code) {
            if (p_id == m_id && p_code == VXCORE_OK) {
              completionObservedInstalledState =
                  clean.getContentRaw() == incoming &&
                  m_notebooks->getNotebookConfig(m_id).value(QStringLiteral("description")) ==
                      QJsonValue(QStringLiteral("remote metadata revision")) &&
                  m_observer->frozen.isEmpty();
            }
          });
  QCOMPARE(sync(), VXCORE_OK);
  QVERIFY(completionObservedInstalledState);
  QCOMPARE(clean.getContentRaw(), incoming);
  QVERIFY(!clean.isModified());
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), incoming);
  QCOMPARE(m_changed->size(), 1);
  QVERIFY(changedPaths().contains(QStringLiteral("clean.md")));
  QVERIFY(changedPaths().contains(external));
  QVERIFY(changedPaths().contains(c_config));
  QVERIFY(changedPaths().contains(c_folderConfig));
  const auto after = m_notebooks->listFolderChildren(m_id, QString());
  QCOMPARE(after.value(QStringLiteral("files"))
               .toArray()
               .first()
               .toObject()
               .value(QStringLiteral("name")),
           reversed.first().toObject().value(QStringLiteral("name")));
  bool visible = false;
  for (const auto &entry :
       m_notebooks->listFolderExternal(m_id, QString()).value(QStringLiteral("files")).toArray()) {
    visible |= entry.toObject().value(QStringLiteral("name")).toString() == external;
  }
  QVERIFY(visible);
  const auto routing = m_notebooks->getNotebookConfig(m_id);
  QCOMPARE(routing.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString(),
           QStringLiteral("webdav"));
  QVERIFY(routing.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool());
  QVERIFY(m_notebooks->getLastSyncUtc(m_id) >= m_lastSuccess);
}

void TestWebDavSyncService::laterSaveAndEditorCommentDraftsProtectTheWholeCohort() {
  auto clean = open(QStringLiteral("clean.md"));
  auto saved = open(QStringLiteral("saved.md"));
  auto draft = open(QStringLiteral("draft.md"));
  QVERIFY(clean.isValid() && saved.isValid() && draft.isValid());
  const auto originalClean = clean.getContentRaw();
  const auto originalComment = readBytes(path(m_commentPath));
  putRemote(QStringLiteral("clean.md"), "incoming clean\n");
  putRemote(QStringLiteral("saved.md"), "incoming saved\n");
  putRemote(QStringLiteral("draft.md"), "incoming draft\n");
  auto incomingComments = m_comments->load({m_id, QStringLiteral("draft.md")}).m_comments;
  incomingComments.m_comments.first().m_text = QStringLiteral("remote comment");
  putRemote(m_commentPath, jsonBytes(incomingComments.toJson()));
  armDownloadBarrier();
  startSync();
  QVERIFY(waitDownloadBarrier());

  QString editor = QStringLiteral("completed save after preparation\n");
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  m_buffers->registerActiveWriter(saved.id(), 1, [&]() { return editor; });
  m_buffers->markDirty(saved.id());
  m_buffers->syncNow(saved.id());
  QVERIFY(waitUntil([&]() { return !m_buffers->isSaveQueueBusy(saved.id()); }));
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  m_buffers->setAutoSavePolicy(AutoSavePolicy::None);
  const QByteArray draftText("unsaved editor text must survive\n");
  QVERIFY(draft.setContentRaw(draftText));
  m_buffers->markDirty(draft.id());
  const NodeIdentifier commentNode{m_id, QStringLiteral("draft.md")};
  auto commentDraft = m_comments->load(commentNode).m_comments;
  commentDraft.m_comments.first().m_text = QStringLiteral("unsaved view-owned comment");
  int forcedCommentFlushes = 0;
  auto commentOwner = m_comments->registerFlushParticipant(
      commentNode,
      [&]() {
        ++forcedCommentFlushes;
        m_comments->scheduleSave(commentNode, commentDraft, 2);
      },
      2);
  QVERIFY(commentOwner.isValid());
  releaseDownloadBarrier();

  QCOMPARE(finishSync(), VXCORE_ERR_SYNC_CONFLICT);
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), originalClean);
  QCOMPARE(clean.getContentRaw(), originalClean);
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  QCOMPARE(draft.getContentRaw(), draftText);
  QVERIFY(draft.isModified() || m_buffers->isDirty(draft.id()));
  QCOMPARE(readBytes(path(m_commentPath)), originalComment);
  QCOMPARE(commentDraft.m_comments.first().m_text, QStringLiteral("unsaved view-owned comment"));
  QCOMPARE(forcedCommentFlushes, 0);
  QVERIFY(m_changed->isEmpty());
  QVERIFY(conflictPaths().contains(QStringLiteral("saved.md")));
  QVERIFY(conflictPaths().contains(QStringLiteral("draft.md")));
  QVERIFY(conflictPaths().contains(m_commentPath));
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QVERIFY(m_observer->frozen.isEmpty());
  QVERIFY(!readObject(path(c_pending)).value(QStringLiteral("operations")).toArray().isEmpty());
  m_buffers->unregisterActiveWriter(saved.id(), 1);
}

std::unique_ptr<GateBarrier> TestWebDavSyncService::startDrainWait(Buffer2 &p_save,
                                                                   QString &p_editor) {
  putRemote(QStringLiteral("clean.md"), "incoming after drain\n");
  putRemote(QStringLiteral("saved.md"), "incoming save target\n");
  armDownloadBarrier();
  startSync();
  if (!waitDownloadBarrier()) {
    return {};
  }
  auto held = std::make_unique<GateBarrier>(*m_gate, m_id);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  m_buffers->registerActiveWriter(p_save.id(), 1, [&p_editor]() { return p_editor; });
  m_buffers->markDirty(p_save.id());
  m_buffers->syncNow(p_save.id());
  releaseDownloadBarrier();
  if (!waitUntil([&]() { return !m_observer->frozen.isEmpty(); }, 4000)) {
    return {};
  }
  return held;
}

void TestWebDavSyncService::drainsQueuedSavesBeforeApply() {
  auto clean = open(QStringLiteral("clean.md"));
  auto saved = open(QStringLiteral("saved.md"));
  QVERIFY(clean.isValid() && saved.isValid());
  const auto original = clean.getContentRaw();
  QString editor = QStringLiteral("save queued before reservation\n");
  auto held = startDrainWait(saved, editor);
  QVERIFY(held);
  QVERIFY(m_buffers->isSaveQueueBusy(saved.id()));
  QVERIFY(!m_buffers->isSyncApplyReady(m_id));
  QVERIFY(m_finished->isEmpty());
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), original);
  held->release();
  QCOMPARE(finishSync(), VXCORE_ERR_SYNC_CONFLICT);
  QVERIFY(!m_buffers->isSaveQueueBusy(saved.id()));
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), original);
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QVERIFY(m_observer->frozen.isEmpty());
  m_buffers->unregisterActiveWriter(saved.id(), 1);
  QVERIFY(!m_notebooks->createFile(m_id, QString(), QStringLiteral("allowed-after.md")).isEmpty());
}

void TestWebDavSyncService::reservationBlocksMetadataButOtherNotebookSaves() {
  auto clean = open(QStringLiteral("clean.md"));
  QVERIFY(clean.isValid());
  const auto otherRoot = m_temp->filePath(QStringLiteral("other-notebook"));
  const auto otherId = m_notebooks->createNotebook(
      otherRoot, QStringLiteral("{\"name\":\"Other\"}"), NotebookType::Bundled);
  QVERIFY(!otherId.isEmpty());
  QVERIFY(!m_notebooks->createFile(otherId, QString(), QStringLiteral("other.md")).isEmpty());
  auto other = m_buffers->openBuffer({otherId, QStringLiteral("other.md")});
  QVERIFY(other.isValid());
  putRemote(QStringLiteral("clean.md"), "incoming while metadata is reserved\n");
  armDownloadBarrier();
  startSync();
  QVERIFY(waitDownloadBarrier());
  GateBarrier applying(*m_gate, m_id);
  releaseDownloadBarrier();
  // beginSyncApply emits this synchronously; its immediately following readiness
  // check reserves core metadata before the GUI returns to this event-loop wait.
  QVERIFY(waitUntil([&]() { return m_observer->frozen.contains(clean.id()); }));
  const auto sameConfig = jsonBytes(m_notebooks->getNotebookConfig(m_id));
  const auto id = m_id.toUtf8();
  QCOMPARE(vxcore_notebook_update_config(m_context, id.constData(), sameConfig.constData()),
           VXCORE_ERR_SYNC_IN_PROGRESS);
  char *newFile = nullptr;
  QCOMPARE(vxcore_file_create(m_context, id.constData(), "", "must-not-exist.md", &newFile),
           VXCORE_ERR_SYNC_IN_PROGRESS);
  QVERIFY(newFile == nullptr);
  QVERIFY(!QFileInfo::exists(path(QStringLiteral("must-not-exist.md"))));
  QString editor = QStringLiteral("another notebook remains writable\n");
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  m_buffers->registerActiveWriter(other.id(), 2, [&]() { return editor; });
  m_buffers->markDirty(other.id());
  m_buffers->syncNow(other.id());
  QVERIFY(waitUntil([&]() { return !m_buffers->isSaveQueueBusy(other.id()); }));
  QCOMPARE(readBytes(QDir(otherRoot).filePath(QStringLiteral("other.md"))), editor.toUtf8());
  QVERIFY(m_finished->isEmpty());
  applying.release();
  QCOMPARE(finishSync(), VXCORE_OK);
  QCOMPARE(clean.getContentRaw(), QByteArray("incoming while metadata is reserved\n"));
  QVERIFY(m_observer->frozen.isEmpty());
  m_buffers->unregisterActiveWriter(other.id(), 2);
  QVERIFY(!m_notebooks->createFile(m_id, QString(), QStringLiteral("allowed-after.md")).isEmpty());
}

void TestWebDavSyncService::cancelBeforeGuiReservationLeavesIncomingFilesStaged() {
  auto clean = open(QStringLiteral("clean.md"));
  QVERIFY(clean.isValid());
  const auto original = clean.getContentRaw();
  putRemote(QStringLiteral("clean.md"), "cancelled incoming revision\n");
  armDownloadBarrier();
  startSync();
  QVERIFY(waitDownloadBarrier());
  m_sync->cancelSync(m_id);
  releaseDownloadBarrier();
  QCOMPARE(finishSync(), VXCORE_ERR_CANCELLED);
  QCOMPARE(clean.getContentRaw(), original);
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), original);
  QVERIFY(m_changed->isEmpty());
  QVERIFY(m_observer->frozen.isEmpty());
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QCOMPARE(sync(), VXCORE_OK);
  QCOMPARE(clean.getContentRaw(), QByteArray("cancelled incoming revision\n"));
}

void TestWebDavSyncService::cancelWhileDrainingPreservesTheQueuedSave() {
  auto clean = open(QStringLiteral("clean.md"));
  auto saved = open(QStringLiteral("saved.md"));
  QVERIFY(clean.isValid() && saved.isValid());
  const auto original = clean.getContentRaw();
  QString editor = QStringLiteral("queued save survives cancel\n");
  auto held = startDrainWait(saved, editor);
  QVERIFY(held);
  QVERIFY(!m_buffers->isSyncApplyReady(m_id));
  m_sync->cancelSync(m_id);
  QCOMPARE(finishSync(), VXCORE_ERR_CANCELLED);
  QVERIFY(m_observer->frozen.isEmpty());
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), original);
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  held->release();
  QVERIFY(waitUntil([&]() { return !m_buffers->isSaveQueueBusy(saved.id()); }));
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  QVERIFY(m_changed->isEmpty());
  m_buffers->unregisterActiveWriter(saved.id(), 1);
  QVERIFY(!m_notebooks->createFile(m_id, QString(), QStringLiteral("after-cancel.md")).isEmpty());
}

void TestWebDavSyncService::drainTimeoutRetiresReservationWithoutDroppingSave() {
  auto clean = open(QStringLiteral("clean.md"));
  auto saved = open(QStringLiteral("saved.md"));
  QVERIFY(clean.isValid() && saved.isValid());
  const auto original = clean.getContentRaw();
  QString editor = QStringLiteral("queued save survives bounded drain timeout\n");
  auto held = startDrainWait(saved, editor);
  QVERIFY(held);
  QElapsedTimer elapsed;
  elapsed.start();
  QCOMPARE(finishSync(), VXCORE_ERR_SYNC_IN_PROGRESS);
  QVERIFY2(elapsed.elapsed() < 8000, "The five-second save drain did not retire its reservation");
  QVERIFY(m_buffers->isSaveQueueBusy(saved.id()));
  QVERIFY(m_observer->frozen.isEmpty());
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), original);
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  held->release();
  QVERIFY(waitUntil([&]() { return !m_buffers->isSaveQueueBusy(saved.id()); }));
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  QVERIFY(m_changed->isEmpty());
  m_buffers->unregisterActiveWriter(saved.id(), 1);
  QVERIFY(!m_notebooks->createFile(m_id, QString(), QStringLiteral("after-timeout.md")).isEmpty());
}

void TestWebDavSyncService::shutdownWithPendingHandshakeCannotStartLateApply() {
  auto clean = open(QStringLiteral("clean.md"));
  auto saved = open(QStringLiteral("saved.md"));
  QVERIFY(clean.isValid() && saved.isValid());
  const auto original = clean.getContentRaw();
  QString editor = QStringLiteral("queued save survives shutdown\n");
  auto held = startDrainWait(saved, editor);
  QVERIFY(held);
  QVERIFY(!m_buffers->isSyncApplyReady(m_id));
  QElapsedTimer shutdown;
  shutdown.start();
  // Deliberately do not pump the event loop while shutdown joins its queue workers.
  m_sync->shutdown();
  QVERIFY2(shutdown.elapsed() < 3000, "Shutdown waited for a GUI acknowledgement or save drain");
  held->release();
  QVERIFY(waitUntil([&]() { return !m_buffers->isSaveQueueBusy(saved.id()); }));
  // Flush late queued lambdas/timer delivery AFTER shutdown; none may acquire a reservation.
  QCoreApplication::sendPostedEvents();
  QTest::qWait(100);
  QCOMPARE(clean.getContentRaw(), original);
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))), original);
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QVERIFY(m_observer->frozen.isEmpty());
  QVERIFY(m_changed->isEmpty());
  m_buffers->unregisterActiveWriter(saved.id(), 1);
  QVERIFY(!m_notebooks->createFile(m_id, QString(), QStringLiteral("after-shutdown.md")).isEmpty());
}

void TestWebDavSyncService::partialApplyFailureRefreshesInstalledPathsWithoutSuccess() {
  auto clean = open(QStringLiteral("clean.md"));
  QVERIFY(clean.isValid());
  putRemote(QStringLiteral("clean.md"), "installed before later publication failure\n");
  // This payload sorts after clean.md and exceeds the POSIX test-only file limit.
  // All downloads finish before the GUI reservation installs the fault.
  const QByteArray largerPayload(128 * 1024, 'z');
  putRemote(QStringLiteral("z-large.bin"), largerPayload);
  ApplyPublicationFailure obstruction(path(c_state));
  bool installed = false;
  m_observer->replacement = [&](const QString &p_buffer, bool p_active) {
    if (p_buffer == clean.id() && p_active && !installed) {
      installed = obstruction.install();
    }
  };
  const auto result = sync();
  m_observer->replacement = {};
  QVERIFY(installed);
  QCOMPARE(result, VXCORE_ERR_IO);
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))),
           QByteArray("installed before later publication failure\n"));
  QCOMPARE(clean.getContentRaw(), readBytes(path(QStringLiteral("clean.md"))));
  QCOMPARE(m_changed->size(), 1);
  QVERIFY(changedPaths().contains(QStringLiteral("clean.md")));
  QVERIFY(m_observer->frozen.isEmpty());
  QVERIFY(!QFileInfo::exists(path(QStringLiteral("z-large.bin"))));
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QVERIFY(!readObject(path(c_pending)).value(QStringLiteral("operations")).toArray().isEmpty());
  QVERIFY(obstruction.restore());
  QCOMPARE(sync(), VXCORE_OK);
  QCOMPARE(clean.getContentRaw(), QByteArray("installed before later publication failure\n"));
  QCOMPARE(readBytes(path(QStringLiteral("z-large.bin"))), largerPayload);
}

void TestWebDavSyncService::metadataRefreshFailureDoesNotStampSuccess() {
  auto clean = open(QStringLiteral("clean.md"));
  QVERIFY(clean.isValid());
  auto config = QJsonDocument::fromJson(remoteBytes(c_config)).object();
  config.insert(QStringLiteral("description"), QStringLiteral("installed metadata, failed cache"));
  putRemote(c_config, jsonBytes(config));
  putRemote(QStringLiteral("clean.md"), "installed before metadata database failure\n");
  char *localData = nullptr;
  QCOMPARE(vxcore_context_get_data_path(m_context, VXCORE_DATA_LOCAL, &localData), VXCORE_OK);
  const auto database = QDir(QString::fromUtf8(localData))
                            .filePath(QStringLiteral("notebooks/%1/metadata.db").arg(m_id));
  vxcore_string_free(localData);
  QVERIFY(QFileInfo::exists(database));
  {
    DatabaseWriteLock lock;
    QVERIFY(lock.acquire(database));
    const auto result = sync();
    QVERIFY(result != VXCORE_OK);
    QCOMPARE(readObject(path(c_config)).value(QStringLiteral("description")).toString(),
             QStringLiteral("installed metadata, failed cache"));
    QCOMPARE(readBytes(path(QStringLiteral("clean.md"))),
             QByteArray("installed before metadata database failure\n"));
    QVERIFY(changedPaths().contains(c_config));
    QVERIFY(changedPaths().contains(QStringLiteral("clean.md")));
    QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
    QVERIFY(m_observer->frozen.isEmpty());
  }
  QVERIFY(!m_notebooks->createFile(m_id, QString(), QStringLiteral("after-refresh-error.md"))
               .isEmpty());
}

void TestWebDavSyncService::conflictSnapshotCarriesKeepBothPolicy() {
  auto local = m_notebooks->getNotebookConfig(m_id);
  local.insert(QStringLiteral("description"), QStringLiteral("local metadata"));
  QVERIFY(m_notebooks->updateNotebookConfig(m_id, QString::fromUtf8(jsonBytes(local))));
  auto remote = QJsonDocument::fromJson(remoteBytes(c_config)).object();
  remote.insert(QStringLiteral("description"), QStringLiteral("remote metadata"));
  putRemote(c_config, jsonBytes(remote));
  QVERIFY(writeBytes(path(QStringLiteral("draft.md")), "local ordinary conflict\n"));
  putRemote(QStringLiteral("draft.md"), "remote ordinary conflict\n");
  QCOMPARE(sync(), VXCORE_ERR_SYNC_CONFLICT);
  QVERIFY(conflictPaths().contains(c_config));
  QVERIFY(conflictPaths().contains(QStringLiteral("draft.md")));
  const auto policy = m_sync->keepBothUnsupportedPaths(m_id);
  QVERIFY(policy.contains(c_config));
  QVERIFY(!policy.contains(QStringLiteral("draft.md")));
  QString encoded;
  QCOMPARE(m_notebooks->getSyncConflicts(m_id, encoded), VXCORE_OK);
  const auto conflicts = QJsonDocument::fromJson(encoded.toUtf8())
                             .object()
                             .value(QStringLiteral("conflicts"))
                             .toArray();
  QSet<QString> found;
  for (const auto &value : conflicts) {
    const auto conflict = value.toObject();
    const auto name = conflict.value(QLatin1String(vxcore::kJsonKeyPath)).toString();
    found.insert(name);
    QCOMPARE(conflict.value(QLatin1String(vxcore::kJsonKeyCanKeepBoth)).toBool(),
             !policy.contains(name));
  }
  QVERIFY(found.contains(c_config) && found.contains(QStringLiteral("draft.md")));
  QCOMPARE(m_notebooks->resolveSyncConflict(m_id, c_config, QStringLiteral("keep_both")),
           VXCORE_ERR_UNSUPPORTED);
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QCOMPARE(readBytes(path(QStringLiteral("draft.md"))), QByteArray("local ordinary conflict\n"));
  QCOMPARE(remoteBytes(QStringLiteral("draft.md")), QByteArray("remote ordinary conflict\n"));
}

void TestWebDavSyncService::liveEncryptedLeaseRefusesEnvelopeReplacement() {
  const QByteArray password("disposable-service-password");
  auto preparation = m_notebooks->prepareNotebookEncryption(m_id, QString(), password);
  QVERIFY(preparation.isValid());
  QString noteId;
  const QByteArray plaintext("private original body must never appear in DAV or sync state");
  auto create = std::async(std::launch::async, [&]() {
    NotebookIoGate::ScopedLock gate(*m_gate, m_id);
    auto result = m_notebooks->commitNotebookEncryption(preparation);
    if (result == VXCORE_OK) {
      result = m_notebooks->createEncryptedNote(m_id, QString(), QStringLiteral("secret.md"),
                                                QStringLiteral("markdown"), plaintext, &noteId);
    }
    return result;
  });
  QVERIFY(waitUntil([&]() {
    return create.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
  }));
  QCOMPARE(create.get(), VXCORE_OK);
  auto note = m_buffers->openBufferByNodeId(noteId);
  QVERIFY(note.isValid() && note.isEncrypted());
  QCOMPARE(note.getContentRaw(), plaintext);
  const auto notePath = note.nodeId().relativePath;
  const auto oldCiphertext = readBytes(path(notePath));
  const auto oldEnvelope = readBytes(path(c_envelope));
  QCOMPARE(sync(), VXCORE_OK);
  m_lastSuccess = m_notebooks->getLastSyncUtc(m_id);
  QCOMPARE(remoteBytes(notePath), oldCiphertext);
  QCOMPARE(remoteBytes(c_envelope), oldEnvelope);

  using Encryption = vxcore::NotebookEncryption;
  Encryption::KeyEnvelope envelope;
  Encryption::Key masterKey, notebookKey, noteKey;
  Encryption::ObjectHeader header;
  QCOMPARE(Encryption::ReadObjectHeader(vxcore::PathFromUtf8(path(notePath).toUtf8().toStdString()),
                                        header),
           VXCORE_OK);
  QCOMPARE(Encryption::PrepareNewKeys(m_id.toStdString(), password.constData(),
                                      size_t(password.size()), envelope, masterKey, notebookKey),
           VXCORE_OK);
  QCOMPARE(Encryption::GenerateKey(noteKey), VXCORE_OK);
  const QByteArray incomingPlaintext("private incoming body is encrypted as a complete revision");
  const std::vector<uint8_t> body(incomingPlaintext.cbegin(), incomingPlaintext.cend());
  const auto staged = m_temp->filePath(QStringLiteral("replacement.vne"));
  QCOMPARE(Encryption::WriteNoteSnapshot(vxcore::PathFromUtf8(staged.toUtf8().toStdString()),
                                         envelope, notebookKey, noteKey, header.document_id, body,
                                         {{"editorType", "markdown"}}),
           VXCORE_OK);
  std::string encodedEnvelope;
  QCOMPARE(Encryption::EncodeKeyEnvelope(envelope, encodedEnvelope), VXCORE_OK);
  const auto incomingEnvelope = QByteArray::fromStdString(encodedEnvelope);
  const auto incomingCiphertext = readBytes(staged);
  putRemote(c_envelope, incomingEnvelope);
  putRemote(notePath, incomingCiphertext);
  auto lease = note.acquireProtectedLease();
  QVERIFY(lease && lease->isCurrent());
  const auto refused = sync();
  QVERIFY(refused == VXCORE_ERR_SYNC_IN_PROGRESS || refused == VXCORE_ERR_ENCRYPTION_LOCKED);
  QCOMPARE(readBytes(path(c_envelope)), oldEnvelope);
  QCOMPARE(readBytes(path(notePath)), oldCiphertext);
  // Pending key-envelope recovery deliberately blocks all protected core reads,
  // including cached plaintext. The view-owned text is not reread through this guard.
  const void *unavailable = reinterpret_cast<const void *>(quintptr(1));
  size_t unavailableSize = 1;
  QCOMPARE(vxcore_buffer_get_content_raw(m_context, note.id().toUtf8().constData(), &unavailable,
                                         &unavailableSize),
           VXCORE_ERR_SYNC_CONFLICT);
  QVERIFY(unavailable == nullptr);
  QCOMPARE(unavailableSize, size_t(0));
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), m_lastSuccess);
  QVERIFY(m_changed->isEmpty());
  lease.reset();
  QVERIFY(m_buffers->closeBuffer(note.id()));
  QCOMPARE(m_notebooks->lockAllEncryption(), VXCORE_OK);
  QCOMPARE(sync(), VXCORE_OK);
  QCOMPARE(readBytes(path(c_envelope)), incomingEnvelope);
  QCOMPARE(readBytes(path(notePath)), incomingCiphertext);
  QVERIFY(!m_notebooks->encryptionStatus(m_id).value(QStringLiteral("unlocked")).toBool());
  QCOMPARE(m_notebooks->unlockNotebookEncryption(m_id, password), VXCORE_OK);
  note = m_buffers->openBufferByNodeId(noteId);
  QVERIFY(note.isValid());
  QCOMPARE(note.getContentRaw(), incomingPlaintext);

  const auto tree =
      control({{QStringLiteral("action"), QStringLiteral("tree")}, {QStringLiteral("limit"), 1000}})
          .value(QStringLiteral("entries"))
          .toArray();
  for (const auto &value : tree) {
    const auto entry = value.toObject();
    if (entry.value(QStringLiteral("kind")).toString() != QLatin1String("file")) {
      continue;
    }
    const auto remotePath = entry.value(QStringLiteral("path")).toString();
    QVERIFY(!remotePath.startsWith(QStringLiteral("vx_notebook/vx_sync/")));
    const auto bytes = remoteBytes(remotePath);
    QVERIFY(!bytes.contains(plaintext));
    QVERIFY(!bytes.contains(incomingPlaintext));
  }
  QDirIterator snapshots(path(QStringLiteral("vx_notebook/vx_sync/webdav")), QDir::Files,
                         QDirIterator::Subdirectories);
  while (snapshots.hasNext()) {
    const auto bytes = readBytes(snapshots.next());
    QVERIFY(!bytes.contains(plaintext));
    QVERIFY(!bytes.contains(incomingPlaintext));
  }
  QVERIFY(m_buffers->closeBuffer(note.id()));
  QCOMPARE(m_notebooks->lockAllEncryption(), VXCORE_OK);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestWebDavSyncService)
#include "test_webdav_sync_service.moc"
