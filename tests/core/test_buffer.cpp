#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/searchresulttypes.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <functional>
#include <memory>
#include <thread>

using namespace vnotex;

namespace tests {

namespace {

QByteArray readReplacementFile(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeReplacementFile(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_bytes) == p_bytes.size();
}

struct ReplacementCompletion {
  int token;
  NodeIdentifier nodeId;
  QString bufferId;
  int matches;
  bool saved;
  QString error;
};

struct ReplacementState {
  QString bufferId;
  bool active;
  bool changed;
  bool saved;
};

} // namespace

// BufferService's QObject base is private: retain the production named-slot boundary.
class ReplacementObserver : public QObject {
  Q_OBJECT

public:
  explicit ReplacementObserver(BufferService &p_buffers) {
    connect(p_buffers.asQObject(), SIGNAL(protectedOpenRequested(NodeIdentifier, FileOpenSettings)),
            this, SLOT(onProtectedOpenRequested(NodeIdentifier, FileOpenSettings)));
    connect(p_buffers.asQObject(),
            SIGNAL(contentReplacementStateChanged(QString, bool, bool, bool)), this,
            SLOT(onState(QString, bool, bool, bool)));
    connect(p_buffers.asQObject(),
            SIGNAL(searchReplacementFinished(int, NodeIdentifier, QString, int, bool, QString)),
            this, SLOT(onFinished(int, NodeIdentifier, QString, int, bool, QString)));
  }

  int unlockRequests = 0;
  QVector<ReplacementCompletion> completions;
  QVector<ReplacementState> states;
  std::function<void(const ReplacementState &)> stateCallback;
  std::function<void(const ReplacementCompletion &)> finishedCallback;

signals:
  void stateObserved();
  void finished();

private slots:
  void onProtectedOpenRequested(const NodeIdentifier &, const FileOpenSettings &) {
    ++unlockRequests;
  }

  void onState(const QString &p_id, bool p_active, bool p_changed, bool p_saved) {
    const ReplacementState state{p_id, p_active, p_changed, p_saved};
    states.append(state);
    if (stateCallback) {
      stateCallback(state);
    }
    emit stateObserved();
  }

  void onFinished(int p_token, const NodeIdentifier &p_nodeId, const QString &p_bufferId,
                  int p_matches, bool p_saved, const QString &p_error) {
    const ReplacementCompletion result{p_token, p_nodeId, p_bufferId, p_matches, p_saved, p_error};
    completions.append(result);
    if (finishedCallback) {
      finishedCallback(result);
    }
    emit finished();
  }
};

namespace {

// A deterministic busy-save barrier; no polling sleeps or production queue internals.
class ReplacementGateBarrier {
public:
  ReplacementGateBarrier(NotebookIoGate &p_gate, const QString &p_notebookId)
      : m_thread([this, &p_gate, p_notebookId]() {
          NotebookIoGate::ScopedLock lock(p_gate, p_notebookId);
          m_started.release();
          m_release.acquire();
        }) {
    m_started.acquire();
  }

  ~ReplacementGateBarrier() { release(); }

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

// Hold the specified QtConcurrent preparation executor before it can read any source bytes.
class ReplacementPreparationBarrier {
public:
  ReplacementPreparationBarrier()
      : m_pool(QThreadPool::globalInstance()), m_previousMax(m_pool->maxThreadCount()) {
    m_pool->setMaxThreadCount(1);
    m_future = QtConcurrent::run([this]() {
      m_started.release();
      m_release.acquire();
    });
    m_started.acquire();
  }

  ~ReplacementPreparationBarrier() { release(); }

  void release() {
    if (!m_released) {
      m_released = true;
      m_release.release();
      m_future.waitForFinished();
      m_pool->setMaxThreadCount(m_previousMax);
    }
  }

private:
  QThreadPool *m_pool;
  int m_previousMax;
  QSemaphore m_started;
  QSemaphore m_release;
  QFuture<void> m_future;
  bool m_released = false;
};

// Inject an actual filesystem write failure only at the established virtual save boundary,
// after the real queue has installed the transformed bytes. The original file remains intact.
class ReplacementWriteFailureService : public BufferService {
public:
  using BufferService::BufferService;

  QString failurePath;
  bool obstructionCreated = false;
  bool originalRestored = false;
  bool writeFailed = false;

  bool saveBuffer(const QString &p_bufferId) override {
    const auto preservedPath = failurePath + QStringLiteral(".preserved");
    if (!QFile::rename(failurePath, preservedPath)) {
      return false;
    }
    obstructionCreated = QDir().mkdir(failurePath);
    const bool saved = obstructionCreated && BufferService::saveBuffer(p_bufferId);
    writeFailed = obstructionCreated && !saved;
    if (obstructionCreated) {
      QDir().rmdir(failurePath);
    }
    originalRestored = QFile::rename(preservedPath, failurePath);
    return saved;
  }
};

class ReplacementDestructionService : public BufferService {
public:
  using BufferService::BufferService;
  std::function<void()> beforeDestruction;

  ~ReplacementDestructionService() override {
    // Enter destruction while preparation is still queued, then allow the base destructor
    // to cancel/join it. No GUI event loop may run while the service is being torn down.
    if (beforeDestruction) {
      beforeDestruction();
    }
  }
};

} // namespace

class TestBuffer : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // Buffer handle basics
  void testDefaultConstructor();
  void testIsValid();
  void testId();
  void testNodeId();

  // Buffer via BufferService::openBuffer
  void testOpenReturnsValidBuffer();
  void testOpenCancelledByHook();

  // Per-buffer content operations
  void testGetContentRaw();
  void testSetContentRaw();
  void testSave();
  void testSaveCancelledByHook();
  void testSaveFiresAfterHook();
  void testReload();
  void testGetContent();
  void testSetContent();

  // Per-buffer state operations
  void testIsModified();
  void testGetState();
  void testGetBuffer();

  // Per-buffer asset operations
  void testInsertAssetRaw();
  void testInsertAsset();
  void testDeleteAsset();
  void testGetAssetsFolder();

  // Per-buffer attachment operations
  void testInsertAttachment();
  void testListAttachments();
  void testDeleteAttachment();
  void testRenameAttachment();
  void testGetAttachmentsFolder();
  void testInsertAttachmentFiresHooks();
  void testDeleteAttachmentHookCancel();
  void testRenameAttachmentFiresHooks();
  void testAttachmentChangedSignal();
  void testHasAttachments();

  // Invalid buffer guard
  void testInvalidBufferOperations();
  void testMixedNoteSaveWhileProtectedLocking();
  void testEncryptedBodyKeepsAssetsPlaintext();

  // Search-result replacement persistence and lifecycle.
  void testSearchReplacementWriterDurability_data();
  void testSearchReplacementWriterDurability();
  void testSearchReplacementPrivateOpenAndNoop();
  void testSearchReplacementUnsupportedTarget();
  void testSearchReplacementReadOnly_data();
  void testSearchReplacementReadOnly();
  void testSearchReplacementProtected();
  void testSearchReplacementWaitsForAutosave();
  void testSearchReplacementBeforeSaveVeto();
  void testSearchReplacementDigestConflict();
  void testSearchReplacementExternalChangeErrors_data();
  void testSearchReplacementExternalChangeErrors();
  void testSearchReplacementWriteFailureRecovery();
  void testSearchReplacementStaleWriter();
  void testSearchReplacementAfterSaveEdit();
  void testSearchReplacementLifecycleGuards();
  void testSearchReplacementCancelPreparation_data();
  void testSearchReplacementCancelPreparation();
  void testSearchReplacementDestroyPreparation();
  void testSearchReplacementEncoding_data();
  void testSearchReplacementEncoding();

private:
  SearchFileResult
  createReplacementTarget(const QString &p_path,
                          const QByteArray &p_bytes = QByteArrayLiteral("foo foo\nkeep\n"));
  QString replacementFilePath(const SearchFileResult &p_target) const;

  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestBuffer::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("buffer_handle_test"));
  QString configJson =
      QStringLiteral(R"({"name": "Buffer Handle Test", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());
}

void TestBuffer::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestBuffer::cleanup() {
  // Close all open buffers between tests.
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

// ============ Buffer handle basics ============

void TestBuffer::testDefaultConstructor() {
  Buffer2 buf;
  QVERIFY(!buf.isValid());
  QVERIFY(buf.id().isEmpty());
  QVERIFY(!buf.nodeId().isValid());
}

void TestBuffer::testIsValid() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
}

void TestBuffer::testId() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(!buf.id().isEmpty());
}

void TestBuffer::testNodeId() {
  NodeIdentifier nodeId{m_notebookId, QStringLiteral("test.md")};
  Buffer2 buf = m_bufferService->openBuffer(nodeId);
  QVERIFY(buf.isValid());
  QCOMPARE(buf.nodeId().notebookId, m_notebookId);
  QCOMPARE(buf.nodeId().relativePath, QStringLiteral("test.md"));
  QCOMPARE(buf.nodeId(), nodeId);
}

// ============ Buffer via BufferService::openBuffer ============

void TestBuffer::testOpenReturnsValidBuffer() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.id().isEmpty());
}

void TestBuffer::testOpenCancelledByHook() {
  int hookId = m_hookMgr->addAction(
      HookNames::FileBeforeOpen, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(!buf.isValid());

  m_hookMgr->removeAction(hookId);
}

// ============ Per-buffer content operations ============

void TestBuffer::testGetContentRaw() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  // Just verify it doesn't crash. Content may be empty for a new file.
  buf.getContentRaw();
}

void TestBuffer::testSetContentRaw() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QByteArray expected("Hello Buffer");
  QVERIFY(buf.setContentRaw(expected));
  QCOMPARE(buf.getContentRaw(), expected);
}

void TestBuffer::testSave() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("save me")));
  QVERIFY(buf.save());
}

void TestBuffer::testSaveCancelledByHook() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("should not save")));

  int hookId = m_hookMgr->addAction(
      HookNames::FileBeforeSave, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  QVERIFY(!buf.save());

  m_hookMgr->removeAction(hookId);
}

void TestBuffer::testSaveFiresAfterHook() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("fire after")));

  bool afterFired = false;
  int hookId = m_hookMgr->addAction(
      HookNames::FileAfterSave,
      [&afterFired](HookContext &, const QVariantMap &) { afterFired = true; }, 10);

  QVERIFY(buf.save());
  QVERIFY(afterFired);

  m_hookMgr->removeAction(hookId);
}

void TestBuffer::testReload() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("reload content")));
  QVERIFY(buf.save());
  QVERIFY(buf.reload());
}

void TestBuffer::testGetContent() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QJsonObject content = buf.getContent();
  // May be empty or contain "content" key — just verify no crash.
  QVERIFY(content.isEmpty() || content.contains(QStringLiteral("content")));
}

void TestBuffer::testSetContent() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  // Use setContentRaw + getContentRaw for reliable round-trip.
  QByteArray expected("JSON round-trip");
  QVERIFY(buf.setContentRaw(expected));
  QCOMPARE(buf.getContentRaw(), expected);
}

// ============ Per-buffer state operations ============

void TestBuffer::testIsModified() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QVERIFY(buf.setContentRaw(QByteArray("modified")));
  QVERIFY(buf.isModified());

  QVERIFY(buf.save());
  QVERIFY(!buf.isModified());
}

void TestBuffer::testGetState() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QCOMPARE(buf.getState(), BufferState::Normal);
}

void TestBuffer::testGetBuffer() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QJsonObject bufInfo = buf.getBuffer();
  QVERIFY(!bufInfo.isEmpty());
}

// ============ Per-buffer asset operations ============

void TestBuffer::testInsertAssetRaw() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString relativePath =
      buf.insertAssetRaw(QStringLiteral("asset.bin"), QByteArray("\x01\x02\x03", 3));
  QVERIFY(!relativePath.isEmpty());
}

void TestBuffer::testInsertAsset() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("insert_asset_src.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("asset file") > 0);
  src.close();

  QString relativePath = buf.insertAsset(srcPath);
  QVERIFY(!relativePath.isEmpty());
}

void TestBuffer::testDeleteAsset() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString relativePath = buf.insertAssetRaw(QStringLiteral("to_delete.bin"), QByteArray("abc"));
  QVERIFY(!relativePath.isEmpty());
  QVERIFY(buf.deleteAsset(relativePath));
}

void TestBuffer::testGetAssetsFolder() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.getAssetsFolder().isEmpty());
}

// ============ Per-buffer attachment operations ============

void TestBuffer::testInsertAttachment() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_attach_src.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("attachment") > 0);
  src.close();

  QString filename = buf.insertAttachment(srcPath);
  QVERIFY(!filename.isEmpty());
}

void TestBuffer::testListAttachments() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_list_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("list") > 0);
  src.close();

  QString filename = buf.insertAttachment(srcPath);
  QVERIFY(!filename.isEmpty());

  QJsonArray attachments = buf.listAttachments();
  bool found = false;
  for (const auto &val : attachments) {
    if (val.toString() == filename) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestBuffer::testDeleteAttachment() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_del_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("delete") > 0);
  src.close();

  QString filename = buf.insertAttachment(srcPath);
  QVERIFY(!filename.isEmpty());
  QVERIFY(buf.deleteAttachment(filename));

  QJsonArray attachments = buf.listAttachments();
  bool found = false;
  for (const auto &val : attachments) {
    if (val.toString() == filename) {
      found = true;
      break;
    }
  }
  QVERIFY(!found);
}

void TestBuffer::testRenameAttachment() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_rename_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("rename") > 0);
  src.close();

  QString oldFilename = buf.insertAttachment(srcPath);
  QVERIFY(!oldFilename.isEmpty());

  QString newFilename = buf.renameAttachment(oldFilename, QStringLiteral("buf_renamed.txt"));
  QVERIFY(!newFilename.isEmpty());

  QJsonArray attachments = buf.listAttachments();
  bool foundOld = false;
  bool foundNew = false;
  for (const auto &val : attachments) {
    QString name = val.toString();
    if (name == oldFilename) {
      foundOld = true;
    }
    if (name == newFilename) {
      foundNew = true;
    }
  }
  QVERIFY(!foundOld);
  QVERIFY(foundNew);
}

void TestBuffer::testGetAttachmentsFolder() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.getAttachmentsFolder().isEmpty());
}

void TestBuffer::testInsertAttachmentFiresHooks() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_hook_add_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("hook add") > 0);
  src.close();

  bool beforeFired = false;
  bool afterFired = false;
  AttachmentAddEvent beforeEvent;
  AttachmentAddEvent afterEvent;

  int beforeId = m_hookMgr->addAction<AttachmentAddEvent>(
      HookNames::AttachmentBeforeAdd,
      [&beforeFired, &beforeEvent](HookContext &, const AttachmentAddEvent &p_event) {
        beforeFired = true;
        beforeEvent = p_event;
      },
      10);

  int afterId = m_hookMgr->addAction<AttachmentAddEvent>(
      HookNames::AttachmentAfterAdd,
      [&afterFired, &afterEvent](HookContext &, const AttachmentAddEvent &p_event) {
        afterFired = true;
        afterEvent = p_event;
      },
      10);

  QString filename = buf.insertAttachment(srcPath);

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);

  QVERIFY(!filename.isEmpty());
  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(beforeEvent.bufferId, buf.id());
  QCOMPARE(beforeEvent.sourcePath, srcPath);
  QVERIFY(beforeEvent.filename.isEmpty());
  QCOMPARE(afterEvent.bufferId, buf.id());
  QCOMPARE(afterEvent.sourcePath, srcPath);
  QCOMPARE(afterEvent.filename, filename);
}

void TestBuffer::testDeleteAttachmentHookCancel() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_hook_del_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("hook delete") > 0);
  src.close();

  QString filename = buf.insertAttachment(srcPath);
  QVERIFY(!filename.isEmpty());

  int hookId = m_hookMgr->addAction<AttachmentDeleteEvent>(
      HookNames::AttachmentBeforeDelete,
      [](HookContext &p_ctx, const AttachmentDeleteEvent &) { p_ctx.cancel(); }, 10);

  QVERIFY(!buf.deleteAttachment(filename));

  m_hookMgr->removeAction(hookId);

  QJsonArray attachments = buf.listAttachments();
  bool found = false;
  for (const auto &val : attachments) {
    if (val.toString() == filename) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestBuffer::testRenameAttachmentFiresHooks() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_hook_rename_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("hook rename") > 0);
  src.close();

  QString oldFilename = buf.insertAttachment(srcPath);
  QVERIFY(!oldFilename.isEmpty());

  bool beforeFired = false;
  bool afterFired = false;
  AttachmentRenameEvent beforeEvent;
  AttachmentRenameEvent afterEvent;

  int beforeId = m_hookMgr->addAction<AttachmentRenameEvent>(
      HookNames::AttachmentBeforeRename,
      [&beforeFired, &beforeEvent](HookContext &, const AttachmentRenameEvent &p_event) {
        beforeFired = true;
        beforeEvent = p_event;
      },
      10);

  int afterId = m_hookMgr->addAction<AttachmentRenameEvent>(
      HookNames::AttachmentAfterRename,
      [&afterFired, &afterEvent](HookContext &, const AttachmentRenameEvent &p_event) {
        afterFired = true;
        afterEvent = p_event;
      },
      10);

  QString requestedName = QStringLiteral("buf_hook_renamed.txt");
  QString actualName = buf.renameAttachment(oldFilename, requestedName);

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);

  QVERIFY(!actualName.isEmpty());
  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(beforeEvent.bufferId, buf.id());
  QCOMPARE(beforeEvent.oldFilename, oldFilename);
  QCOMPARE(beforeEvent.newFilename, requestedName);
  QCOMPARE(afterEvent.bufferId, buf.id());
  QCOMPARE(afterEvent.oldFilename, oldFilename);
  QCOMPARE(afterEvent.newFilename, actualName);
}

void TestBuffer::testAttachmentChangedSignal() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QSignalSpy spy(m_bufferService->asQObject(), SIGNAL(attachmentChanged(QString)));

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_signal_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("signal") > 0);
  src.close();

  QString filename = buf.insertAttachment(srcPath);
  QVERIFY(!filename.isEmpty());
  QCOMPARE(spy.count(), 1);

  QList<QVariant> args = spy.takeFirst();
  QCOMPARE(args.size(), 1);
  QCOMPARE(args[0].toString(), buf.id());
}

void TestBuffer::testHasAttachments() {
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  const QJsonArray existing = buf.listAttachments();
  for (const auto &val : existing) {
    const QString filename = val.toString();
    if (!filename.isEmpty()) {
      QVERIFY(buf.deleteAttachment(filename));
    }
  }

  QVERIFY(!buf.hasAttachments());

  QString srcPath = m_tempDir.filePath(QStringLiteral("buf_has_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("has") > 0);
  src.close();

  QString filename = buf.insertAttachment(srcPath);
  QVERIFY(!filename.isEmpty());
  QVERIFY(buf.hasAttachments());

  QVERIFY(buf.deleteAttachment(filename));
  QVERIFY(!buf.hasAttachments());
}

// ============ Invalid buffer guard ============

void TestBuffer::testInvalidBufferOperations() {
  Buffer2 buf; // Default-constructed, invalid.
  QVERIFY(!buf.isValid());

  // All operations should return safe defaults, not crash.
  QVERIFY(!buf.save());
  QVERIFY(!buf.reload());
  QVERIFY(buf.getContentRaw().isEmpty());
  QVERIFY(!buf.setContentRaw(QByteArray("fail")));
  QVERIFY(buf.getContent().isEmpty());
  QVERIFY(!buf.setContent(QStringLiteral("{}")));
  QCOMPARE(buf.getState(), BufferState::Normal);
  QVERIFY(!buf.isModified());
  QVERIFY(buf.getBuffer().isEmpty());
  QVERIFY(buf.insertAssetRaw(QStringLiteral("x"), QByteArray("y")).isEmpty());
  QVERIFY(buf.insertAsset(QStringLiteral("/tmp/x")).isEmpty());
  QVERIFY(!buf.deleteAsset(QStringLiteral("x")));
  QVERIFY(buf.getAssetsFolder().isEmpty());
  QVERIFY(buf.insertAttachment(QStringLiteral("/tmp/x")).isEmpty());
  QVERIFY(!buf.deleteAttachment(QStringLiteral("x")));
  QVERIFY(buf.renameAttachment(QStringLiteral("a"), QStringLiteral("b")).isEmpty());
  QVERIFY(buf.listAttachments().isEmpty());
  QVERIFY(buf.getAttachmentsFolder().isEmpty());
}

void TestBuffer::testMixedNoteSaveWhileProtectedLocking() {
  NotebookIoGate gate;
  SyncWorkQueueManager queues;
  HookManager hooks;
  QString plainText = QStringLiteral("ordinary text while the vault locks");
  QString privateText = QStringLiteral("protected unsaved edit survives the lock barrier");
  BufferService buffers(m_context, &hooks, &gate, AutoSavePolicy::AutoSave);
  auto setup = m_notebookService->prepareNotebookEncryption(
      m_notebookId, QString(), QByteArrayLiteral("mixed-note-regression-password"));
  QVERIFY(setup.isValid());
  QString protectedId;
  {
    auto maintenance = queues.tryAcquireMaintenance({m_notebookId});
    QVERIFY(maintenance.isValid());
    NotebookIoGate::ScopedLock lock(gate, m_notebookId);
    QCOMPARE(m_notebookService->commitNotebookEncryption(setup), VXCORE_OK);
    QCOMPARE(m_notebookService->createEncryptedNote(
                 m_notebookId, QString(), QStringLiteral("mixed-private.md"),
                 QStringLiteral("markdown"), QByteArrayLiteral("original protected body"),
                 &protectedId),
             VXCORE_OK);
  }
  auto secret = buffers.openBufferByNodeId(protectedId);
  auto plain = buffers.openBuffer({m_notebookId, QStringLiteral("test.md")});
  QVERIFY(secret.isValid());
  QVERIFY(plain.isValid());
  const auto cleanup = qScopeGuard([&]() {
    buffers.cancelProtectedLocking();
    buffers.unregisterActiveWriter(secret.id(), 1);
    buffers.unregisterActiveWriter(plain.id(), 2);
    buffers.closeBuffer(secret.id());
    buffers.closeBuffer(plain.id());
    m_notebookService->lockAllEncryption();
  });
  buffers.registerActiveWriter(secret.id(), 1, [&]() { return privateText; });
  buffers.registerActiveWriter(plain.id(), 2, [&]() { return plainText; });
  buffers.markDirty(secret.id());
  buffers.markDirty(plain.id());
  QVERIFY(buffers.beginProtectedLocking());
  buffers.registerActiveWriter(secret.id(), 3,
                               []() { return QStringLiteral("stale sibling text"); });
  QVERIFY(!secret.setContentRaw(QByteArrayLiteral("rejected mutation")));
  buffers.syncNow(plain.id());
  QTRY_VERIFY_WITH_TIMEOUT(!buffers.isSaveQueueBusy(plain.id()), 10000);
  QFile publicFile(plain.resolvedPath());
  QVERIFY(publicFile.open(QIODevice::ReadOnly));
  QCOMPARE(publicFile.readAll(), plainText.toUtf8());
  publicFile.close();
  QString error;
  QVERIFY2(buffers.saveForSnapshot(secret.id(), 1000, &error), qPrintable(error));
  buffers.cancelProtectedLocking();
  QCOMPARE(secret.getContentRaw(), privateText.toUtf8());
  QFile ciphertext(secret.resolvedPath());
  QVERIFY(ciphertext.open(QIODevice::ReadOnly));
  QVERIFY(!ciphertext.readAll().contains(privateText.toUtf8()));
  ciphertext.close();
  buffers.unregisterActiveWriter(secret.id(), 1);
  QVERIFY(buffers.closeBuffer(secret.id()));
  QCOMPARE(m_notebookService->lockAllEncryption(), VXCORE_OK);
  plainText += QStringLiteral(" and after all keys are released");
  buffers.markDirty(plain.id());
  buffers.syncNow(plain.id());
  QTRY_VERIFY_WITH_TIMEOUT(!buffers.isSaveQueueBusy(plain.id()), 10000);
  QVERIFY(publicFile.open(QIODevice::ReadOnly));
  QCOMPARE(publicFile.readAll(), plainText.toUtf8());
}

void TestBuffer::testEncryptedBodyKeepsAssetsPlaintext() {
  const auto notebookId = m_notebookService->createNotebook(
      m_tempDir.filePath(QStringLiteral("body-only-assets")),
      QStringLiteral("{\"name\":\"Body-only assets\"}"), NotebookType::Bundled);
  QVERIFY(!notebookId.isEmpty());
  NotebookIoGate gate;
  SyncWorkQueueManager queues;
  HookManager hooks;
  BufferService buffers(m_context, &hooks, &gate, AutoSavePolicy::None);
  auto setup = m_notebookService->prepareNotebookEncryption(
      notebookId, QString(), QByteArrayLiteral("body-only-asset-password"));
  QVERIFY(setup.isValid());
  QString noteId;
  const QByteArray body("the note body alone is secret");
  {
    auto maintenance = queues.tryAcquireMaintenance({notebookId});
    QVERIFY(maintenance.isValid());
    NotebookIoGate::ScopedLock lock(gate, notebookId);
    QCOMPARE(m_notebookService->commitNotebookEncryption(setup), VXCORE_OK);
    QCOMPARE(m_notebookService->createEncryptedNote(notebookId, QString(),
                                                    QStringLiteral("assets.md"),
                                                    QStringLiteral("markdown"), body, &noteId),
             VXCORE_OK);
  }
  auto note = buffers.openBufferByNodeId(noteId);
  QVERIFY(note.isValid());
  const auto cleanup = qScopeGuard([&]() {
    buffers.cancelProtectedLocking();
    buffers.closeBuffer(note.id());
    m_notebookService->lockAllEncryption();
  });
  const QByteArray imageBytes("ordinary image asset bytes");
  const auto image = note.insertAssetRaw(QStringLiteral("image.png"), imageBytes);
  QVERIFY(!image.isEmpty());
  QVERIFY(!image.startsWith(QLatin1String("vxasset:")));
  const auto imagePath = QDir(note.getResourceBasePath()).filePath(image);
  QFile imageFile(imagePath);
  QVERIFY(imageFile.open(QIODevice::ReadOnly));
  QCOMPARE(imageFile.readAll(), imageBytes);
  imageFile.close();

  const auto sourcePath = m_tempDir.filePath(QStringLiteral("receipt.txt"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  const QByteArray attachmentBytes("ordinary attachment bytes");
  QCOMPARE(source.write(attachmentBytes), qint64(attachmentBytes.size()));
  source.close();
  const auto filename = note.insertAttachment(sourcePath);
  QCOMPARE(filename, QStringLiteral("receipt.txt"));
  QCOMPARE(note.listAttachments(), QJsonArray{filename});
  const auto renamed = note.renameAttachment(filename, QStringLiteral("renamed.txt"));
  QCOMPARE(renamed, QStringLiteral("renamed.txt"));
  const auto attachmentPath = QDir(note.getAttachmentsFolder()).filePath(renamed);
  QFile attachment(attachmentPath);
  QVERIFY(attachment.open(QIODevice::ReadOnly));
  QCOMPARE(attachment.readAll(), attachmentBytes);
  attachment.close();
  QCOMPARE(note.getContentRaw(), body);

  // The body lock barrier does not turn existing plaintext assets into encrypted resources.
  QVERIFY(buffers.beginProtectedLocking());
  VxCoreError error;
  QCOMPARE(note.readResource(image, &error), imageBytes);
  QCOMPARE(error, VXCORE_OK);
  QCOMPARE(note.listAttachments(), QJsonArray{renamed});
  buffers.cancelProtectedLocking();

  const auto node = note.nodeId();
  QVERIFY(buffers.closeBuffer(note.id()));
  FileOpenSettings settings;
  settings.m_readOnly = true;
  note = buffers.openBuffer(node, settings);
  QVERIFY(note.isValid());
  QVERIFY(note.isReadOnly());
  QVERIFY(note.insertAssetRaw(QStringLiteral("blocked.png"), imageBytes).isEmpty());
  QVERIFY(note.insertAttachment(sourcePath).isEmpty());
  QVERIFY(!note.deleteAsset(image));
  QVERIFY(!note.deleteAttachment(renamed));
  QVERIFY(note.renameAttachment(renamed, QStringLiteral("blocked.txt")).isEmpty());
  QCOMPARE(note.readResource(image), imageBytes);
  QVERIFY(QFileInfo::exists(attachmentPath));

  QVERIFY(buffers.closeBuffer(note.id()));
  note = buffers.openBuffer(node);
  QVERIFY(note.isValid());
  QVERIFY(note.deleteAttachment(renamed));
  QVERIFY(!QFileInfo::exists(attachmentPath));
  QVERIFY(note.deleteAsset(image));
  QVERIFY(!QFileInfo::exists(imagePath));
}

// ============ Search-result replacement persistence ============

SearchFileResult TestBuffer::createReplacementTarget(const QString &p_path,
                                                     const QByteArray &p_bytes) {
  SearchFileResult target;
  const auto slash = p_path.lastIndexOf(QLatin1Char('/'));
  const auto parent = slash < 0 ? QString() : p_path.left(slash);
  if (!parent.isEmpty() && m_notebookService->createFolderPath(m_notebookId, parent).isEmpty()) {
    return target;
  }
  target.m_id = m_notebookService->createFile(m_notebookId, parent,
                                              slash < 0 ? p_path : p_path.mid(slash + 1));
  if (target.m_id.isEmpty()) {
    return target;
  }
  target.m_notebookId = m_notebookId;
  target.m_path = p_path;
  if (!writeReplacementFile(replacementFilePath(target), p_bytes)) {
    return SearchFileResult();
  }
  SearchLineMatch line;
  line.m_lineNumber = 1;
  line.m_lineText = QStringLiteral("foo foo");
  line.m_segments = {{0, 3}, {4, 7}};
  target.m_lineMatches = {line};
  target.m_matchCount = 2;
  target.m_replacementSupported = true;
  return target;
}

QString TestBuffer::replacementFilePath(const SearchFileResult &p_target) const {
  return QDir(m_tempDir.filePath(QStringLiteral("buffer_handle_test"))).filePath(p_target.m_path);
}

void TestBuffer::testSearchReplacementWriterDurability_data() {
  QTest::addColumn<int>("policy");
  QTest::newRow("sync-only") << int(AutoSavePolicy::None);
  QTest::newRow("auto-save") << int(AutoSavePolicy::AutoSave);
  QTest::newRow("backup-only") << int(AutoSavePolicy::BackupFile);
}

void TestBuffer::testSearchReplacementWriterDurability() {
  QFETCH(int, policy);
  const auto target = createReplacementTarget(QStringLiteral("replace-writer-%1.md").arg(policy),
                                              QByteArrayLiteral("foo foo\r\nkeep\r\n"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  const QByteArray original = readReplacementFile(path);
  const QByteArray expected("bar bar\r\ndraft\r\n");
  QString editor = QStringLiteral("foo foo\r\ndraft\r\n");
  NotebookIoGate gate;
  HookManager hooks;
  BufferService buffers(m_context, &hooks, &gate, AutoSavePolicy(policy));
  auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  buffers.registerActiveWriter(note.id(), 1, [&]() { return editor; });
  buffers.markDirty(note.id());
  const auto capturedRevision = buffers.currentRevision(note.id());
  QCOMPARE(note.getContentRaw(), original);
  QVERIFY(buffers.isDirty(note.id()));

  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool mutationsRejected = false;
  bool staleWriterSuppressedDuringRefresh = false;
  bool hooksOutsideGate = true;
  bool refreshedBeforeAfterSave = false;
  bool terminalDurable = false;
  bool terminalOnOwnerThread = false;
  observer.stateCallback = [&](const ReplacementState &p_state) {
    if (p_state.active) {
      const auto revision = buffers.currentRevision(note.id());
      const auto encoding = buffers.bufferEncoding(note.id());
      const bool guarded = buffers.isContentReplacementActive(note.id());
      const bool setRejected = !note.setContentRaw(QByteArrayLiteral("stale mutation"));
      const bool reloadRejected = !note.reload();
      const bool closeRejected = !buffers.closeBuffer(note.id());
      buffers.setBufferEncoding(note.id(), QStringLiteral("GB18030"));
      buffers.markDirty(note.id());
      buffers.registerActiveWriter(note.id(), 2,
                                   []() { return QStringLiteral("stale sibling writer"); });
      buffers.syncNow(note.id());
      mutationsRejected = guarded && setRejected && reloadRejected && closeRejected &&
                          buffers.bufferEncoding(note.id()) == encoding &&
                          buffers.currentRevision(note.id()) == revision &&
                          note.getContentRaw() == original;
    } else if (p_state.changed) {
      buffers.pullActiveWriterContent(note.id());
      staleWriterSuppressedDuringRefresh = note.getContentRaw() == expected;
      // This is the consumer refresh contract, not a direct write into BufferService.
      editor = buffers.decodeContent(note.id(), note.getContentRaw());
    }
  };
  const int beforeHook = hooks.addAction<BufferEvent>(
      HookNames::FileBeforeSave, [&](HookContext &, const BufferEvent &) {
        NotebookIoGate::ScopedTryLock lock(gate, target.m_notebookId, 0);
        hooksOutsideGate = hooksOutsideGate && lock.isLocked();
      });
  const int afterHook = hooks.addAction<BufferEvent>(
      HookNames::FileAfterSave, [&](HookContext &, const BufferEvent &) {
        NotebookIoGate::ScopedTryLock lock(gate, target.m_notebookId, 0);
        hooksOutsideGate = hooksOutsideGate && lock.isLocked();
        refreshedBeforeAfterSave = editor.toUtf8() == expected;
      });
  observer.finishedCallback = [&](const ReplacementCompletion &p_result) {
    terminalOnOwnerThread = QThread::currentThread() == buffers.asQObject()->thread();
    terminalDurable = p_result.saved && readReplacementFile(path) == expected &&
                      note.getContentRaw() == expected && !buffers.isSaveQueueBusy(note.id()) &&
                      !buffers.isDirty(note.id()) &&
                      buffers.lastSavedRevision(note.id()) == buffers.currentRevision(note.id()) &&
                      buffers.currentRevision(note.id()) > capturedRevision;
  };
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(beforeHook);
    hooks.removeAction(afterHook);
    buffers.unregisterActiveWriter(note.id(), 1);
    buffers.closeBuffer(note.id());
  });

  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QCOMPARE(completed.count(), 0); // Completion cannot outrun the caller's token assignment.
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QCOMPARE(result.nodeId, note.nodeId());
  QCOMPARE(result.bufferId, note.id());
  QCOMPARE(result.matches, 2);
  QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
  QVERIFY(terminalOnOwnerThread);
  QVERIFY(terminalDurable);
  QVERIFY(mutationsRejected);
  QVERIFY(staleWriterSuppressedDuringRefresh);
  QVERIFY(hooksOutsideGate);
  QVERIFY(refreshedBeforeAfterSave);
  QVERIFY(!buffers.isContentReplacementActive(note.id()));
  QVERIFY(!note.isModified());
  QCOMPARE(observer.states.size(), 2);
  QVERIFY(observer.states.constLast().changed);
  QVERIFY(observer.states.constLast().saved);

  // A later focus-loss capture must not resurrect either pre-replacement editor.
  QVERIFY(buffers.pullActiveWriterContent(note.id()));
  QCOMPARE(note.getContentRaw(), expected);
  editor += QStringLiteral("later edit\r\n");
  buffers.markDirty(note.id());
  QVERIFY(buffers.pullActiveWriterContent(note.id()));
  QCOMPARE(note.getContentRaw(), editor.toUtf8());
  QCOMPARE(readReplacementFile(path), expected);
  QVERIFY(buffers.isDirty(note.id()));
  QCOMPARE(completed.count(), 1);
}

void TestBuffer::testSearchReplacementPrivateOpenAndNoop() {
  const auto target = createReplacementTarget(QStringLiteral("replace-unopened.md"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool openedUi = false;
  int beforeSaves = 0;
  int afterSaves = 0;
  bool durableInCompletion = false;
  const int openHook = hooks.addAction<FileOpenEvent>(
      HookNames::FileAfterOpen, [&](HookContext &, const FileOpenEvent &) { openedUi = true; });
  const int beforeHook = hooks.addAction<BufferEvent>(
      HookNames::FileBeforeSave, [&](HookContext &, const BufferEvent &) { ++beforeSaves; });
  const int afterHook = hooks.addAction<BufferEvent>(
      HookNames::FileAfterSave, [&](HookContext &, const BufferEvent &) { ++afterSaves; });
  observer.finishedCallback = [&](const ReplacementCompletion &p_result) {
    if (p_result.matches > 0) {
      durableInCompletion =
          p_result.saved && readReplacementFile(path) == QByteArrayLiteral("bar bar\nkeep\n");
    }
  };
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(openHook);
    hooks.removeAction(beforeHook);
    hooks.removeAction(afterHook);
  });
  QVERIFY(buffers.listBuffers().isEmpty());

  const int noopToken = buffers.replaceSearchMatches(target, QStringLiteral("foo"));
  QVERIFY(noopToken > 0);
  QCOMPARE(completed.count(), 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(observer.completions.constLast().token, noopToken);
  QCOMPARE(observer.completions.constLast().matches, 0);
  QVERIFY(observer.completions.constLast().error.isEmpty());
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("foo foo\nkeep\n"));
  QCOMPARE(beforeSaves, 0);
  QCOMPARE(afterSaves, 0);
  QVERIFY(buffers.listBuffers().isEmpty());

  const int changedToken = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(changedToken > noopToken);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 2);
  const auto result = observer.completions.constLast();
  QCOMPARE(result.token, changedToken);
  QCOMPARE(result.matches, 2);
  QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
  QVERIFY(durableInCompletion);
  QCOMPARE(beforeSaves, 1);
  QCOMPARE(afterSaves, 1);
  QVERIFY(!openedUi);
  QVERIFY(buffers.listBuffers().isEmpty());
}

void TestBuffer::testSearchReplacementUnsupportedTarget() {
  auto target = createReplacementTarget(QStringLiteral("replace-unsupported.md"));
  QVERIFY(!target.m_id.isEmpty());
  // A handcrafted/default result and an old unsupported-backend result have no capability.
  target.m_replacementSupported = false;
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool opened = false;
  const int hook = hooks.addAction<FileOpenEvent>(
      HookNames::FileAfterOpen, [&](HookContext &, const FileOpenEvent &) { opened = true; });
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(hook);
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QCOMPARE(completed.count(), 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(!result.saved);
  QCOMPARE(result.matches, 0);
  QVERIFY(!result.error.isEmpty());
  QVERIFY(!opened);
  QVERIFY(observer.states.isEmpty());
  QVERIFY(buffers.listBuffers().isEmpty());
  QCOMPARE(readReplacementFile(replacementFilePath(target)), QByteArrayLiteral("foo foo\nkeep\n"));
}

void TestBuffer::testSearchReplacementReadOnly_data() {
  QTest::addColumn<bool>("privateOpen");
  QTest::newRow("open-readonly-view") << false;
  QTest::newRow("unopened-readonly-notebook") << true;
}

void TestBuffer::testSearchReplacementReadOnly() {
  QFETCH(bool, privateOpen);
  const auto target =
      createReplacementTarget(QStringLiteral("replace-readonly-%1.md").arg(int(privateOpen)));
  QVERIFY(!target.m_id.isEmpty());
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  Buffer2 note;
  if (!privateOpen) {
    FileOpenSettings settings;
    settings.m_readOnly = true;
    note = buffers.openBuffer({target.m_notebookId, target.m_path}, settings);
    QVERIFY(note.isValid());
    QVERIFY(note.isReadOnly());
  }
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
    if (note.isValid()) {
      buffers.closeBuffer(note.id());
    }
  });
  if (privateOpen) {
    QCOMPARE(vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true),
             VXCORE_OK);
  }
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(!result.saved);
  QCOMPARE(result.matches, 0);
  QVERIFY(!result.error.isEmpty());
  QCOMPARE(readReplacementFile(replacementFilePath(target)), QByteArrayLiteral("foo foo\nkeep\n"));
  if (privateOpen) {
    QVERIFY(buffers.listBuffers().isEmpty());
  } else {
    QVERIFY(buffers.getBufferHandle(note.id()).isValid());
    QCOMPARE(note.getContentRaw(), QByteArrayLiteral("foo foo\nkeep\n"));
    QVERIFY(note.isReadOnly());
    QVERIFY(!buffers.isDirty(note.id()));
  }
}

void TestBuffer::testSearchReplacementProtected() {
  const auto root = m_tempDir.filePath(QStringLiteral("replace-protected-notebook"));
  const auto notebookId = m_notebookService->createNotebook(
      root, QStringLiteral("{\"name\":\"Replacement protection\"}"), NotebookType::Bundled);
  QVERIFY(!notebookId.isEmpty());
  NotebookIoGate gate;
  SyncWorkQueueManager queues;
  auto setup = m_notebookService->prepareNotebookEncryption(
      notebookId, QString(), QByteArrayLiteral("replacement-protection-password"));
  QVERIFY(setup.isValid());
  QString nodeId;
  {
    auto maintenance = queues.tryAcquireMaintenance({notebookId});
    QVERIFY(maintenance.isValid());
    NotebookIoGate::ScopedTryLock lock(gate, notebookId, 0);
    QVERIFY(lock.isLocked());
    QCOMPARE(m_notebookService->commitNotebookEncryption(setup), VXCORE_OK);
    QCOMPARE(m_notebookService->createEncryptedNote(
                 notebookId, QString(), QStringLiteral("private.md"), QStringLiteral("markdown"),
                 QByteArrayLiteral("foo foo\nkeep\n"), &nodeId),
             VXCORE_OK);
  }
  const auto relativePath = m_notebookService->getNodePathById(notebookId, nodeId);
  QVERIFY(!relativePath.isEmpty());
  const auto path = QDir(root).filePath(relativePath);
  const auto encryptedBytes = readReplacementFile(path);
  QVERIFY(!encryptedBytes.isEmpty());
  QVERIFY(!encryptedBytes.contains(QByteArrayLiteral("foo foo")));
  QCOMPARE(m_notebookService->lockAllEncryption(), VXCORE_OK);

  SearchFileResult target;
  target.m_notebookId = notebookId;
  target.m_path = relativePath;
  target.m_id = nodeId;
  target.m_replacementSupported = true;
  SearchLineMatch line;
  line.m_lineNumber = 1;
  line.m_lineText = QStringLiteral("foo foo");
  line.m_segments = {{0, 3}, {4, 7}};
  target.m_lineMatches = {line};
  target.m_matchCount = 2;
  HookManager hooks;
  BufferService buffers(m_context, &hooks, &gate, AutoSavePolicy::None);
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool opened = false;
  const int hook = hooks.addAction<FileOpenEvent>(
      HookNames::FileAfterOpen, [&](HookContext &, const FileOpenEvent &) { opened = true; });
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(hook);
    m_notebookService->lockAllEncryption();
  });

  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(!result.saved);
  QCOMPARE(result.matches, 0);
  QVERIFY(!result.error.isEmpty());
  QCOMPARE(readReplacementFile(path), encryptedBytes);
  QCOMPARE(observer.unlockRequests, 0);
  QVERIFY(!opened);
  QVERIFY(buffers.protectedBuffers().isEmpty());
  QVERIFY(buffers.listBuffers().isEmpty());
  // Even a subsequent normal open still needs explicit unlock; replacement did not decrypt it.
  QVERIFY(!buffers.openBuffer({notebookId, relativePath}).isValid());
  QCOMPARE(observer.unlockRequests, 1);
  QCOMPARE(readReplacementFile(path), encryptedBytes);
}

void TestBuffer::testSearchReplacementWaitsForAutosave() {
  const auto target = createReplacementTarget(QStringLiteral("replace-busy.md"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  QString editor = QStringLiteral("foo foo\nfirst draft\n");
  NotebookIoGate gate;
  HookManager hooks;
  BufferService buffers(m_context, &hooks, &gate, AutoSavePolicy::AutoSave);
  auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  int captures = 0;
  buffers.registerActiveWriter(note.id(), 1, [&]() {
    ++captures;
    return editor;
  });
  ReplacementObserver observer(buffers);
  QSignalSpy frozen(&observer, &ReplacementObserver::stateObserved);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  QSignalSpy autoSaved(buffers.asQObject(), SIGNAL(bufferAutoSaved(QString)));
  ReplacementGateBarrier barrier(gate, target.m_notebookId);
  const auto cleanup = qScopeGuard([&]() {
    barrier.release();
    buffers.shutdown();
    buffers.unregisterActiveWriter(note.id(), 1);
    buffers.closeBuffer(note.id());
  });
  buffers.markDirty(note.id());
  buffers.syncNow(note.id());
  QVERIFY(buffers.isSaveQueueBusy(note.id()));
  QCOMPARE(captures, 1);
  editor = QStringLiteral("foo foo\nlatest unsaved draft\n");
  buffers.markDirty(note.id());
  const auto unsavedRevision = buffers.currentRevision(note.id());
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(frozen.wait(10000));
  QVERIFY(buffers.isContentReplacementActive(note.id()));
  QVERIFY(buffers.isSaveQueueBusy(note.id()));
  QCOMPARE(completed.count(), 0);
  QCOMPARE(captures, 1); // Capture must wait for the earlier queued snapshot to settle.
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("foo foo\nkeep\n"));
  barrier.release();
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY2(result.saved, qPrintable(result.error));
  QCOMPARE(result.matches, 2);
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("bar bar\nlatest unsaved draft\n"));
  QCOMPARE(note.getContentRaw(), readReplacementFile(path));
  QVERIFY(buffers.currentRevision(note.id()) > unsavedRevision);
  QCOMPARE(buffers.lastSavedRevision(note.id()), buffers.currentRevision(note.id()));
  QVERIFY(!buffers.isDirty(note.id()));
  QCOMPARE(autoSaved.count(), 0); // The old completion must not project old text into the view.
}

void TestBuffer::testSearchReplacementBeforeSaveVeto() {
  const auto target = createReplacementTarget(QStringLiteral("replace-veto.md"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  QString editor = QStringLiteral("foo foo\nunsaved writer\n");
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  buffers.registerActiveWriter(note.id(), 1, [&]() { return editor; });
  buffers.markDirty(note.id());
  const auto revision = buffers.currentRevision(note.id());
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool beforeSave = false;
  bool afterSave = false;
  const int beforeHook = hooks.addAction<BufferEvent>(
      HookNames::FileBeforeSave, [&](HookContext &p_ctx, const BufferEvent &p_event) {
        beforeSave = p_event.bufferId == note.id();
        p_ctx.cancel();
      });
  const int afterHook = hooks.addAction<BufferEvent>(
      HookNames::FileAfterSave, [&](HookContext &, const BufferEvent &) { afterSave = true; });
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(beforeHook);
    hooks.removeAction(afterHook);
    buffers.unregisterActiveWriter(note.id(), 1);
    buffers.closeBuffer(note.id());
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(beforeSave);
  QVERIFY(!afterSave);
  QVERIFY(!result.saved);
  QVERIFY(!result.error.isEmpty());
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("foo foo\nkeep\n"));
  QCOMPARE(note.getContentRaw(), QByteArrayLiteral("foo foo\nkeep\n"));
  QCOMPARE(buffers.currentRevision(note.id()), revision);
  QVERIFY(buffers.isDirty(note.id()));
  QVERIFY(!buffers.isContentReplacementActive(note.id()));
  QVERIFY(!observer.states.constLast().changed);
  QString restoredWriter;
  QVERIFY(buffers.captureActiveWriterContent(note.id(), &restoredWriter));
  QCOMPARE(restoredWriter, editor);
  QVERIFY(buffers.pullActiveWriterContent(note.id()));
  QCOMPARE(note.getContentRaw(), editor.toUtf8());
}

void TestBuffer::testSearchReplacementExternalChangeErrors_data() {
  QTest::addColumn<int>("failure");
  QTest::addColumn<QString>("expectedError");
  QTest::newRow("check-api-failure")
      << 0 << QStringLiteral("Could not check the note for changes on disk.");
  QTest::newRow("file-changed") << 1
                                << QStringLiteral(
                                       "The note changed on disk. Search again before replacing.");
  QTest::newRow("file-missing") << 2
                                << QStringLiteral(
                                       "The note changed on disk. Search again before replacing.");
}

void TestBuffer::testSearchReplacementExternalChangeErrors() {
  QFETCH(int, failure);
  QFETCH(QString, expectedError);
  const auto target =
      createReplacementTarget(QStringLiteral("replace-external-error-%1.md").arg(failure));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  BufferCoreService core(m_context);
  const auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  const QByteArray original = note.getContentRaw(); // Load before changing the backing file.
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool injected = false;
  bool bufferClosed = false;
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    if (!bufferClosed)
      buffers.closeBuffer(note.id());
  });
  observer.stateCallback = [&](const ReplacementState &p_state) {
    if (!p_state.active)
      return;
    if (failure == 0) {
      // Bypass the reservation through the real core API to make the check itself fail.
      injected = bufferClosed = core.closeBuffer(note.id());
    } else if (failure == 1) {
      injected = writeReplacementFile(path, QByteArrayLiteral("external"));
      QFile file(path);
      injected = injected && file.open(QIODevice::ReadWrite) &&
                 file.setFileTime(QDateTime::currentDateTime().addSecs(2),
                                  QFileDevice::FileModificationTime);
    } else {
      injected = QFile::remove(path);
    }
  };
  QVERIFY(buffers.replaceSearchMatches(target, QStringLiteral("bar")) > 0);
  QVERIFY(completed.wait(10000));
  QVERIFY(injected);
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.error, expectedError);
  QCOMPARE(result.matches, 0);
  QVERIFY(!result.saved);
  QVERIFY(!buffers.isContentReplacementActive(note.id()));
  if (failure == 2) {
    QVERIFY(!QFile::exists(path));
    QCOMPARE(note.getState(), BufferState::FileMissing);
  } else {
    QCOMPARE(readReplacementFile(path), failure == 1 ? QByteArrayLiteral("external") : original);
    if (failure == 1)
      QCOMPARE(note.getState(), BufferState::FileChanged);
  }
}

void TestBuffer::testSearchReplacementDigestConflict() {
  const auto target = createReplacementTarget(QStringLiteral("replace-disk-conflict.md"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  const QByteArray external("foo foo\nexternal disk edit\n");
  QString editor = QStringLiteral("foo foo\nunsaved editor edit\n");
  NotebookIoGate gate;
  HookManager hooks;
  BufferService buffers(m_context, &hooks, &gate, AutoSavePolicy::None);
  auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  buffers.registerActiveWriter(note.id(), 1, [&]() { return editor; });
  buffers.markDirty(note.id());
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool changedAfterPreparation = false;
  const int hook = hooks.addAction<BufferEvent>(
      HookNames::FileBeforeSave, [&](HookContext &, const BufferEvent &) {
        // FileBeforeSave is after preparation but before enqueue. Change only an unrelated
        // line: matching the reviewed line again is not a substitute for the source digest.
        NotebookIoGate::ScopedTryLock lock(gate, target.m_notebookId, 0);
        changedAfterPreparation = lock.isLocked() && writeReplacementFile(path, external);
      });
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(hook);
    buffers.unregisterActiveWriter(note.id(), 1);
    buffers.closeBuffer(note.id());
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(changedAfterPreparation);
  QVERIFY(!result.saved);
  QVERIFY(!result.error.isEmpty());
  QCOMPARE(readReplacementFile(path), external);
  QCOMPARE(note.getContentRaw(), QByteArrayLiteral("foo foo\nkeep\n"));
  QVERIFY(buffers.isDirty(note.id()));
  QVERIFY(!buffers.isSaveQueueBusy(note.id()));
  QVERIFY(!buffers.isContentReplacementActive(note.id()));
  QVERIFY(!observer.states.constLast().changed);
  QString restoredWriter;
  QVERIFY(buffers.captureActiveWriterContent(note.id(), &restoredWriter));
  QCOMPARE(restoredWriter, editor);
}

void TestBuffer::testSearchReplacementWriteFailureRecovery() {
  const auto target = createReplacementTarget(QStringLiteral("replace-write-failure.md"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  const QByteArray expected("bar bar\nkeep\n");
  HookManager hooks;
  ReplacementWriteFailureService buffers(m_context, &hooks, AutoSavePolicy::AutoSave);
  buffers.failurePath = path;
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool afterSave = false;
  QByteArray refreshedRecoveryText;
  bool recoveryDirtyAtCompletion = false;
  const int hook = hooks.addAction<BufferEvent>(
      HookNames::FileAfterSave, [&](HookContext &, const BufferEvent &) { afterSave = true; });
  observer.stateCallback = [&](const ReplacementState &p_state) {
    if (!p_state.active && p_state.changed) {
      refreshedRecoveryText = buffers.getBufferHandle(p_state.bufferId).getContentRaw();
    }
  };
  observer.finishedCallback = [&](const ReplacementCompletion &p_result) {
    auto recovered = buffers.getBufferHandle(p_result.bufferId);
    recoveryDirtyAtCompletion =
        recovered.isValid() && recovered.getContentRaw() == expected &&
        buffers.isDirty(p_result.bufferId) &&
        buffers.lastSavedRevision(p_result.bufferId) < buffers.currentRevision(p_result.bufferId) &&
        !buffers.isSaveQueueBusy(p_result.bufferId);
  };
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(hook);
    for (const auto &entry : buffers.listBuffers()) {
      buffers.closeBuffer(entry.toObject().value(QStringLiteral("id")).toString());
    }
  });
  QVERIFY(buffers.listBuffers().isEmpty());
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(!result.saved);
  QVERIFY(!result.error.isEmpty());
  QVERIFY(buffers.obstructionCreated);
  QVERIFY(buffers.writeFailed);
  QVERIFY(buffers.originalRestored);
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("foo foo\nkeep\n"));
  QVERIFY(!afterSave);
  QVERIFY(recoveryDirtyAtCompletion);
  QCOMPARE(refreshedRecoveryText, expected);
  QVERIFY(observer.states.constLast().changed);
  QVERIFY(!observer.states.constLast().saved);
  QVERIFY(!buffers.isContentReplacementActive(result.bufferId));
  QCOMPARE(result.nodeId, (NodeIdentifier{target.m_notebookId, target.m_path}));

  // The completion-owned unopened buffer must survive until a recovery editor claims it.
  auto recovery = buffers.openBuffer(result.nodeId);
  QVERIFY(recovery.isValid());
  QCOMPARE(recovery.id(), result.bufferId);
  QCOMPARE(recovery.getContentRaw(), expected);
  QVERIFY(recovery.isModified());
  QVERIFY(buffers.isDirty(recovery.id()));

  QVERIFY(recovery.save());
  QCOMPARE(readReplacementFile(path), expected);
  QVERIFY(!recovery.isModified());
  QVERIFY(!buffers.isDirty(recovery.id()));
  QCOMPARE(buffers.lastSavedRevision(recovery.id()), buffers.currentRevision(recovery.id()));
  QVERIFY(afterSave);
}

void TestBuffer::testSearchReplacementStaleWriter() {
  const auto target = createReplacementTarget(QStringLiteral("replace-stale-writer.md"));
  QVERIFY(!target.m_id.isEmpty());
  QString editor = QStringLiteral("foo changed\nunsaved draft\n");
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  buffers.registerActiveWriter(note.id(), 1, [&]() { return editor; });
  buffers.markDirty(note.id());
  const auto revision = buffers.currentRevision(note.id());
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  bool saveAttempted = false;
  const int hook = hooks.addAction<BufferEvent>(
      HookNames::FileBeforeSave, [&](HookContext &, const BufferEvent &) { saveAttempted = true; });
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(hook);
    buffers.unregisterActiveWriter(note.id(), 1);
    buffers.closeBuffer(note.id());
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  QVERIFY(!observer.completions.constFirst().saved);
  QVERIFY(!observer.completions.constFirst().error.isEmpty());
  QVERIFY(!saveAttempted);
  QCOMPARE(readReplacementFile(replacementFilePath(target)), QByteArrayLiteral("foo foo\nkeep\n"));
  QCOMPARE(note.getContentRaw(), QByteArrayLiteral("foo foo\nkeep\n"));
  QCOMPARE(buffers.currentRevision(note.id()), revision);
  QVERIFY(buffers.isDirty(note.id()));
  QVERIFY(!buffers.isContentReplacementActive(note.id()));
  QString captured;
  QVERIFY(buffers.captureActiveWriterContent(note.id(), &captured));
  QCOMPARE(captured, editor);

  // Identical replacement on a valid line must not save unrelated dirty editor text either.
  editor = QStringLiteral("foo foo\nunsaved draft\n");
  buffers.markDirty(note.id());
  const auto noopRevision = buffers.currentRevision(note.id());
  const int noopToken = buffers.replaceSearchMatches(target, QStringLiteral("foo"));
  QVERIFY(noopToken > token);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 2);
  const auto noop = observer.completions.constLast();
  QCOMPARE(noop.token, noopToken);
  QCOMPARE(noop.matches, 0);
  QVERIFY(noop.error.isEmpty());
  QVERIFY(!saveAttempted);
  QCOMPARE(readReplacementFile(replacementFilePath(target)), QByteArrayLiteral("foo foo\nkeep\n"));
  QCOMPARE(buffers.currentRevision(note.id()), noopRevision);
  QVERIFY(buffers.isDirty(note.id()));
  QVERIFY(buffers.captureActiveWriterContent(note.id(), &captured));
  QCOMPARE(captured, editor);
}

void TestBuffer::testSearchReplacementAfterSaveEdit() {
  const auto target = createReplacementTarget(QStringLiteral("replace-after-save-edit.md"));
  QVERIFY(!target.m_id.isEmpty());
  QString editor = QStringLiteral("foo foo\nunsaved draft\n");
  const QByteArray expected("bar bar\nunsaved draft\n");
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  auto note = buffers.openBuffer({target.m_notebookId, target.m_path});
  QVERIFY(note.isValid());
  buffers.registerActiveWriter(note.id(), 1, [&]() { return editor; });
  buffers.markDirty(note.id());
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  observer.stateCallback = [&](const ReplacementState &p_state) {
    if (!p_state.active && p_state.changed) {
      editor = buffers.decodeContent(note.id(), note.getContentRaw());
    }
  };
  bool editedAfterDurability = false;
  const int hook = hooks.addAction<BufferEvent>(HookNames::FileAfterSave, [&](HookContext &,
                                                                              const BufferEvent &) {
    editedAfterDurability =
        editor.toUtf8() == expected && readReplacementFile(replacementFilePath(target)) == expected;
    editor += QStringLiteral("after-save edit\n");
    buffers.markDirty(note.id());
  });
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    hooks.removeAction(hook);
    buffers.unregisterActiveWriter(note.id(), 1);
    buffers.closeBuffer(note.id());
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  QVERIFY(editedAfterDurability);
  QVERIFY2(observer.completions.constFirst().saved,
           qPrintable(observer.completions.constFirst().error));
  QCOMPARE(readReplacementFile(replacementFilePath(target)), expected);
  QVERIFY(buffers.isDirty(note.id()));
  QVERIFY(buffers.lastSavedRevision(note.id()) < buffers.currentRevision(note.id()));
  QVERIFY(buffers.pullActiveWriterContent(note.id()));
  QCOMPARE(note.getContentRaw(), editor.toUtf8());
  QCOMPARE(readReplacementFile(replacementFilePath(target)), expected);
}

void TestBuffer::testSearchReplacementLifecycleGuards() {
  const auto target = createReplacementTarget(QStringLiteral("replace-guard/child.md"));
  QVERIFY(!target.m_id.isEmpty());
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  ReplacementObserver observer(buffers);
  QSignalSpy frozen(&observer, &ReplacementObserver::stateObserved);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  ReplacementPreparationBarrier barrier;
  const auto cleanup = qScopeGuard([&]() {
    barrier.release();
    buffers.shutdown();
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(frozen.wait(10000));
  QVERIFY(observer.states.constFirst().active);
  const auto reservedId = observer.states.constFirst().bufferId;
  QVERIFY(buffers.isContentReplacementActive(reservedId));

  NotebookCloseEvent notebookEvent;
  notebookEvent.notebookId = target.m_notebookId;
  QVERIFY(hooks.doAction(HookNames::NotebookBeforeClose, notebookEvent));
  notebookEvent.notebookId = QStringLiteral("another-notebook");
  QVERIFY(!hooks.doAction(HookNames::NotebookBeforeClose, notebookEvent));
  NodeOperationEvent operation;
  operation.notebookId = target.m_notebookId;
  operation.relativePath = target.m_path;
  QVERIFY(hooks.doAction(HookNames::NodeBeforeDelete, operation));
  QVERIFY(hooks.doAction(HookNames::NodeBeforeMove, operation));
  operation.relativePath = QStringLiteral("replace-guard");
  operation.isFolder = true;
  QVERIFY(hooks.doAction(HookNames::NodeBeforeDelete, operation));
  QVERIFY(hooks.doAction(HookNames::NodeBeforeMove, operation));
  operation.relativePath.clear();
  QVERIFY(hooks.doAction(HookNames::NodeBeforeDelete, operation));
  operation.relativePath = QStringLiteral("replace");
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeDelete, operation));
  operation.relativePath = QStringLiteral("replace-guard-other");
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeMove, operation));
  operation.relativePath = QStringLiteral("replace-guard");
  operation.notebookId = QStringLiteral("another-notebook");
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeDelete, operation));
  NodeRenameEvent rename;
  rename.notebookId = target.m_notebookId;
  rename.relativePath = target.m_path;
  rename.newName = QStringLiteral("renamed.md");
  QVERIFY(hooks.doAction(HookNames::NodeBeforeRename, rename));
  rename.relativePath = QStringLiteral("replace-guard");
  rename.isFolder = true;
  QVERIFY(hooks.doAction(HookNames::NodeBeforeRename, rename));
  rename.relativePath = QStringLiteral("replace");
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeRename, rename));

  // A competing request may receive a token, but cannot steal this file's reservation.
  const int competingToken = buffers.replaceSearchMatches(target, QStringLiteral("other"));
  QVERIFY(competingToken > token);
  QVERIFY(completed.wait(10000));
  QCOMPARE(observer.completions.constFirst().token, competingToken);
  QVERIFY(!observer.completions.constFirst().saved);
  QVERIFY(!observer.completions.constFirst().error.isEmpty());
  QVERIFY(buffers.isContentReplacementActive(reservedId));
  buffers.cancelSearchReplacement(token);
  barrier.release();
  QVERIFY(completed.count() == 2 || completed.wait(10000));
  QCOMPARE(completed.count(), 2);
  QCOMPARE(observer.completions.constLast().token, token);
  QVERIFY(!observer.completions.constLast().saved);
  QVERIFY(!buffers.isContentReplacementActive(reservedId));
  QCOMPARE(readReplacementFile(replacementFilePath(target)), QByteArrayLiteral("foo foo\nkeep\n"));
  QVERIFY(buffers.listBuffers().isEmpty());

  notebookEvent.notebookId = target.m_notebookId;
  operation.notebookId = target.m_notebookId;
  operation.relativePath = QStringLiteral("replace-guard");
  rename.relativePath = target.m_path;
  rename.isFolder = false;
  QVERIFY(!hooks.doAction(HookNames::NotebookBeforeClose, notebookEvent));
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeDelete, operation));
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeMove, operation));
  QVERIFY(!hooks.doAction(HookNames::NodeBeforeRename, rename));
}

void TestBuffer::testSearchReplacementCancelPreparation_data() {
  QTest::addColumn<bool>("existingWriter");
  QTest::newRow("private-unopened") << false;
  QTest::newRow("existing-dirty-writer") << true;
}

void TestBuffer::testSearchReplacementCancelPreparation() {
  QFETCH(bool, existingWriter);
  const auto target = createReplacementTarget(
      QStringLiteral("replace-cancel-preparation-%1.md").arg(int(existingWriter)));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  QString editor = QStringLiteral("foo foo\nunsaved cancellation draft\n");
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  Buffer2 note;
  if (existingWriter) {
    note = buffers.openBuffer({target.m_notebookId, target.m_path});
    QVERIFY(note.isValid());
    buffers.registerActiveWriter(note.id(), 1, [&]() { return editor; });
    buffers.markDirty(note.id());
  }
  ReplacementObserver observer(buffers);
  QSignalSpy frozen(&observer, &ReplacementObserver::stateObserved);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  ReplacementPreparationBarrier barrier;
  const auto cleanup = qScopeGuard([&]() {
    barrier.release();
    buffers.shutdown();
    if (note.isValid()) {
      buffers.unregisterActiveWriter(note.id(), 1);
      buffers.closeBuffer(note.id());
    }
  });
  const int token = buffers.replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QCOMPARE(completed.count(), 0);
  QVERIFY(frozen.wait(10000));
  const auto reservedId = observer.states.constFirst().bufferId;
  QVERIFY(buffers.isContentReplacementActive(reservedId));
  QVERIFY(!buffers.isSaveQueueBusy(reservedId));
  QCOMPARE(completed.count(), 0);
  buffers.cancelSearchReplacement(token);
  barrier.release();
  QVERIFY(completed.count() == 1 || completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY(!result.saved);
  QCOMPARE(result.matches, 0);
  QVERIFY(!buffers.isContentReplacementActive(reservedId));
  QVERIFY(!observer.states.constLast().active);
  QVERIFY(!observer.states.constLast().changed);
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("foo foo\nkeep\n"));
  if (existingWriter) {
    QVERIFY(buffers.getBufferHandle(note.id()).isValid());
    QVERIFY(buffers.isDirty(note.id()));
    QString restored;
    QVERIFY(buffers.captureActiveWriterContent(note.id(), &restored));
    QCOMPARE(restored, editor);
  } else {
    QVERIFY(buffers.listBuffers().isEmpty());
  }

  // The canceled request cannot later overwrite a successful successor or emit twice.
  const int nextToken = buffers.replaceSearchMatches(target, QStringLiteral("baz"));
  QVERIFY(nextToken > token);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 2);
  QCOMPARE(observer.completions.constLast().token, nextToken);
  QVERIFY2(observer.completions.constLast().saved,
           qPrintable(observer.completions.constLast().error));
  QCOMPARE(readReplacementFile(path),
           existingWriter ? QByteArrayLiteral("baz baz\nunsaved cancellation draft\n")
                          : QByteArrayLiteral("baz baz\nkeep\n"));
}

void TestBuffer::testSearchReplacementDestroyPreparation() {
  const auto target = createReplacementTarget(QStringLiteral("replace-destroy-preparation.md"));
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  HookManager hooks;
  auto buffers = std::unique_ptr<ReplacementDestructionService>(
      new ReplacementDestructionService(m_context, &hooks, AutoSavePolicy::None));
  ReplacementObserver observer(*buffers);
  QSignalSpy frozen(&observer, &ReplacementObserver::stateObserved);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  ReplacementPreparationBarrier barrier;
  bool enteredWithReservation = false;
  auto *pendingService = buffers.get();
  buffers->beforeDestruction = [&, pendingService]() {
    enteredWithReservation =
        !observer.states.isEmpty() &&
        pendingService->isContentReplacementActive(observer.states.constFirst().bufferId);
    barrier.release();
  };
  const auto cleanup = qScopeGuard([&]() {
    barrier.release();
    buffers.reset();
  });
  const int token = buffers->replaceSearchMatches(target, QStringLiteral("bar"));
  QVERIFY(token > 0);
  QVERIFY(frozen.wait(10000));
  QCOMPARE(completed.count(), 0);
  buffers.reset();
  QVERIFY(enteredWithReservation);
  QCOMPARE(completed.count(), 1);
  QCOMPARE(observer.completions.constFirst().token, token);
  QVERIFY(!observer.completions.constFirst().saved);
  QVERIFY(!observer.states.constLast().active);
  QVERIFY(!observer.states.constLast().changed);
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("foo foo\nkeep\n"));
  QVERIFY(m_bufferService->listBuffers().isEmpty());

  HookManager nextHooks;
  BufferService next(m_context, &nextHooks, AutoSavePolicy::None);
  ReplacementObserver nextObserver(next);
  QSignalSpy nextCompleted(&nextObserver, &ReplacementObserver::finished);
  const auto nextCleanup = qScopeGuard([&]() { next.shutdown(); });
  QVERIFY(next.replaceSearchMatches(target, QStringLiteral("baz")) > 0);
  QVERIFY(nextCompleted.wait(10000));
  QVERIFY2(nextObserver.completions.constFirst().saved,
           qPrintable(nextObserver.completions.constFirst().error));
  QCOMPARE(readReplacementFile(path), QByteArrayLiteral("baz baz\nkeep\n"));
  QCOMPARE(completed.count(), 1);
  QVERIFY(next.listBuffers().isEmpty());
}

void TestBuffer::testSearchReplacementEncoding_data() {
  QTest::addColumn<QByteArray>("source");
  QTest::addColumn<QString>("encoding");
  QTest::addColumn<QString>("replacement");
  QTest::addColumn<QByteArray>("expected");
  QTest::addColumn<bool>("privateOpen");
  QTest::addColumn<bool>("success");

  const auto bom = QByteArray::fromHex("efbbbf");
  QTest::newRow("private-utf8-bom")
      << (bom + QByteArrayLiteral("foo foo\r\nkeep\r\n")) << QString() << QStringLiteral("bar")
      << (bom + QByteArrayLiteral("bar bar\r\nkeep\r\n")) << true << true;

  auto *gb18030 = QTextCodec::codecForName("GB18030");
  QVERIFY(gb18030);
  const auto sourceText = QStringLiteral("foo foo\r\n\u4F60\u597D\r\n");
  const auto replacement = QStringLiteral("\u4E16\u754C");
  const auto expectedText =
      replacement + QLatin1Char(' ') + replacement + QStringLiteral("\r\n\u4F60\u597D\r\n");
  const auto gbSource = gb18030->fromUnicode(sourceText);
  const auto gbExpected = gb18030->fromUnicode(expectedText);
  QTest::newRow("private-gb18030")
      << gbSource << QString() << replacement << gbExpected << true << true;
  QTest::newRow("existing-gb18030")
      << gbSource << QStringLiteral("GB18030") << replacement << gbExpected << false << true;

  const auto latinSource =
      QByteArrayLiteral("foo foo\r\ncaf") + char(0xE9) + QByteArrayLiteral("\r\n");
  QTest::newRow("unrepresentable-latin1")
      << latinSource << QStringLiteral("ISO-8859-1") << QStringLiteral("\u20AC") << latinSource
      << false << false;
  const auto binarySource =
      QByteArrayLiteral("foo foo\n") + QByteArray(1, '\0') + QByteArrayLiteral("binary");
  QTest::newRow("nul-body") << binarySource << QString() << QStringLiteral("bar") << binarySource
                            << true << false;
  const auto invalidSource = QByteArrayLiteral("foo foo\n") + QByteArray::fromHex("ffff");
  QTest::newRow("invalid-supported-codecs")
      << invalidSource << QString() << QStringLiteral("bar") << invalidSource << true << false;
}

void TestBuffer::testSearchReplacementEncoding() {
  QFETCH(QByteArray, source);
  QFETCH(QString, encoding);
  QFETCH(QString, replacement);
  QFETCH(QByteArray, expected);
  QFETCH(bool, privateOpen);
  QFETCH(bool, success);
  const auto target = createReplacementTarget(
      QStringLiteral("replace-codec-%1.md").arg(QString::fromLatin1(QTest::currentDataTag())),
      source);
  QVERIFY(!target.m_id.isEmpty());
  const auto path = replacementFilePath(target);
  HookManager hooks;
  BufferService buffers(m_context, &hooks, AutoSavePolicy::None);
  Buffer2 note;
  if (!privateOpen) {
    note = buffers.openBuffer({target.m_notebookId, target.m_path});
    QVERIFY(note.isValid());
    buffers.setBufferEncoding(note.id(), encoding);
  }
  ReplacementObserver observer(buffers);
  QSignalSpy completed(&observer, &ReplacementObserver::finished);
  const auto cleanup = qScopeGuard([&]() {
    buffers.shutdown();
    if (note.isValid()) {
      buffers.closeBuffer(note.id());
    }
  });
  const int token = buffers.replaceSearchMatches(target, replacement);
  QVERIFY(token > 0);
  QVERIFY(completed.wait(10000));
  QCOMPARE(completed.count(), 1);
  const auto result = observer.completions.constFirst();
  QCOMPARE(result.token, token);
  QVERIFY2(result.saved == success, qPrintable(result.error));
  QCOMPARE(result.matches, success ? 2 : 0);
  QCOMPARE(result.error.isEmpty(), success);
  QCOMPARE(readReplacementFile(path), expected);
  if (privateOpen) {
    QVERIFY(buffers.listBuffers().isEmpty());
  } else {
    QCOMPARE(note.getContentRaw(), expected);
    QVERIFY(!buffers.isDirty(note.id()));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBuffer)
#include "test_buffer.moc"
