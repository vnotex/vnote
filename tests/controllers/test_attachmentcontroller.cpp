#include <QtTest>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <thread>

#include <vxcore/vxcore.h>

#include <controllers/attachmentcontroller.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>

namespace tests {

// Intercepts desktop file opens so a regression never launches an external process.
class UrlSink : public QObject {
  Q_OBJECT

public:
  QList<QUrl> m_opened;

public slots:
  void onUrl(const QUrl &p_url) { m_opened.append(p_url); }
};

// Regression gate for the controller GUI cleanup: AttachmentController no longer
// opens a QFileDialog / QMessageBox, so a GUILESS test can drive add + delete
// straight through without blocking on a modal.
class TestAttachmentController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testOpenAttachmentsOpensReadableBuffer();
  void testAddAttachmentsCopiesFilesAndEmits();
  void testAddAttachmentsWithoutBufferIsNoOp();
  void testAddAttachmentsWithInvalidBufferIsNoOp();
  void testAddAttachmentsWithEmptyListIsNoOp();
  void testScanRegistersOnlyEligibleFiles();
  void testScanHonorsCancellationAndMissingFiles();
  void testScanDoesNotWriteWhileNotebookBusy();
  void testDeleteAttachmentsRemovesFileAndEmits();
  void testDeleteAttachmentsWithoutBufferIsNoOp();
  void testDeleteAttachmentsWithInvalidBufferIsNoOp();
  void testDeleteAttachmentsWithEmptyListIsNoOp();

private:
  void reopenCleanBuffer();
  QString writeSourceFile(const QString &p_fileName);
  QStringList currentAttachments();

  UrlSink m_urlSink;
  QTemporaryDir m_tempDir;
  VxCoreContextHandle m_context = nullptr;
  vnotex::ServiceLocator m_services;
  vnotex::NotebookCoreService *m_notebookService = nullptr;
  vnotex::HookManager *m_hookMgr = nullptr;
  vnotex::NotebookIoGate m_ioGate;
  vnotex::BufferService *m_bufferService = nullptr;
  QString m_notebookId;
  vnotex::Buffer2 m_buffer;
};

void TestAttachmentController::initTestCase() {
  QDesktopServices::setUrlHandler(QStringLiteral("file"), &m_urlSink, "onUrl");
  QVERIFY(m_tempDir.isValid());

  // CRITICAL: must run before vxcore_context_create().
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new vnotex::NotebookCoreService(m_context, this);
  m_hookMgr = new vnotex::HookManager(this);
  m_bufferService = new vnotex::BufferService(m_context, m_hookMgr, &m_ioGate,
                                              vnotex::AutoSavePolicy::AutoSave, this);

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
  QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
  cleanup();

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
  if (m_bufferService) {
    const QJsonArray buffers = m_bufferService->listBuffers();
    for (const auto &bufVal : buffers) {
      const QString id = bufVal.toObject()[QStringLiteral("id")].toString();
      if (!id.isEmpty()) {
        m_bufferService->closeBuffer(id);
      }
    }
  }
  m_buffer = vnotex::Buffer2();
  m_urlSink.m_opened.clear();
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

void TestAttachmentController::testOpenAttachmentsOpensReadableBuffer() {
  reopenCleanBuffer();

  const QString srcPath = writeSourceFile(QStringLiteral("open me-\u4e2d.txt"));
  QVERIFY(!srcPath.isEmpty());
  const QString name = m_buffer.insertAttachment(srcPath);
  QVERIFY(!name.isEmpty());
  const QString folder = m_buffer.getAttachmentsFolder();
  QVERIFY(!folder.isEmpty());
  const QString attachmentPath = QFileInfo(folder + QLatin1Char('/') + name).canonicalFilePath();
  QVERIFY(!attachmentPath.isEmpty());

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  const auto matchingBufferIds = [this, &attachmentPath]() {
    QStringList ids;
    const QJsonArray buffers = m_bufferService->listBuffers();
    for (const auto &bufVal : buffers) {
      const QString id = bufVal.toObject()[QStringLiteral("id")].toString();
      const auto buffer = m_bufferService->getBufferHandle(id);
      if (QFileInfo(buffer.resolvedPath()).canonicalFilePath() == attachmentPath) {
        ids.append(id);
      }
    }
    return ids;
  };

  controller.openAttachments({name});

  const QStringList openedIds = matchingBufferIds();
  QCOMPARE(openedIds.size(), 1);
  const auto attachmentBuffer = m_bufferService->getBufferHandle(openedIds.first());
  QVERIFY(attachmentBuffer.isValid());
  QCOMPARE(attachmentBuffer.getContentRaw(), QByteArray("payload"));
  QVERIFY(m_urlSink.m_opened.isEmpty());

  controller.openAttachments({name});

  QCOMPARE(matchingBufferIds(), openedIds);
  QVERIFY(m_urlSink.m_opened.isEmpty());
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

void TestAttachmentController::testScanRegistersOnlyEligibleFiles() {
  const QString notePath = QStringLiteral("scan_eligible.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), notePath).isEmpty());
  m_buffer = m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, notePath});
  QVERIFY(m_buffer.isValid());

  const QByteArray savedContent("# Attachment scan\n");
  QVERIFY(m_buffer.setContentRaw(savedContent));
  QVERIFY(m_buffer.save());
  const QString folder = m_notebookService->getAttachmentsFolder(m_notebookId, notePath);
  QVERIFY(!folder.isEmpty());
  const QDir attachmentDir(folder);
  QVERIFY(QDir().mkpath(attachmentDir.filePath(QStringLiteral("nested"))));

  const QMap<QString, QByteArray> files{
      {QStringLiteral("indexed.txt"), QByteArray("already indexed\r\n")},
      {QStringLiteral("loose.txt"), QByteArray("loose attachment\n")},
      {QStringLiteral("unused.png"), QByteArray::fromHex("89504e470d0a1a0a00010203")},
      {QStringLiteral("live.png"), QByteArray("currently referenced image")},
      {QStringLiteral("removed.png"), QByteArray("tracked image with its link removed")},
      {QStringLiteral("comments.json"), QByteArray(R"({"comments":[]})")},
      {QStringLiteral(".gitkeep"), QByteArray()},
      {QStringLiteral("nested/child.txt"), QByteArray("not a direct attachment")}};
  for (auto it = files.cbegin(); it != files.cend(); ++it) {
    QFile file(attachmentDir.filePath(it.key()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(it.value()), static_cast<qint64>(it.value().size()));
  }
  QCOMPARE(vxcore_file_add_attachment(m_context, m_notebookId.toUtf8().constData(),
                                      notePath.toUtf8().constData(), "indexed.txt"),
           VXCORE_OK);

  const QString livePath = attachmentDir.filePath(QStringLiteral("live.png"));
  const QString removedPath = attachmentDir.filePath(QStringLiteral("removed.png"));
  const QString removedAlias = attachmentDir.filePath(QStringLiteral("nested/../removed.png"));
  QVERIFY(QFileInfo(livePath).isAbsolute());
  QVERIFY(QFileInfo(removedAlias).isAbsolute());
  QCOMPARE(QFileInfo(removedAlias).canonicalFilePath(), QFileInfo(removedPath).canonicalFilePath());
  const QStringList exclusions{QFileInfo(livePath).canonicalFilePath(), removedAlias};

  const QDir noteDir(QFileInfo(m_buffer.resolvedPath()).path());
  const QByteArray unsavedContent =
      savedContent + "[Loose](" +
      noteDir.relativeFilePath(attachmentDir.filePath(QStringLiteral("loose.txt"))).toUtf8() +
      ")\n![Live](" + noteDir.relativeFilePath(livePath).toUtf8() + ")\n";
  QVERIFY(m_buffer.setContentRaw(unsavedContent));
  QVERIFY(m_buffer.isModified());

  const auto entryFilters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
  const QStringList originalEntries = attachmentDir.entryList(entryFilters, QDir::Name);
  const QStringList expectedNames{QStringLiteral("indexed.txt"), QStringLiteral("loose.txt"),
                                  QStringLiteral("unused.png")};
  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);

  for (int scan = 0; scan < 2; ++scan) {
    controller.scanAttachments(exclusions);

    QStringList names = currentAttachments();
    names.sort();
    QCOMPARE(names, expectedNames);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(attachmentDir.entryList(entryFilters, QDir::Name), originalEntries);
    QCOMPARE(
        QDir(attachmentDir.filePath(QStringLiteral("nested"))).entryList(entryFilters, QDir::Name),
        QStringList{QStringLiteral("child.txt")});
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
      QFile file(attachmentDir.filePath(it.key()));
      QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
      QCOMPARE(file.readAll(), it.value());
    }
    QFile note(m_buffer.resolvedPath());
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(note.readAll(), savedContent);
    QCOMPARE(m_buffer.getContentRaw(), unsavedContent);
    QVERIFY(m_buffer.isModified());
  }

  // Attachment membership survives discarding the note's unrelated unsaved edits.
  QVERIFY(m_bufferService->closeBuffer(m_buffer.id()));
  m_buffer = m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, notePath});
  QVERIFY(m_buffer.isValid());
  QStringList reopenedNames = currentAttachments();
  reopenedNames.sort();
  QCOMPARE(reopenedNames, expectedNames);
  QCOMPARE(m_buffer.getContentRaw(), savedContent);
  QVERIFY(!m_buffer.isModified());
}

void TestAttachmentController::testScanHonorsCancellationAndMissingFiles() {
  const QString notePath = QStringLiteral("scan_hook_failures.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), notePath).isEmpty());
  m_buffer = m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, notePath});
  QVERIFY(m_buffer.isValid());
  const QString folder = m_notebookService->getAttachmentsFolder(m_notebookId, notePath);
  QVERIFY(!folder.isEmpty());
  QVERIFY(QDir().mkpath(folder));
  const QDir attachmentDir(folder);
  const QMap<QString, QByteArray> files{
      {QStringLiteral("a_cancel.txt"), QByteArray("cancelled attachment")},
      {QStringLiteral("b_missing.txt"), QByteArray("removed by the before hook")},
      {QStringLiteral("c_success.txt"), QByteArray("registered after both failures")}};
  for (auto it = files.cbegin(); it != files.cend(); ++it) {
    QFile file(attachmentDir.filePath(it.key()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(it.value()), static_cast<qint64>(it.value().size()));
  }

  const QString cancelledPath = attachmentDir.filePath(QStringLiteral("a_cancel.txt"));
  const QString missingPath = attachmentDir.filePath(QStringLiteral("b_missing.txt"));
  bool removedByHook = false;
  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);
  {
    const int hookId = m_hookMgr->addAction<vnotex::AttachmentAddEvent>(
        vnotex::HookNames::AttachmentBeforeAdd,
        [&](vnotex::HookContext &p_ctx, const vnotex::AttachmentAddEvent &p_event) {
          if (p_event.bufferId != m_buffer.id()) {
            return;
          }
          const QString sourcePath = QFileInfo(p_event.sourcePath).canonicalFilePath();
          if (sourcePath == QFileInfo(cancelledPath).canonicalFilePath()) {
            p_ctx.cancel();
          } else if (sourcePath == QFileInfo(missingPath).canonicalFilePath()) {
            removedByHook = QFile::remove(p_event.sourcePath);
          }
        });
    const auto removeHook = qScopeGuard([this, hookId]() { m_hookMgr->removeAction(hookId); });
    controller.scanAttachments({});
  }

  QVERIFY(removedByHook);
  QVERIFY(!QFile::exists(missingPath));
  QCOMPARE(currentAttachments(), QStringList{QStringLiteral("c_success.txt")});
  QCOMPARE(addedSpy.count(), 1);
  const QStringList remainingNames{QStringLiteral("a_cancel.txt"), QStringLiteral("c_success.txt")};
  QCOMPARE(attachmentDir.entryList(
               QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name),
           remainingNames);
  for (const QString &name : remainingNames) {
    QFile file(attachmentDir.filePath(name));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    QCOMPARE(file.readAll(), files.value(name));
  }
}

void TestAttachmentController::testScanDoesNotWriteWhileNotebookBusy() {
  const QString notePath = QStringLiteral("scan_busy.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), notePath).isEmpty());
  m_buffer = m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, notePath});
  QVERIFY(m_buffer.isValid());
  const QString folder = m_notebookService->getAttachmentsFolder(m_notebookId, notePath);
  QVERIFY(!folder.isEmpty());
  QVERIFY(QDir().mkpath(folder));
  const QDir attachmentDir(folder);
  const QString candidatePath = attachmentDir.filePath(QStringLiteral("busy.txt"));
  const QByteArray payload("leave these bytes in place");
  {
    QFile file(candidatePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
  }

  vnotex::AttachmentController controller(m_services);
  controller.setBuffer(&m_buffer);
  QSignalSpy addedSpy(&controller, &vnotex::AttachmentController::attachmentAdded);
  {
    QSemaphore acquired;
    QSemaphore release;
    std::thread holder([&]() {
      vnotex::NotebookIoGate::ScopedLock lock(m_ioGate, m_notebookId);
      acquired.release();
      release.acquire();
    });
    const auto releaseHolder = qScopeGuard([&]() {
      release.release();
      holder.join();
    });
    acquired.acquire();

    // Scan must return while the worker still owns the shared notebook gate.
    controller.scanAttachments({});
    QCOMPARE(currentAttachments(), QStringList());
    QCOMPARE(addedSpy.count(), 0);
    QFile file(candidatePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), payload);
  }

  controller.scanAttachments({});
  QCOMPARE(currentAttachments(), QStringList{QStringLiteral("busy.txt")});
  QCOMPARE(addedSpy.count(), 1);
  QCOMPARE(attachmentDir.entryList(
               QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name),
           QStringList{QStringLiteral("busy.txt")});
  {
    QFile file(candidatePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), payload);
  }
  QVERIFY(m_bufferService->closeBuffer(m_buffer.id()));
  m_buffer = vnotex::Buffer2();

  // Use a separately reopened read-only notebook, not a disabled UI control.
  const QString readOnlyRoot = m_tempDir.filePath(QStringLiteral("scan_read_only_notebook"));
  const QString readOnlyNotebookId = m_notebookService->createNotebook(
      readOnlyRoot, QStringLiteral(R"({"name":"Read-only scan","version":"1"})"),
      vnotex::NotebookType::Bundled);
  QVERIFY(!readOnlyNotebookId.isEmpty());
  const auto closeReadOnlyNotebook = qScopeGuard([&]() {
    if (m_buffer.isValid()) {
      m_bufferService->closeBuffer(m_buffer.id());
      m_buffer = vnotex::Buffer2();
    }
    m_notebookService->closeNotebook(readOnlyNotebookId);
  });
  const QString readOnlyNotePath = QStringLiteral("scan.md");
  QVERIFY(
      !m_notebookService->createFile(readOnlyNotebookId, QString(), readOnlyNotePath).isEmpty());
  const QString readOnlyFolder =
      m_notebookService->getAttachmentsFolder(readOnlyNotebookId, readOnlyNotePath);
  QVERIFY(!readOnlyFolder.isEmpty());
  QVERIFY(QDir().mkpath(readOnlyFolder));
  const QString blockedPath = QDir(readOnlyFolder).filePath(QStringLiteral("blocked.txt"));
  {
    QFile file(blockedPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
  }
  QVERIFY(m_notebookService->closeNotebook(readOnlyNotebookId));
  QCOMPARE(m_notebookService->openNotebookEx(readOnlyRoot, QStringLiteral(R"({"readOnly":true})")),
           readOnlyNotebookId);
  m_buffer =
      m_bufferService->openBuffer(vnotex::NodeIdentifier{readOnlyNotebookId, readOnlyNotePath});
  QVERIFY(m_buffer.isValid());
  QVERIFY(m_buffer.isReadOnly());
  QCOMPARE(m_buffer.listUnindexedAttachments(), QJsonArray{QStringLiteral("blocked.txt")});

  addedSpy.clear();
  controller.scanAttachments({});
  QCOMPARE(currentAttachments(), QStringList());
  QCOMPARE(addedSpy.count(), 0);
  QCOMPARE(QDir(readOnlyFolder)
               .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDir::Name),
           QStringList{QStringLiteral("blocked.txt")});
  QFile blockedFile(blockedPath);
  QVERIFY(blockedFile.open(QIODevice::ReadOnly));
  QCOMPARE(blockedFile.readAll(), payload);
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
