// SPDX-License-Identifier: LGPL-3.0-or-later
// Real TLS provider profile, real sync engine/executor; only the OS vault is replaced.
#include <QDir>
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
#include <QtTest>
#include <chrono>
#include <curl/curl.h>
#include <functional>
#include <future>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <csignal>
#include <sys/resource.h>
#endif
#include "core/notebook.h"
#include "sync/credential_provider.h"
#include "sync/sync_backend_registry.h"
#include "sync/sync_json_keys.h"
#include "utils/file_utils.h"
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
#include <core/services/syncworkqueuemanager.h>
#include <temp_dir_fixture.h>
#include <test_helper.h>
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

using namespace vnotex;
namespace tests {
namespace {
const QString c_config = QStringLiteral("vx_notebook/config.json");
const QString c_state = QStringLiteral("vx_notebook/vx_sync/jianguoyun/state.json");
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
    // POSIX cases must include an incoming payload larger than this write limit.
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

class MemoryCredentialsStore final : public SyncCredentialsStore {
public:
  explicit MemoryCredentialsStore(ServiceLocator &p_services) : SyncCredentialsStore(p_services) {}
  bool hasCredentials(const QString &p_id) const override { return m_values.contains(p_id); }
  void storeCredentials(const QString &p_id, const SyncCredential &p_value) override {
    m_values.insert(p_id, p_value);
    QMetaObject::invokeMethod(
        this, [this, p_id]() { emit credentialsStored(p_id); }, Qt::QueuedConnection);
  }
  void retrieveCredentials(const QString &p_id) override {
    const auto value = m_values.value(p_id);
    QMetaObject::invokeMethod(
        this, [this, p_id, value]() { emit credentialsRetrieved(p_id, value); },
        Qt::QueuedConnection);
  }
  void deleteCredentials(const QString &p_id) override {
    m_values.remove(p_id);
    QMetaObject::invokeMethod(
        this, [this, p_id]() { emit credentialsDeleted(p_id); }, Qt::QueuedConnection);
  }
  void refreshKnownIds() override {}

private:
  QHash<QString, SyncCredential> m_values;
};
} // namespace

class JianguoyunBufferObserver final : public QObject {
  Q_OBJECT
public:
  explicit JianguoyunBufferObserver(BufferService &p_buffers) {
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

class TestJianguoyunSyncService : public QObject {
  Q_OBJECT
public:
  ~TestJianguoyunSyncService() override { teardown(); }
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void persistenceFailureCannotPublishGenesis();
  void incomingRevisionReloadsBufferAndMetadata();
  void dirtyEditorAndCommentProtectWholeCohort();
  void queuedSaveDrainsBeforeApply();
  void partialApplyRefreshesOnlyInstalledPaths();
  void metadataRefreshFailureDoesNotStampSuccess();
  void encryptedLeaseRefusesEnvelopeReplacement();
  void interruptedEncryptedApplyResumesVerifiedCohort();

private:
  void teardown();
  void exerciseEncryptedCohort(bool p_interrupted);
  QString path(const QString &p_relative) const { return QDir(m_root).filePath(p_relative); }
  SyncSettings settings() const;
  VxCoreError bootstrap();
  VxCoreError peerClone();
  VxCoreError peerSync();
  VxCoreError waitSync();
  VxCoreError sync() {
    m_finished->clear();
    m_changed->clear();
    m_sync->triggerSyncNow(m_id);
    return waitSync();
  }
  bool peerWrite(const QString &p_path, const QByteArray &p_bytes) {
    return writeBytes(QDir(m_peerRoot).filePath(p_path), p_bytes);
  }
  void armHeadBarrier();
  bool waitHeadBarrier();
  void releaseHeadBarrier();
  QStringList changedPaths() const;
  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<TempDirFixture> m_temp;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<CommentService> m_comments;
  std::unique_ptr<SyncWorkQueueManager> m_queue;
  std::unique_ptr<MemoryCredentialsStore> m_credentials;
  std::unique_ptr<SyncService> m_sync;
  std::unique_ptr<JianguoyunBufferObserver> m_observer;
  std::unique_ptr<QSignalSpy> m_finished;
  std::unique_ptr<QSignalSpy> m_changed;
  std::unique_ptr<vxcore::ISyncBackend> m_peer;
  QString m_root, m_peerRoot, m_id, m_commentPath;
};

void TestJianguoyunSyncService::initTestCase() {
  vxcore_set_test_mode(1);
  QVERIFY(QUrl(QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_URL"))).scheme() ==
          QLatin1String("https"));
  QVERIFY(QFileInfo::exists(QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_CA_FILE"))));
  QCOMPARE(curl_global_init(CURL_GLOBAL_DEFAULT), CURLE_OK);
  qRegisterMetaType<VxCoreError>("VxCoreError");
  qRegisterMetaType<NodeIdentifier>("NodeIdentifier");
  qRegisterMetaType<NodeIdentifier>("vnotex::NodeIdentifier");
}

void TestJianguoyunSyncService::init() {
  teardown();
  bool ready = false;
  const auto rollback = qScopeGuard([&]() {
    if (!ready)
      teardown();
  });
  control({{QStringLiteral("action"), QStringLiteral("reset")}});
  vxcore_clear_test_directory();
  m_temp.reset(new TempDirFixture());
  QVERIFY(m_temp->isValid());
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_services.reset(new ServiceLocator());
  m_hooks.reset(new HookManager());
  m_gate.reset(new NotebookIoGate());
  m_notebooks.reset(new NotebookCoreService(m_context));
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_buffers.reset(new BufferService(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None));
  m_comments.reset(new CommentService(m_notebooks.get(), m_gate.get(), m_hooks.get()));
  m_queue.reset(new SyncWorkQueueManager());
  m_credentials.reset(new MemoryCredentialsStore(*m_services));
  m_services->registerService<NotebookCoreService>(m_notebooks.get());
  m_services->registerService<BufferService>(m_buffers.get());
  m_services->registerService<CommentService>(m_comments.get());
  m_services->registerService<NotebookIoGate>(m_gate.get());
  m_services->registerService<SyncWorkQueueManager>(m_queue.get());
  m_services->registerService<SyncCredentialsStore>(m_credentials.get());
  m_sync.reset(new SyncService(*m_services));
  m_observer.reset(new JianguoyunBufferObserver(*m_buffers));
  m_finished.reset(new QSignalSpy(m_sync.get(), &SyncService::syncFinished));
  m_changed.reset(new QSignalSpy(m_sync.get(), &SyncService::workingTreeChanged));
  m_root = m_temp->filePath(QStringLiteral("notebook"));
  m_peerRoot = m_temp->filePath(QStringLiteral("peer"));
  m_id = m_notebooks->createNotebook(m_root, QStringLiteral("{\"name\":\"Managed service\"}"),
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
  auto config = m_notebooks->getNotebookConfig(m_id);
  config[QLatin1String(vxcore::kJsonKeyAutoSyncEnabled)] = false;
  QVERIFY(m_notebooks->updateNotebookConfig(m_id, QString::fromUtf8(jsonBytes(config))));
  ready = true;
}

void TestJianguoyunSyncService::cleanup() { teardown(); }
void TestJianguoyunSyncService::teardown() {
  if (m_context) {
    try {
      control({{QStringLiteral("action"), QStringLiteral("clear_faults")}});
    } catch (...) {
    }
  }
  if (m_observer)
    m_observer->replacement = {};
  if (m_sync)
    m_sync->shutdown();
  if (m_comments)
    m_comments->shutdown();
  if (m_buffers)
    m_buffers->shutdown();
  m_peer.reset();
  m_finished.reset();
  m_changed.reset();
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
  if (m_context)
    vxcore_context_destroy(m_context);
  m_context = nullptr;
  m_temp.reset();
  m_id.clear();
  m_root.clear();
  m_peerRoot.clear();
  m_commentPath.clear();
}

SyncSettings TestJianguoyunSyncService::settings() const {
  return {QStringLiteral("jianguoyun"),
          QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_URL")),
          {QStringLiteral("jianguoyun"), QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_USERNAME")),
           QString::fromUtf8(qgetenv("VXCORE_WEBDAV_TEST_PASSWORD"))}};
}

VxCoreError TestJianguoyunSyncService::bootstrap() {
  QSignalSpy setup(m_sync.get(), &SyncService::bootstrapAndPersistFinished);
  m_finished->clear();
  m_changed->clear();
  m_sync->bootstrapAndPersist(m_id, settings());
  if (!waitUntil([&]() { return !setup.isEmpty(); }, 60000))
    return VXCORE_ERR_UNKNOWN;
  const auto result = VxCoreError(setup.first().at(1).toInt());
  return result == VXCORE_OK ? waitSync() : result;
}

VxCoreError TestJianguoyunSyncService::waitSync() {
  if (!waitUntil([&]() { return !m_finished->isEmpty() && !m_sync->isSyncInProgress(m_id); },
                 60000))
    return VXCORE_ERR_UNKNOWN;
  if (m_finished->size() != 1 || m_finished->first().at(0).toString() != m_id)
    return VXCORE_ERR_UNKNOWN;
  return qvariant_cast<VxCoreError>(m_finished->first().at(1));
}

VxCoreError TestJianguoyunSyncService::peerClone() {
  if (!QDir().mkpath(m_peerRoot))
    return VXCORE_ERR_IO;
  vxcore::SyncConfig config;
  config.backend = "jianguoyun";
  config.remote_url = settings().m_remoteUrl.toStdString();
  vxcore::SyncCredentials credentials;
  credentials.extra = {
      {vxcore::kJsonKeyUsername, settings().m_credentials.m_username.toStdString()},
      {vxcore::kJsonKeyPassword, settings().m_credentials.m_secret.toStdString()}};
  m_peer = vxcore::SyncBackendRegistry::Instance().Create(
      config.backend, config, std::make_shared<vxcore::InMemoryCredentialProvider>(credentials));
  if (!m_peer)
    return VXCORE_ERR_UNKNOWN_BACKEND;
  auto work = std::async(std::launch::async, [&]() {
    auto result = m_peer->Clone(m_peerRoot.toStdString(), config);
    return result == VXCORE_OK ? m_peer->Initialize(m_peerRoot.toStdString(), config) : result;
  });
  if (!waitUntil(
          [&]() {
            return work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
          },
          60000))
    return VXCORE_ERR_UNKNOWN;
  return work.get();
}
VxCoreError TestJianguoyunSyncService::peerSync() {
  auto work = std::async(std::launch::async, [&]() { return m_peer->Sync(nullptr, nullptr); });
  if (!waitUntil(
          [&]() {
            return work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
          },
          60000))
    return VXCORE_ERR_UNKNOWN;
  return work.get();
}
QStringList TestJianguoyunSyncService::changedPaths() const {
  QStringList paths;
  for (const auto &event : *m_changed)
    if (event.at(0).toString() == m_id)
      paths.append(event.at(1).toStringList());
  return paths;
}
void TestJianguoyunSyncService::armHeadBarrier() {
  control({{QStringLiteral("action"), QStringLiteral("barrier")},
           {QStringLiteral("id"), QStringLiteral("head")},
           {QStringLiteral("method"), QStringLiteral("GET")},
           {QStringLiteral("path"), QStringLiteral(".vnote-sync/head.json")},
           {QStringLiteral("phase"), QStringLiteral("before")}});
}
bool TestJianguoyunSyncService::waitHeadBarrier() {
  return control({{QStringLiteral("action"), QStringLiteral("wait")},
                  {QStringLiteral("id"), QStringLiteral("head")},
                  {QStringLiteral("timeout_ms"), 10000}})
      .value(QStringLiteral("reached"))
      .toBool();
}
void TestJianguoyunSyncService::releaseHeadBarrier() {
  control({{QStringLiteral("action"), QStringLiteral("release")},
           {QStringLiteral("id"), QStringLiteral("head")}});
}

void TestJianguoyunSyncService::persistenceFailureCannotPublishGenesis() {
  m_sync->testForceNextPersistFailure(QStringLiteral("injected durable routing failure"));
  QVERIFY(bootstrap() != VXCORE_OK);
  const auto head = control({{QStringLiteral("action"), QStringLiteral("inspect")},
                             {QStringLiteral("path"), QStringLiteral(".vnote-sync/head.json")}});
  QVERIFY(!head.value(QStringLiteral("exists")).toBool());
  QVERIFY(QFileInfo::exists(path(QStringLiteral("clean.md"))));
  QVERIFY(!m_sync->isSyncRegistered(m_id));
}

void TestJianguoyunSyncService::incomingRevisionReloadsBufferAndMetadata() {
  QCOMPARE(bootstrap(), VXCORE_OK);
  QCOMPARE(peerClone(), VXCORE_OK);
  auto clean = m_buffers->openBuffer({m_id, QStringLiteral("clean.md")});
  QVERIFY(clean.isValid());
  QVERIFY(peerWrite(QStringLiteral("clean.md"), "incoming managed bytes\n"));
  auto config = readObject(QDir(m_peerRoot).filePath(c_config));
  config[QStringLiteral("description")] = QStringLiteral("managed metadata revision");
  QVERIFY(peerWrite(c_config, jsonBytes(config)));
  QCOMPARE(peerSync(), VXCORE_OK);
  const auto previous = m_notebooks->getLastSyncUtc(m_id);
  bool completedWithInstalledBytes = false;
  connect(m_sync.get(), &SyncService::syncFinished, this,
          [&](const QString &id, VxCoreError result) {
            if (id == m_id && result == VXCORE_OK)
              completedWithInstalledBytes =
                  clean.getContentRaw() == QByteArray("incoming managed bytes\n");
          });
  QCOMPARE(sync(), VXCORE_OK);
  QVERIFY(completedWithInstalledBytes);
  QCOMPARE(clean.getContentRaw(), QByteArray("incoming managed bytes\n"));
  QCOMPARE(m_notebooks->getNotebookConfig(m_id).value(QStringLiteral("description")).toString(),
           QStringLiteral("managed metadata revision"));
  QVERIFY(changedPaths().contains(QStringLiteral("clean.md")) && changedPaths().contains(c_config));
  QVERIFY(m_notebooks->getLastSyncUtc(m_id) > previous);
  QVERIFY(m_observer->frozen.isEmpty());
}

void TestJianguoyunSyncService::dirtyEditorAndCommentProtectWholeCohort() {
  QCOMPARE(bootstrap(), VXCORE_OK);
  QCOMPARE(peerClone(), VXCORE_OK);
  auto clean = m_buffers->openBuffer({m_id, QStringLiteral("clean.md")});
  auto draft = m_buffers->openBuffer({m_id, QStringLiteral("draft.md")});
  QVERIFY(clean.isValid() && draft.isValid());
  const auto original = clean.getContentRaw();
  const auto originalComment = readBytes(path(m_commentPath));
  const auto previous = m_notebooks->getLastSyncUtc(m_id);
  QVERIFY(peerWrite(QStringLiteral("clean.md"), "remote clean\n"));
  QVERIFY(peerWrite(QStringLiteral("draft.md"), "remote draft\n"));
  auto remoteComments = m_comments->load({m_id, QStringLiteral("draft.md")}).m_comments;
  remoteComments.m_comments.first().m_text = QStringLiteral("remote comment");
  QVERIFY(peerWrite(m_commentPath, jsonBytes(remoteComments.toJson())));
  QCOMPARE(peerSync(), VXCORE_OK);
  armHeadBarrier();
  m_finished->clear();
  m_changed->clear();
  m_sync->triggerSyncNow(m_id);
  QVERIFY(waitHeadBarrier());
  QVERIFY(draft.setContentRaw("unsaved editor revision\n"));
  m_buffers->markDirty(draft.id());
  int flushes = 0;
  auto participant = m_comments->registerFlushParticipant(
      {m_id, QStringLiteral("draft.md")}, [&]() { ++flushes; }, 2);
  QVERIFY(participant.isValid());
  releaseHeadBarrier();
  QCOMPARE(waitSync(), VXCORE_ERR_SYNC_CONFLICT);
  QCOMPARE(clean.getContentRaw(), original);
  QCOMPARE(readBytes(path(m_commentPath)), originalComment);
  QCOMPARE(draft.getContentRaw(), QByteArray("unsaved editor revision\n"));
  QVERIFY(draft.isModified() || m_buffers->isDirty(draft.id()));
  QCOMPARE(flushes, 0);
  QVERIFY(m_changed->isEmpty());
  QVERIFY(m_observer->frozen.isEmpty());
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), previous);
}

void TestJianguoyunSyncService::queuedSaveDrainsBeforeApply() {
  QCOMPARE(bootstrap(), VXCORE_OK);
  QCOMPARE(peerClone(), VXCORE_OK);
  auto clean = m_buffers->openBuffer({m_id, QStringLiteral("clean.md")});
  auto saved = m_buffers->openBuffer({m_id, QStringLiteral("saved.md")});
  QVERIFY(clean.isValid() && saved.isValid());
  const auto original = clean.getContentRaw();
  const auto previous = m_notebooks->getLastSyncUtc(m_id);
  QVERIFY(peerWrite(QStringLiteral("clean.md"), "incoming after save\n"));
  QVERIFY(peerWrite(QStringLiteral("saved.md"), "competing save target\n"));
  QCOMPARE(peerSync(), VXCORE_OK);
  armHeadBarrier();
  m_finished->clear();
  m_changed->clear();
  m_sync->triggerSyncNow(m_id);
  QVERIFY(waitHeadBarrier());
  auto held = std::make_unique<GateBarrier>(*m_gate, m_id);
  QString editor = QStringLiteral("queued local save\n");
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  m_buffers->registerActiveWriter(saved.id(), 1, [&]() { return editor; });
  const auto retireWriter =
      qScopeGuard([&]() { m_buffers->unregisterActiveWriter(saved.id(), 1); });
  m_buffers->markDirty(saved.id());
  m_buffers->syncNow(saved.id());
  releaseHeadBarrier();
  QVERIFY(waitUntil([&]() { return !m_observer->frozen.isEmpty(); }, 10000));
  QVERIFY(m_buffers->isSaveQueueBusy(saved.id()));
  QVERIFY(m_finished->isEmpty());
  QCOMPARE(clean.getContentRaw(), original);
  held->release();
  QCOMPARE(waitSync(), VXCORE_ERR_SYNC_CONFLICT);
  QCOMPARE(readBytes(path(QStringLiteral("saved.md"))), editor.toUtf8());
  QCOMPARE(clean.getContentRaw(), original);
  QVERIFY(m_observer->frozen.isEmpty());
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), previous);
}

void TestJianguoyunSyncService::partialApplyRefreshesOnlyInstalledPaths() {
  QCOMPARE(bootstrap(), VXCORE_OK);
  QCOMPARE(peerClone(), VXCORE_OK);
  auto clean = m_buffers->openBuffer({m_id, QStringLiteral("clean.md")});
  QVERIFY(clean.isValid());
  const auto previous = m_notebooks->getLastSyncUtc(m_id);
  QVERIFY(peerWrite(QStringLiteral("clean.md"), "installed before failure\n"));
  QVERIFY(peerWrite(QStringLiteral("z-large.bin"), QByteArray(128 * 1024, 'z')));
  QCOMPARE(peerSync(), VXCORE_OK);
  ApplyPublicationFailure obstruction(path(c_state));
  bool installed = false;
  m_observer->replacement = [&](const QString &id, bool active) {
    if (id == clean.id() && active && !installed)
      installed = obstruction.install();
  };
  const auto result = sync();
  m_observer->replacement = {};
  QVERIFY(installed);
  QCOMPARE(result, VXCORE_ERR_IO);
  QCOMPARE(clean.getContentRaw(), QByteArray("installed before failure\n"));
  QVERIFY(changedPaths().contains(QStringLiteral("clean.md")));
  QVERIFY(!QFileInfo::exists(path(QStringLiteral("z-large.bin"))));
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), previous);
  QVERIFY(m_observer->frozen.isEmpty());
  QVERIFY(obstruction.restore());
  QCOMPARE(sync(), VXCORE_OK);
  QCOMPARE(readBytes(path(QStringLiteral("z-large.bin"))), QByteArray(128 * 1024, 'z'));
}

void TestJianguoyunSyncService::metadataRefreshFailureDoesNotStampSuccess() {
  QCOMPARE(bootstrap(), VXCORE_OK);
  QCOMPARE(peerClone(), VXCORE_OK);
  const auto previous = m_notebooks->getLastSyncUtc(m_id);
  auto config = readObject(QDir(m_peerRoot).filePath(c_config));
  config[QStringLiteral("description")] = QStringLiteral("installed before cache failure");
  QVERIFY(peerWrite(c_config, jsonBytes(config)));
  QVERIFY(peerWrite(QStringLiteral("clean.md"), "installed despite cache failure\n"));
  QCOMPARE(peerSync(), VXCORE_OK);
  char *localData = nullptr;
  QCOMPARE(vxcore_context_get_data_path(m_context, VXCORE_DATA_LOCAL, &localData), VXCORE_OK);
  const auto database = QDir(QString::fromUtf8(localData))
                            .filePath(QStringLiteral("notebooks/%1/metadata.db").arg(m_id));
  vxcore_string_free(localData);
  DatabaseWriteLock lock;
  QVERIFY(lock.acquire(database));
  QVERIFY(sync() != VXCORE_OK);
  QCOMPARE(readObject(path(c_config)).value(QStringLiteral("description")).toString(),
           QStringLiteral("installed before cache failure"));
  QCOMPARE(readBytes(path(QStringLiteral("clean.md"))),
           QByteArray("installed despite cache failure\n"));
  QVERIFY(changedPaths().contains(c_config));
  QCOMPARE(m_notebooks->getLastSyncUtc(m_id), previous);
  QVERIFY(m_observer->frozen.isEmpty());
}

void TestJianguoyunSyncService::encryptedLeaseRefusesEnvelopeReplacement() {
  exerciseEncryptedCohort(false);
}
void TestJianguoyunSyncService::interruptedEncryptedApplyResumesVerifiedCohort() {
  exerciseEncryptedCohort(true);
}
void TestJianguoyunSyncService::exerciseEncryptedCohort(bool p_interrupted) {
  QCOMPARE(bootstrap(), VXCORE_OK);
  const QByteArray password("disposable-managed-service-password");
  auto preparation = m_notebooks->prepareNotebookEncryption(m_id, QString(), password);
  QVERIFY(preparation.isValid());
  QString noteId;
  const QByteArray plaintext("private managed original body");
  auto create = std::async(std::launch::async, [&]() {
    NotebookIoGate::ScopedLock gate(*m_gate, m_id);
    auto result = m_notebooks->commitNotebookEncryption(preparation);
    return result == VXCORE_OK
               ? m_notebooks->createEncryptedNote(m_id, QString(), QStringLiteral("secret.md"),
                                                  QStringLiteral("markdown"), plaintext, &noteId)
               : result;
  });
  QVERIFY(waitUntil([&]() {
    return create.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
  }));
  QCOMPARE(create.get(), VXCORE_OK);
  auto note = m_buffers->openBufferByNodeId(noteId);
  QVERIFY(note.isValid() && note.isEncrypted());
  const auto notePath = note.nodeId().relativePath;
  const auto oldCiphertext = readBytes(path(notePath)), oldEnvelope = readBytes(path(c_envelope));
  QCOMPARE(sync(), VXCORE_OK);
  QCOMPARE(peerClone(), VXCORE_OK);
  const auto previous = m_notebooks->getLastSyncUtc(m_id);
  using Encryption = vxcore::NotebookEncryption;
  Encryption::KeyEnvelope envelope;
  Encryption::Key masterKey, notebookKey, noteKey;
  Encryption::ObjectHeader header;
  QCOMPARE(Encryption::ReadObjectHeader(vxcore::PathFromUtf8(path(notePath).toStdString()), header),
           VXCORE_OK);
  QCOMPARE(Encryption::PrepareNewKeys(m_id.toStdString(), password.constData(),
                                      size_t(password.size()), envelope, masterKey, notebookKey),
           VXCORE_OK);
  QCOMPARE(Encryption::GenerateKey(noteKey), VXCORE_OK);
  // Exceed the POSIX write limit after a smaller incoming file has been installed.
  const QByteArray incomingPlaintext =
      QByteArray("private managed replacement body\n") + QByteArray(128 * 1024, 'x');
  const std::vector<uint8_t> body(incomingPlaintext.cbegin(), incomingPlaintext.cend());
  const auto replacement = m_temp->filePath(QStringLiteral("replacement.vne"));
  QCOMPARE(Encryption::WriteNoteSnapshot(vxcore::PathFromUtf8(replacement.toStdString()), envelope,
                                         notebookKey, noteKey, header.document_id, body,
                                         {{"editorType", "markdown"}}),
           VXCORE_OK);
  QVERIFY(QFileInfo(replacement).size() > 64 * 1024);
  std::string encodedEnvelope;
  QCOMPARE(Encryption::EncodeKeyEnvelope(envelope, encodedEnvelope), VXCORE_OK);
  QVERIFY(peerWrite(c_envelope, QByteArray::fromStdString(encodedEnvelope)));
  QVERIFY(peerWrite(notePath, readBytes(replacement)));
  QVERIFY(peerWrite(QStringLiteral("clean.md"), "installed before encrypted failure\n"));
  QCOMPARE(peerSync(), VXCORE_OK);
  if (p_interrupted) {
    QVERIFY(m_buffers->closeBuffer(note.id()));
    QCOMPARE(m_notebooks->lockAllEncryption(), VXCORE_OK);
    auto clean = m_buffers->openBuffer({m_id, QStringLiteral("clean.md")});
    QVERIFY(clean.isValid());
    ApplyPublicationFailure obstruction(path(c_state));
    bool installed = false;
    m_observer->replacement = [&](const QString &id, bool active) {
      if (id == clean.id() && active && !installed)
        installed = obstruction.install();
    };
    const auto partial = sync();
    m_observer->replacement = {};
    QVERIFY(installed);
    QCOMPARE(partial, VXCORE_ERR_IO);
    QVERIFY(!changedPaths().isEmpty());
    QCOMPARE(m_notebooks->getLastSyncUtc(m_id), previous);
    QVERIFY(obstruction.restore());
  } else {
    auto lease = note.acquireProtectedLease();
    QVERIFY(lease && lease->isCurrent());
    const auto refused = sync();
    QVERIFY(refused == VXCORE_ERR_SYNC_IN_PROGRESS || refused == VXCORE_ERR_ENCRYPTION_LOCKED);
    QCOMPARE(readBytes(path(c_envelope)), oldEnvelope);
    QCOMPARE(readBytes(path(notePath)), oldCiphertext);
    QCOMPARE(m_notebooks->getLastSyncUtc(m_id), previous);
    QVERIFY(m_changed->isEmpty());
    lease.reset();
    QVERIFY(m_buffers->closeBuffer(note.id()));
    QCOMPARE(m_notebooks->lockAllEncryption(), VXCORE_OK);
  }
  QCOMPARE(sync(), VXCORE_OK);
  QCOMPARE(readBytes(path(c_envelope)), QByteArray::fromStdString(encodedEnvelope));
  QCOMPARE(m_notebooks->unlockNotebookEncryption(m_id, password), VXCORE_OK);
  note = m_buffers->openBufferByNodeId(noteId);
  QVERIFY(note.isValid());
  QCOMPARE(note.getContentRaw(), incomingPlaintext);
  QVERIFY(m_buffers->closeBuffer(note.id()));
  QCOMPARE(m_notebooks->lockAllEncryption(), VXCORE_OK);
}
} // namespace tests
QTEST_GUILESS_MAIN(tests::TestJianguoyunSyncService)
#include "test_jianguoyun_sync_service.moc"
