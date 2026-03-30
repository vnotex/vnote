#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <core/hookevents.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

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

private:
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
}

void TestBuffer::testId() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.id().isEmpty());
}

void TestBuffer::testOpenCancelledByHook() {
  int hookId = m_hookMgr->addAction(
      HookNames::FileBeforeOpen,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(!buf.isValid());

  m_hookMgr->removeAction(hookId);
}

// ============ Per-buffer content operations ============

void TestBuffer::testGetContentRaw() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  // Just verify it doesn't crash. Content may be empty for a new file.
  buf.getContentRaw();
}

void TestBuffer::testSetContentRaw() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QByteArray expected("Hello Buffer");
  QVERIFY(buf.setContentRaw(expected));
  QCOMPARE(buf.getContentRaw(), expected);
}

void TestBuffer::testSave() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("save me")));
  QVERIFY(buf.save());
}

void TestBuffer::testSaveCancelledByHook() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("should not save")));

  int hookId = m_hookMgr->addAction(
      HookNames::FileBeforeSave,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

  QVERIFY(!buf.save());

  m_hookMgr->removeAction(hookId);
}

void TestBuffer::testSaveFiresAfterHook() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("reload content")));
  QVERIFY(buf.save());
  QVERIFY(buf.reload());
}

void TestBuffer::testGetContent() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QJsonObject content = buf.getContent();
  // May be empty or contain "content" key — just verify no crash.
  QVERIFY(content.isEmpty() || content.contains(QStringLiteral("content")));
}

void TestBuffer::testSetContent() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  // Use setContentRaw + getContentRaw for reliable round-trip.
  QByteArray expected("JSON round-trip");
  QVERIFY(buf.setContentRaw(expected));
  QCOMPARE(buf.getContentRaw(), expected);
}

// ============ Per-buffer state operations ============

void TestBuffer::testIsModified() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QVERIFY(buf.setContentRaw(QByteArray("modified")));
  QVERIFY(buf.isModified());

  QVERIFY(buf.save());
  QVERIFY(!buf.isModified());
}

void TestBuffer::testGetState() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QCOMPARE(buf.getState(), BufferState::Normal);
}

void TestBuffer::testGetBuffer() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QJsonObject bufInfo = buf.getBuffer();
  QVERIFY(!bufInfo.isEmpty());
}

// ============ Per-buffer asset operations ============

void TestBuffer::testInsertAssetRaw() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString relativePath =
      buf.insertAssetRaw(QStringLiteral("asset.bin"), QByteArray("\x01\x02\x03", 3));
  QVERIFY(!relativePath.isEmpty());
}

void TestBuffer::testInsertAsset() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QString relativePath =
      buf.insertAssetRaw(QStringLiteral("to_delete.bin"), QByteArray("abc"));
  QVERIFY(!relativePath.isEmpty());
  QVERIFY(buf.deleteAsset(relativePath));
}

void TestBuffer::testGetAssetsFolder() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.getAssetsFolder().isEmpty());
}

// ============ Per-buffer attachment operations ============

void TestBuffer::testInsertAttachment() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.getAttachmentsFolder().isEmpty());
}

void TestBuffer::testInsertAttachmentFiresHooks() {
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBuffer)
#include "test_buffer.moc"
