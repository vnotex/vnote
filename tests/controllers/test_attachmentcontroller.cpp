#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <vxcore/vxcore.h>

#include <controllers/attachmentcontroller.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>

namespace tests {

// Regression gate for the controller GUI cleanup: AttachmentController no longer
// opens a QFileDialog / QMessageBox, so a GUILESS test can drive add + delete
// straight through without blocking on a modal.
class TestAttachmentController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testAddAttachmentsCopiesFilesAndEmits();
  void testAddAttachmentsWithoutBufferIsNoOp();
  void testAddAttachmentsWithInvalidBufferIsNoOp();
  void testAddAttachmentsWithEmptyListIsNoOp();
  void testDeleteAttachmentsRemovesFileAndEmits();
  void testDeleteAttachmentsWithoutBufferIsNoOp();
  void testDeleteAttachmentsWithInvalidBufferIsNoOp();
  void testDeleteAttachmentsWithEmptyListIsNoOp();

private:
  void reopenCleanBuffer();
  QString writeSourceFile(const QString &p_fileName);
  QStringList currentAttachments();

  QTemporaryDir m_tempDir;
  VxCoreContextHandle m_context = nullptr;
  vnotex::ServiceLocator m_services;
  vnotex::NotebookCoreService *m_notebookService = nullptr;
  vnotex::HookManager *m_hookMgr = nullptr;
  vnotex::BufferService *m_bufferService = nullptr;
  QString m_notebookId;
  vnotex::Buffer2 m_buffer;
};

void TestAttachmentController::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // CRITICAL: must run before vxcore_context_create().
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new vnotex::NotebookCoreService(m_context, this);
  m_hookMgr = new vnotex::HookManager(this);
  m_bufferService =
      new vnotex::BufferService(m_context, m_hookMgr, vnotex::AutoSavePolicy::AutoSave, this);

  m_services.registerService<vnotex::NotebookCoreService>(m_notebookService);
  m_services.registerService<vnotex::HookManager>(m_hookMgr);
  m_services.registerService<vnotex::BufferService>(m_bufferService);

  const QString nbPath = m_tempDir.filePath(QStringLiteral("attachment_controller_test"));
  const QString configJson =
      QStringLiteral(R"({"name":"AttCtrl","description":"Test","version":"1"})");
  m_notebookId =
      m_notebookService->createNotebook(nbPath, configJson, vnotex::NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  const QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());
}

void TestAttachmentController::cleanupTestCase() {
  if (m_buffer.isValid()) {
    m_bufferService->closeBuffer(m_buffer.id());
    m_buffer = vnotex::Buffer2();
  }

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

void TestAttachmentController::cleanup() {
  if (m_buffer.isValid()) {
    m_bufferService->closeBuffer(m_buffer.id());
    m_buffer = vnotex::Buffer2();
  }
}

void TestAttachmentController::reopenCleanBuffer() {
  if (m_buffer.isValid()) {
    m_bufferService->closeBuffer(m_buffer.id());
    m_buffer = vnotex::Buffer2();
  }

  m_buffer = m_bufferService->openBuffer(
      vnotex::NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(m_buffer.isValid());

  const QJsonArray attachments = m_buffer.listAttachments();
  for (const auto &val : attachments) {
    const QString name = val.toString();
    if (!name.isEmpty()) {
      QVERIFY(m_buffer.deleteAttachment(name));
    }
  }
}

QString TestAttachmentController::writeSourceFile(const QString &p_fileName) {
  const QString srcPath = m_tempDir.filePath(p_fileName);
  QFile src(srcPath);
  if (!src.open(QIODevice::WriteOnly)) {
    return QString();
  }

  src.write(QByteArray("payload"));
  src.close();
  return srcPath;
}

QStringList TestAttachmentController::currentAttachments() {
  QStringList names;
  const QJsonArray attachments = m_buffer.listAttachments();
  for (const auto &val : attachments) {
    names.append(val.toString());
  }
  return names;
}

void TestAttachmentController::testAddAttachmentsCopiesFilesAndEmits() {
  reopenCleanBuffer();

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);

  const QString srcPath = writeSourceFile(QStringLiteral("added.txt"));
  QVERIFY(!srcPath.isEmpty());

  controller.addAttachments({srcPath});

  QCOMPARE(addedSpy.count(), 1);

  const QStringList names = currentAttachments();
  QCOMPARE(names.size(), 1);
  QCOMPARE(names.first(), QStringLiteral("added.txt"));

  const QString folder = m_buffer.getAttachmentsFolder();
  QVERIFY(!folder.isEmpty());
  QVERIFY(QFile::exists(folder + QLatin1Char('/') + names.first()));
}

void TestAttachmentController::testAddAttachmentsWithoutBufferIsNoOp() {
  vnotex::AttachmentController controller(m_services);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);

  const QString srcPath = writeSourceFile(QStringLiteral("orphan.txt"));
  QVERIFY(!srcPath.isEmpty());

  controller.addAttachments({srcPath});

  QCOMPARE(addedSpy.count(), 0);
}

void TestAttachmentController::testAddAttachmentsWithInvalidBufferIsNoOp() {
  vnotex::Buffer2 invalidBuffer;
  QVERIFY(!invalidBuffer.isValid());

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&invalidBuffer);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);

  const QString srcPath = writeSourceFile(QStringLiteral("invalid.txt"));
  QVERIFY(!srcPath.isEmpty());

  controller.addAttachments({srcPath});

  QCOMPARE(addedSpy.count(), 0);
}

void TestAttachmentController::testAddAttachmentsWithEmptyListIsNoOp() {
  reopenCleanBuffer();

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);

  controller.addAttachments(QStringList());

  QCOMPARE(addedSpy.count(), 0);
  QCOMPARE(currentAttachments().size(), 0);
}

void TestAttachmentController::testDeleteAttachmentsRemovesFileAndEmits() {
  reopenCleanBuffer();

  const QString srcPath = writeSourceFile(QStringLiteral("doomed.txt"));
  QVERIFY(!srcPath.isEmpty());
  const QString name = m_buffer.insertAttachment(srcPath);
  QVERIFY(!name.isEmpty());

  const QString folder = m_buffer.getAttachmentsFolder();
  QVERIFY(QFile::exists(folder + QLatin1Char('/') + name));

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy deletedSpy(&controller, &vnotex::AttachmentController::attachmentDeleted);

  controller.deleteAttachments({name});

  QCOMPARE(deletedSpy.count(), 1);
  QVERIFY(!currentAttachments().contains(name));
  QVERIFY(!QFile::exists(folder + QLatin1Char('/') + name));
}

void TestAttachmentController::testDeleteAttachmentsWithoutBufferIsNoOp() {
  vnotex::AttachmentController controller(m_services);
  QSignalSpy deletedSpy(&controller, &vnotex::AttachmentController::attachmentDeleted);

  controller.deleteAttachments({QStringLiteral("whatever.txt")});

  QCOMPARE(deletedSpy.count(), 0);
}

void TestAttachmentController::testDeleteAttachmentsWithInvalidBufferIsNoOp() {
  vnotex::Buffer2 invalidBuffer;
  QVERIFY(!invalidBuffer.isValid());

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&invalidBuffer);
  QSignalSpy deletedSpy(&controller, &vnotex::AttachmentController::attachmentDeleted);

  controller.deleteAttachments({QStringLiteral("whatever.txt")});

  QCOMPARE(deletedSpy.count(), 0);
}

void TestAttachmentController::testDeleteAttachmentsWithEmptyListIsNoOp() {
  reopenCleanBuffer();

  const QString srcPath = writeSourceFile(QStringLiteral("kept.txt"));
  QVERIFY(!srcPath.isEmpty());
  const QString name = m_buffer.insertAttachment(srcPath);
  QVERIFY(!name.isEmpty());

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy deletedSpy(&controller, &vnotex::AttachmentController::attachmentDeleted);

  controller.deleteAttachments(QStringList());

  QCOMPARE(deletedSpy.count(), 0);
  QVERIFY(currentAttachments().contains(name));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestAttachmentController)
#include "test_attachmentcontroller.moc"
