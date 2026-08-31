// T8: Buffer2::isReadOnly() + BufferCoreService::isNotebookReadOnlyForBuffer() query plumbing.
//
// Verifies the public read-only query surface:
//   - Buffer2::isReadOnly()                : delegates to BufferService (state resolved at open)
//   - BufferCoreService::isNotebookReadOnlyForBuffer(): resolves buffer → notebook, queries vxcore
//
// Tests against a bundled notebook with real vxcore integration.

#include <QtTest>

#include <core/hookevents.h>
#include <core/hooknames.h>
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
  void testForcedReadOnlyOverride();
  void testNotebookReadOnlyResolvedAtOpen();
  void testReadOnlyIsVisibleInsideFileAfterOpenHook();
  void testRestoredBufferHandleAdoptsReadOnly();
  void testForcedReadOnlyUpgradesDeduplicatedWritableBuffer();
  void testClosedBufferStateIsForgotten();
  void testNotebookCloseSweepsBufferState();

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
  // NotebookAfterClose is what BufferService's stale-buffer sweep listens to.
  m_notebookService->setHookManager(m_hookMgr);
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
  const QString relPath = QStringLiteral("test.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  // Verify notebook is NOT read-only
  bool readOnly = true;
  VxCoreError err =
      vxcore_notebook_is_read_only(m_context, m_notebookId.toUtf8().constData(), &readOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(readOnly, false);

  // Open buffer by relative path. vxcore gates open on disk existence, and the
  // node UUID returned by createFile is NOT a valid on-disk path.
  QString bufferId = m_bufferCoreService->openBuffer(m_notebookId, relPath);
  QVERIFY(!bufferId.isEmpty());

  // Test BufferCoreService::isNotebookReadOnlyForBuffer
  bool coreReadOnly = m_bufferCoreService->isNotebookReadOnlyForBuffer(bufferId);
  QCOMPARE(coreReadOnly, false);

  // Cleanup
  m_bufferCoreService->closeBuffer(bufferId);
}

void TestBufferReadOnly::testBufferReadOnlyTrue() {
  // Subtest 2: Open notebook, set read-only via vxcore_notebook_set_read_only,
  // open buffer, assert isReadOnly() == true

  // Create a test file in root folder
  const QString relPath = QStringLiteral("test2.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  // Set notebook to read-only
  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true);
  QCOMPARE(err, VXCORE_OK);

  // Verify notebook IS read-only
  bool readOnly = false;
  err = vxcore_notebook_is_read_only(m_context, m_notebookId.toUtf8().constData(), &readOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(readOnly, true);

  // Open buffer by relative path. vxcore gates open on disk existence, and the
  // node UUID returned by createFile is NOT a valid on-disk path.
  QString bufferId = m_bufferCoreService->openBuffer(m_notebookId, relPath);
  QVERIFY(!bufferId.isEmpty());

  // Test BufferCoreService::isNotebookReadOnlyForBuffer
  bool coreReadOnly = m_bufferCoreService->isNotebookReadOnlyForBuffer(bufferId);
  QCOMPARE(coreReadOnly, true);

  // Cleanup
  m_bufferCoreService->closeBuffer(bufferId);
}

void TestBufferReadOnly::testBufferReadOnlyDefensiveInvalid() {
  // Subtest 3: Defensive: query isBufferReadOnly on a non-existent buffer id
  // → returns false (does NOT crash)

  QString invalidBufferId = QStringLiteral("nonexistent-buffer-id");

  // Should return false defensively (and log a warning)
  bool readOnly = m_bufferCoreService->isNotebookReadOnlyForBuffer(invalidBufferId);
  QCOMPARE(readOnly, false);

  // Also test via Buffer2 with invalid handle
  Buffer2 invalidBuf;
  QCOMPARE(invalidBuf.isValid(), false);
  QCOMPARE(invalidBuf.isReadOnly(), false); // noexcept, should not crash
}

void TestBufferReadOnly::testForcedReadOnlyOverride() {
  // The notebook is writable, but a buffer opened with
  // FileOpenSettings::m_readOnly == true must still report read-only.
  // (Reproduces the "View Logs" bug: a forced-read-only buffer in a writable
  // notebook used to open editable.)

  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test_forced_ro.md"));
  QVERIFY(!fileId.isEmpty());

  // Sanity: notebook is NOT read-only.
  bool nbReadOnly = true;
  VxCoreError err =
      vxcore_notebook_is_read_only(m_context, m_notebookId.toUtf8().constData(), &nbReadOnly);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(nbReadOnly, false);

  // Open via BufferService with the per-open read-only override set.
  FileOpenSettings settings;
  settings.m_readOnly = true;
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("test_forced_ro.md")}, settings);
  QVERIFY(buf.isValid());
  const QString bufferId = buf.id();

  // Override is honored through both the handle and the service query.
  QCOMPARE(buf.isReadOnly(), true);
  QCOMPARE(m_bufferService->isBufferReadOnly(bufferId), true);

  // Close clears the forced-read-only state.
  QVERIFY(m_bufferService->closeBuffer(bufferId));

  // Reopen the SAME file with default settings (m_readOnly == false): the
  // override must be gone and the notebook-derived (writable) state applies.
  Buffer2 buf2 = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("test_forced_ro.md")});
  QVERIFY(buf2.isValid());
  QCOMPARE(buf2.isReadOnly(), false);
  QCOMPARE(m_bufferService->isBufferReadOnly(buf2.id()), false);

  m_bufferService->closeBuffer(buf2.id());
}

void TestBufferReadOnly::testNotebookReadOnlyResolvedAtOpen() {
  // A buffer opened WITHOUT the per-open override in a read-only notebook must
  // still report read-only: BufferService resolves the notebook flag at open
  // time and stores it as a plain per-buffer fact.
  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test_nb_ro.md"));
  QVERIFY(!fileId.isEmpty());

  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true);
  QCOMPARE(err, VXCORE_OK);

  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test_nb_ro.md")});
  QVERIFY(buf.isValid());
  QCOMPARE(buf.isReadOnly(), true);

  m_bufferService->closeBuffer(buf.id());
}

void TestBufferReadOnly::testReadOnlyIsVisibleInsideFileAfterOpenHook() {
  // Regression: ViewAreaController builds the ViewWindow synchronously inside
  // the FileAfterOpen hook and reads Buffer2::isReadOnly() there. If the
  // read-only state were resolved AFTER the hook, the editor would be created
  // writable for a read-only buffer (the "View Logs" bug).
  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test_hook_ro.md"));
  QVERIFY(!fileId.isEmpty());

  bool observed = false;
  bool readOnlyInHook = false;
  const int hookId = m_hookMgr->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [&](HookContext &, const FileOpenEvent &p_event) {
        observed = true;
        readOnlyInHook = m_bufferService->isBufferReadOnly(p_event.bufferId);
      },
      10);

  FileOpenSettings settings;
  settings.m_readOnly = true;
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("test_hook_ro.md")}, settings);

  m_hookMgr->removeAction(hookId);

  QVERIFY(buf.isValid());
  QVERIFY(observed);
  QCOMPARE(readOnlyInHook, true);

  m_bufferService->closeBuffer(buf.id());
}

void TestBufferReadOnly::testRestoredBufferHandleAdoptsReadOnly() {
  // Session restore: vxcore reconstructs persisted buffers before BufferService
  // exists, and ViewAreaController re-attaches them via getBufferHandle() —
  // never through openBuffer(). The handle must still report read-only, or the
  // restored ViewWindow2 would be built writable.
  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test_restore_ro.md"));
  QVERIFY(!fileId.isEmpty());

  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true);
  QCOMPARE(err, VXCORE_OK);

  // Open behind BufferService's back, as a session restore would.
  const QString bufferId =
      m_bufferCoreService->openBuffer(m_notebookId, QStringLiteral("test_restore_ro.md"));
  QVERIFY(!bufferId.isEmpty());
  QVERIFY(!m_bufferService->isBufferReadOnly(bufferId)); // not resolved yet

  Buffer2 restored = m_bufferService->getBufferHandle(bufferId);
  QVERIFY(restored.isValid());
  QCOMPARE(restored.isReadOnly(), true);

  m_bufferService->closeBuffer(bufferId);
}

void TestBufferReadOnly::testForcedReadOnlyUpgradesDeduplicatedWritableBuffer() {
  // vxcore dedups buffers by path, so opening a file normally and THEN through
  // a read-only entry point (e.g. "View Logs") returns the SAME buffer id. The
  // forced-read-only open must upgrade the already-resolved writable buffer,
  // otherwise the second open silently opens editable.
  const QString rel = QStringLiteral("test_dedup_ro.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), rel).isEmpty());

  Buffer2 writable = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, rel});
  QVERIFY(writable.isValid());
  QCOMPARE(writable.isReadOnly(), false);

  FileOpenSettings settings;
  settings.m_readOnly = true;
  Buffer2 forced = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, rel}, settings);
  QVERIFY(forced.isValid());
  QCOMPARE(forced.id(), writable.id()); // deduplicated
  QCOMPARE(forced.isReadOnly(), true);
  // The first handle names the same buffer, so it must agree.
  QCOMPARE(writable.isReadOnly(), true);

  m_bufferService->closeBuffer(forced.id());
}

void TestBufferReadOnly::testClosedBufferStateIsForgotten() {
  // The ordinary tab-close path removes the buffer from its workspace and lets
  // vxcore auto-close it, bypassing BufferService::closeBuffer. forgetBufferIfClosed
  // is what keeps the resolved read-only fact (and every other per-buffer map)
  // from growing for the whole session.
  const QString rel = QStringLiteral("test_forget_ro.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), rel).isEmpty());

  FileOpenSettings settings;
  settings.m_readOnly = true;
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, rel}, settings);
  QVERIFY(buf.isValid());
  const QString bufferId = buf.id();
  QVERIFY(m_bufferService->isBufferReadOnly(bufferId));

  // Still open -> no-op.
  QCOMPARE(m_bufferService->forgetBufferIfClosed(bufferId), false);
  QVERIFY(m_bufferService->isBufferReadOnly(bufferId));

  // Close behind the service's back, as vxcore's workspace orphan cleanup does.
  QVERIFY(m_bufferCoreService->closeBuffer(bufferId));
  QCOMPARE(m_bufferService->forgetBufferIfClosed(bufferId), true);
  QCOMPARE(m_bufferService->isBufferReadOnly(bufferId), false);
}

void TestBufferReadOnly::testNotebookCloseSweepsBufferState() {
  // Closing a notebook drops its buffers inside vxcore without routing through
  // BufferService::closeBuffer or the per-tab forget hook. The NotebookAfterClose
  // sweep is what keeps the per-buffer maps from retaining them.
  const QString nbPath = m_tempDir.filePath(QStringLiteral("sweep_notebook"));
  const QString configJson = QStringLiteral(R"({"name": "Sweep", "description": "sweep"})");
  const QString nbId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  const QString rel = QStringLiteral("sweep.md");
  QVERIFY(!m_notebookService->createFile(nbId, QString(), rel).isEmpty());

  FileOpenSettings settings;
  settings.m_readOnly = true;
  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{nbId, rel}, settings);
  QVERIFY(buf.isValid());
  const QString bufferId = buf.id();
  QVERIFY(m_bufferService->isBufferReadOnly(bufferId));

  // closeNotebook fires NotebookAfterClose, which BufferService subscribes to.
  QCOMPARE(m_notebookService->closeNotebook(nbId), true);
  QCOMPARE(m_bufferService->isBufferReadOnly(bufferId), false);
}

} // namespace tests
QTEST_GUILESS_MAIN(tests::TestBufferReadOnly)
#include "test_buffer_read_only.moc"
