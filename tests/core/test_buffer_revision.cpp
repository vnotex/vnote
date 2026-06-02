// T6: Snapshot + revision tracking in BufferService.
//
// Verifies the public revision-tracking surface added for snapshot-versioned
// saves (T7 will wire BufferSaveQueue::saveFinished to markRevisionSaved):
//   - currentRevision()    : monotonic, bumped per editor change.
//   - lastSavedRevision()  : advances only forward.
//   - markRevisionSaved()  : stale completions ignored; matching revision
//                            clears the dirty flag.
//   - isDirty()            : preserved "has unsaved content" semantic.
//
// These subtests intentionally avoid opening real notebook buffers — the
// revision API is keyed by an opaque buffer-id string and uses lazy record
// init in markDirty(), so a synthetic id is sufficient.

#include <QtTest>

#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestBufferRevision : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testRevisionIncrementsOnEdit();
  void testStaleSaveLeavesBufferDirty();
  void testMatchingRevisionClearsBufferDirty();

private:
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  BufferService *m_bufferService = nullptr;

  static QString bufferId() { return QStringLiteral("test-buffer-rev"); }
};

void TestBufferRevision::initTestCase() {
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::None, this);
}

void TestBufferRevision::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestBufferRevision::cleanup() {
  // Close the synthetic id between subtests to wipe the BufferRecord.
  // closeBuffer on an unknown id is a harmless no-op at the vxcore layer.
  m_bufferService->closeBuffer(bufferId());
}

void TestBufferRevision::testRevisionIncrementsOnEdit() {
  const QString id = bufferId();
  QCOMPARE(m_bufferService->currentRevision(id), quint64(0));
  QCOMPARE(m_bufferService->lastSavedRevision(id), quint64(0));
  QVERIFY(!m_bufferService->isDirty(id));

  m_bufferService->markDirty(id);
  QCOMPARE(m_bufferService->currentRevision(id), quint64(1));
  QVERIFY(m_bufferService->isDirty(id));

  m_bufferService->markDirty(id);
  QCOMPARE(m_bufferService->currentRevision(id), quint64(2));
  QVERIFY(m_bufferService->isDirty(id));
}

void TestBufferRevision::testStaleSaveLeavesBufferDirty() {
  const QString id = bufferId();

  m_bufferService->markDirty(id); // rev=1 ("a")
  const quint64 rev1 = m_bufferService->currentRevision(id);
  QCOMPARE(rev1, quint64(1));

  m_bufferService->markDirty(id); // rev=2 ("ab")
  QCOMPARE(m_bufferService->currentRevision(id), quint64(2));

  // A stale save (in-flight when rev2 was typed) completes with rev1.
  m_bufferService->markRevisionSaved(id, rev1);

  QCOMPARE(m_bufferService->lastSavedRevision(id), quint64(1));
  QVERIFY(m_bufferService->isDirty(id));
}

void TestBufferRevision::testMatchingRevisionClearsBufferDirty() {
  const QString id = bufferId();

  m_bufferService->markDirty(id); // rev=1
  const quint64 rev1 = m_bufferService->currentRevision(id);
  QCOMPARE(rev1, quint64(1));
  QVERIFY(m_bufferService->isDirty(id));

  m_bufferService->markRevisionSaved(id, rev1);

  QCOMPARE(m_bufferService->lastSavedRevision(id), quint64(1));
  QVERIFY(!m_bufferService->isDirty(id));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferRevision)
#include "test_buffer_revision.moc"
