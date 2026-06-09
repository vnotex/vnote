// T8: Buffer2::isReadOnly() + BufferCoreService::isBufferReadOnly() query plumbing.
//
// Verifies the public read-only query surface:
//   - Buffer2::isReadOnly()                : delegates to BufferCoreService
//   - BufferCoreService::isBufferReadOnly(): resolves buffer → notebook, queries vxcore
//
// Tests against a bundled notebook with real vxcore integration.

#include <QtTest>

#include <core/services/buffercoreservice.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestBufferReadOnly : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testBufferReadOnlyFalse();
  void testBufferReadOnlyTrue();
  void testBufferReadOnlyDefensiveInvalid();

private:
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferCoreService *m_bufferCoreService = nullptr;
  BufferService *m_bufferService = nullptr;
  TempDirFixture m_tempDir;

  QString m_notebookId;
};

void TestBufferReadOnly::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context);

  m_hookMgr = new HookManager(this);
  m_notebookService = new NotebookCoreService(m_context, this);
  m_bufferCoreService = new BufferCoreService(m_context, this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::None, this);

  // Create a test notebook (bundled type)
  QString nbPath = m_tempDir.filePath(QStringLiteral("test_ro_notebook"));
  QString configJson = QStringLiteral(R"({"name": "Test RO", "description": "Read-Only Test"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestBufferReadOnly::cleanupTestCase() {
  if (!m_notebookId.isEmpty()) {
    m_notebookService->closeNotebook(m_notebookId);
  }
  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_bufferCoreService;
  m_bufferCoreService = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestBufferReadOnly::cleanup() {
  // Reset read-only flag to false for next test
  if (!m_notebookId.isEmpty()) {
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  }
}

void TestBufferReadOnly::testBufferReadOnlyFalse() {
  // Subtest 1: Open notebook with read_only=false, open a buffer, assert isReadOnly() == false

  // Create a test file in root folder
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());

  // Verify notebook is NOT read-only
  bool readOnly = true;
  VxCoreError err = vxcore_notebook_is_read_only(m_context, m_notebookId.toUtf8().constData(), &readOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(readOnly, false);

  // Open buffer
  QString bufferId = m_bufferCoreService->openBuffer(m_notebookId, fileId);
  QVERIFY(!bufferId.isEmpty());

  // Test BufferCoreService::isBufferReadOnly
  bool coreReadOnly = m_bufferCoreService->isBufferReadOnly(bufferId);
  QCOMPARE(coreReadOnly, false);

  // Cleanup
  m_bufferCoreService->closeBuffer(bufferId);
}

void TestBufferReadOnly::testBufferReadOnlyTrue() {
  // Subtest 2: Open notebook, set read-only via vxcore_notebook_set_read_only,
  // open buffer, assert isReadOnly() == true

  // Create a test file in root folder
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test2.md"));
  QVERIFY(!fileId.isEmpty());

  // Set notebook to read-only
  VxCoreError err = vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true);
  QCOMPARE(err, VXCORE_OK);

  // Verify notebook IS read-only
  bool readOnly = false;
  err = vxcore_notebook_is_read_only(m_context, m_notebookId.toUtf8().constData(), &readOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(readOnly, true);

  // Open buffer
  QString bufferId = m_bufferCoreService->openBuffer(m_notebookId, fileId);
  QVERIFY(!bufferId.isEmpty());

  // Test BufferCoreService::isBufferReadOnly
  bool coreReadOnly = m_bufferCoreService->isBufferReadOnly(bufferId);
  QCOMPARE(coreReadOnly, true);

  // Cleanup
  m_bufferCoreService->closeBuffer(bufferId);
}

void TestBufferReadOnly::testBufferReadOnlyDefensiveInvalid() {
  // Subtest 3: Defensive: query isBufferReadOnly on a non-existent buffer id
  // → returns false (does NOT crash)

  QString invalidBufferId = QStringLiteral("nonexistent-buffer-id");

  // Should return false defensively (and log a warning)
  bool readOnly = m_bufferCoreService->isBufferReadOnly(invalidBufferId);
  QCOMPARE(readOnly, false);

  // Also test via Buffer2 with invalid handle
  Buffer2 invalidBuf;
  QCOMPARE(invalidBuf.isValid(), false);
  QCOMPARE(invalidBuf.isReadOnly(), false); // noexcept, should not crash
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferReadOnly)
#include "test_buffer_read_only.moc"
