#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <memory>

#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVirtualBuffer)
#include "test_virtual_buffer.moc"
