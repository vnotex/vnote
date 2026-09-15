#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTextCodec>
#include <QtTest>

#include <controllers/managenotebookscontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

// Encoding-override coverage for BufferService (SSOT) + Buffer2 delegation.
class TestBufferEncoding : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testDefaultEncodingIsUtf8();
  void testSetAndGetEncoding();
  void testEncodeDecodeGb18030RoundTrip();
  void testUnknownCodecFallsBackToUtf8();
  void testDecodeGb18030Bytes();
  void testSaveWritesGb18030Bytes();
  void testCloseClearsEncoding();
  void testUtf8RegressionRoundTrip();
  void testLineEndingSavedBytes_data();
  void testLineEndingSavedBytes();
  void testLiveWriterAutoSave();
  void testWriterPullAndBackup();
  void testRawBytesRemainExact();
  void testLineEndingControllerPersistence();
  void testProtectedWriterLineEndings();

private:
  Buffer2 openTestBuffer();
  bool applyLineEnding(const QString &p_ending);
  ServiceLocator m_services;

  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;

  // "你好世界" — outside ASCII, differs between UTF-8 and GB18030 byte-wise.
  const QString m_cjk = QString::fromUtf8("\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C");
};

void TestBufferEncoding::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_services.registerService(m_notebookService);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("buffer_encoding_test"));
  QString configJson =
      QStringLiteral(R"({"name": "Encoding Test", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());
}

void TestBufferEncoding::cleanupTestCase() {
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

void TestBufferEncoding::cleanup() {
  QTRY_VERIFY_WITH_TIMEOUT(m_bufferService->protectedOperationsIdle(), 10000);
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  QVERIFY(applyLineEnding(QString()));
}

Buffer2 TestBufferEncoding::openTestBuffer() {
  return m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
}

void TestBufferEncoding::testDefaultEncodingIsUtf8() {
  Buffer2 buf = openTestBuffer();
  QVERIFY(buf.isValid());
  QCOMPARE(buf.encoding(), QStringLiteral("UTF-8"));
  QCOMPARE(m_bufferService->bufferEncoding(buf.id()), QStringLiteral("UTF-8"));
}

void TestBufferEncoding::testSetAndGetEncoding() {
  Buffer2 buf = openTestBuffer();
  QVERIFY(buf.setEncoding(QStringLiteral("GB18030")));
  QCOMPARE(buf.encoding(), QStringLiteral("GB18030"));

  // Reset via empty name returns to default.
  QVERIFY(buf.setEncoding(QString()));
  QCOMPARE(buf.encoding(), QStringLiteral("UTF-8"));
}

void TestBufferEncoding::testEncodeDecodeGb18030RoundTrip() {
  Buffer2 buf = openTestBuffer();
  buf.setEncoding(QStringLiteral("GB18030"));

  const QByteArray encoded = m_bufferService->encodeContent(buf.id(), m_cjk);
  // Must equal a direct GB18030 encode, and NOT the UTF-8 bytes.
  QTextCodec *gb = QTextCodec::codecForName("GB18030");
  QVERIFY(gb != nullptr);
  QCOMPARE(encoded, gb->fromUnicode(m_cjk));
  QVERIFY(encoded != m_cjk.toUtf8());

  const QString decoded = m_bufferService->decodeContent(buf.id(), encoded);
  QCOMPARE(decoded, m_cjk);
}

void TestBufferEncoding::testUnknownCodecFallsBackToUtf8() {
  Buffer2 buf = openTestBuffer();
  buf.setEncoding(QStringLiteral("NoSuchCodec-XYZ"));

  // Fallback: behaves exactly like UTF-8.
  QCOMPARE(m_bufferService->encodeContent(buf.id(), m_cjk), m_cjk.toUtf8());
  QCOMPARE(m_bufferService->decodeContent(buf.id(), m_cjk.toUtf8()), m_cjk);
}

void TestBufferEncoding::testDecodeGb18030Bytes() {
  Buffer2 buf = openTestBuffer();

  QTextCodec *gb = QTextCodec::codecForName("GB18030");
  const QByteArray gbBytes = gb->fromUnicode(m_cjk);

  // Decoding GB18030 bytes as UTF-8 (default) yields mojibake, not the CJK text.
  QVERIFY(buf.decode(gbBytes) != m_cjk);

  // After picking GB18030, decode reproduces the original text.
  buf.setEncoding(QStringLiteral("GB18030"));
  QCOMPARE(buf.decode(gbBytes), m_cjk);
}

void TestBufferEncoding::testSaveWritesGb18030Bytes() {
  Buffer2 buf = openTestBuffer();
  buf.setEncoding(QStringLiteral("GB18030"));

  // Encode + persist the CJK text through the buffer's encoding.
  const QByteArray encoded = m_bufferService->encodeContent(buf.id(), m_cjk);
  QVERIFY(buf.setContentRaw(encoded));
  QVERIFY(buf.save());

  // The on-disk file must contain GB18030 bytes, not UTF-8.
  QString path = buf.resolvedPath();
  QVERIFY(!path.isEmpty());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray onDisk = f.readAll();
  f.close();

  QTextCodec *gb = QTextCodec::codecForName("GB18030");
  QCOMPARE(onDisk, gb->fromUnicode(m_cjk));
  QVERIFY(onDisk != m_cjk.toUtf8());
}

void TestBufferEncoding::testCloseClearsEncoding() {
  Buffer2 buf = openTestBuffer();
  const QString id = buf.id();
  buf.setEncoding(QStringLiteral("GB18030"));
  QCOMPARE(m_bufferService->bufferEncoding(id), QStringLiteral("GB18030"));

  QVERIFY(m_bufferService->closeBuffer(id));
  // A recycled/reopened buffer must not inherit the stale override.
  QCOMPARE(m_bufferService->bufferEncoding(id), QStringLiteral("UTF-8"));
}

void TestBufferEncoding::testUtf8RegressionRoundTrip() {
  Buffer2 buf = openTestBuffer();
  // No override set — the default path must still be plain UTF-8.
  QCOMPARE(m_bufferService->encodeContent(buf.id(), m_cjk), m_cjk.toUtf8());
  QCOMPARE(buf.decode(m_cjk.toUtf8()), m_cjk);
}

bool TestBufferEncoding::applyLineEnding(const QString &p_ending) {
  ManageNotebooksController controller(m_services);
  const auto info = controller.getNotebookInfo(m_notebookId);
  NotebookUpdateInput input;
  input.notebookId = info.id;
  input.name = info.name;
  input.description = info.description;
  input.recycleBinFolder = info.recycleBinFolder;
  input.lineEnding = p_ending;
  return controller.updateNotebook(input).success;
}

void TestBufferEncoding::testLineEndingSavedBytes_data() {
  QTest::addColumn<QString>("ending");
  QTest::addColumn<QString>("text");
  QTest::addColumn<QString>("expected");
  QTest::addColumn<QString>("codec");
  const auto mixed = QStringLiteral("A\r\nB\rC\nD");
  QTest::newRow("inherit-mixed") << QString() << mixed << mixed << QStringLiteral("UTF-8");
  QTest::newRow("lf") << QStringLiteral("lf") << mixed << QStringLiteral("A\nB\nC\nD")
                      << QStringLiteral("UTF-8");
  QTest::newRow("cr") << QStringLiteral("cr") << mixed << QStringLiteral("A\rB\rC\rD")
                      << QStringLiteral("UTF-8");
  QTest::newRow("crlf") << QStringLiteral("crlf") << mixed << QStringLiteral("A\r\nB\r\nC\r\nD")
                        << QStringLiteral("UTF-8");
  QTest::newRow("idempotent-crlf") << QStringLiteral("crlf") << QStringLiteral("A\r\nB\r\n")
                                   << QStringLiteral("A\r\nB\r\n") << QStringLiteral("UTF-8");
  QTest::newRow("empty") << QStringLiteral("crlf") << QString() << QString()
                         << QStringLiteral("UTF-8");
  QTest::newRow("single-line") << QStringLiteral("crlf") << m_cjk << m_cjk
                               << QStringLiteral("UTF-8");
  QTest::newRow("default-lf") << QString() << QStringLiteral("A\nB\n") << QStringLiteral("A\nB\n")
                              << QStringLiteral("UTF-8");
  const auto unicode = m_cjk + QStringLiteral("\r\n\0x\rY\n\u2028\u2029 ");
  const auto normalized = m_cjk + QStringLiteral("\r\n\0x\r\nY\r\n\u2028\u2029 ");
  QTest::newRow("utf16-before-encoding")
      << QStringLiteral("crlf") << unicode << normalized << QStringLiteral("UTF-16LE");
  QTest::newRow("gb18030-before-encoding")
      << QStringLiteral("crlf") << unicode << normalized << QStringLiteral("GB18030");
}

void TestBufferEncoding::testLineEndingSavedBytes() {
  QFETCH(QString, ending);
  QFETCH(QString, text);
  QFETCH(QString, expected);
  QFETCH(QString, codec);
  QVERIFY(applyLineEnding(ending));
  auto buffer = openTestBuffer();
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.setEncoding(codec));
  QVERIFY(buffer.setContentRaw(m_bufferService->encodeContent(buffer.id(), text)));
  QVERIFY(buffer.save());
  QFile file(buffer.resolvedPath());
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  auto *encoder = QTextCodec::codecForName(codec.toUtf8());
  QVERIFY(encoder);
  QCOMPARE(bytes, encoder->fromUnicode(expected));
  QCOMPARE(encoder->toUnicode(bytes), expected);
}

void TestBufferEncoding::testLiveWriterAutoSave() {
  auto buffer = openTestBuffer();
  QVERIFY(buffer.isValid());
  QString text = QStringLiteral("A\r\nB\rC\nD");
  m_bufferService->registerActiveWriter(buffer.id(), 1, [&]() { return text; });
  const auto cleanup =
      qScopeGuard([&]() { m_bufferService->unregisterActiveWriter(buffer.id(), 1); });
  QSignalSpy saved(m_bufferService->asQObject(), SIGNAL(bufferAutoSaved(QString)));
  QVERIFY(applyLineEnding(QStringLiteral("lf")));
  QVERIFY(!m_bufferService->isDirty(buffer.id()));
  m_bufferService->markDirty(buffer.id());
  const auto firstRevision = m_bufferService->currentRevision(buffer.id());
  m_bufferService->syncNow(buffer.id());
  // A queued snapshot retains LF even when the setting changes before delivery.
  QVERIFY(applyLineEnding(QStringLiteral("crlf")));
  QTRY_COMPARE_WITH_TIMEOUT(m_bufferService->lastSavedRevision(buffer.id()), firstRevision, 10000);
  QTRY_VERIFY_WITH_TIMEOUT(!m_bufferService->isSaveQueueBusy(buffer.id()), 10000);
  QVERIFY(!saved.isEmpty());
  QFile file(buffer.resolvedPath());
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("A\nB\nC\nD"));
  file.close();
  text += QStringLiteral("\nE");
  m_bufferService->markDirty(buffer.id());
  const auto secondRevision = m_bufferService->currentRevision(buffer.id());
  m_bufferService->syncNow(buffer.id());
  QTRY_COMPARE_WITH_TIMEOUT(m_bufferService->lastSavedRevision(buffer.id()), secondRevision, 10000);
  QTRY_VERIFY_WITH_TIMEOUT(!m_bufferService->isSaveQueueBusy(buffer.id()), 10000);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("A\r\nB\r\nC\r\nD\r\nE"));
  file.close();
  QVERIFY(m_bufferService->checkExternalChanges(buffer));
  QCOMPARE(buffer.getState(), BufferState::Normal);
  QVERIFY(applyLineEnding(QString()));
  m_bufferService->markDirty(buffer.id());
  const auto inheritedRevision = m_bufferService->currentRevision(buffer.id());
  m_bufferService->syncNow(buffer.id());
  QTRY_COMPARE_WITH_TIMEOUT(m_bufferService->lastSavedRevision(buffer.id()), inheritedRevision,
                            10000);
  QTRY_VERIFY_WITH_TIMEOUT(!m_bufferService->isSaveQueueBusy(buffer.id()), 10000);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), text.toUtf8());
}

void TestBufferEncoding::testWriterPullAndBackup() {
  auto buffer = openTestBuffer();
  QVERIFY(buffer.isValid());
  QString text = QStringLiteral("manual\r\nbody\rthird\nlast");
  m_bufferService->registerActiveWriter(buffer.id(), 1, [&]() { return text; });
  const auto cleanup =
      qScopeGuard([&]() { m_bufferService->unregisterActiveWriter(buffer.id(), 1); });
  QVERIFY(applyLineEnding(QStringLiteral("lf")));
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::None);
  m_bufferService->markDirty(buffer.id());
  QVERIFY(m_bufferService->pullActiveWriterContent(buffer.id()));
  QVERIFY(buffer.save());
  QFile file(buffer.resolvedPath());
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("manual\nbody\nthird\nlast"));
  file.close();
  QVERIFY(applyLineEnding(QStringLiteral("cr")));
  m_bufferService->markDirty(buffer.id());
  m_bufferService->syncNow(buffer.id());
  QCOMPARE(buffer.getContentRaw(), QByteArray("manual\rbody\rthird\rlast"));
  QString error;
  QVERIFY2(m_bufferService->saveForSnapshot(buffer.id(), 1000, &error), qPrintable(error));
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("manual\rbody\rthird\rlast"));
  file.close();
  QVERIFY(applyLineEnding(QStringLiteral("crlf")));
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::BackupFile);
  text = QStringLiteral("backup\nbody\rfinal");
  QSignalSpy saved(m_bufferService->asQObject(), SIGNAL(bufferAutoSaved(QString)));
  m_bufferService->markDirty(buffer.id());
  m_bufferService->syncNow(buffer.id());
  QTRY_COMPARE(saved.count(), 1);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("manual\rbody\rthird\rlast"));
  file.close();
  QCOMPARE(vxcore_buffer_recover_backup(m_context, buffer.id().toUtf8().constData()), VXCORE_OK);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("backup\r\nbody\r\nfinal"));
}

void TestBufferEncoding::testRawBytesRemainExact() {
  QVERIFY(applyLineEnding(QStringLiteral("crlf")));
  auto buffer = openTestBuffer();
  const QByteArray bytes("A\rB\nC\r\n\0D", 10);
  QVERIFY(buffer.setContentRaw(bytes));
  QVERIFY(buffer.save());
  QFile file(buffer.resolvedPath());
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), bytes);
}

void TestBufferEncoding::testLineEndingControllerPersistence() {
  auto config = m_notebookService->getNotebookConfig(m_notebookId);
  const QJsonObject unrelated{
      {QStringLiteral("nested"), QJsonObject{{QStringLiteral("keep"), 42}}}};
  config[QStringLiteral("metadata")] = unrelated;
  QVERIFY(m_notebookService->updateNotebookConfig(
      m_notebookId, QString::fromUtf8(QJsonDocument(config).toJson())));
  QVERIFY(applyLineEnding(QStringLiteral("crlf")));
  ManageNotebooksController controller(m_services);
  QCOMPARE(controller.getNotebookInfo(m_notebookId).lineEnding, QStringLiteral("crlf"));
  const QString path = controller.getNotebookInfo(m_notebookId).rootFolder;
  QVERIFY(m_notebookService->closeNotebook(m_notebookId));
  QCOMPARE(m_notebookService->openNotebook(path), m_notebookId);
  QCOMPARE(controller.getNotebookInfo(m_notebookId).lineEnding, QStringLiteral("crlf"));
  auto stored = m_notebookService->getNotebookConfig(m_notebookId);
  QCOMPARE(stored.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("nested")),
           unrelated.value(QStringLiteral("nested")));
  QVERIFY(!applyLineEnding(QStringLiteral("CRLF")));
  QCOMPARE(m_notebookService->getNotebookConfig(m_notebookId), stored);
  QVERIFY(applyLineEnding(QString()));
  QCOMPARE(m_notebookService->getNotebookConfig(m_notebookId)
               .value(QStringLiteral("metadata"))
               .toObject(),
           unrelated);
  // Unknown future values survive unrelated edits when the selection remains inherit.
  stored = m_notebookService->getNotebookConfig(m_notebookId);
  auto metadata = unrelated;
  metadata[QStringLiteral("lineEnding")] = QJsonObject{{QStringLiteral("future"), true}};
  stored[QStringLiteral("metadata")] = metadata;
  QVERIFY(m_notebookService->updateNotebookConfig(
      m_notebookId, QString::fromUtf8(QJsonDocument(stored).toJson())));
  QVERIFY(applyLineEnding(QString()));
  QCOMPARE(m_notebookService->getNotebookConfig(m_notebookId)
               .value(QStringLiteral("metadata"))
               .toObject(),
           metadata);
  const auto raw = m_notebookService->createNotebook(
      m_tempDir.filePath(QStringLiteral("raw-line-ending")),
      QStringLiteral("{\"name\":\"raw\",\"metadata\":{\"lineEnding\":\"cr\",\"keep\":42}}"),
      NotebookType::Raw);
  QVERIFY(!raw.isEmpty());
  NotebookUpdateInput input;
  input.notebookId = raw;
  input.name = QStringLiteral("raw renamed");
  input.lineEnding = QStringLiteral("invalid-but-ignored");
  const auto rawMetadata =
      m_notebookService->getNotebookConfig(raw).value(QStringLiteral("metadata"));
  QVERIFY(controller.updateNotebook(input).success);
  QCOMPARE(m_notebookService->getNotebookConfig(raw).value(QStringLiteral("metadata")),
           rawMetadata);
  QVERIFY(controller.getNotebookInfo(raw).lineEnding.isEmpty());
  QVERIFY(m_notebookService->closeNotebook(raw));
  input.notebookId = QStringLiteral("missing-notebook");
  QVERIFY(!controller.updateNotebook(input).success);
}

void TestBufferEncoding::testProtectedWriterLineEndings() {
  const auto notebookId = m_notebookService->createNotebook(
      m_tempDir.filePath(QStringLiteral("protected-line-endings")),
      QStringLiteral("{\"name\":\"Protected line endings\"}"), NotebookType::Bundled);
  QVERIFY(!notebookId.isEmpty());
  NotebookIoGate gate;
  SyncWorkQueueManager queues;
  BufferService buffers(m_context, m_hookMgr, &gate, AutoSavePolicy::AutoSave);
  const QByteArray password("line-ending-test-password");
  const auto notebook = notebookId.toUtf8();
  VxCoreEncryptionSetupHandle setup = nullptr;
  QCOMPARE(vxcore_encryption_prepare_notebook(m_context, notebook.constData(), nullptr,
                                              password.constData(), size_t(password.size()),
                                              &setup),
           VXCORE_OK);
  const auto releaseSetup = qScopeGuard([&]() { vxcore_encryption_free_setup(m_context, setup); });
  char *rawId = nullptr;
  {
    auto maintenance = queues.tryAcquireMaintenance({notebookId});
    QVERIFY(maintenance.isValid());
    NotebookIoGate::ScopedLock lock(gate, notebookId);
    QCOMPARE(vxcore_encryption_commit_notebook(m_context, setup), VXCORE_OK);
    QCOMPARE(vxcore_encryption_create_note(m_context, notebook.constData(), "", "secret.txt",
                                           "text", "", 0, &rawId),
             VXCORE_OK);
  }
  const auto noteId = QString::fromUtf8(rawId);
  vxcore_string_free(rawId);
  auto buffer = buffers.openBufferByNodeId(noteId);
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.isEncrypted());
  ManageNotebooksController controller(m_services);
  NotebookUpdateInput input;
  input.notebookId = notebookId;
  input.name = QStringLiteral("Protected line endings");
  input.lineEnding = QStringLiteral("crlf");
  QVERIFY(controller.updateNotebook(input).success);
  const auto text =
      QStringLiteral("private-line-ending-sentinel\r\n") + m_cjk + QStringLiteral("\rB\nC");
  const auto expected =
      QStringLiteral("private-line-ending-sentinel\r\n") + m_cjk + QStringLiteral("\r\nB\r\nC");
  buffers.registerActiveWriter(buffer.id(), 1, [text]() { return text; });
  const auto cleanup = qScopeGuard([&]() {
    buffers.unregisterActiveWriter(buffer.id(), 1);
    buffers.closeBuffer(buffer.id());
    vxcore_encryption_lock_all(m_context);
    m_notebookService->closeNotebook(notebookId);
  });
  QSignalSpy saved(buffers.asQObject(),
                   SIGNAL(protectedSaveFinished(QString, quint64, quint64, bool, int)));
  buffers.markDirty(buffer.id());
  const auto revision = buffers.currentRevision(buffer.id());
  buffers.syncNow(buffer.id());
  QTRY_COMPARE_WITH_TIMEOUT(buffers.lastSavedRevision(buffer.id()), revision, 10000);
  QTRY_VERIFY_WITH_TIMEOUT(buffers.protectedOperationsIdle(), 10000);
  QVERIFY(!saved.isEmpty());
  QCOMPARE(saved.last().at(2).toULongLong(), revision);
  QCOMPARE(saved.last().at(4).toInt(), int(VXCORE_OK));
  QFile file(buffer.resolvedPath());
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto ciphertext = file.readAll();
  QVERIFY(!ciphertext.contains("private-line-ending-sentinel"));
  file.close();
  buffers.unregisterActiveWriter(buffer.id(), 1);
  QVERIFY(buffers.closeBuffer(buffer.id()));
  QCOMPARE(vxcore_encryption_lock_all(m_context), VXCORE_OK);
  QCOMPARE(vxcore_encryption_unlock_notebook(m_context, notebook.constData(), password.constData(),
                                             size_t(password.size())),
           VXCORE_OK);
  buffer = buffers.openBufferByNodeId(noteId);
  QVERIFY(buffer.isValid());
  QCOMPARE(buffer.getContentRaw(), expected.toUtf8());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferEncoding)
#include "test_buffer_encoding.moc"
