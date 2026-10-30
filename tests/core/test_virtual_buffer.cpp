#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#include <core/hooknames.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

namespace {

// Drive the real autosave timeout explicitly, without a wall-clock race against
// semaphore-controlled saves or the manual-save nested event loop.
class ScopedAutoSaveTimer {
public:
  explicit ScopedAutoSaveTimer(BufferService *service)
      : m_timer(service->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly)),
        m_interval(m_timer->interval()) {
    m_timer->setInterval(std::numeric_limits<int>::max());
  }

  ~ScopedAutoSaveTimer() { m_timer->setInterval(m_interval); }

  bool tick() { return QMetaObject::invokeMethod(m_timer, "timeout", Qt::DirectConnection); }

private:
  QTimer *m_timer;
  int m_interval;
};

struct MemoryDestination {
  QMutex mutex;
  QByteArray pending;
  QByteArray committed = "original destination";
  std::atomic<int> stages{0};
  std::atomic<bool> stagedOffThread{false};
  std::atomic<int> stageError{VXCORE_OK};
  int finalizations = 0;
  bool finalizedOnOwner = false;
  bool throwException = false;
  bool throwBadAlloc = false;
  VxCoreError finalizeError = VXCORE_OK;
};

MemoryBufferSource stagedSource(const QString &address,
                                const std::shared_ptr<MemoryDestination> &destination) {
  MemoryBufferSource source;
  source.address = address;
  source.displayName = QStringLiteral("Staged destination");
  source.editorType = QStringLiteral("Text");
  source.content = destination->committed;
  const auto owner = QThread::currentThread();
  source.save = [destination, owner](QByteArrayView bytes) {
    ++destination->stages;
    destination->stagedOffThread = QThread::currentThread() != owner;
    const auto error = static_cast<VxCoreError>(destination->stageError.load());
    if (error != VXCORE_OK)
      return error;
    QMutexLocker lock(&destination->mutex);
    destination->pending = QByteArray(bytes.data(), bytes.size());
    return VXCORE_OK;
  };
  source.finalizeSave = [destination, owner]() {
    ++destination->finalizations;
    destination->finalizedOnOwner = QThread::currentThread() == owner;
    if (destination->throwBadAlloc)
      throw std::bad_alloc();
    if (destination->throwException)
      throw std::runtime_error("destination refused the staged snapshot");
    if (destination->finalizeError != VXCORE_OK)
      return destination->finalizeError;
    QMutexLocker lock(&destination->mutex);
    destination->committed = std::move(destination->pending);
    return VXCORE_OK;
  };
  return source;
}

} // namespace

class TestVirtualBuffer : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testOpenVirtualBufferReturnsValid();
  void testVirtualBufferDedup();
  void testVirtualBufferNotInSession();
  void testVirtualBufferCloseRemovesTracking();
  void testIsVirtualBuffer();
  void testMemoryLoadSaveReload();
  void testMemoryScratchIsNotDurable();
  void testMemoryFailuresRetainDraft();
  void testMemoryAutosaveKeepsNewerEdits();
  void testMemorySourceLifetime();
  void testMemoryDedupAndWidgetCollision();
  void testMemorySavePolicy_data();
  void testMemorySavePolicy();
  void testMemoryFinalizerSerializesAutosaves();
  void testMemoryAutosaveFinalizerFailure();
  void testMemoryFinalizerFailures_data();
  void testMemoryFinalizerFailures();
  void testMemoryManualSaveCancelled();
  void testMemoryManualSaveRetired();
  void testMemoryFinalizerKeepsNewerEdits();
  void testMemoryFinalizerPolicy();

private:
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  BufferService *m_bufferService = nullptr;
};

void TestVirtualBuffer::initTestCase() {
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);
  QVERIFY(m_hookMgr != nullptr);
  QVERIFY(m_bufferService != nullptr);
}

void TestVirtualBuffer::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestVirtualBuffer::cleanup() {
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

void TestVirtualBuffer::testOpenVirtualBufferReturnsValid() {
  Buffer2 buf = m_bufferService->openVirtualBuffer(QStringLiteral("vx://test"));
  QVERIFY(buf.isValid());
  QVERIFY(!buf.id().isEmpty());
  QCOMPARE(buf.nodeId().relativePath, QStringLiteral("vx://test"));
  QVERIFY(buf.nodeId().notebookId.isEmpty());
}

void TestVirtualBuffer::testVirtualBufferDedup() {
  Buffer2 buf1 = m_bufferService->openVirtualBuffer(QStringLiteral("vx://settings"));
  Buffer2 buf2 = m_bufferService->openVirtualBuffer(QStringLiteral("vx://settings"));

  QVERIFY(buf1.isValid());
  QVERIFY(buf2.isValid());
  QCOMPARE(buf1.id(), buf2.id());

  int virtualCount = 0;
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &val : buffers) {
    QJsonObject obj = val.toObject();
    if (obj.value(QStringLiteral("filePath")).toString() == QStringLiteral("vx://settings")) {
      ++virtualCount;
    }
  }
  QCOMPARE(virtualCount, 1);
}

void TestVirtualBuffer::testVirtualBufferNotInSession() {
  Buffer2 buf = m_bufferService->openVirtualBuffer(QStringLiteral("vx://session-excluded"));
  QVERIFY(buf.isValid());

  char *sessionJson = nullptr;
  VxCoreError err = vxcore_context_get_session_config(m_context, &sessionJson);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(sessionJson != nullptr);

  QJsonDocument sessionDoc = QJsonDocument::fromJson(QByteArray(sessionJson));
  vxcore_string_free(sessionJson);
  QVERIFY(sessionDoc.isObject());

  QJsonArray sessionBuffers = sessionDoc.object().value(QStringLiteral("buffers")).toArray();
  for (const auto &val : sessionBuffers) {
    QJsonObject obj = val.toObject();
    QVERIFY(obj.value(QStringLiteral("id")).toString() != buf.id());
    QVERIFY(obj.value(QStringLiteral("filePath")).toString() !=
            QStringLiteral("vx://session-excluded"));
  }
}

void TestVirtualBuffer::testVirtualBufferCloseRemovesTracking() {
  Buffer2 buf = m_bufferService->openVirtualBuffer(QStringLiteral("vx://close"));
  QVERIFY(buf.isValid());
  const QString oldId = buf.id();

  QVERIFY(m_bufferService->closeBuffer(oldId));
  QVERIFY(!m_bufferService->isVirtualBuffer(oldId));

  Buffer2 reopened = m_bufferService->openVirtualBuffer(QStringLiteral("vx://close"));
  QVERIFY(reopened.isValid());
  QVERIFY(reopened.id() != oldId);
}

void TestVirtualBuffer::testIsVirtualBuffer() {
  Buffer2 buf = m_bufferService->openVirtualBuffer(QStringLiteral("vx://is-virtual"));
  QVERIFY(buf.isValid());

  QVERIFY(m_bufferService->isVirtualBuffer(buf.id()));
  QVERIFY(!m_bufferService->isVirtualBuffer(QStringLiteral("non-existent-buffer-id")));

  char *rawId = nullptr;
  QCOMPARE(vxcore_buffer_open_virtual(m_context, "widget-without-uri-scheme", &rawId), VXCORE_OK);
  const auto id = QString::fromUtf8(rawId);
  vxcore_string_free(rawId);
  const auto adopted = m_bufferService->getBufferHandle(id);
  QVERIFY(!adopted.isFileBacked());
  QVERIFY(adopted.resolvedPath().isEmpty());
  QVERIFY(!adopted.canReload());
}

void TestVirtualBuffer::testMemoryLoadSaveReload() {
  QByteArray stored("initial\0bytes", 13);
  std::atomic<bool> savedOffThread{false};
  const auto ownerThread = QThread::currentThread();
  MemoryBufferSource source;
  source.address = QStringLiteral("vx://memory/lifecycle");
  source.displayName = QStringLiteral("Memory text");
  source.editorType = QStringLiteral("Text");
  source.load = [&](QByteArray &out) {
    out = stored;
    return VXCORE_OK;
  };
  source.save = [&](QByteArrayView bytes) {
    savedOffThread = QThread::currentThread() != ownerThread;
    stored = QByteArray(bytes.data(), bytes.size());
    return VXCORE_OK;
  };
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  QVERIFY(!buffer.isFileBacked());
  QCOMPARE(buffer.displayName(), QStringLiteral("Memory text"));
  QCOMPARE(buffer.getContentRaw(), stored);
  const QByteArray edited("new\0content", 11);
  QVERIFY(buffer.setContentRaw(edited));
  QVERIFY(buffer.save());
  QCOMPARE(stored, edited);
  QVERIFY(savedOffThread.load());
  QVERIFY(!buffer.isModified());
  stored = "replacement from source";
  QVERIFY(buffer.reload());
  QCOMPARE(buffer.getContentRaw(), stored);
  QVERIFY(!buffer.isModified());
}

void TestVirtualBuffer::testMemoryScratchIsNotDurable() {
  MemoryBufferSource source;
  source.address = QStringLiteral("vx://memory/scratch");
  source.displayName = QStringLiteral("Scratch");
  source.editorType = QStringLiteral("Text");
  source.content = "original";
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.setContentRaw("unsaved draft"));
  const auto revision = buffer.getRevision();
  VxCoreError error;
  QVERIFY(!buffer.save(&error));
  QCOMPARE(error, VXCORE_ERR_UNSUPPORTED);
  QVERIFY(!buffer.reload(&error));
  QCOMPARE(error, VXCORE_ERR_UNSUPPORTED);
  QCOMPARE(buffer.getContentRaw(), QByteArray("unsaved draft"));
  QCOMPARE(buffer.getRevision(), revision);
  QVERIFY(buffer.isModified());
  QString message;
  QVERIFY(!m_bufferService->saveForSnapshot(buffer.id(), 0, &message));
  QVERIFY(!buffer.canSave());
  QVERIFY(!buffer.canReload());
}

void TestVirtualBuffer::testMemoryFailuresRetainDraft() {
  bool failLoad = false;
  MemoryBufferSource source;
  source.address = QStringLiteral("vx://memory/errors");
  source.displayName = QStringLiteral("Failures");
  source.editorType = QStringLiteral("Text");
  source.load = [&](QByteArray &out) {
    out = failLoad ? "partial output must not replace draft" : "initial";
    return failLoad ? VXCORE_ERR_IO : VXCORE_OK;
  };
  source.save = [](QByteArrayView) { return VXCORE_ERR_IO; };
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.setContentRaw("retained draft"));
  const auto revision = buffer.getRevision();
  VxCoreError error;
  QVERIFY(!buffer.save(&error));
  QCOMPARE(error, VXCORE_ERR_IO);
  failLoad = true;
  QVERIFY(!buffer.reload(&error));
  QCOMPARE(error, VXCORE_ERR_IO);
  QCOMPARE(buffer.getContentRaw(), QByteArray("retained draft"));
  QCOMPARE(buffer.getRevision(), revision);
  QVERIFY(buffer.isModified());
}

void TestVirtualBuffer::testMemoryAutosaveKeepsNewerEdits() {
  struct State {
    QSemaphore entered;
    QSemaphore release;
    QByteArray stored;
    std::atomic<int> calls{0};
    QString editor = QStringLiteral("first snapshot");
  };
  const auto state = std::make_shared<State>();
  const auto unblock = qScopeGuard([state]() { state->release.release(); });
  MemoryBufferSource source;
  source.address = QStringLiteral("vx://memory/revisions");
  source.displayName = QStringLiteral("Revisions");
  source.editorType = QStringLiteral("Text");
  source.save = [state](QByteArrayView bytes) {
    if (++state->calls == 1) {
      state->entered.release();
      state->release.acquire();
    }
    state->stored = QByteArray(bytes.data(), bytes.size());
    return VXCORE_OK;
  };
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  m_bufferService->registerActiveWriter(buffer.id(), 1, [state]() { return state->editor; });
  m_bufferService->markDirty(buffer.id());
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  QTRY_VERIFY(state->entered.available() == 1);
  state->editor = QStringLiteral("newer unsynchronized edit");
  m_bufferService->markDirty(buffer.id());
  state->release.release();
  QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(state->stored, QByteArray("first snapshot"));
  QVERIFY(buffer.isModified());
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(state->stored, state->editor.toUtf8());
  QVERIFY(!buffer.isModified());
}

void TestVirtualBuffer::testMemorySourceLifetime() {
  const auto entered = std::make_shared<QSemaphore>();
  const auto release = std::make_shared<QSemaphore>();
  const auto unblock = qScopeGuard([release]() { release->release(); });
  auto owner = std::make_shared<int>(0);
  std::weak_ptr<int> alive = owner;
  MemoryBufferSource source;
  source.address = QStringLiteral("vx://memory/lifetime");
  source.displayName = QStringLiteral("Lifetime");
  source.editorType = QStringLiteral("Text");
  source.save = [owner, entered, release](QByteArrayView) {
    entered->release();
    release->acquire();
    return VXCORE_OK;
  };
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  owner.reset();
  QVERIFY(buffer.setContentRaw("pending"));
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  QTRY_VERIFY(entered->available() == 1);
  QVERIFY(m_bufferService->closeBuffer(buffer.id()));
  QVERIFY(!alive.expired());
  MemoryBufferSource replacement;
  replacement.address = QStringLiteral("vx://memory/lifetime");
  replacement.displayName = QStringLiteral("Replacement");
  replacement.editorType = QStringLiteral("Text");
  replacement.content = "different generation";
  auto reopened = m_bufferService->openMemoryBuffer(std::move(replacement));
  QVERIFY(reopened.id() != buffer.id());
  release->release();
  QTRY_VERIFY(alive.expired());
  QCOMPARE(reopened.getContentRaw(), QByteArray("different generation"));
  QVERIFY(!reopened.isModified());
}

void TestVirtualBuffer::testMemoryDedupAndWidgetCollision() {
  MemoryBufferSource source;
  source.address = QStringLiteral("vx://memory/dedup");
  source.displayName = QStringLiteral("Deduplication");
  source.editorType = QStringLiteral("Text");
  source.content = "keep original";
  auto first = m_bufferService->openMemoryBuffer(source);
  source.content = "must not overwrite";
  auto second = m_bufferService->openMemoryBuffer(source);
  QCOMPARE(second.id(), first.id());
  QCOMPARE(second.getContentRaw(), QByteArray("keep original"));
  QVERIFY(!m_bufferService->openVirtualBuffer(source.address).isValid());
  source.displayName = QStringLiteral("Incompatible metadata");
  VxCoreError error;
  QVERIFY(!m_bufferService->openMemoryBuffer(source, {}, &error).isValid());
  QCOMPARE(error, VXCORE_ERR_ALREADY_EXISTS);
  source.address = QStringLiteral("vx://widget/collision");
  QVERIFY(m_bufferService->openVirtualBuffer(source.address).isValid());
  QVERIFY(!m_bufferService->openMemoryBuffer(source, {}, &error).isValid());
  QCOMPARE(error, VXCORE_ERR_ALREADY_EXISTS);
}

void TestVirtualBuffer::testMemorySavePolicy_data() {
  QTest::addColumn<int>("policy");
  QTest::addColumn<bool>("withFinalizer");
  QTest::newRow("sync-only-finalizer") << int(AutoSavePolicy::None) << true;
  QTest::newRow("autosave-finalizer") << int(AutoSavePolicy::AutoSave) << true;
  QTest::newRow("backup-finalizer") << int(AutoSavePolicy::BackupFile) << true;
  QTest::newRow("sync-only-worker") << int(AutoSavePolicy::None) << false;
  QTest::newRow("autosave-worker") << int(AutoSavePolicy::AutoSave) << false;
  QTest::newRow("backup-worker") << int(AutoSavePolicy::BackupFile) << false;
}

void TestVirtualBuffer::testMemorySavePolicy() {
  QFETCH(int, policy);
  QFETCH(bool, withFinalizer);
  const bool automatic = policy == int(AutoSavePolicy::AutoSave);
  ScopedAutoSaveTimer timer(m_bufferService);
  m_bufferService->setAutoSavePolicy(static_cast<AutoSavePolicy>(policy));
  const auto restorePolicy =
      qScopeGuard([&]() { m_bufferService->setAutoSavePolicy(AutoSavePolicy::AutoSave); });
  const auto destination = std::make_shared<MemoryDestination>();
  auto source = stagedSource(QStringLiteral("vx://memory/save-policy"), destination);
  if (!withFinalizer) {
    source.finalizeSave = {};
    const auto owner = QThread::currentThread();
    source.save = [destination, owner](QByteArrayView bytes) {
      ++destination->stages;
      destination->stagedOffThread = QThread::currentThread() != owner;
      QMutexLocker lock(&destination->mutex);
      destination->committed = QByteArray(bytes.data(), bytes.size());
      return VXCORE_OK;
    };
  }
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  QString editor = QString::fromUtf8("Changed β\nthird line");
  m_bufferService->registerActiveWriter(buffer.id(), 1, [&]() { return editor; });
  QSignalSpy autosaved(m_bufferService->asQObject(), SIGNAL(bufferAutoSaved(QString)));
  QSignalSpy failed(m_bufferService->asQObject(), SIGNAL(bufferAutoSaveFailed(QString)));
  QSignalSpy errors(m_bufferService->asQObject(), SIGNAL(saveError(QString, QString)));
  int afterSaves = 0;
  bool durableAtAfterSave = false;
  const int hook = m_hookMgr->addAction(
      HookNames::FileAfterSave,
      [&](HookContext &, const QVariantMap &) {
        ++afterSaves;
        QMutexLocker lock(&destination->mutex);
        durableAtAfterSave = destination->committed == editor.toUtf8() && !buffer.isModified();
      },
      10);
  const auto removeHook = qScopeGuard([&]() { m_hookMgr->removeAction(hook); });

  for (bool tick : {false, true}) {
    if (tick)
      editor += QStringLiteral("\nnew edit before the autosave tick");
    m_bufferService->markDirty(buffer.id());
    QVERIFY(tick ? timer.tick() : m_bufferService->syncNow(buffer.id()));
    QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
    QCOMPARE(buffer.getContentRaw(), editor.toUtf8());
    const int saves = automatic ? (tick ? 2 : 1) : 0;
    QCOMPARE(destination->stages.load(), saves);
    QCOMPARE(destination->finalizations, withFinalizer ? saves : 0);
    QCOMPARE(destination->committed,
             automatic ? editor.toUtf8() : QByteArray("original destination"));
    QCOMPARE(buffer.isModified(), !automatic);
    QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()),
             automatic ? m_bufferService->currentRevision(buffer.id()) : quint64(0));
    QCOMPARE(autosaved.size(), saves);
    QCOMPARE(afterSaves, 0);
    QVERIFY(failed.isEmpty());
    QVERIFY(errors.isEmpty());
  }

  editor += QStringLiteral("\nexplicit save");
  m_bufferService->markDirty(buffer.id());
  const auto revision = m_bufferService->currentRevision(buffer.id());
  VxCoreError error = VXCORE_ERR_UNKNOWN;
  QVERIFY(buffer.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(destination->committed, editor.toUtf8());
  const int saves = automatic ? 3 : 1;
  QCOMPARE(destination->stages.load(), saves);
  QVERIFY(destination->stagedOffThread.load());
  QCOMPARE(destination->finalizations, withFinalizer ? saves : 0);
  QCOMPARE(destination->finalizedOnOwner, withFinalizer);
  QVERIFY(!buffer.isModified());
  QVERIFY(!m_bufferService->isDirty(buffer.id()));
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), revision);
  QCOMPARE(afterSaves, 1);
  QVERIFY(durableAtAfterSave);
  QCOMPARE(autosaved.size(), automatic ? 2 : 0);

  // A later save must use the new snapshot, not the previous staged bytes.
  editor = QString();
  m_bufferService->markDirty(buffer.id());
  QVERIFY(buffer.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(destination->committed, QByteArray());
  QCOMPARE(destination->stages.load(), saves + 1);
  QCOMPARE(destination->finalizations, withFinalizer ? saves + 1 : 0);
  QCOMPARE(afterSaves, 2);
  QVERIFY(durableAtAfterSave);
  QVERIFY(!buffer.isModified());
}

void TestVirtualBuffer::testMemoryFinalizerSerializesAutosaves() {
  ScopedAutoSaveTimer timer(m_bufferService);
  const auto destination = std::make_shared<MemoryDestination>();
  const auto firstStageReturned = std::make_shared<QSemaphore>();
  const auto nextStageEntered = std::make_shared<QSemaphore>();
  auto source = stagedSource(QStringLiteral("vx://memory/serialized-finalizer"), destination);
  const auto stage = std::move(source.save);
  source.save = [stage, destination, firstStageReturned, nextStageEntered](QByteArrayView bytes) {
    const auto result = stage(bytes);
    if (destination->stages.load() == 1)
      firstStageReturned->release();
    else
      nextStageEntered->release();
    return result;
  };
  QList<QByteArray> committedSnapshots;
  const auto finalize = std::move(source.finalizeSave);
  source.finalizeSave = [&, finalize]() {
    const auto result = finalize();
    committedSnapshots.append(destination->committed);
    return result;
  };
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  QString editor = QStringLiteral("first staged snapshot");
  m_bufferService->registerActiveWriter(buffer.id(), 1, [&]() { return editor; });
  QSignalSpy autosaved(m_bufferService->asQObject(), SIGNAL(bufferAutoSaved(QString)));
  m_bufferService->markDirty(buffer.id());
  const auto firstRevision = m_bufferService->currentRevision(buffer.id());
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  // Do not pump GUI completion: workers can otherwise run ahead of finalizers.
  QVERIFY(firstStageReturned->tryAcquire(1, 5000));
  editor = QStringLiteral("newer draft while GUI completion is pending");
  m_bufferService->markDirty(buffer.id());
  const auto lastRevision = m_bufferService->currentRevision(buffer.id());
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  QVERIFY(!nextStageEntered->tryAcquire(1, 1000));
  QCOMPARE(destination->stages.load(), 1);
  QCOMPARE(destination->finalizations, 0);
  QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(committedSnapshots, QList<QByteArray>{QByteArray("first staged snapshot")});
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), firstRevision);
  QCOMPARE(buffer.getContentRaw(), editor.toUtf8());
  QVERIFY(buffer.isModified());
  QVERIFY(m_bufferService->isDirty(buffer.id()));
  QVERIFY(autosaved.isEmpty());

  QVERIFY(timer.tick());
  QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(committedSnapshots,
           QList<QByteArray>({QByteArray("first staged snapshot"), editor.toUtf8()}));
  QCOMPARE(destination->stages.load(), 2);
  QCOMPARE(destination->finalizations, 2);
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), lastRevision);
  QVERIFY(!buffer.isModified());
  QVERIFY(!m_bufferService->isDirty(buffer.id()));
  QCOMPARE(autosaved.size(), 1);
}

void TestVirtualBuffer::testMemoryAutosaveFinalizerFailure() {
  ScopedAutoSaveTimer timer(m_bufferService);
  const auto destination = std::make_shared<MemoryDestination>();
  destination->finalizeError = VXCORE_ERR_IO;
  auto source = stagedSource(QStringLiteral("vx://memory/autosave-finalizer-error"), destination);
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  const QByteArray draft("autosave destination must accept this snapshot");
  QVERIFY(buffer.setContentRaw(draft));
  QSignalSpy autosaved(m_bufferService->asQObject(), SIGNAL(bufferAutoSaved(QString)));
  QSignalSpy failed(m_bufferService->asQObject(), SIGNAL(bufferAutoSaveFailed(QString)));
  QSignalSpy errors(m_bufferService->asQObject(), SIGNAL(saveError(QString, QString)));
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(destination->committed, QByteArray("original destination"));
  QCOMPARE(destination->finalizations, 1);
  QCOMPARE(buffer.getContentRaw(), draft);
  QVERIFY(buffer.isModified());
  QVERIFY(m_bufferService->isDirty(buffer.id()));
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), quint64(0));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(errors.size(), 1);
  QCOMPARE(errors.first().at(1).toString(), QString::fromUtf8(vxcore_error_message(VXCORE_ERR_IO)));
  QVERIFY(autosaved.isEmpty());

  destination->finalizeError = VXCORE_OK;
  QVERIFY(timer.tick());
  QTRY_VERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(destination->committed, draft);
  QCOMPARE(destination->finalizations, 2);
  QVERIFY(destination->finalizedOnOwner);
  QVERIFY(!buffer.isModified());
  QVERIFY(!m_bufferService->isDirty(buffer.id()));
  QCOMPARE(autosaved.size(), 1);
  QCOMPARE(failed.size(), 1);
  QCOMPARE(errors.size(), 1);
}

void TestVirtualBuffer::testMemoryFinalizerFailures_data() {
  QTest::addColumn<int>("failure");
  QTest::addColumn<int>("expectedError");
  QTest::newRow("destination-error") << 0 << int(VXCORE_ERR_IO);
  QTest::newRow("destination-exception") << 1 << int(VXCORE_ERR_UNKNOWN);
  QTest::newRow("destination-bad-alloc") << 2 << int(VXCORE_ERR_OUT_OF_MEMORY);
  QTest::newRow("worker-error") << 3 << int(VXCORE_ERR_IO);
}

void TestVirtualBuffer::testMemoryFinalizerFailures() {
  QFETCH(int, failure);
  QFETCH(int, expectedError);
  ScopedAutoSaveTimer timer(m_bufferService);
  const auto destination = std::make_shared<MemoryDestination>();
  destination->finalizeError = failure == 0 ? VXCORE_ERR_IO : VXCORE_OK;
  destination->throwException = failure == 1;
  destination->throwBadAlloc = failure == 2;
  destination->stageError = failure == 3 ? VXCORE_ERR_IO : VXCORE_OK;
  auto source = stagedSource(QStringLiteral("vx://memory/finalizer-errors"), destination);
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  const QByteArray draft("retained\0binary draft", 21);
  QVERIFY(buffer.setContentRaw(draft));
  const auto coreRevision = buffer.getRevision();
  const auto revision = m_bufferService->currentRevision(buffer.id());
  int afterSaves = 0;
  const int hook = m_hookMgr->addAction(
      HookNames::FileAfterSave, [&](HookContext &, const QVariantMap &) { ++afterSaves; }, 10);
  const auto removeHook = qScopeGuard([&]() { m_hookMgr->removeAction(hook); });
  QSignalSpy errors(m_bufferService->asQObject(), SIGNAL(saveError(QString, QString)));
  VxCoreError error = VXCORE_OK;
  QVERIFY(!buffer.save(&error));
  QCOMPARE(int(error), expectedError);
  QCOMPARE(destination->committed, QByteArray("original destination"));
  QCOMPARE(destination->stages.load(), 1);
  QCOMPARE(destination->finalizations, failure == 3 ? 0 : 1);
  QCOMPARE(buffer.getContentRaw(), draft);
  QCOMPARE(buffer.getRevision(), coreRevision);
  QVERIFY(buffer.isModified());
  QVERIFY(m_bufferService->isDirty(buffer.id()));
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), quint64(0));
  QVERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(afterSaves, 0);
  QCOMPARE(errors.size(), 1);
  QCOMPARE(errors.at(0).at(0).toString(), buffer.id());
  QCOMPARE(errors.at(0).at(1).toString(), QString::fromUtf8(vxcore_error_message(error)));

  destination->finalizeError = VXCORE_OK;
  destination->throwException = false;
  destination->throwBadAlloc = false;
  destination->stageError = VXCORE_OK;
  QVERIFY(buffer.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(destination->committed, draft);
  QCOMPARE(destination->stages.load(), 2);
  QCOMPARE(destination->finalizations, failure == 3 ? 1 : 2);
  QVERIFY(destination->finalizedOnOwner);
  QVERIFY(!buffer.isModified());
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), revision);
  QCOMPARE(afterSaves, 1);
  QCOMPARE(errors.size(), 1);
}

void TestVirtualBuffer::testMemoryManualSaveCancelled() {
  ScopedAutoSaveTimer timer(m_bufferService);
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::None);
  const auto restorePolicy =
      qScopeGuard([&]() { m_bufferService->setAutoSavePolicy(AutoSavePolicy::AutoSave); });
  const auto destination = std::make_shared<MemoryDestination>();
  auto source = stagedSource(QStringLiteral("vx://memory/manual-cancelled"), destination);
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  QVERIFY(buffer.setContentRaw("cancelled draft"));
  bool cancel = true;
  int beforeSaves = 0;
  int afterSaves = 0;
  const int before = m_hookMgr->addAction(
      HookNames::FileBeforeSave,
      [&](HookContext &context, const QVariantMap &) {
        ++beforeSaves;
        if (cancel)
          context.cancel();
      },
      10);
  const int after = m_hookMgr->addAction(
      HookNames::FileAfterSave, [&](HookContext &, const QVariantMap &) { ++afterSaves; }, 10);
  const auto removeHooks = qScopeGuard([&]() {
    m_hookMgr->removeAction(before);
    m_hookMgr->removeAction(after);
  });
  VxCoreError error = VXCORE_OK;
  QVERIFY(!buffer.save(&error));
  QCOMPARE(error, VXCORE_ERR_CANCELLED);
  QCOMPARE(beforeSaves, 1);
  QCOMPARE(afterSaves, 0);
  QVERIFY(timer.tick());
  QVERIFY(!m_bufferService->isSaveQueueBusy(buffer.id()));
  QCOMPARE(destination->stages.load(), 0);
  QCOMPARE(destination->finalizations, 0);
  QCOMPARE(destination->committed, QByteArray("original destination"));
  QCOMPARE(buffer.getContentRaw(), QByteArray("cancelled draft"));
  QVERIFY(buffer.isModified());
  cancel = false;
  QVERIFY(buffer.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(beforeSaves, 2);
  QCOMPARE(afterSaves, 1);
  QCOMPARE(destination->stages.load(), 1);
  QCOMPARE(destination->finalizations, 1);
  QCOMPARE(destination->committed, QByteArray("cancelled draft"));
  QVERIFY(!buffer.isModified());
}

void TestVirtualBuffer::testMemoryManualSaveRetired() {
  ScopedAutoSaveTimer timer(m_bufferService);
  const auto destination = std::make_shared<MemoryDestination>();
  const auto replacementDestination = std::make_shared<MemoryDestination>();
  const auto entered = std::make_shared<QSemaphore>();
  const auto release = std::make_shared<QSemaphore>();
  const auto unblock = qScopeGuard([release]() { release->release(); });
  auto source = stagedSource(QStringLiteral("vx://memory/manual-retired"), destination);
  const auto destroyedOnOwner = std::make_shared<std::atomic<int>>(0);
  const auto owner = QThread::currentThread();
  auto lifetime = std::shared_ptr<int>(new int(0), [owner, destroyedOnOwner](int *token) {
    destroyedOnOwner->store(QThread::currentThread() == owner ? 1 : -1);
    delete token;
  });
  source.finalizeSave = [lifetime = std::move(lifetime),
                         finalize = std::move(source.finalizeSave)]() { return finalize(); };
  const auto stage = std::move(source.save);
  source.save = [stage, entered, release](QByteArrayView bytes) {
    const auto error = stage(bytes);
    entered->release();
    release->acquire();
    return error;
  };
  auto buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  QVERIFY(buffer.setContentRaw("old generation draft"));
  const auto oldId = buffer.id();
  Buffer2 reopened;
  bool workerEntered = false;
  bool closed = false;
  bool replacementEdited = false;
  VxCoreError openError = VXCORE_ERR_UNKNOWN;
  int afterSaves = 0;
  const int hook = m_hookMgr->addAction(
      HookNames::FileAfterSave, [&](HookContext &, const QVariantMap &) { ++afterSaves; }, 10);
  const auto removeHook = qScopeGuard([&]() { m_hookMgr->removeAction(hook); });
  QTimer retire;
  retire.setSingleShot(true);
  connect(&retire, &QTimer::timeout, this, [&]() {
    // This timer runs inside save()'s event loop. The worker cannot finish
    // until the same address has acquired its replacement source.
    workerEntered = entered->tryAcquire(1, 5000);
    if (workerEntered) {
      closed = m_bufferService->closeBuffer(oldId);
      auto replacement =
          stagedSource(QStringLiteral("vx://memory/manual-retired"), replacementDestination);
      reopened = m_bufferService->openMemoryBuffer(std::move(replacement), {}, &openError);
      if (reopened.isValid())
        replacementEdited = reopened.setContentRaw("replacement draft");
    }
    release->release();
  });
  retire.start(0);
  VxCoreError error = VXCORE_OK;
  QVERIFY(!buffer.save(&error));
  QCOMPARE(error, VXCORE_ERR_INVALID_STATE);
  QVERIFY(workerEntered);
  QVERIFY(closed);
  QCOMPARE(openError, VXCORE_OK);
  QVERIFY(replacementEdited);
  QVERIFY(reopened.id() != oldId);
  QTRY_COMPARE(destroyedOnOwner->load(), 1);
  QCOMPARE(destination->stages.load(), 1);
  QCOMPARE(destination->finalizations, 0);
  QCOMPARE(destination->committed, QByteArray("original destination"));
  QCOMPARE(replacementDestination->stages.load(), 0);
  QCOMPARE(replacementDestination->finalizations, 0);
  QCOMPARE(reopened.getContentRaw(), QByteArray("replacement draft"));
  QVERIFY(reopened.isModified());
  QCOMPARE(m_bufferService->lastSavedRevision(reopened.id()), quint64(0));
  QCOMPARE(afterSaves, 0);
  QVERIFY(reopened.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(replacementDestination->committed, QByteArray("replacement draft"));
  QCOMPARE(replacementDestination->stages.load(), 1);
  QCOMPARE(replacementDestination->finalizations, 1);
  QCOMPARE(destination->finalizations, 0);
  QCOMPARE(afterSaves, 1);
  QVERIFY(!reopened.isModified());
}

void TestVirtualBuffer::testMemoryFinalizerKeepsNewerEdits() {
  ScopedAutoSaveTimer timer(m_bufferService);
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::None);
  const auto restorePolicy =
      qScopeGuard([&]() { m_bufferService->setAutoSavePolicy(AutoSavePolicy::AutoSave); });
  const auto destination = std::make_shared<MemoryDestination>();
  auto source = stagedSource(QStringLiteral("vx://memory/finalizer-revision"), destination);
  Buffer2 buffer;
  QString editor = QStringLiteral("snapshot being saved");
  bool changedEditor = false;
  bool dirtyDuringFinalization = false;
  const auto finalize = std::move(source.finalizeSave);
  source.finalizeSave = [&, finalize]() {
    dirtyDuringFinalization = buffer.isModified();
    const auto error = finalize();
    if (!changedEditor) {
      changedEditor = true;
      editor = QStringLiteral("newer editor revision from destination notification");
      m_bufferService->markDirty(buffer.id());
    }
    return error;
  };
  buffer = m_bufferService->openMemoryBuffer(std::move(source));
  QVERIFY(buffer.isMemoryBacked());
  m_bufferService->registerActiveWriter(buffer.id(), 1, [&]() { return editor; });
  m_bufferService->markDirty(buffer.id());
  const auto revision = m_bufferService->currentRevision(buffer.id());
  int afterSaves = 0;
  const int hook = m_hookMgr->addAction(
      HookNames::FileAfterSave, [&](HookContext &, const QVariantMap &) { ++afterSaves; }, 10);
  const auto removeHook = qScopeGuard([&]() { m_hookMgr->removeAction(hook); });
  VxCoreError error = VXCORE_OK;
  QVERIFY(!buffer.save(&error));
  QCOMPARE(error, VXCORE_ERR_FILE_CHANGED_OUTSIDE);
  QVERIFY(dirtyDuringFinalization);
  QCOMPARE(destination->committed, QByteArray("snapshot being saved"));
  QCOMPARE(destination->finalizations, 1);
  QCOMPARE(m_bufferService->lastSavedRevision(buffer.id()), revision);
  QVERIFY(m_bufferService->currentRevision(buffer.id()) > revision);
  QVERIFY(buffer.isModified());
  QVERIFY(m_bufferService->isDirty(buffer.id()));
  QCOMPARE(afterSaves, 0);
  QVERIFY(m_bufferService->syncNow(buffer.id()));
  QCOMPARE(buffer.getContentRaw(), editor.toUtf8());
  QVERIFY(buffer.isModified());
  QCOMPARE(destination->stages.load(), 1);
  QVERIFY(buffer.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(destination->committed, editor.toUtf8());
  QCOMPARE(destination->finalizations, 2);
  QVERIFY(!buffer.isModified());
  QCOMPARE(afterSaves, 1);
}

void TestVirtualBuffer::testMemoryFinalizerPolicy() {
  ScopedAutoSaveTimer timer(m_bufferService);
  const auto firstDestination = std::make_shared<MemoryDestination>();
  auto source = stagedSource(QStringLiteral("vx://memory/finalizer-policy"), firstDestination);
  int opens = 0;
  const int hook = m_hookMgr->addAction(
      HookNames::FileAfterOpen, [&](HookContext &, const QVariantMap &) { ++opens; }, 10);
  const auto removeHook = qScopeGuard([&]() { m_hookMgr->removeAction(hook); });
  VxCoreError error = VXCORE_OK;
  auto invalid = source;
  invalid.save = {};
  QVERIFY(!m_bufferService->openMemoryBuffer(invalid, {}, &error).isValid());
  QCOMPARE(error, VXCORE_ERR_INVALID_PARAM);
  QCOMPARE(opens, 0);
  QVERIFY(m_bufferService->listBuffers().isEmpty());

  auto first = m_bufferService->openMemoryBuffer(source, {}, &error);
  QCOMPARE(error, VXCORE_OK);
  QVERIFY(first.isMemoryBacked());
  QCOMPARE(opens, 1);
  QVERIFY(first.setContentRaw("first retained draft"));
  const auto replacementDestination = std::make_shared<MemoryDestination>();
  auto duplicate = stagedSource(source.address, replacementDestination);
  duplicate.content = "must not replace draft";
  auto second = m_bufferService->openMemoryBuffer(duplicate, {}, &error);
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(second.id(), first.id());
  QCOMPARE(second.getContentRaw(), QByteArray("first retained draft"));
  QVERIFY(second.isModified());
  const int compatibleOpens = opens;
  duplicate.finalizeSave = {};
  QVERIFY(!m_bufferService->openMemoryBuffer(duplicate, {}, &error).isValid());
  QCOMPARE(error, VXCORE_ERR_ALREADY_EXISTS);
  QCOMPARE(opens, compatibleOpens);
  QVERIFY(second.save(&error));
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(firstDestination->committed, QByteArray("first retained draft"));
  QCOMPARE(firstDestination->finalizations, 1);
  QCOMPARE(replacementDestination->stages.load(), 0);
  QCOMPARE(replacementDestination->finalizations, 0);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVirtualBuffer)
#include "test_virtual_buffer.moc"
