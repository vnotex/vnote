#include <QtTest>

#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include <functional>
#include <memory>
#include <thread>

#include <controllers/typstviewwindowcontroller.h>
#include <core/fileopensettings.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <temp_dir_fixture.h>
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {
QString mainName() { return QStringLiteral("\u4e3b \u6587\u6863.typst"); }
QString importedName() { return QStringLiteral("\u7ae0 \u8282.typst"); }
QString importedPath() { return QStringLiteral("chapters/") + importedName(); }
QByteArray importedContent() { return QByteArrayLiteral("== Imported heading\nImported text.\n"); }

bool writeFile(const QString &p_path, const QByteArray &p_content) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_content) == p_content.size();
}

QByteArray fileBytes(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray imageBytes(const char *p_format = "PNG", const QSize &p_size = QSize(12, 8)) {
  QImage image(p_size, QImage::Format_RGB32);
  image.fill(QColor(28, 164, 92));
  QByteArray bytes;
  QBuffer output(&bytes);
  if (!output.open(QIODevice::WriteOnly) || !image.save(&output, p_format)) {
    return {};
  }
  return bytes;
}

QByteArray svgBytes() {
  return QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"12\" height=\"8\">"
                           "<rect width=\"12\" height=\"8\" fill=\"#1ca45c\"/></svg>");
}

QStringList filesBelow(const QString &p_root) {
  QStringList files;
  QDirIterator it(p_root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    files.append(QDir(p_root).relativeFilePath(it.next()));
  }
  files.sort();
  return files;
}

// The UI-thread gate rejection must be tested against a different owning thread.
// RAII releases the barrier even when a QVERIFY returns from its caller early.
class HeldNotebookGate {
public:
  HeldNotebookGate(NotebookIoGate &p_gate, const QString &p_id)
      : m_worker([this, &p_gate, p_id]() {
          NotebookIoGate::ScopedLock lock(p_gate, p_id);
          m_acquired.release();
          m_release.acquire();
        }) {
    m_acquired.acquire();
  }
  ~HeldNotebookGate() {
    m_release.release();
    m_worker.join();
  }

private:
  QSemaphore m_acquired;
  QSemaphore m_release;
  std::thread m_worker;
};

bool gateAvailableFromWorker(NotebookIoGate &p_gate, const QString &p_id) {
  bool acquired = false;
  std::thread worker([&]() {
    NotebookIoGate::ScopedTryLock lock(p_gate, p_id, 0);
    acquired = lock.isLocked();
  });
  worker.join();
  return acquired;
}

// Reply only after a complete real HTTP request. The callback runs while
// NetworkAccess's nested event loop is active, without timing sleeps or mocks.
class ResourceImageServer : public QObject {
public:
  ResourceImageServer() {
    connect(&server, &QTcpServer::newConnection, this, [this]() {
      while (auto *socket = server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, request = QByteArray(), replied = false]() mutable {
                  request += socket->readAll();
                  if (replied || !request.contains("\r\n\r\n")) {
                    return;
                  }
                  replied = true;
                  ++requests;
                  if (beforeReply) {
                    beforeReply();
                  }
                  socket->write(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) +
                                QByteArrayLiteral(
                                    " Response\r\nContent-Type: image/png\r\nContent-Length: ") +
                                QByteArray::number(body.size()) +
                                QByteArrayLiteral("\r\nConnection: close\r\n\r\n"));
                  socket->write(body);
                  socket->disconnectFromHost();
                });
      }
    });
  }
  bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
  QString url() const {
    return QStringLiteral("http://127.0.0.1:%1/image.png").arg(server.serverPort());
  }
  QTcpServer server;
  QByteArray body;
  int status = 200;
  int requests = 0;
  std::function<void()> beforeReply;
};

// Intercept even the rejected schemes exercised below: a policy regression must not
// launch a browser, file association, or other system process.
QStringList interceptedSchemes() {
  return {QStringLiteral("http"),   QStringLiteral("https"), QStringLiteral("ftp"),
          QStringLiteral("mailto"), QStringLiteral("file"),  QStringLiteral("javascript"),
          QStringLiteral("data"),   QStringLiteral("smb"),   QStringLiteral("qrc"),
          QStringLiteral("vx"),     QStringLiteral("c"),     QString()};
}
} // namespace

class TypstUrlSink : public QObject {
  Q_OBJECT
public:
  QList<QUrl> opened;

public slots:
  void onUrl(const QUrl &p_url) { opened.append(p_url); }
};

class TestTypstViewWindowController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void unicodeProjectIdentityAndRetarget();
  void unavailableBuffer_data();
  void unavailableBuffer();
  void encryptedBufferIsRejected();
  void indexedSource_data();
  void indexedSource();
  void unindexedSourceUsesExternalIdentity();
  void externalProjectSource();
  void rejectedSource_data();
  void rejectedSource();
  void sourceSymlinkEscapeIsRejected();
  void sourceOpenCanBeCancelledByHook();
  void indexedLink_data();
  void indexedLink();
  void unindexedLink_data();
  void unindexedLink();
  void rejectedLink_data();
  void rejectedLink();
  void externalSchemesRequireOneShotConsent_data();
  void externalSchemesRequireOneShotConsent();
  void cancelledAndMismatchedConfirmationsAreConsumed();
  void pendingConfirmationCannotBeReplaced();
  void staleConfirmation_data();
  void staleConfirmation();
  void nestedSameUrlAfterRetargetCannotReuseConsent();
  void removedLocalFileCannotBeConfirmed();
  void destructionDuringConfirmation();
  void destructionDuringSourceOpen();
  void imageStorage_data();
  void imageStorage();
  void imageNormalization_data();
  void imageNormalization();
  void invalidImageBytes_data();
  void invalidImageBytes();
  void imageFolderContainment_data();
  void imageFolderContainment();
  void imageFolderJunctionEscape();
  void noCopyImageReferences();
  void resourceSourceRejection_data();
  void resourceSourceRejection();
  void resourceLinksAndOpenPolicy();
  void crossDriveRelativeLink();
  void emptyAndVerbatimTextFiles();
  void attachmentCollisionAndMetadata();
  void outsideProjectAttachmentLink();
  void attachmentEligibilityDoesNotCreateFolders();
  void resourceGate_data();
  void resourceGate();
  void attachmentBeforeHook_data();
  void attachmentBeforeHook();
  void attachmentAfterHook_data();
  void attachmentAfterHook();
  void failedRollbackReportsRetainedResource();
  void unavailableResourceBuffer_data();
  void unavailableResourceBuffer();
  void insertionInvalidationPreservesConsent();
  void networkImageAcquisition_data();
  void networkImageAcquisition();

private:
  void clearOpenObservations();
  void verifySourceOpen(const NodeIdentifier &p_node, const QString &p_path, int p_offset,
                        bool p_readOnly);

  TypstUrlSink m_urlSink;
  std::unique_ptr<TempDirFixture> m_tempDir;
  QList<QPair<QByteArray, QByteArray>> m_tempEnvironment;
  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<SyncWorkQueueManager> m_queues;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<TypstViewWindowController> m_controller;
  QString m_notebookId;
  QString m_notebookPath;
  Buffer2 m_mainBuffer;
  QList<FileOpenEvent> m_beforeOpen;
  QList<FileOpenEvent> m_afterOpen;
  QList<Buffer2> m_openedBuffers;
  QList<bool> m_readOnlyAtOpen;
};

void TestTypstViewWindowController::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
  for (const auto &scheme : interceptedSchemes()) {
    QDesktopServices::setUrlHandler(scheme, &m_urlSink, "onUrl");
  }
}

void TestTypstViewWindowController::cleanupTestCase() {
  for (const auto &scheme : interceptedSchemes()) {
    QDesktopServices::unsetUrlHandler(scheme);
  }
}

void TestTypstViewWindowController::init() {
  m_urlSink.opened.clear();
  m_tempDir.reset(new TempDirFixture());
  QVERIFY(m_tempDir->isValid());
  // vxcore test mode uses std::filesystem's process temp root, not a context option.
  // Isolate each row from other test executables and from previous session records.
  const auto tempRoot = m_tempDir->path().toUtf8();
  for (const char *name : {"TMPDIR", "TMP", "TEMP"}) {
    m_tempEnvironment.append({QByteArray(name), qgetenv(name)});
    QVERIFY(qputenv(name, tempRoot));
  }
  // Every data row gets a new core context and only a disposable notebook.
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context);
  m_hooks.reset(new HookManager());
  m_gate.reset(new NotebookIoGate());
  m_queues.reset(new SyncWorkQueueManager());
  m_notebooks.reset(new NotebookCoreService(m_context));
  m_notebooks->setHookManager(m_hooks.get());
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_buffers.reset(new BufferService(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None));
  m_buffers->setSyncWorkQueueManager(m_queues.get());
  m_services.reset(new ServiceLocator());
  m_services->registerService(m_notebooks.get());
  m_services->registerService(m_buffers.get());
  m_services->registerService(m_hooks.get());

  m_notebookPath = m_tempDir->filePath(QStringLiteral("\u7b14\u8bb0 project"));
  m_notebookId = m_notebooks->createNotebook(
      m_notebookPath, QStringLiteral(R"({"name":"Typst regression","description":"Temporary"})"),
      NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
  QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), mainName()).isEmpty());
  QVERIFY(
      !m_notebooks->createFolder(m_notebookId, QString(), QStringLiteral("chapters")).isEmpty());
  QVERIFY(
      !m_notebooks->createFile(m_notebookId, QStringLiteral("chapters"), importedName()).isEmpty());
  QVERIFY(writeFile(QDir(m_notebookPath).filePath(importedPath()), importedContent()));
  m_mainBuffer = m_buffers->openBuffer({m_notebookId, mainName()});
  QVERIFY(m_mainBuffer.isValid());
  m_controller.reset(new TypstViewWindowController(*m_services));
  QVERIFY(m_controller->setBuffer(m_mainBuffer));

  m_hooks->addAction<FileOpenEvent>(
      HookNames::FileBeforeOpen,
      [this](HookContext &, const FileOpenEvent &p_event) { m_beforeOpen.append(p_event); });
  m_hooks->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen, [this](HookContext &, const FileOpenEvent &p_event) {
        m_afterOpen.append(p_event);
        const auto buffer = m_buffers->getBufferHandle(p_event.bufferId);
        m_openedBuffers.append(buffer);
        m_readOnlyAtOpen.append(buffer.isReadOnly());
      });
  clearOpenObservations();
}

void TestTypstViewWindowController::cleanup() {
  m_controller.reset();
  m_mainBuffer = Buffer2();
  clearOpenObservations();
  m_services.reset();
  m_buffers.reset();
  m_notebooks.reset();
  m_queues.reset();
  m_gate.reset();
  m_hooks.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  for (const auto &entry : m_tempEnvironment) {
    if (entry.second.isNull()) {
      qunsetenv(entry.first.constData());
    } else {
      qputenv(entry.first.constData(), entry.second);
    }
  }
  m_tempEnvironment.clear();
  m_tempDir.reset();
}

void TestTypstViewWindowController::clearOpenObservations() {
  m_beforeOpen.clear();
  m_afterOpen.clear();
  m_openedBuffers.clear();
  m_readOnlyAtOpen.clear();
}

void TestTypstViewWindowController::verifySourceOpen(const NodeIdentifier &p_node,
                                                     const QString &p_path, int p_offset,
                                                     bool p_readOnly) {
  QCOMPARE(m_beforeOpen.size(), 1);
  QCOMPARE(m_afterOpen.size(), 1);
  QCOMPARE(m_openedBuffers.size(), 1);
  const auto &event = m_afterOpen.constFirst();
  const auto &buffer = m_openedBuffers.constFirst();
  QVERIFY(buffer.isValid());
  QVERIFY(!buffer.getBuffer().isEmpty());
  QCOMPARE(buffer.id(), event.bufferId);
  // An absolute-path request can be adopted by the core into its containing notebook.
  // Hooks retain the requested identity; the resolved source must still be the same file.
  if (!p_node.notebookId.isEmpty()) {
    QCOMPARE(buffer.nodeId(), p_node);
  }
  QCOMPARE(QFileInfo(buffer.resolvedPath()).canonicalFilePath(),
           QFileInfo(p_path).canonicalFilePath());
  QCOMPARE(buffer.getContentRaw(), importedContent());
  QCOMPARE(event.notebookId, p_node.notebookId);
  QCOMPARE(event.filePath, p_node.relativePath);
  QCOMPARE(event.cursorOffset, p_offset);
  QCOMPARE(event.mode, static_cast<int>(ViewWindowMode::Edit));
  QVERIFY(event.forceMode);
  QCOMPARE(m_readOnlyAtOpen.constFirst(), p_readOnly);
  QCOMPARE(buffer.isReadOnly(), p_readOnly);
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::unicodeProjectIdentityAndRetarget() {
  QString error = QStringLiteral("old error");
  QVERIFY(m_controller->setBuffer(m_mainBuffer, &error));
  QVERIFY(error.isEmpty());
  QCOMPARE(m_controller->getProject().root, QDir(m_notebookPath).canonicalPath());
  QCOMPARE(m_controller->getProject().mainFile, mainName());
  // A real empty source is usable, unlike a failed/missing buffer.
  VxCoreError readError = VXCORE_ERR_UNKNOWN;
  QCOMPARE(m_mainBuffer.getContentRaw(&readError), QByteArray());
  QCOMPARE(readError, VXCORE_OK);

  const auto destination = QStringLiteral("\u65b0 project");
  const auto renamed = QStringLiteral("\u6539\u540d source.typst");
  QVERIFY(!m_notebooks->createFolder(m_notebookId, QString(), destination).isEmpty());
  QVERIFY(m_notebooks->renameFile(m_notebookId, mainName(), renamed));
  QVERIFY(m_notebooks->moveFile(m_notebookId, renamed, destination));
  const auto retargeted =
      m_buffers->openBuffer({m_notebookId, destination + QLatin1Char('/') + renamed});
  QVERIFY(retargeted.isValid());
  QVERIFY(m_controller->setBuffer(retargeted, &error));
  QVERIFY(error.isEmpty());
  QCOMPARE(m_controller->getProject().root,
           QDir(QDir(m_notebookPath).filePath(destination)).canonicalPath());
  QCOMPARE(m_controller->getProject().mainFile, renamed);

  QVERIFY(
      !m_notebooks->createFile(m_notebookId, destination, QStringLiteral("next.typst")).isEmpty());
  const auto nextPath = QDir(m_notebookPath).filePath(destination + QStringLiteral("/next.typst"));
  QVERIFY(writeFile(nextPath, importedContent()));
  clearOpenObservations();
  m_controller->requestOpenSource(QStringLiteral("next.typst"), 3, 12);
  verifySourceOpen({m_notebookId, destination + QStringLiteral("/next.typst")}, nextPath, 3, false);
}

void TestTypstViewWindowController::unavailableBuffer_data() {
  QTest::addColumn<QString>("kind");
  QTest::newRow("invalid") << QStringLiteral("invalid");
  QTest::newRow("closed") << QStringLiteral("closed");
  QTest::newRow("virtual") << QStringLiteral("virtual");
  QTest::newRow("missing-parent") << QStringLiteral("missing-parent");
}

void TestTypstViewWindowController::unavailableBuffer() {
  QFETCH(QString, kind);
  Buffer2 buffer;
  if (kind == QLatin1String("closed")) {
    buffer = m_mainBuffer;
    QVERIFY(m_buffers->closeBuffer(buffer.id()));
  } else if (kind == QLatin1String("virtual")) {
    buffer = m_buffers->openVirtualBuffer(QStringLiteral("vx://typst-regression"));
    QVERIFY(buffer.isValid());
    QVERIFY(buffer.nodeId().isVirtual());
  } else if (kind == QLatin1String("missing-parent")) {
    const auto root = m_tempDir->createDir(QStringLiteral("vanishing project"));
    const auto path = QDir(root).filePath(QStringLiteral("note.typst"));
    QVERIFY(writeFile(path, QByteArrayLiteral("= Gone\n")));
    buffer = m_buffers->openBuffer({QString(), path});
    QVERIFY(buffer.isValid());
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().rmdir(root));
    QVERIFY(!QFileInfo::exists(root));
  }
  QString error;
  QVERIFY(!m_controller->setBuffer(buffer, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(m_controller->getProject().root.isEmpty());
  QVERIFY(m_controller->getProject().mainFile.isEmpty());
  clearOpenObservations();
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenSource(importedPath(), 0, 4);
  QCOMPARE(errors.size(), 1);
  m_controller->requestOpenLink(QUrl(QStringLiteral("https://example.invalid/unavailable")));
  QVERIFY(confirmations.isEmpty());
  QVERIFY(m_beforeOpen.isEmpty());
  QVERIFY(m_afterOpen.isEmpty());
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::encryptedBufferIsRejected() {
  VxCoreError result = VXCORE_ERR_UNKNOWN;
  QString fileId;
  // Use a supported encrypted Markdown identity, not a fabricated Typst encryption type.
  std::thread worker([&]() {
    auto prepared = m_notebooks->prepareNotebookEncryption(
        m_notebookId, QString(), QByteArrayLiteral("typst-controller-regression-password"));
    if (!prepared.isValid()) {
      return;
    }
    auto maintenance = m_queues->tryAcquireMaintenance({m_notebookId});
    if (!maintenance.isValid()) {
      return;
    }
    NotebookIoGate::ScopedLock lock(*m_gate, m_notebookId);
    result = m_notebooks->commitNotebookEncryption(prepared);
    if (result == VXCORE_OK) {
      result = m_notebooks->createEncryptedNote(
          m_notebookId, QString(), QStringLiteral("secret.md"), QStringLiteral("markdown"),
          QByteArrayLiteral("Protected body"), &fileId);
    }
  });
  worker.join();
  QCOMPARE(result, VXCORE_OK);
  QVERIFY(!fileId.isEmpty());
  const auto buffer = m_buffers->openBuffer({m_notebookId, QStringLiteral("secret.md.vne")});
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.isEncrypted());
  QCOMPARE(buffer.getContentRaw(), QByteArrayLiteral("Protected body"));
  QString error;
  QVERIFY(!m_controller->setBuffer(buffer, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(m_controller->getProject().root.isEmpty());
  QVERIFY(m_controller->getProject().mainFile.isEmpty());
}

void TestTypstViewWindowController::indexedSource_data() {
  QTest::addColumn<QString>("access");
  QTest::newRow("writable") << QStringLiteral("writable");
  QTest::newRow("readonly-notebook") << QStringLiteral("notebook");
  QTest::newRow("existing-readonly-buffer") << QStringLiteral("buffer");
}

void TestTypstViewWindowController::indexedSource() {
  QFETCH(QString, access);
  QString existingId;
  if (access == QLatin1String("notebook")) {
    QCOMPARE(vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true),
             VXCORE_OK);
  } else if (access == QLatin1String("buffer")) {
    FileOpenSettings settings;
    settings.m_readOnly = true;
    const auto existing = m_buffers->openBuffer({m_notebookId, importedPath()}, settings);
    QVERIFY(existing.isValid());
    QVERIFY(existing.isReadOnly());
    existingId = existing.id();
  }
  clearOpenObservations();
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenSource(importedPath(), 3, 19);
  verifySourceOpen({m_notebookId, importedPath()}, QDir(m_notebookPath).filePath(importedPath()), 3,
                   access != QLatin1String("writable"));
  if (!existingId.isEmpty()) {
    QCOMPARE(m_afterOpen.constFirst().bufferId, existingId);
  }
  QVERIFY(errors.isEmpty());
  QVERIFY(confirmations.isEmpty());
}

void TestTypstViewWindowController::unindexedSourceUsesExternalIdentity() {
  const auto relative = QStringLiteral("chapters/unindexed.typst");
  const auto path = QDir(m_notebookPath).filePath(relative);
  QVERIFY(writeFile(path, importedContent()));
  QVERIFY(!m_notebooks->resolvePathToNotebook(path).isEmpty());
  QVERIFY(m_notebooks->getFileInfo(m_notebookId, relative).isEmpty());
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  m_controller->requestOpenSource(relative, 0, 0);
  verifySourceOpen({QString(), QFileInfo(path).canonicalFilePath()}, path, 0, false);
  QVERIFY(errors.isEmpty());
}

void TestTypstViewWindowController::externalProjectSource() {
  const auto root = m_tempDir->createDir(QStringLiteral("external \u9879\u76ee"));
  const auto mainPath = QDir(root).filePath(mainName());
  const auto chapter = QDir(root).filePath(importedName());
  QVERIFY(writeFile(mainPath, QByteArrayLiteral("= Main\n")));
  QVERIFY(writeFile(chapter, importedContent()));
  const auto buffer = m_buffers->openBuffer({QString(), mainPath});
  QVERIFY(buffer.isValid());
  QVERIFY(m_controller->setBuffer(buffer));
  QCOMPARE(m_controller->getProject().root, QDir(root).canonicalPath());
  QCOMPARE(m_controller->getProject().mainFile, mainName());
  clearOpenObservations();
  m_controller->requestOpenSource(importedName(), 3, 19);
  verifySourceOpen({QString(), QFileInfo(chapter).canonicalFilePath()}, chapter, 3, false);
}

void TestTypstViewWindowController::rejectedSource_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<int>("start");
  QTest::addColumn<int>("end");
  QTest::newRow("empty") << QString() << 0 << 1;
  QTest::newRow("dot") << QStringLiteral(".") << 0 << 1;
  QTest::newRow("dotdot") << QStringLiteral("..") << 0 << 1;
  QTest::newRow("traversal") << QStringLiteral("../outside/secret.typst") << 0 << 1;
  QTest::newRow("absolute-existing") << QStringLiteral("<absolute>") << 0 << 1;
  QTest::newRow("drive-absolute") << QStringLiteral("C:/chapter.typst") << 0 << 1;
  QTest::newRow("drive-relative") << QStringLiteral("C:chapter.typst") << 0 << 1;
  QTest::newRow("backslash") << QString(importedPath()).replace(QLatin1Char('/'), QLatin1Char('\\'))
                             << 0 << 1;
  QTest::newRow("network-path") << QStringLiteral("//server/share/chapter.typst") << 0 << 1;
  QTest::newRow("empty-segment") << QStringLiteral("chapters//") + importedName() << 0 << 1;
  QTest::newRow("leading-dot") << QStringLiteral("./") + importedPath() << 0 << 1;
  QTest::newRow("embedded-dot") << QStringLiteral("chapters/./") + importedName() << 0 << 1;
  QTest::newRow("embedded-dotdot") << QStringLiteral("chapters/../") + importedPath() << 0 << 1;
  QTest::newRow("trailing-slash") << importedPath() + QLatin1Char('/') << 0 << 1;
  QTest::newRow("package") << QStringLiteral("@preview/example:1.0.0/main.typ") << 0 << 1;
  QTest::newRow("nul") << importedPath() + QChar(QChar::Null) + QStringLiteral(".typst") << 0 << 1;
  QTest::newRow("directory") << QStringLiteral("chapters") << 0 << 1;
  QTest::newRow("missing-file") << QStringLiteral("missing.typst") << 0 << 1;
  QTest::newRow("negative-start") << importedPath() << -1 << 1;
  QTest::newRow("negative-end") << importedPath() << 0 << -1;
  QTest::newRow("reversed-range") << importedPath() << 19 << 3;
}

void TestTypstViewWindowController::rejectedSource() {
  QFETCH(QString, path);
  QFETCH(int, start);
  QFETCH(int, end);
  const auto outside = m_tempDir->createDir(QStringLiteral("outside"));
  QVERIFY(writeFile(QDir(outside).filePath(QStringLiteral("secret.typst")), importedContent()));
  if (path == QLatin1String("<absolute>")) {
    path = QDir(m_notebookPath).filePath(importedPath());
  }
  const auto bufferCount = m_buffers->listBuffers().size();
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenSource(path, start, end);
  QCOMPARE(errors.size(), 1);
  QCOMPARE(errors.constFirst().constFirst().toString(),
           QStringLiteral("Unable to open the Typst source location"));
  QCOMPARE(m_buffers->listBuffers().size(), bufferCount);
  QVERIFY(m_beforeOpen.isEmpty());
  QVERIFY(m_afterOpen.isEmpty());
  QVERIFY(confirmations.isEmpty());
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::sourceSymlinkEscapeIsRejected() {
  const auto outside = m_tempDir->createDir(QStringLiteral("outside"));
  const auto target = QDir(outside).filePath(QStringLiteral("secret.typst"));
  QVERIFY(writeFile(target, importedContent()));
  const auto link = QDir(m_notebookPath).filePath(QStringLiteral("escape"));
  const auto removeLink = qScopeGuard([&]() {
#ifdef Q_OS_WIN
    QDir().rmdir(link);
#else
    QFile::remove(link);
#endif
  });
#ifdef Q_OS_WIN
  // Junctions run without symlink privileges. Failure is a failed required case, not a skip.
  QProcess process;
  process.start(QStringLiteral("cmd.exe"),
                {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                 QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside)});
  QVERIFY2(process.waitForFinished(10000), qPrintable(process.errorString()));
  QCOMPARE(process.exitStatus(), QProcess::NormalExit);
  QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
  QVERIFY(QFileInfo(link).isJunction());
#else
  QVERIFY2(QFile::link(outside, link), "Cannot create the required directory symlink fixture");
  QVERIFY(QFileInfo(link).isSymbolicLink());
#endif
  const auto traversed = QDir(link).filePath(QStringLiteral("secret.typst"));
  QVERIFY(QFileInfo(traversed).isFile());
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  const auto bufferCount = m_buffers->listBuffers().size();
  m_controller->requestOpenSource(QStringLiteral("escape/secret.typst"), 0, 4);
  QCOMPARE(errors.size(), 1);
  QCOMPARE(m_buffers->listBuffers().size(), bufferCount);
  QVERIFY(m_beforeOpen.isEmpty());
  QVERIFY(m_afterOpen.isEmpty());
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::sourceOpenCanBeCancelledByHook() {
  const int hook = m_hooks->addAction<FileOpenEvent>(
      HookNames::FileBeforeOpen,
      [](HookContext &p_context, const FileOpenEvent &) { p_context.cancel(); }, 20);
  const auto removeHook = qScopeGuard([&]() { m_hooks->removeAction(hook); });
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  const auto bufferCount = m_buffers->listBuffers().size();
  m_controller->requestOpenSource(importedPath(), 3, 19);
  QCOMPARE(m_beforeOpen.size(), 1);
  QVERIFY(m_afterOpen.isEmpty());
  QCOMPARE(errors.size(), 1);
  QCOMPARE(m_buffers->listBuffers().size(), bufferCount);
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::indexedLink_data() {
  QTest::addColumn<bool>("absolute");
  QTest::newRow("relative-indexed") << false;
  QTest::newRow("file-url-indexed") << true;
}

void TestTypstViewWindowController::indexedLink() {
  QFETCH(bool, absolute);
  const auto path = QDir(m_notebookPath).filePath(importedPath());
  auto url = absolute ? QUrl::fromLocalFile(path) : QUrl(importedPath());
  const auto anchor = QStringLiteral("\u7ae0\u8282 heading");
  url.setFragment(anchor);
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(url);
  QCOMPARE(m_beforeOpen.size(), 1);
  QCOMPARE(m_afterOpen.size(), 1);
  const auto &buffer = m_openedBuffers.constFirst();
  QVERIFY(buffer.isValid());
  QCOMPARE(buffer.nodeId(), (NodeIdentifier{m_notebookId, importedPath()}));
  QCOMPARE(buffer.getContentRaw(), importedContent());
  QCOMPARE(m_afterOpen.constFirst().bufferId, buffer.id());
  QCOMPARE(m_afterOpen.constFirst().anchor, anchor);
  QVERIFY(confirmations.isEmpty());
  QVERIFY(errors.isEmpty());
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::unindexedLink_data() {
  QTest::addColumn<bool>("insideNotebook");
  QTest::addColumn<bool>("absolute");
  QTest::newRow("relative-unindexed-in-notebook") << true << false;
  QTest::newRow("file-url-unindexed-in-notebook") << true << true;
  QTest::newRow("relative-outside-project") << false << false;
  QTest::newRow("file-url-outside-project") << false << true;
}

void TestTypstViewWindowController::unindexedLink() {
  QFETCH(bool, insideNotebook);
  QFETCH(bool, absolute);
  const auto path = insideNotebook
                        ? QDir(m_notebookPath).filePath(QStringLiteral("unindexed file.txt"))
                        : m_tempDir->filePath(QStringLiteral("outside file.txt"));
  QVERIFY(writeFile(path, QByteArrayLiteral("Local link target\n")));
  if (insideNotebook) {
    QVERIFY(!m_notebooks->resolvePathToNotebook(path).isEmpty());
    QVERIFY(m_notebooks->getFileInfo(m_notebookId, QStringLiteral("unindexed file.txt")).isEmpty());
  } else {
    QVERIFY(m_notebooks->resolvePathToNotebook(path).isEmpty());
  }
  const auto url =
      absolute ? QUrl::fromLocalFile(path) : QUrl(QDir(m_notebookPath).relativeFilePath(path));
  const auto expected = QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath());
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  const auto bufferCount = m_buffers->listBuffers().size();
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  QCOMPARE(confirmations.constFirst().constFirst().toUrl(), expected);
  QVERIFY(m_urlSink.opened.isEmpty());
  QVERIFY(m_beforeOpen.isEmpty());
  QVERIFY(m_afterOpen.isEmpty());
  QCOMPARE(m_buffers->listBuffers().size(), bufferCount);
  m_controller->openConfirmedExternalLink(expected);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{expected});
  m_controller->openConfirmedExternalLink(expected);
  QCOMPARE(m_urlSink.opened.size(), 1);
  QVERIFY(errors.isEmpty());
}

void TestTypstViewWindowController::rejectedLink_data() {
  QTest::addColumn<QUrl>("url");
  QTest::newRow("javascript") << QUrl(QStringLiteral("javascript:alert(1)"));
  QTest::newRow("data") << QUrl(QStringLiteral("data:text/plain,Typst"));
  QTest::newRow("smb") << QUrl(QStringLiteral("smb://server/share/file"));
  QTest::newRow("qrc") << QUrl(QStringLiteral("qrc:/private/file"));
  QTest::newRow("virtual") << QUrl(QStringLiteral("vx://home"));
  QTest::newRow("network-authority") << QUrl(QStringLiteral("//server/share/file.typst"));
  QTest::newRow("file-authority") << QUrl(QStringLiteral("file://server/share/file.typst"));
  QTest::newRow("drive-relative") << QUrl(QStringLiteral("file:C:chapter.typst"));
  QTest::newRow("backslash") << QUrl(QStringLiteral("chapters%5Cchapter.typst"));
  QTest::newRow("nul-path") << QUrl(QStringLiteral("chapters/%00chapter.typst"));
  QTest::newRow("nul-query") << QUrl(QStringLiteral("https://example.invalid/?q=%00"));
  QTest::newRow("nul-fragment") << QUrl(QStringLiteral("https://example.invalid/#%00"));
  QTest::newRow("invalid-url") << QUrl(QStringLiteral("https://[invalid"), QUrl::StrictMode);
  QTest::newRow("missing-file") << QUrl(QStringLiteral("missing.typst"));
  QTest::newRow("directory") << QUrl(QStringLiteral("chapters"));
}

void TestTypstViewWindowController::rejectedLink() {
  QFETCH(QUrl, url);
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  const auto bufferCount = m_buffers->listBuffers().size();
  m_controller->requestOpenLink(url);
  QCOMPARE(errors.size(), 1);
  QVERIFY(confirmations.isEmpty());
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_beforeOpen.isEmpty());
  QVERIFY(m_afterOpen.isEmpty());
  QCOMPARE(m_buffers->listBuffers().size(), bufferCount);
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::externalSchemesRequireOneShotConsent_data() {
  QTest::addColumn<QUrl>("url");
  QTest::newRow("http") << QUrl(QStringLiteral("http://example.invalid/typst"));
  QTest::newRow("https") << QUrl(QStringLiteral("https://example.invalid/typst"));
  QTest::newRow("ftp") << QUrl(QStringLiteral("ftp://example.invalid/typst"));
  QTest::newRow("mailto") << QUrl(QStringLiteral("mailto:typst@example.invalid"));
}

void TestTypstViewWindowController::externalSchemesRequireOneShotConsent() {
  QFETCH(QUrl, url);
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_urlSink.opened.isEmpty());
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  QCOMPARE(confirmations.constFirst().constFirst().toUrl(), url);
  QVERIFY(m_urlSink.opened.isEmpty());
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{url});
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened.size(), 1);
  QVERIFY(m_beforeOpen.isEmpty());
  QVERIFY(m_afterOpen.isEmpty());
  QVERIFY(errors.isEmpty());
}

void TestTypstViewWindowController::cancelledAndMismatchedConfirmationsAreConsumed() {
  const QUrl url(QStringLiteral("https://example.invalid/consent"));
  const QUrl other(QStringLiteral("https://example.invalid/other"));
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  m_controller->openConfirmedExternalLink(QUrl());
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_urlSink.opened.isEmpty());
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 2);
  m_controller->openConfirmedExternalLink(other);
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_urlSink.opened.isEmpty());
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 3);
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{url});
}

void TestTypstViewWindowController::pendingConfirmationCannotBeReplaced() {
  const QUrl url(QStringLiteral("https://example.invalid/first"));
  const QUrl other(QStringLiteral("https://example.invalid/second"));
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(url);
  m_controller->requestOpenLink(other);
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{url});
  m_controller->openConfirmedExternalLink(other);
  QCOMPARE(m_urlSink.opened.size(), 1);
}

void TestTypstViewWindowController::staleConfirmation_data() {
  QTest::addColumn<QString>("change");
  QTest::newRow("reload-same-buffer") << QStringLiteral("reload");
  QTest::newRow("retarget") << QStringLiteral("retarget");
  QTest::newRow("failed-retarget") << QStringLiteral("invalid");
  QTest::newRow("closed-buffer") << QStringLiteral("close");
}

void TestTypstViewWindowController::staleConfirmation() {
  QFETCH(QString, change);
  const QUrl url(QStringLiteral("https://example.invalid/stale"));
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  if (change == QLatin1String("reload")) {
    QVERIFY(m_mainBuffer.reload());
    QVERIFY(m_controller->setBuffer(m_mainBuffer));
  } else if (change == QLatin1String("retarget")) {
    const auto buffer = m_buffers->openBuffer({m_notebookId, importedPath()});
    QVERIFY(buffer.isValid());
    QVERIFY(m_controller->setBuffer(buffer));
  } else if (change == QLatin1String("invalid")) {
    QVERIFY(!m_controller->setBuffer(Buffer2()));
  } else {
    QVERIFY(m_buffers->closeBuffer(m_mainBuffer.id()));
  }
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_urlSink.opened.isEmpty());
  // Staleness does not poison later requests once a usable project is installed.
  const auto buffer = m_buffers->openBuffer({m_notebookId, mainName()});
  QVERIFY(buffer.isValid());
  QVERIFY(m_controller->setBuffer(buffer));
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 2);
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{url});
}

void TestTypstViewWindowController::nestedSameUrlAfterRetargetCannotReuseConsent() {
  const auto retargeted = m_buffers->openBuffer({m_notebookId, importedPath()});
  QVERIFY(retargeted.isValid());
  const QUrl url(QStringLiteral("https://example.invalid/nested"));
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  int dispatches = 0;
  bool retargetSucceeded = false;
  const auto connection =
      connect(m_controller.get(), &TypstViewWindowController::externalLinkConfirmationRequested,
              this, [&](const QUrl &p_url) {
                ++dispatches;
                if (dispatches != 1) {
                  return;
                }
                retargetSucceeded = m_controller->setBuffer(retargeted);
                // This request occurs while the old modal callback is still on-stack.
                m_controller->requestOpenLink(p_url);
                m_controller->openConfirmedExternalLink(p_url);
              });
  m_controller->requestOpenLink(url);
  disconnect(connection);
  QVERIFY(retargetSucceeded);
  QCOMPARE(m_controller->getProject().root,
           QDir(QDir(m_notebookPath).filePath(QStringLiteral("chapters"))).canonicalPath());
  QCOMPARE(m_controller->getProject().mainFile, importedName());
  QCOMPARE(dispatches, 1);
  QCOMPARE(confirmations.size(), 1);
  QVERIFY(m_urlSink.opened.isEmpty());
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 2);
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{url});
}

void TestTypstViewWindowController::removedLocalFileCannotBeConfirmed() {
  const auto path = m_tempDir->filePath(QStringLiteral("removed.txt"));
  QVERIFY(writeFile(path, QByteArrayLiteral("Remove after consent request")));
  const auto url = QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath());
  QSignalSpy errors(m_controller.get(), &TypstViewWindowController::errorOccurred);
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  QVERIFY(QFile::remove(path));
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(errors.size(), 1);
  QVERIFY(m_urlSink.opened.isEmpty());
  QVERIFY(writeFile(path, QByteArrayLiteral("Recreated target")));
  m_controller->openConfirmedExternalLink(url);
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::destructionDuringConfirmation() {
  QPointer<TypstViewWindowController> guard(m_controller.get());
  connect(m_controller.get(), &TypstViewWindowController::externalLinkConfirmationRequested, this,
          [this](const QUrl &) { m_controller.reset(); });
  m_controller->requestOpenLink(QUrl(QStringLiteral("https://example.invalid/destroyed")));
  QVERIFY(guard.isNull());
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::destructionDuringSourceOpen() {
  QPointer<TypstViewWindowController> guard(m_controller.get());
  m_hooks->addAction<FileOpenEvent>(
      HookNames::FileBeforeOpen,
      [this](HookContext &, const FileOpenEvent &) { m_controller.reset(); });
  m_controller->requestOpenSource(importedPath(), 3, 19);
  QVERIFY(guard.isNull());
  verifySourceOpen({m_notebookId, importedPath()}, QDir(m_notebookPath).filePath(importedPath()), 3,
                   false);
}

void TestTypstViewWindowController::imageStorage_data() {
  QTest::addColumn<QString>("backend");
  QTest::newRow("bundled") << QStringLiteral("bundled");
  QTest::newRow("raw") << QStringLiteral("raw");
  QTest::newRow("external") << QStringLiteral("external");
  QTest::newRow("external-hidden-no-suffix") << QStringLiteral(".hidden");
  QTest::newRow("external-hidden-typst") << QStringLiteral(".hidden.typst");
  QTest::newRow("external-multiple-suffixes") << QStringLiteral("note.foo.typst");
}

void TestTypstViewWindowController::imageStorage() {
  QFETCH(QString, backend);
  auto buffer = m_mainBuffer;
  if (backend != QLatin1String("bundled")) {
    const auto root = m_tempDir->createDir(backend);
    const auto noteName = backend.contains(QLatin1Char('.')) ? backend : mainName();
    const auto path = QDir(root).filePath(noteName);
    QVERIFY(writeFile(path, importedContent()));
    QString notebook;
    if (backend == QLatin1String("raw")) {
      notebook = m_notebooks->createNotebook(root, QStringLiteral("{\"name\":\"Raw\"}"),
                                             NotebookType::Raw);
      QVERIFY(!notebook.isEmpty());
    }
    buffer = m_buffers->openBuffer({notebook, notebook.isEmpty() ? path : mainName()});
    QVERIFY(buffer.isValid());
    QVERIFY(!buffer.isAttachmentSupported());
    QVERIFY(m_controller->setBuffer(buffer));
  }
  const auto root = m_controller->getProject().root;
  const auto bytes = imageBytes();
  QVERIFY(!bytes.isEmpty());
  const auto sourceName = QStringLiteral("图 # 25%.png");
  const auto source = QDir(root).filePath(sourceName);
  QVERIFY(writeFile(source, bytes));
  const auto originalContent = buffer.getContentRaw();
  const auto sourceUrl = QUrl::fromLocalFile(source);
  if (backend != QLatin1String("bundled")) {
    QVERIFY(!m_controller->canAttach(sourceUrl));
    QString created = QStringLiteral("stale");
    QString error;
    QVERIFY(m_controller->insertAttachment(source, &created, &error).isEmpty());
    QVERIFY(created.isEmpty());
    QVERIFY(!error.isEmpty());
  }
  const QStringList sources = {source, sourceName, sourceUrl.toString(QUrl::FullyEncoded),
                               QStringLiteral("data:image/png;base64,") +
                                   QString::fromLatin1(bytes.toBase64())};
  QStringList createdPaths;
  for (const auto &input : sources) {
    QString error = QStringLiteral("old error");
    QCOMPARE(m_controller->readImageData(input, &error), bytes);
    QVERIFY(error.isEmpty());
    QString created = QStringLiteral("old asset");
    const auto relative =
        m_controller->insertImage(bytes, QStringLiteral("wrong.bmp"), &created, &error);
    QVERIFY2(!relative.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(!created.isEmpty());
    QVERIFY(!QDir::isAbsolutePath(relative));
    QVERIFY(!relative.startsWith(QStringLiteral("../")));
    QVERIFY(relative.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive));
    const auto saved = QDir(root).filePath(relative);
    QVERIFY(QFileInfo(saved).isFile());
    QCOMPARE(QFileInfo(saved).absolutePath(), QDir(buffer.getAssetsFolder()).absolutePath());
    QCOMPARE(fileBytes(saved), bytes);
    QCOMPARE(QImage(saved).pixelColor(2, 2), QColor(28, 164, 92));
    QVERIFY(!createdPaths.contains(relative));
    createdPaths.append(relative);
    QVERIFY(buffer.deleteAsset(created));
    QVERIFY(!QFileInfo::exists(saved));
    QCOMPARE(fileBytes(source), bytes);
  }
  QCOMPARE(buffer.getContentRaw(), originalContent);
}

void TestTypstViewWindowController::imageNormalization_data() {
  QTest::addColumn<QByteArray>("format");
  QTest::newRow("bmp") << QByteArrayLiteral("BMP");
  QTest::newRow("xpm") << QByteArrayLiteral("XPM");
  QTest::newRow("svg") << QByteArrayLiteral("SVG");
}

void TestTypstViewWindowController::imageNormalization() {
  QFETCH(QByteArray, format);
  const auto bytes = format == "SVG" ? svgBytes() : imageBytes(format.constData());
  QVERIFY(!bytes.isEmpty());
  const auto source =
      QDir(m_notebookPath)
          .filePath(QStringLiteral("source.") + QString::fromLatin1(format.toLower()));
  QVERIFY(writeFile(source, bytes));
  QString error;
  const auto acquired = m_controller->readImageData(source, &error);
  QVERIFY2(!acquired.isEmpty(), qPrintable(error));
  QVERIFY(acquired.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
  QString created;
  const auto relative =
      m_controller->insertImage(bytes, QStringLiteral("misleading.jpg"), &created, &error);
  QVERIFY2(!relative.isEmpty(), qPrintable(error));
  QVERIFY(relative.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive));
  const auto saved = QDir(m_notebookPath).filePath(relative);
  const auto stored = fileBytes(saved);
  QVERIFY(stored.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
  QCOMPARE(QImage::fromData(stored).size(), QSize(12, 8));
  QCOMPARE(QImage::fromData(stored).pixelColor(2, 2), QColor(28, 164, 92));
  QCOMPARE(fileBytes(source), bytes);
  QVERIFY(m_mainBuffer.deleteAsset(created));
}

void TestTypstViewWindowController::invalidImageBytes_data() {
  QTest::addColumn<QString>("kind");
  for (const auto *kind : {"empty", "invalid", "truncated", "oversized", "dimension", "pixels",
                           "active-svg", "broken-data-url"}) {
    QTest::newRow(kind) << QString::fromLatin1(kind);
  }
}

void TestTypstViewWindowController::invalidImageBytes() {
  QFETCH(QString, kind);
  QByteArray bytes;
  if (kind == QLatin1String("invalid")) {
    bytes = QByteArrayLiteral("not an image");
  } else if (kind == QLatin1String("truncated")) {
    bytes = imageBytes().left(32);
  } else if (kind == QLatin1String("oversized")) {
    bytes = imageBytes();
    bytes.append(QByteArray(32 * 1024 * 1024 + 1 - bytes.size(), 'x'));
  } else if (kind == QLatin1String("dimension")) {
    bytes = imageBytes("PNG", QSize(8193, 1));
    QVERIFY(!bytes.isEmpty());
  } else if (kind == QLatin1String("pixels")) {
    bytes = imageBytes("PNG", QSize(4097, 4096));
    QVERIFY(!bytes.isEmpty());
  } else if (kind == QLatin1String("active-svg")) {
    bytes = QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"12\" height=\"8\">"
                              "<script>alert(1)</script></svg>");
  }
  const auto source = QDir(m_notebookPath).filePath(QStringLiteral("rejected.png"));
  QVERIFY(writeFile(source, bytes));
  const auto before = filesBelow(m_notebookPath);
  QString error = QStringLiteral("old");
  QString created = QStringLiteral("old");
  QVERIFY(m_controller->insertImage(bytes, QStringLiteral("bad.png"), &created, &error).isEmpty());
  QVERIFY(created.isEmpty());
  QVERIFY(!error.isEmpty());
  const auto input = kind == QLatin1String("broken-data-url")
                         ? QStringLiteral("data:image/png;base64,%%%not-base64!")
                         : source;
  error.clear();
  QVERIFY(m_controller->readImageData(input, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(m_controller->imageReference(QUrl::fromLocalFile(source), &error).isEmpty());
  QVERIFY(!error.isEmpty());
  QCOMPARE(filesBelow(m_notebookPath), before);
  QCOMPARE(fileBytes(source), bytes);
}

void TestTypstViewWindowController::imageFolderContainment_data() {
  QTest::addColumn<QString>("kind");
  QTest::newRow("absolute-outside") << QStringLiteral("absolute");
  QTest::newRow("parent-outside") << QStringLiteral("parent");
  QTest::newRow("uncreated-contained-ancestors") << QStringLiteral("contained");
}

void TestTypstViewWindowController::imageFolderContainment() {
  QFETCH(QString, kind);
  const auto outside = m_tempDir->createDir(QStringLiteral("outside-images"));
  const auto sentinel = QDir(outside).filePath(QStringLiteral("keep.png"));
  const auto bytes = imageBytes();
  QVERIFY(writeFile(sentinel, bytes));
  auto config = m_notebooks->getNotebookConfig(m_notebookId);
  const auto folder = kind == QLatin1String("contained")
                          ? QDir(m_notebookPath).filePath(QStringLiteral("new/nested/images"))
                          : QDir(outside).filePath(QStringLiteral("new-branch"));
  config[QLatin1String(vxcore::kJsonKeyAssetsFolder)] =
      kind == QLatin1String("absolute")
          ? folder
          : (kind == QLatin1String("parent") ? QStringLiteral("../outside-images/new-branch")
                                             : QStringLiteral("new/nested/images"));
  QVERIFY(m_notebooks->updateNotebookConfig(
      m_notebookId, QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))));
  QVERIFY(!QFileInfo::exists(folder));
  const auto before = filesBelow(outside);
  QString created = QStringLiteral("stale");
  QString error;
  const auto relative =
      m_controller->insertImage(bytes, QStringLiteral("new.png"), &created, &error);
  if (kind == QLatin1String("contained")) {
    QVERIFY2(!relative.isEmpty(), qPrintable(error));
    QVERIFY(relative.startsWith(QStringLiteral("new/nested/images/")));
    QCOMPARE(fileBytes(QDir(m_notebookPath).filePath(relative)), bytes);
    QVERIFY(m_mainBuffer.deleteAsset(created));
  } else {
    QVERIFY(relative.isEmpty());
    QVERIFY(created.isEmpty());
    QCOMPARE(error, QStringLiteral("The image folder is outside the Typst project; choose an image "
                                   "folder inside the note directory"));
    QVERIFY(!QFileInfo::exists(folder));
  }
  QCOMPARE(filesBelow(outside), before);
  QCOMPARE(fileBytes(sentinel), bytes);
  QCOMPARE(m_controller->getProject().root, QDir(m_notebookPath).canonicalPath());
}

void TestTypstViewWindowController::imageFolderJunctionEscape() {
  const auto outside = m_tempDir->createDir(QStringLiteral("outside-images"));
  const auto link = QDir(m_notebookPath).filePath(QStringLiteral("image-escape"));
  const auto removeLink = qScopeGuard([&]() {
#ifdef Q_OS_WIN
    QDir().rmdir(link);
#else
    QFile::remove(link);
#endif
  });
#ifdef Q_OS_WIN
  QProcess process;
  process.start(QStringLiteral("cmd.exe"),
                {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                 QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside)});
  QVERIFY(process.waitForFinished(10000));
  if (process.exitCode() != 0) {
    QSKIP("Directory junction creation is not permitted on this test filesystem");
  }
  QVERIFY(QFileInfo(link).isJunction());
#else
  if (!QFile::link(outside, link)) {
    QSKIP("Directory symlink creation is not permitted on this test filesystem");
  }
#endif
  const auto bytes = imageBytes();
  const auto sentinel = QDir(outside).filePath(QStringLiteral("keep.png"));
  QVERIFY(writeFile(sentinel, bytes));
  auto config = m_notebooks->getNotebookConfig(m_notebookId);
  config[QLatin1String(vxcore::kJsonKeyAssetsFolder)] = QStringLiteral("image-escape/uncreated");
  QVERIFY(m_notebooks->updateNotebookConfig(
      m_notebookId, QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))));
  const auto before = filesBelow(outside);
  QString error;
  QString created = QStringLiteral("stale");
  QVERIFY(m_controller->insertImage(bytes, QStringLiteral("new.png"), &created, &error).isEmpty());
  QVERIFY(created.isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(m_controller
              ->imageReference(QUrl::fromLocalFile(QDir(link).filePath(QStringLiteral("keep.png"))),
                               &error)
              .isEmpty());
  QCOMPARE(filesBelow(outside), before);
  QCOMPARE(fileBytes(sentinel), bytes);
  QVERIFY(!QFileInfo::exists(QDir(outside).filePath(QStringLiteral("uncreated"))));
  QCOMPARE(m_controller->getProject().root, QDir(m_notebookPath).canonicalPath());
}

void TestTypstViewWindowController::noCopyImageReferences() {
  const auto bytes = imageBytes();
  const auto name = QStringLiteral("图 # 25%.png");
  const auto path = QDir(m_notebookPath).filePath(name);
  QVERIFY(writeFile(path, bytes));
  const auto svgPath = QDir(m_notebookPath).filePath(QStringLiteral("native.svg"));
  QVERIFY(writeFile(svgPath, svgBytes()));
  const auto bmpPath = QDir(m_notebookPath).filePath(QStringLiteral("unsupported.bmp"));
  QVERIFY(writeFile(bmpPath, imageBytes("BMP")));
  const auto xpmPath = QDir(m_notebookPath).filePath(QStringLiteral("unsupported.xpm"));
  QVERIFY(writeFile(xpmPath, imageBytes("XPM")));
  const auto outside = m_tempDir->filePath(QStringLiteral("outside.png"));
  QVERIFY(writeFile(outside, bytes));
  const auto before = filesBelow(m_notebookPath);
  QString error = QStringLiteral("old");
  QCOMPARE(m_controller->imageReference(QUrl::fromLocalFile(path), &error), name);
  QVERIFY(error.isEmpty());
  QUrl relative;
  relative.setPath(name);
  QCOMPARE(m_controller->imageReference(relative, &error), name);
  QCOMPARE(m_controller->imageReference(QUrl::fromLocalFile(svgPath), &error),
           QStringLiteral("native.svg"));
  const QList<QUrl> rejected = {
      QUrl::fromLocalFile(bmpPath), QUrl::fromLocalFile(xpmPath), QUrl::fromLocalFile(outside),
      QUrl(QStringLiteral("https://example.invalid/image.png")),
      QUrl(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64()))};
  for (const auto &url : rejected) {
    QVERIFY(m_controller->imageReference(url, &error).isEmpty());
    QVERIFY(!error.isEmpty());
  }
  QCOMPARE(filesBelow(m_notebookPath), before);
  QCOMPARE(fileBytes(path), bytes);
  QCOMPARE(fileBytes(svgPath), svgBytes());
  QCOMPARE(fileBytes(outside), bytes);
  const auto assets = m_notebooks->getAttachmentsFolder(m_notebookId, mainName());
  QVERIFY(!assets.isEmpty());
  QVERIFY(!QFileInfo::exists(assets));
  // Rejection as a no-copy reference must not prohibit an explicit import.
  QCOMPARE(m_controller->readImageData(outside, &error), bytes);
  QString created;
  const auto imported =
      m_controller->insertImage(bytes, QStringLiteral("outside.png"), &created, &error);
  QVERIFY2(!imported.isEmpty(), qPrintable(error));
  QCOMPARE(fileBytes(QDir(m_notebookPath).filePath(imported)), bytes);
  QVERIFY(m_mainBuffer.deleteAsset(created));
  QCOMPARE(fileBytes(outside), bytes);
}

void TestTypstViewWindowController::resourceSourceRejection_data() {
  QTest::addColumn<QString>("source");
  QTest::newRow("empty") << QString();
  QTest::newRow("drive-relative") << QStringLiteral("C:image.png");
  QTest::newRow("file-drive-relative") << QStringLiteral("file:C:image.png");
  QTest::newRow("nul") << (QStringLiteral("image") + QChar(0) + QStringLiteral(".png"));
  QTest::newRow("encoded-nul") << QStringLiteral("file:///image%00.png");
  QTest::newRow("unsupported-scheme") << QStringLiteral("smb://server/image.png");
  QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)");
  QTest::newRow("qrc") << QStringLiteral("qrc:/image.png");
  QTest::newRow("missing") << QStringLiteral("missing.png");
  QTest::newRow("directory") << QStringLiteral("chapters");
  QTest::newRow("invalid-uri") << QStringLiteral("http://[invalid");
}

void TestTypstViewWindowController::resourceSourceRejection() {
  QFETCH(QString, source);
  const auto before = filesBelow(m_notebookPath);
  QString error;
  QVERIFY(m_controller->readImageData(source, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  error.clear();
  QVERIFY(m_controller->linkDestination(source, false, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  error.clear();
  QVERIFY(m_controller->linkDestination(source, true, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  QCOMPARE(filesBelow(m_notebookPath), before);
  QVERIFY(m_urlSink.opened.isEmpty());
}

void TestTypstViewWindowController::resourceLinksAndOpenPolicy() {
  const auto name = QStringLiteral("file # 25%.txt");
  const auto path = QDir(m_notebookPath).filePath(name);
  const auto content = QByteArrayLiteral("existing link target\n");
  QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), name).isEmpty());
  QVERIFY(writeFile(path, content));
  QString error = QStringLiteral("old");
  const auto absolute = m_controller->linkDestination(path, false, &error);
  QVERIFY2(!absolute.isEmpty(), qPrintable(error));
  QVERIFY(error.isEmpty());
  QCOMPARE(QUrl(absolute).toLocalFile(), path);
  QVERIFY(QUrl(absolute).fragment().isEmpty());
  QVERIFY(absolute.contains(QStringLiteral("%23")));
  QVERIFY(absolute.contains(QStringLiteral("%25")));
  const auto relative = m_controller->linkDestination(name, false, &error);
  QVERIFY2(!relative.isEmpty(), qPrintable(error));
  QVERIFY(QUrl(relative).isRelative());
  QCOMPARE(QUrl(relative).path(), name);
  QCOMPARE(m_controller->linkDestination(path, true, &error), relative);
  QVERIFY(!relative.startsWith(QStringLiteral("./")));

  auto uri = QUrl::fromLocalFile(path);
  uri.setQuery(QStringLiteral("q=图 # 25%"), QUrl::DecodedMode);
  uri.setFragment(QStringLiteral("section # 25%"), QUrl::DecodedMode);
  const auto destination =
      m_controller->linkDestination(uri.toString(QUrl::FullyEncoded), true, &error);
  QVERIFY2(!destination.isEmpty(), qPrintable(error));
  QCOMPARE(QUrl(destination).path(), name);
  QCOMPARE(QUrl(destination).query(QUrl::FullyDecoded), uri.query(QUrl::FullyDecoded));
  QCOMPARE(QUrl(destination).fragment(QUrl::FullyDecoded), uri.fragment(QUrl::FullyDecoded));
  QVERIFY(m_urlSink.opened.isEmpty());
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  clearOpenObservations();
  m_controller->requestOpenLink(QUrl(destination));
  QVERIFY(confirmations.isEmpty());
  QCOMPARE(m_afterOpen.size(), 1);
  QCOMPARE(m_openedBuffers.constFirst().nodeId().relativePath, name);
  QCOMPARE(QFileInfo(m_openedBuffers.constFirst().resolvedPath()).canonicalFilePath(),
           QFileInfo(path).canonicalFilePath());
  QCOMPARE(m_afterOpen.constFirst().anchor, uri.fragment(QUrl::FullyDecoded));
  QVERIFY(m_urlSink.opened.isEmpty());

  const auto outside = m_tempDir->filePath(QStringLiteral("outside # 25%.txt"));
  QVERIFY(writeFile(outside, content));
  const auto outsideLink = m_controller->linkDestination(outside, true, &error);
  QVERIFY2(!outsideLink.isEmpty(), qPrintable(error));
  QVERIFY(QUrl(outsideLink).isRelative());
  QCOMPARE(QDir::cleanPath(QDir(m_notebookPath).filePath(QUrl(outsideLink).path())),
           QDir::cleanPath(outside));
  m_controller->requestOpenLink(QUrl(outsideLink));
  QCOMPARE(confirmations.size(), 1);
  const auto outsideRequest = confirmations.constLast().constFirst().toUrl();
  QCOMPARE(QFileInfo(outsideRequest.toLocalFile()).canonicalFilePath(),
           QFileInfo(outside).canonicalFilePath());
  m_controller->openConfirmedExternalLink(outsideRequest);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{outsideRequest});

  for (const auto *source : {"https://example.invalid/a%20b?q=1#part", "http://example.invalid/",
                             "ftp://example.invalid/file", "mailto:typst@example.invalid"}) {
    const auto input = QString::fromLatin1(source);
    QCOMPARE(m_controller->linkDestination(input, false, &error), input);
    QVERIFY(error.isEmpty());
  }
  QVERIFY(
      m_controller->linkDestination(QStringLiteral("https://example.invalid/?q=%00"), false, &error)
          .isEmpty());
  QVERIFY(
      m_controller->linkDestination(QStringLiteral("https://example.invalid/#%00"), false, &error)
          .isEmpty());
  QCOMPARE(fileBytes(path), content);
  QCOMPARE(fileBytes(outside), content);
}

void TestTypstViewWindowController::emptyAndVerbatimTextFiles() {
  const auto empty = QDir(m_notebookPath).filePath(QStringLiteral("empty.txt"));
  const auto path = QDir(m_notebookPath).filePath(QStringLiteral("source.txt"));
  const auto content = QStringLiteral("#let name = \"图\"\n[] #strong(\"literal\")\n");
  QVERIFY(writeFile(empty, {}));
  QVERIFY(writeFile(path, content.toUtf8()));
  QString text = QStringLiteral("old text");
  QString error = QStringLiteral("old error");
  QVERIFY(m_controller->readFileContent(QUrl::fromLocalFile(empty), &text, &error));
  QVERIFY(text.isEmpty());
  QVERIFY(error.isEmpty());
  QVERIFY(m_controller->readFileContent(QUrl(QStringLiteral("source.txt")), &text, &error));
  QCOMPARE(text, content);
  QVERIFY(error.isEmpty());
  const auto image = QDir(m_notebookPath).filePath(QStringLiteral("not-text.png"));
  QVERIFY(writeFile(image, imageBytes()));
  const QList<QUrl> rejected = {QUrl::fromLocalFile(image), QUrl(QStringLiteral("missing.txt")),
                                QUrl(QStringLiteral("chapters")),
                                QUrl(QStringLiteral("https://example.invalid/file.txt"))};
  for (const auto &url : rejected) {
    text = QStringLiteral("stale");
    error.clear();
    QVERIFY(!m_controller->readFileContent(url, &text, &error));
    QVERIFY(text.isEmpty());
    QVERIFY(!error.isEmpty());
  }
  QCOMPARE(fileBytes(path), content.toUtf8());
  QVERIFY(QFileInfo(empty).isFile());
  QCOMPARE(QFileInfo(empty).size(), 0);
}

void TestTypstViewWindowController::attachmentCollisionAndMetadata() {
  const auto firstDir = m_tempDir->createDir(QStringLiteral("first"));
  const auto secondDir = m_tempDir->createDir(QStringLiteral("second"));
  const auto basename = QStringLiteral("same # 25%.txt");
  const auto first = QDir(firstDir).filePath(basename);
  const auto second = QDir(secondDir).filePath(basename);
  const auto firstBytes = QByteArrayLiteral("first preexisting attachment\n");
  const auto secondBytes = QByteArrayLiteral("second imported attachment\n");
  QVERIFY(writeFile(first, firstBytes));
  QVERIFY(writeFile(second, secondBytes));
  const auto original = m_mainBuffer.insertAttachment(first);
  QVERIFY(!original.isEmpty());
  const auto folder = m_mainBuffer.getAttachmentsFolder();
  const auto originalPath = QDir(folder).filePath(original);
  QVERIFY(m_controller->canAttach(QUrl::fromLocalFile(second)));
  QVERIFY(!m_controller->canAttach(QUrl::fromLocalFile(originalPath)));
  QVERIFY(!m_controller->canAttach(QUrl::fromLocalFile(secondDir)));
  QVERIFY(!m_controller->canAttach(QUrl(QStringLiteral("https://example.invalid/file.txt"))));
  const auto before = m_mainBuffer.listAttachments();
  QVERIFY(before.contains(original));
  QString created = QStringLiteral("old");
  QString error = QStringLiteral("old");
  const auto link = m_controller->insertAttachment(second, &created, &error);
  QVERIFY2(!link.isEmpty(), qPrintable(error));
  QVERIFY(error.isEmpty());
  QVERIFY(!created.isEmpty());
  QVERIFY(created != original);
  const auto actualPath = QDir(folder).filePath(created);
  QCOMPARE(QFileInfo(QDir(m_notebookPath).filePath(QUrl(link).path())).canonicalFilePath(),
           QFileInfo(actualPath).canonicalFilePath());
  QCOMPARE(fileBytes(actualPath), secondBytes);
  QCOMPARE(fileBytes(originalPath), firstBytes);
  QCOMPARE(fileBytes(second), secondBytes);
  const auto after = m_mainBuffer.listAttachments();
  QCOMPARE(after.size(), before.size() + 1);
  QVERIFY(after.contains(original));
  QVERIFY(after.contains(created));
  QVERIFY(m_mainBuffer.deleteAttachment(created));
  QVERIFY(!QFileInfo::exists(actualPath));
  QCOMPARE(m_mainBuffer.listAttachments(), before);
  QCOMPARE(fileBytes(originalPath), firstBytes);
}

void TestTypstViewWindowController::outsideProjectAttachmentLink() {
  const auto outside = m_tempDir->createDir(QStringLiteral("outside-attachments"));
  auto config = m_notebooks->getNotebookConfig(m_notebookId);
  config[QLatin1String(vxcore::kJsonKeyAssetsFolder)] = outside;
  QVERIFY(m_notebooks->updateNotebookConfig(
      m_notebookId, QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))));
  const auto source = m_tempDir->filePath(QStringLiteral("attach.txt"));
  const auto bytes = QByteArrayLiteral("attachment outside compiler sandbox\n");
  QVERIFY(writeFile(source, bytes));
  QVERIFY(m_controller->canAttach(QUrl::fromLocalFile(source)));
  QString created;
  QString error;
  const auto link = m_controller->insertAttachment(source, &created, &error);
  QVERIFY2(!link.isEmpty(), qPrintable(error));
  QVERIFY(!created.isEmpty());
  const auto actual = QDir(m_mainBuffer.getAttachmentsFolder()).filePath(created);
  QVERIFY(QDir(outside).relativeFilePath(actual).startsWith(QStringLiteral("../")) == false);
  QCOMPARE(fileBytes(actual), bytes);
  QVERIFY(m_mainBuffer.listAttachments().contains(created));
  QCOMPARE(QFileInfo(QDir(m_notebookPath).filePath(QUrl(link).path())).canonicalFilePath(),
           QFileInfo(actual).canonicalFilePath());
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(QUrl(link));
  QCOMPARE(confirmations.size(), 1);
  QCOMPARE(
      QFileInfo(confirmations.constFirst().constFirst().toUrl().toLocalFile()).canonicalFilePath(),
      QFileInfo(actual).canonicalFilePath());
  QCOMPARE(m_controller->getProject().root, QDir(m_notebookPath).canonicalPath());
  QVERIFY(m_mainBuffer.deleteAttachment(created));
  QCOMPARE(fileBytes(source), bytes);
}

void TestTypstViewWindowController::resourceGate_data() {
  QTest::addColumn<bool>("external");
  QTest::newRow("notebook-assets-and-attachments") << false;
  QTest::newRow("external-assets-empty-gate-key") << true;
}

void TestTypstViewWindowController::resourceGate() {
  QFETCH(bool, external);
  auto buffer = m_mainBuffer;
  if (external) {
    const auto root = m_tempDir->createDir(QStringLiteral("external"));
    const auto path = QDir(root).filePath(mainName());
    QVERIFY(writeFile(path, importedContent()));
    buffer = m_buffers->openBuffer({QString(), path});
    QVERIFY(buffer.isValid());
    QVERIFY(m_controller->setBuffer(buffer));
  }
  const auto root = m_controller->getProject().root;
  const auto bytes = imageBytes();
  QString asset;
  QString error;
  const auto assetPath =
      m_controller->insertImage(bytes, QStringLiteral("existing.png"), &asset, &error);
  QVERIFY2(!assetPath.isEmpty(), qPrintable(error));
  const auto source = m_tempDir->filePath(QStringLiteral("source.txt"));
  const auto text = QByteArrayLiteral("existing attachment\n");
  QVERIFY(writeFile(source, text));
  QString attachment;
  if (!external) {
    QVERIFY(!m_controller->insertAttachment(source, &attachment, &error).isEmpty());
  }
  const auto before = filesBelow(buffer.getAssetsFolder());
  const auto metadata = buffer.listAttachments();
  {
    HeldNotebookGate lock(*m_gate, buffer.nodeId().notebookId);
    QString created = QStringLiteral("stale");
    QVERIFY(m_controller->insertImage(bytes, QStringLiteral("blocked.png"), &created, &error)
                .isEmpty());
    QVERIFY(created.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(buffer.insertAssetRaw(QStringLiteral("raw-blocked.png"), bytes).isEmpty());
    QVERIFY(!buffer.deleteAsset(asset));
    if (!external) {
      created = QStringLiteral("stale");
      error.clear();
      QVERIFY(m_controller->insertAttachment(source, &created, &error).isEmpty());
      QVERIFY(created.isEmpty());
      QVERIFY(!error.isEmpty());
      QVERIFY(buffer.insertAttachment(source).isEmpty());
      QVERIFY(!buffer.deleteAttachment(attachment));
      QCOMPARE(fileBytes(QDir(buffer.getAttachmentsFolder()).filePath(attachment)), text);
    }
    QCOMPARE(filesBelow(buffer.getAssetsFolder()), before);
    QCOMPARE(buffer.listAttachments(), metadata);
    QCOMPARE(fileBytes(QDir(root).filePath(assetPath)), bytes);
  }
  QString created;
  const auto later =
      m_controller->insertImage(bytes, QStringLiteral("allowed.png"), &created, &error);
  QVERIFY2(!later.isEmpty(), qPrintable(error));
  QCOMPARE(fileBytes(QDir(root).filePath(later)), bytes);
  QVERIFY(buffer.deleteAsset(created));
  QVERIFY(buffer.deleteAsset(asset));
  if (!external) {
    QVERIFY(!m_controller->insertAttachment(source, &created, &error).isEmpty());
    QCOMPARE(fileBytes(QDir(buffer.getAttachmentsFolder()).filePath(created)), text);
    QVERIFY(buffer.deleteAttachment(created));
    QVERIFY(buffer.deleteAttachment(attachment));
    QVERIFY(buffer.listAttachments().isEmpty());
  }
}

void TestTypstViewWindowController::attachmentBeforeHook_data() {
  QTest::addColumn<QString>("change");
  for (const auto *change : {"cancel", "close", "retarget", "revoke"}) {
    QTest::newRow(change) << QString::fromLatin1(change);
  }
}

void TestTypstViewWindowController::attachmentBeforeHook() {
  QFETCH(QString, change);
  const auto source = m_tempDir->filePath(QStringLiteral("blocked.txt"));
  const auto bytes = QByteArrayLiteral("must never be copied\n");
  QVERIFY(writeFile(source, bytes));
  const auto oldFolder = m_notebooks->getAttachmentsFolder(m_notebookId, mainName());
  QVERIFY(!oldFolder.isEmpty());
  const auto oldBuffer = m_mainBuffer;
  const auto oldMetadata = oldBuffer.listAttachments();
  const auto oldFiles = filesBelow(oldFolder);
  bool fired = false;
  bool gateFree = false;
  bool changed = false;
  const auto hook = m_hooks->addAction<AttachmentAddEvent>(
      HookNames::AttachmentBeforeAdd, [&](HookContext &context, const AttachmentAddEvent &) {
        fired = true;
        gateFree = gateAvailableFromWorker(*m_gate, m_notebookId);
        if (change == QLatin1String("cancel")) {
          context.cancel();
          changed = true;
        } else if (change == QLatin1String("retarget")) {
          changed =
              m_notebooks->renameFile(m_notebookId, mainName(), QStringLiteral("retargeted.typst"));
        } else if (change == QLatin1String("revoke")) {
          const auto readOnly = m_buffers->getBufferHandle(oldBuffer.id(), true);
          changed = readOnly.isValid() && readOnly.isReadOnly();
        } else {
          changed = m_buffers->closeBuffer(oldBuffer.id());
        }
      });
  const auto removeHook = qScopeGuard([&]() { m_hooks->removeAction(hook); });
  QString created = QStringLiteral("stale");
  QString error;
  QVERIFY(m_controller->insertAttachment(source, &created, &error).isEmpty());
  QVERIFY(fired);
  QVERIFY(gateFree);
  QVERIFY(changed);
  QVERIFY(created.isEmpty());
  QVERIFY(!error.isEmpty());
  QCOMPARE(filesBelow(oldFolder), oldFiles);
  if (oldBuffer.isValid()) {
    QCOMPARE(oldBuffer.listAttachments(), oldMetadata);
  }
  QCOMPARE(fileBytes(source), bytes);
}

void TestTypstViewWindowController::attachmentAfterHook_data() {
  QTest::addColumn<QString>("change");
  QTest::newRow("invalidate") << QStringLiteral("invalidate");
  QTest::newRow("replace-buffer") << QStringLiteral("replace");
  QTest::newRow("destroy-controller") << QStringLiteral("destroy");
}

void TestTypstViewWindowController::attachmentAfterHook() {
  QFETCH(QString, change);
  const auto first = m_tempDir->filePath(QStringLiteral("keep.txt"));
  const auto second = m_tempDir->filePath(QStringLiteral("rollback.txt"));
  QVERIFY(writeFile(first, QByteArrayLiteral("keep original")));
  QVERIFY(writeFile(second, QByteArrayLiteral("rollback only this copy")));
  const auto original = m_mainBuffer.insertAttachment(first);
  QVERIFY(!original.isEmpty());
  const auto folder = m_mainBuffer.getAttachmentsFolder();
  const auto before = filesBelow(folder);
  const auto metadata = m_mainBuffer.listAttachments();
  const auto replacement = m_buffers->openBuffer({m_notebookId, importedPath()});
  QVERIFY(replacement.isValid());
  bool fired = false;
  bool gateFree = false;
  bool changed = true;
  QString added;
  bool deleteBeforeOutsideGate = false;
  bool deleteAfterOutsideGate = false;
  const auto beforeDelete = m_hooks->addAction<AttachmentDeleteEvent>(
      HookNames::AttachmentBeforeDelete, [&](HookContext &, const AttachmentDeleteEvent &event) {
        deleteBeforeOutsideGate =
            event.filename == added && gateAvailableFromWorker(*m_gate, m_notebookId);
      });
  const auto afterDelete = m_hooks->addAction<AttachmentDeleteEvent>(
      HookNames::AttachmentAfterDelete, [&](HookContext &, const AttachmentDeleteEvent &event) {
        deleteAfterOutsideGate =
            event.filename == added && gateAvailableFromWorker(*m_gate, m_notebookId);
      });
  const auto removeDeletionHooks = qScopeGuard([&]() {
    m_hooks->removeAction(beforeDelete);
    m_hooks->removeAction(afterDelete);
  });
  const auto hook = m_hooks->addAction<AttachmentAddEvent>(
      HookNames::AttachmentAfterAdd, [&](HookContext &, const AttachmentAddEvent &event) {
        fired = true;
        added = event.filename;
        gateFree = gateAvailableFromWorker(*m_gate, m_notebookId);
        if (change == QLatin1String("invalidate")) {
          m_controller->invalidateInsertionRequests();
        } else if (change == QLatin1String("replace")) {
          changed = m_controller->setBuffer(replacement);
        } else {
          m_controller.reset();
        }
      });
  const auto removeHook = qScopeGuard([&]() { m_hooks->removeAction(hook); });
  QPointer<TypstViewWindowController> guard(m_controller.get());
  QString created = QStringLiteral("stale");
  QString error;
  QVERIFY(m_controller->insertAttachment(second, &created, &error).isEmpty());
  QVERIFY(fired);
  QVERIFY(gateFree);
  QVERIFY(changed);
  QVERIFY(deleteBeforeOutsideGate);
  QVERIFY(deleteAfterOutsideGate);
  QVERIFY(!added.isEmpty());
  QVERIFY(created.isEmpty());
  QVERIFY(!error.isEmpty());
  QCOMPARE(guard.isNull(), change == QLatin1String("destroy"));
  QVERIFY(!QFileInfo::exists(QDir(folder).filePath(added)));
  QCOMPARE(filesBelow(folder), before);
  QCOMPARE(m_mainBuffer.listAttachments(), metadata);
  QCOMPARE(fileBytes(QDir(folder).filePath(original)), QByteArrayLiteral("keep original"));
  QCOMPARE(fileBytes(second), QByteArrayLiteral("rollback only this copy"));
  QVERIFY(replacement.listAttachments().isEmpty());
}

void TestTypstViewWindowController::unavailableResourceBuffer_data() {
  QTest::addColumn<QString>("kind");
  QTest::newRow("read-only") << QStringLiteral("read-only");
  QTest::newRow("closed") << QStringLiteral("closed");
  QTest::newRow("invalid") << QStringLiteral("invalid");
}

void TestTypstViewWindowController::unavailableResourceBuffer() {
  QFETCH(QString, kind);
  const auto bytes = imageBytes();
  const auto source = QDir(m_notebookPath).filePath(QStringLiteral("source.png"));
  const auto text = QDir(m_notebookPath).filePath(QStringLiteral("text.txt"));
  QVERIFY(writeFile(source, bytes));
  QVERIFY(writeFile(text, QByteArrayLiteral("source text")));
  if (kind == QLatin1String("invalid")) {
    QVERIFY(!m_controller->setBuffer(Buffer2()));
  } else {
    QVERIFY(m_buffers->closeBuffer(m_mainBuffer.id()));
    if (kind == QLatin1String("read-only")) {
      FileOpenSettings settings;
      settings.m_readOnly = true;
      const auto buffer = m_buffers->openBuffer({m_notebookId, mainName()}, settings);
      QVERIFY(buffer.isValid());
      QVERIFY(buffer.isReadOnly());
      QVERIFY(m_controller->setBuffer(buffer));
    }
  }
  const auto before = filesBelow(m_notebookPath);
  QString error;
  QString created = QStringLiteral("stale");
  QVERIFY(m_controller->readImageData(source, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  error.clear();
  QVERIFY(
      m_controller->insertImage(bytes, QStringLiteral("blocked.png"), &created, &error).isEmpty());
  QVERIFY(created.isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(!m_controller->canAttach(QUrl::fromLocalFile(text)));
  created = QStringLiteral("stale");
  error.clear();
  QVERIFY(m_controller->insertAttachment(text, &created, &error).isEmpty());
  QVERIFY(created.isEmpty());
  QVERIFY(!error.isEmpty());
  QString content = QStringLiteral("stale");
  error.clear();
  QVERIFY(!m_controller->readFileContent(QUrl::fromLocalFile(text), &content, &error));
  QVERIFY(content.isEmpty());
  QVERIFY(!error.isEmpty());
  QCOMPARE(filesBelow(m_notebookPath), before);
  QCOMPARE(fileBytes(source), bytes);
  QCOMPARE(fileBytes(text), QByteArrayLiteral("source text"));
}

void TestTypstViewWindowController::insertionInvalidationPreservesConsent() {
  const QUrl url(QStringLiteral("https://example.invalid/pending-consent"));
  QSignalSpy confirmations(m_controller.get(),
                           &TypstViewWindowController::externalLinkConfirmationRequested);
  m_controller->requestOpenLink(url);
  QCOMPARE(confirmations.size(), 1);
  m_controller->invalidateInsertionRequests();
  m_controller->invalidateInsertionRequests();
  QVERIFY(m_urlSink.opened.isEmpty());
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened, QList<QUrl>{url});
  m_controller->openConfirmedExternalLink(url);
  QCOMPARE(m_urlSink.opened.size(), 1);
  QString created;
  QString error;
  const auto bytes = imageBytes();
  const auto relative =
      m_controller->insertImage(bytes, QStringLiteral("after-cancel.png"), &created, &error);
  QVERIFY2(!relative.isEmpty(), qPrintable(error));
  QCOMPARE(fileBytes(QDir(m_notebookPath).filePath(relative)), bytes);
  QVERIFY(m_mainBuffer.deleteAsset(created));
}

void TestTypstViewWindowController::networkImageAcquisition_data() {
  QTest::addColumn<QString>("change");
  for (const auto *change : {"success", "http-error", "oversized", "invalid-image", "invalidate",
                             "same-buffer", "replace", "close", "destroy"}) {
    QTest::newRow(change) << QString::fromLatin1(change);
  }
}

void TestTypstViewWindowController::networkImageAcquisition() {
  QFETCH(QString, change);
  ResourceImageServer server;
  QVERIFY(server.listen());
  const auto bytes = imageBytes();
  server.body = bytes;
  if (change == QLatin1String("http-error")) {
    server.status = 404;
  } else if (change == QLatin1String("oversized")) {
    server.body.append(QByteArray(32 * 1024 * 1024 + 1 - server.body.size(), 'x'));
  } else if (change == QLatin1String("invalid-image")) {
    server.body = QByteArrayLiteral("not an image");
  }
  const auto replacement = m_buffers->openBuffer({m_notebookId, importedPath()});
  QVERIFY(replacement.isValid());
  bool callbackSucceeded = true;
  server.beforeReply = [&]() {
    if (change == QLatin1String("invalidate")) {
      m_controller->invalidateInsertionRequests();
    } else if (change == QLatin1String("same-buffer")) {
      callbackSucceeded = m_controller->setBuffer(m_mainBuffer);
    } else if (change == QLatin1String("replace")) {
      callbackSucceeded = m_controller->setBuffer(replacement);
    } else if (change == QLatin1String("close")) {
      callbackSucceeded = m_buffers->closeBuffer(m_mainBuffer.id());
    } else if (change == QLatin1String("destroy")) {
      m_controller.reset();
    }
  };
  const auto before = filesBelow(m_notebookPath);
  QPointer<TypstViewWindowController> guard(m_controller.get());
  QString error = QStringLiteral("old error");
  const auto acquired = m_controller->readImageData(server.url(), &error);
  QCOMPARE(server.requests, 1);
  QVERIFY(callbackSucceeded);
  QCOMPARE(guard.isNull(), change == QLatin1String("destroy"));
  // Acquisition alone is never an implicit asset write, even on success.
  QCOMPARE(filesBelow(m_notebookPath), before);
  if (change == QLatin1String("success")) {
    QVERIFY(error.isEmpty());
    QCOMPARE(acquired, bytes);
    QString created;
    const auto relative =
        m_controller->insertImage(acquired, QStringLiteral("download.png"), &created, &error);
    QVERIFY2(!relative.isEmpty(), qPrintable(error));
    QVERIFY(!relative.contains(QStringLiteral("http")));
    QCOMPARE(fileBytes(QDir(m_notebookPath).filePath(relative)), bytes);
    QCOMPARE(server.requests, 1);
    QVERIFY(m_mainBuffer.deleteAsset(created));
  } else {
    QVERIFY(acquired.isEmpty());
    QVERIFY(!error.isEmpty());
  }
}

void TestTypstViewWindowController::attachmentEligibilityDoesNotCreateFolders() {
  const auto prefix = QDir(m_notebookPath).filePath(QStringLiteral("chooser-only/nested"));
  auto config = m_notebooks->getNotebookConfig(m_notebookId);
  config[QLatin1String(vxcore::kJsonKeyAssetsFolder)] = QStringLiteral("chooser-only/nested");
  QVERIFY(m_notebooks->updateNotebookConfig(
      m_notebookId, QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))));
  const auto folder = m_notebooks->getAttachmentsFolder(m_notebookId, mainName());
  QVERIFY(!folder.isEmpty());
  const auto source = m_tempDir->filePath(QStringLiteral("eligible.txt"));
  const auto bytes = QByteArrayLiteral("eligibility must not copy or create folders");
  QVERIFY(writeFile(source, bytes));
  QVERIFY(!QFileInfo::exists(prefix));
  QVERIFY(m_controller->canAttach(QUrl::fromLocalFile(source)));
  QVERIFY(!m_controller->canAttach(QUrl(QStringLiteral("eligible.txt"))));
  QVERIFY(!m_controller->canAttach(
      QUrl::fromLocalFile(m_tempDir->filePath(QStringLiteral("missing.txt")))));
  QVERIFY(!m_controller->canAttach(QUrl::fromLocalFile(m_notebookPath)));
  QVERIFY(!QFileInfo::exists(prefix));
  QVERIFY(!QFileInfo::exists(folder));
  QCOMPARE(fileBytes(source), bytes);
  QVERIFY(m_mainBuffer.listAttachments().isEmpty());
}

void TestTypstViewWindowController::crossDriveRelativeLink() {
#ifdef Q_OS_WIN
  const auto currentDrive = QFileInfo(m_notebookPath).absoluteFilePath().left(2);
  for (const auto &drive : QDir::drives()) {
    const auto root = drive.absoluteFilePath();
    if (root.left(2).compare(currentDrive, Qt::CaseInsensitive) == 0) {
      continue;
    }
    QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("vnote-typst-link-XXXXXX")));
    if (!temporary.isValid()) {
      continue;
    }
    const auto path = QDir(temporary.path()).filePath(QStringLiteral("other # 25%.txt"));
    const auto bytes = QByteArrayLiteral("cross-drive link target");
    QVERIFY(writeFile(path, bytes));
    auto uri = QUrl::fromLocalFile(path);
    uri.setQuery(QStringLiteral("q=25%"), QUrl::DecodedMode);
    uri.setFragment(QStringLiteral("part # 1"), QUrl::DecodedMode);
    QString error;
    const auto destination =
        m_controller->linkDestination(uri.toString(QUrl::FullyEncoded), true, &error);
    QVERIFY2(!destination.isEmpty(), qPrintable(error));
    const QUrl result(destination);
    QVERIFY(result.isLocalFile());
    QVERIFY(!result.isRelative());
    QCOMPARE(QFileInfo(result.toLocalFile()).canonicalFilePath(),
             QFileInfo(path).canonicalFilePath());
    QCOMPARE(result.query(QUrl::FullyDecoded), uri.query(QUrl::FullyDecoded));
    QCOMPARE(result.fragment(QUrl::FullyDecoded), uri.fragment(QUrl::FullyDecoded));
    QCOMPARE(fileBytes(path), bytes);
    return;
  }
  QSKIP("No second writable Windows drive is available for a real cross-drive target");
#else
  QSKIP("Cross-drive file URI fallback is Windows-specific; encoded URI semantics run on all "
        "platforms");
#endif
}

void TestTypstViewWindowController::failedRollbackReportsRetainedResource() {
  const auto originalSource = m_tempDir->filePath(QStringLiteral("keep.txt"));
  const auto source = m_tempDir->filePath(QStringLiteral("uncommitted.txt"));
  QVERIFY(writeFile(originalSource, QByteArrayLiteral("untouched original")));
  QVERIFY(writeFile(source, QByteArrayLiteral("retained because deletion was vetoed")));
  const auto original = m_mainBuffer.insertAttachment(originalSource);
  QVERIFY(!original.isEmpty());
  const auto folder = m_mainBuffer.getAttachmentsFolder();
  QString added;
  const auto afterAdd = m_hooks->addAction<AttachmentAddEvent>(
      HookNames::AttachmentAfterAdd, [&](HookContext &, const AttachmentAddEvent &event) {
        added = event.filename;
        m_controller->invalidateInsertionRequests();
      });
  const auto beforeDelete = m_hooks->addAction<AttachmentDeleteEvent>(
      HookNames::AttachmentBeforeDelete,
      [&](HookContext &context, const AttachmentDeleteEvent &event) {
        if (event.filename == added) {
          context.cancel();
        }
      });
  const auto removeHooks = qScopeGuard([&]() {
    m_hooks->removeAction(afterAdd);
    m_hooks->removeAction(beforeDelete);
  });
  QString created = QStringLiteral("stale");
  QString error;
  QVERIFY(m_controller->insertAttachment(source, &created, &error).isEmpty());
  QVERIFY(created.isEmpty());
  QVERIFY(!added.isEmpty());
  QVERIFY(error.contains(QStringLiteral("Unable to clean up an uncommitted Typst resource")));
  QCOMPARE(fileBytes(QDir(folder).filePath(added)), fileBytes(source));
  QVERIFY(m_mainBuffer.listAttachments().contains(added));
  QCOMPARE(fileBytes(QDir(folder).filePath(original)), QByteArrayLiteral("untouched original"));
  QVERIFY(m_mainBuffer.listAttachments().contains(original));
}

} // namespace tests

QTEST_MAIN(tests::TestTypstViewWindowController)
#include "test_typstviewwindowcontroller.moc"
