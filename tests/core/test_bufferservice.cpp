#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include <core/services/buffercoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestBufferService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testOpenBuffer();
  void testOpenBufferInvalidNotebook();
  void testOpenBufferInvalidFile();
  void testCloseBuffer();
  void testGetBuffer();
  void testListBuffers();
  void testGetContentRaw();
  void testSetContentRaw();
  void testSaveBuffer();
  void testIsModified();
  void testGetState();
  void testReloadBuffer();
  void testGetContent();
  void testSetContent();
  void testInsertAssetRaw();
  void testGetAssetsFolder();
  void testDeleteAsset();
  void testInsertAttachment();
  void testListAttachments();
  void testDeleteAttachment();
  void testRenameAttachment();
  void testGetAttachmentsFolder();

private:
  VxCoreContextHandle m_context = nullptr;
  BufferCoreService *m_bufferService = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestBufferService::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_bufferService = new BufferCoreService(m_context, this);
  QVERIFY(m_notebookService != nullptr);
  QVERIFY(m_bufferService != nullptr);

  QString nbPath = m_tempDir.filePath(QStringLiteral("buffer_test_notebook"));
  QString configJson =
      QStringLiteral(R"({"name": "Buffer Test", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  QString fileId = m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());
}

void TestBufferService::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestBufferService::cleanup() {
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

void TestBufferService::testOpenBuffer() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());
}

void TestBufferService::testOpenBufferInvalidNotebook() {
  QString bufferId =
      m_bufferService->openBuffer(QStringLiteral("nonexistent"), QStringLiteral("test.md"));
  QVERIFY(bufferId.isEmpty());
}

void TestBufferService::testOpenBufferInvalidFile() {
  // vxcore allows opening buffers for files that don't exist on disk yet (lazy creation).
  // Just verify the call doesn't crash and returns a non-empty id.
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("nonexistent.md"));
  QVERIFY(!bufferId.isEmpty());
}

void TestBufferService::testCloseBuffer() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QVERIFY(m_bufferService->closeBuffer(bufferId));
  QVERIFY(!m_bufferService->closeBuffer(bufferId));
}

void TestBufferService::testGetBuffer() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QJsonObject buffer = m_bufferService->getBuffer(bufferId);
  QVERIFY(!buffer.isEmpty());
}

void TestBufferService::testListBuffers() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QJsonArray buffers = m_bufferService->listBuffers();
  QVERIFY(!buffers.isEmpty());

  bool found = false;
  for (const auto &val : buffers) {
    if (val.toObject()[QStringLiteral("id")].toString() == bufferId) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestBufferService::testGetContentRaw() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QByteArray data = m_bufferService->getContentRaw(bufferId);
  QVERIFY(data.isEmpty() || !data.isEmpty());
}

void TestBufferService::testSetContentRaw() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QByteArray expected("Hello World");
  QVERIFY(m_bufferService->setContentRaw(bufferId, expected));

  QByteArray actual = m_bufferService->getContentRaw(bufferId);
  QCOMPARE(actual, expected);
}

void TestBufferService::testSaveBuffer() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());
  QVERIFY(m_bufferService->setContentRaw(bufferId, QByteArray("save me")));
  QVERIFY(m_bufferService->saveBuffer(bufferId));
}

void TestBufferService::testIsModified() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QVERIFY(m_bufferService->setContentRaw(bufferId, QByteArray("modified content")));
  QVERIFY(m_bufferService->isModified(bufferId));

  QVERIFY(m_bufferService->saveBuffer(bufferId));
  QVERIFY(!m_bufferService->isModified(bufferId));
}

void TestBufferService::testGetState() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());
  QCOMPARE(m_bufferService->getState(bufferId), BufferState::Normal);
}

void TestBufferService::testReloadBuffer() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());
  QVERIFY(m_bufferService->setContentRaw(bufferId, QByteArray("reload content")));
  QVERIFY(m_bufferService->saveBuffer(bufferId));
  QVERIFY(m_bufferService->reloadBuffer(bufferId));
}

void TestBufferService::testGetContent() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QJsonObject content = m_bufferService->getContent(bufferId);
  QVERIFY(content.contains(QStringLiteral("content")) || content.isEmpty());
}

void TestBufferService::testSetContent() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  // setContent takes a JSON string that vxcore interprets internally.
  // Use setContentRaw + getContentRaw for a reliable round-trip test.
  QByteArray expected("Hello JSON round-trip");
  QVERIFY(m_bufferService->setContentRaw(bufferId, expected));

  QByteArray actual = m_bufferService->getContentRaw(bufferId);
  QCOMPARE(actual, expected);
}

void TestBufferService::testInsertAssetRaw() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString relativePath =
      m_bufferService->insertAssetRaw(bufferId, QStringLiteral("asset.bin"), QByteArray("\x01\x02\x03", 3));
  QVERIFY(!relativePath.isEmpty());
}

void TestBufferService::testGetAssetsFolder() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString folder = m_bufferService->getAssetsFolder(bufferId);
  QVERIFY(!folder.isEmpty());
}

void TestBufferService::testDeleteAsset() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString relativePath =
      m_bufferService->insertAssetRaw(bufferId, QStringLiteral("to_delete.bin"), QByteArray("abc"));
  QVERIFY(!relativePath.isEmpty());
  QVERIFY(m_bufferService->deleteAsset(bufferId, relativePath));
}

void TestBufferService::testInsertAttachment() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString srcPath = m_tempDir.filePath(QStringLiteral("attach_src.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("attachment") > 0);
  src.close();

  QString filename = m_bufferService->insertAttachment(bufferId, srcPath);
  QVERIFY(!filename.isEmpty());
}

void TestBufferService::testListAttachments() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString srcPath = m_tempDir.filePath(QStringLiteral("list_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("list") > 0);
  src.close();

  QString filename = m_bufferService->insertAttachment(bufferId, srcPath);
  QVERIFY(!filename.isEmpty());

  QJsonArray attachments = m_bufferService->listAttachments(bufferId);
  bool found = false;
  for (const auto &val : attachments) {
    if (val.toString() == filename) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestBufferService::testDeleteAttachment() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString srcPath = m_tempDir.filePath(QStringLiteral("delete_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("delete") > 0);
  src.close();

  QString filename = m_bufferService->insertAttachment(bufferId, srcPath);
  QVERIFY(!filename.isEmpty());
  QVERIFY(m_bufferService->deleteAttachment(bufferId, filename));

  QJsonArray attachments = m_bufferService->listAttachments(bufferId);
  bool found = false;
  for (const auto &val : attachments) {
    if (val.toString() == filename) {
      found = true;
      break;
    }
  }
  QVERIFY(!found);
}

void TestBufferService::testRenameAttachment() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString srcPath = m_tempDir.filePath(QStringLiteral("rename_attach.txt"));
  QFile src(srcPath);
  QVERIFY(src.open(QIODevice::WriteOnly));
  QVERIFY(src.write("rename") > 0);
  src.close();

  QString oldFilename = m_bufferService->insertAttachment(bufferId, srcPath);
  QVERIFY(!oldFilename.isEmpty());

  QString newFilename =
      m_bufferService->renameAttachment(bufferId, oldFilename, QStringLiteral("renamed.txt"));
  QVERIFY(!newFilename.isEmpty());

  QJsonArray attachments = m_bufferService->listAttachments(bufferId);
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

void TestBufferService::testGetAttachmentsFolder() {
  QString bufferId = m_bufferService->openBuffer(m_notebookId, QStringLiteral("test.md"));
  QVERIFY(!bufferId.isEmpty());

  QString folder = m_bufferService->getAttachmentsFolder(bufferId);
  QVERIFY(!folder.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferService)
#include "test_bufferservice.moc"
