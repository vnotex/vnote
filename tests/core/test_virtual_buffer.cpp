#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
  void testVirtualBufferSaveNoOp();
  void testVirtualBufferCloseRemovesTracking();
  void testIsVirtualBuffer();

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

void TestVirtualBuffer::testVirtualBufferSaveNoOp() {
  Buffer2 buf = m_bufferService->openVirtualBuffer(QStringLiteral("vx://save-noop"));
  QVERIFY(buf.isValid());
  QVERIFY(buf.save());
  QVERIFY(buf.isValid());
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
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVirtualBuffer)
#include "test_virtual_buffer.moc"
