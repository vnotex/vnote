#include <QtTest>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUrl>

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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTypstViewWindowController)
#include "test_typstviewwindowcontroller.moc"
