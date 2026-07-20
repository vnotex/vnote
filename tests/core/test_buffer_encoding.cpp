#include <QSignalSpy>
#include <QTextCodec>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
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

private:
  Buffer2 openTestBuffer();

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
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferEncoding)
#include "test_buffer_encoding.moc"
