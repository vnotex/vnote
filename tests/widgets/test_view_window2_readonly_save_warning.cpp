// Exercise production read-only warnings through real buffer save/edit rejections.
// The modal suppression seam leaves the per-view cooldown bookkeeping active.

#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <widgets/viewwindow2.h>

using namespace vnotex;

namespace tests {

// Minimal concrete ViewWindow2 subclass — the pure virtuals must be implemented
// to instantiate. None of the bodies are exercised by this test.
class TestViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  TestViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent = nullptr)
      : ViewWindow2(p_services, p_buffer, p_parent) {
    // Disable the actual QMessageBox so the test's event loop does not
    // block. The cooldown bookkeeping (m_readOnlyWarningCount) still
    // increments per show attempt, which is what we assert on.
    testSetReadOnlyWarningSuppressModal(true);
  }

  void setMode(ViewWindowMode) override {}
  void syncEditorFromBuffer() override {}
  void setModified(bool) override {}
  QString getLatestContent() const override { return {}; }
  void scrollUp() override {}
  void scrollDown() override {}
  void zoom(bool) override {}
};

class TestViewWindow2ReadOnlySaveWarning : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testSingleSaveOnReadOnlyBufferShowsModal();
  void testRapidSavesAreDeduped();
  void testCooldownExpiryAllowsSecondModal();
  void testMarkDirtyRejectionAlsoShowsModal();
  void testRejectionForOtherBufferDoesNotShowModal();

private:
  Buffer2 openBuffer(const QString &p_relativePath);
  void setNotebookReadOnly(bool p_readOnly);

  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestViewWindow2ReadOnlySaveWarning::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::None, this);

  m_services.registerService<BufferService>(m_bufferService);
  m_services.registerService<NotebookCoreService>(m_notebookService);
  m_services.registerService<HookManager>(m_hookMgr);

  const QString nbPath = m_tempDir.filePath(QStringLiteral("ro_save_warning_test"));
  const QString configJson =
      QStringLiteral(R"({"name": "T28 Warning", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestViewWindow2ReadOnlySaveWarning::cleanupTestCase() {
  if (!m_notebookId.isEmpty() && m_context) {
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  }
  if (m_bufferService) {
    m_bufferService->shutdown(2000);
  }
  if (!m_notebookId.isEmpty() && m_notebookService) {
    m_notebookService->closeNotebook(m_notebookId);
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

void TestViewWindow2ReadOnlySaveWarning::cleanup() {
  if (!m_notebookId.isEmpty()) {
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  }
  const QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &v : buffers) {
    const QString id = v.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

Buffer2 TestViewWindow2ReadOnlySaveWarning::openBuffer(const QString &p_relativePath) {
  return m_bufferService->openBuffer(NodeIdentifier{m_notebookId, p_relativePath});
}

void TestViewWindow2ReadOnlySaveWarning::setNotebookReadOnly(bool p_readOnly) {
  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), p_readOnly);
  QCOMPARE(err, VXCORE_OK);
}

// =============================================================================
// Subtest 1: a single rejected save shows the modal exactly once.
//
// Exercise Buffer2::save() through the service guard and production warning slots.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testSingleSaveOnReadOnlyBufferShowsModal() {
  const QString relPath = QStringLiteral("ro_single.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  // Seed a writable baseline on disk, close, THEN flip RO and re-open: a
  // buffer's read-only state is resolved when it is opened.
  Buffer2 seed = openBuffer(relPath);
  QVERIFY(seed.isValid());
  QVERIFY(seed.setContentRaw(QByteArray("original\n")));
  QVERIFY(seed.save());
  m_bufferService->closeBuffer(seed.id());

  setNotebookReadOnly(true);
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QCOMPARE(view.testReadOnlyWarningCount(), 0);

  QVERIFY(!buf.save());
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

// =============================================================================
// Subtest 2: five rapid rejections within < 2 s collapse into a single modal.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testRapidSavesAreDeduped() {
  const QString relPath = QStringLiteral("ro_burst.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());
  // Seed a writable baseline on disk, close, THEN flip RO and re-open.
  Buffer2 seed = openBuffer(relPath);
  QVERIFY(seed.isValid());
  QVERIFY(seed.setContentRaw(QByteArray("original\n")));
  QVERIFY(seed.save());
  m_bufferService->closeBuffer(seed.id());
  setNotebookReadOnly(true);
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  view.testResetReadOnlyWarning();

  // Drive five back-to-back rejection events. Each is well under the
  // 2-second cooldown so only the first should bump the modal counter.
  for (int i = 0; i < 5; ++i) {
    QVERIFY(!buf.save());
  }
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

// =============================================================================
// Subtest 3: after the cooldown window elapses, the next rejection shows a
// fresh modal.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testCooldownExpiryAllowsSecondModal() {
  const QString relPath = QStringLiteral("ro_cooldown.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());
  // Read-only is resolved at open time, so flip the notebook BEFORE opening.
  setNotebookReadOnly(true);
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  view.testResetReadOnlyWarning();

  // First burst → 1 modal.
  QVERIFY(!buf.save());
  QCOMPARE(view.testReadOnlyWarningCount(), 1);

  // Wait past the 2-second cooldown. Using QTest::qWait keeps the Qt event
  // loop pumping (signals stay live) but is also a real wall-clock delay.
  // Add a small margin (2200 ms) to absorb timer jitter.
  QTest::qWait(2200);

  // Second burst → cooldown expired, should bump again.
  QVERIFY(!buf.save());
  QCOMPARE(view.testReadOnlyWarningCount(), 2);
}

// =============================================================================
// Subtest 4: markDirty rejection also bumps the shared cooldown counter.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testMarkDirtyRejectionAlsoShowsModal() {
  const QString relPath = QStringLiteral("ro_markdirty.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());
  // Seed a writable baseline on disk, close, THEN flip RO and re-open.
  Buffer2 seed = openBuffer(relPath);
  QVERIFY(seed.isValid());
  QVERIFY(seed.setContentRaw(QByteArray("original\n")));
  QVERIFY(seed.save());
  m_bufferService->closeBuffer(seed.id());
  setNotebookReadOnly(true);
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  view.testResetReadOnlyWarning();

  // markDirty on a RO buffer fires BufferService::dirtyRejectedReadOnly,
  // which routes through onDirtyRejectedReadOnly -> showReadOnlyWarning().
  m_bufferService->markDirty(buf.id());
  QCOMPARE(view.testReadOnlyWarningCount(), 1);

  // Save and edit rejections share the same cooldown.
  QVERIFY(!buf.save());
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

// =============================================================================
// Subtest 5: rejection signal for a DIFFERENT buffer is filtered out — a
// sibling ViewWindow2 for buffer B must not surface buffer A's rejection.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testRejectionForOtherBufferDoesNotShowModal() {
  const QString relA = QStringLiteral("ro_other_a.md");
  const QString relB = QStringLiteral("ro_other_b.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), relA).isEmpty());
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), relB).isEmpty());

  // Read-only is resolved at open time, so flip the notebook BEFORE opening.
  setNotebookReadOnly(true);
  Buffer2 bufA = openBuffer(relA);
  Buffer2 bufB = openBuffer(relB);
  QVERIFY(bufA.isValid());
  QVERIFY(bufB.isValid());
  QVERIFY(bufA.isReadOnly());
  QVERIFY(bufB.isReadOnly());

  TestViewWindow2 view(m_services, bufA);
  view.testResetReadOnlyWarning();

  // Rejection for buffer B (other) → view (for buffer A) MUST stay silent.
  QVERIFY(!bufB.save());
  QCOMPARE(view.testReadOnlyWarningCount(), 0);

  // Rejection for buffer A → bump.
  QVERIFY(!bufA.save());
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

} // namespace tests

QTEST_MAIN(tests::TestViewWindow2ReadOnlySaveWarning)
#include "test_view_window2_readonly_save_warning.moc"
