// T16: Read-only guards on BufferService::markDirty and BufferSaveQueue::enqueue.
//
// Locks in the contract introduced 2026-06-09:
//   - On a read-only notebook (vxcore_notebook_set_read_only(..., true)):
//       * BufferService::markDirty()      MUST emit dirtyRejectedReadOnly,
//                                          MUST NOT add the buffer to the
//                                          dirty set, MUST NOT bump revision,
//                                          MUST NOT start the autosave timer.
//       * BufferSaveQueue::enqueue()       MUST emit saveRejectedReadOnly
//                                          BEFORE any mutex/queue/worker work,
//                                          MUST NOT fire saveFinished, and
//                                          the file on disk MUST be untouched.
//   - On a writable notebook neither rejection signal fires and both APIs
//     behave normally — guards must be invisible to the happy path.

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/services/buffer2.h>
#include <core/services/buffercoreservice.h>
#include <core/services/buffersavequeue.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestBufferReadOnlyGuard : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testMarkDirtyRejectedOnReadOnlyNotebook();
  void testEnqueueRejectedOnReadOnlyNotebook();
  void testWritableNotebookStillWorks();

private:
  // Read the full byte sequence at p_path; empty QByteArray on open failure.
  QByteArray readFile(const QString &p_path) const;

  // Flip the test notebook's read-only flag via the vxcore C ABI.
  void setNotebookReadOnly(bool p_readOnly);

  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferCoreService *m_bufferCoreService = nullptr;
  BufferService *m_bufferService = nullptr;
  NotebookIoGate *m_gate = nullptr;
  BufferSaveQueue *m_saveQueue = nullptr;
  TempDirFixture m_tempDir;

  QString m_notebookId;
};

void TestBufferReadOnlyGuard::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context);

  m_hookMgr = new HookManager(this);
  m_notebookService = new NotebookCoreService(m_context, this);
  m_bufferCoreService = new BufferCoreService(m_context, this);
  m_gate = new NotebookIoGate();
  // BufferService has its OWN internal save queue + gate; we use this instance
  // exclusively to exercise markDirty (Subtest 1) and the writable markDirty
  // regression (Subtest 3 path A).
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);
  // Separately constructed BufferSaveQueue wrapping the same BufferCoreService.
  // This isolates the BufferSaveQueue::enqueue guard from the BufferService
  // autosave timer and lets us assert on its signals directly.
  m_saveQueue = new BufferSaveQueue(*m_bufferCoreService, *m_gate, this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("guard_notebook"));
  QString configJson = QStringLiteral(R"({"name": "T16 Guard", "description": "RO guard"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestBufferReadOnlyGuard::cleanupTestCase() {
  // Drain queue workers before tearing down anything they depend on.
  if (m_saveQueue) {
    m_saveQueue->shutdown(2000);
  }
  delete m_saveQueue;
  m_saveQueue = nullptr;

  // Gate must outlive the queue (workers may hold ScopedLocks).
  delete m_gate;
  m_gate = nullptr;

  if (m_bufferService) {
    m_bufferService->shutdown(2000);
  }

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

void TestBufferReadOnlyGuard::cleanup() {
  // Reset RO state and close any open buffers so subtests are independent.
  if (!m_notebookId.isEmpty()) {
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  }

  // Close BufferService-tracked buffers (drops dirty set, autosave state).
  QJsonArray bufs = m_bufferService->listBuffers();
  for (const auto &v : bufs) {
    QString id = v.toObject().value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

QByteArray TestBufferReadOnlyGuard::readFile(const QString &p_path) const {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return f.readAll();
}

void TestBufferReadOnlyGuard::setNotebookReadOnly(bool p_readOnly) {
  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), p_readOnly);
  QCOMPARE(err, VXCORE_OK);
}

// =================================================================
// Subtest 1: BufferService::markDirty on a read-only notebook is rejected.
// =================================================================
void TestBufferReadOnlyGuard::testMarkDirtyRejectedOnReadOnlyNotebook() {
  // Open the buffer while still writable, then flip the notebook to read-only.
  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("ro_dirty.md"));
  QVERIFY(!fileId.isEmpty());

  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, fileId});
  QVERIFY(buf.isValid());

  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  // BufferService privately inherits QObject (via BufferCoreService) so the
  // SIGNAL() macro form is required (matches the existing test_buffer.cpp
  // pattern at line 564).
  QSignalSpy rejectSpy(m_bufferService->asQObject(), SIGNAL(dirtyRejectedReadOnly(QString)));
  QVERIFY(rejectSpy.isValid());

  QVERIFY(!m_bufferService->isDirty(buf.id()));
  QVERIFY(!m_bufferService->isModified(buf.id()));
  const quint64 revBefore = m_bufferService->currentRevision(buf.id());

  // Trigger the guard. The guarded branch returns BEFORE the dirty set / revision
  // mutation / autosave timer start. We expect a warning log on stderr — suppress
  // it so a clean test run is visually unambiguous.
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression("markDirty rejected: buffer is read-only.*"));
  m_bufferService->markDirty(buf.id());

  QCOMPARE(rejectSpy.count(), 1);
  QCOMPARE(rejectSpy.at(0).at(0).toString(), buf.id());

  // The buffer state must be untouched: not dirty, not modified, revision
  // counter unchanged.
  QVERIFY(!m_bufferService->isDirty(buf.id()));
  QVERIFY(!m_bufferService->isModified(buf.id()));
  QCOMPARE(m_bufferService->currentRevision(buf.id()), revBefore);
}

// =================================================================
// Subtest 2: BufferSaveQueue::enqueue on a read-only notebook is rejected
// AND the file on disk is unchanged.
// =================================================================
void TestBufferReadOnlyGuard::testEnqueueRejectedOnReadOnlyNotebook() {
  // Create a fresh file and open via BufferCoreService directly so we have a
  // bufferId that the queue's IBufferCoreService reference recognises.
  QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("ro_enqueue.md"));
  QVERIFY(!fileId.isEmpty());

  QString bufferId = m_bufferCoreService->openBuffer(m_notebookId, fileId);
  QVERIFY(!bufferId.isEmpty());

  // Persist a known payload to disk BEFORE flipping read-only. This becomes
  // the baseline byte sequence the guard must preserve.
  const QByteArray initial = QByteArrayLiteral("INITIAL_CONTENT_T16");
  QVERIFY(m_bufferCoreService->setContentRaw(bufferId, initial));
  QVERIFY(m_bufferCoreService->saveBuffer(bufferId));

  const QString diskPath = m_bufferCoreService->getResolvedPath(m_notebookId, fileId);
  QVERIFY(!diskPath.isEmpty());
  QCOMPARE(readFile(diskPath), initial);

  setNotebookReadOnly(true);

  // Capture both signals so we can prove rejection fired AND saveFinished
  // never did.
  QSignalSpy rejectSpy(m_saveQueue, &BufferSaveQueue::saveRejectedReadOnly);
  QSignalSpy finishedSpy(m_saveQueue, &BufferSaveQueue::saveFinished);
  QVERIFY(rejectSpy.isValid());
  QVERIFY(finishedSpy.isValid());

  const QByteArray attempted = QByteArrayLiteral("MODIFIED_PAYLOAD_T16");

  // Expect (and suppress) the qCWarning emitted by the guard.
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression("enqueue rejected: buffer is read-only.*"));
  m_saveQueue->enqueue(m_notebookId, bufferId, QString::fromUtf8(attempted), /*revision=*/1);

  // Even though the guard is synchronous, briefly pump events so any worker
  // that slipped past the guard (a real bug) would surface a saveFinished
  // signal we can detect.
  QTest::qWait(200);

  QCOMPARE(rejectSpy.count(), 1);
  QCOMPARE(rejectSpy.at(0).at(0).toString(), bufferId);
  QCOMPARE(finishedSpy.count(), 0);

  // The on-disk file must STILL hold the initial bytes — proves the guard
  // fired BEFORE setContentRaw/saveBuffer could touch the working tree.
  QCOMPARE(readFile(diskPath), initial);

  m_bufferCoreService->closeBuffer(bufferId);
}

// =================================================================
// Subtest 3: Writable notebook — no rejection, full happy path on both
// markDirty AND enqueue. Regression coverage.
// =================================================================
void TestBufferReadOnlyGuard::testWritableNotebookStillWorks() {
  // -------- Path A: markDirty regression --------
  QString fileIdA = m_notebookService->createFile(m_notebookId, QString(),
                                                  QStringLiteral("writable_markdirty.md"));
  QVERIFY(!fileIdA.isEmpty());

  Buffer2 buf = m_bufferService->openBuffer(NodeIdentifier{m_notebookId, fileIdA});
  QVERIFY(buf.isValid());
  QVERIFY(!buf.isReadOnly());

  QSignalSpy dirtyRejectSpy(m_bufferService->asQObject(), SIGNAL(dirtyRejectedReadOnly(QString)));
  QVERIFY(dirtyRejectSpy.isValid());

  const quint64 revBefore = m_bufferService->currentRevision(buf.id());

  m_bufferService->markDirty(buf.id());

  // No rejection AND the buffer entered the dirty set AND revision bumped.
  QCOMPARE(dirtyRejectSpy.count(), 0);
  QVERIFY(m_bufferService->isDirty(buf.id()));
  QVERIFY(m_bufferService->currentRevision(buf.id()) > revBefore);

  // -------- Path B: BufferSaveQueue::enqueue end-to-end regression --------
  QString fileIdB =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("writable_enqueue.md"));
  QVERIFY(!fileIdB.isEmpty());

  QString bufferIdB = m_bufferCoreService->openBuffer(m_notebookId, fileIdB);
  QVERIFY(!bufferIdB.isEmpty());

  const QByteArray initial = QByteArrayLiteral("INITIAL_WRITABLE");
  QVERIFY(m_bufferCoreService->setContentRaw(bufferIdB, initial));
  QVERIFY(m_bufferCoreService->saveBuffer(bufferIdB));

  const QString diskPath = m_bufferCoreService->getResolvedPath(m_notebookId, fileIdB);
  QVERIFY(!diskPath.isEmpty());
  QCOMPARE(readFile(diskPath), initial);

  QSignalSpy saveRejectSpy(m_saveQueue, &BufferSaveQueue::saveRejectedReadOnly);
  QSignalSpy finishedSpy(m_saveQueue, &BufferSaveQueue::saveFinished);
  QVERIFY(saveRejectSpy.isValid());
  QVERIFY(finishedSpy.isValid());

  const QByteArray attempted = QByteArrayLiteral("WRITABLE_PAYLOAD_NEW");
  m_saveQueue->enqueue(m_notebookId, bufferIdB, QString::fromUtf8(attempted), /*revision=*/42);

  // saveFinished bounces back to the calling thread via QueuedConnection;
  // pump events until it arrives (or timeout).
  QElapsedTimer timer;
  timer.start();
  while (finishedSpy.count() < 1 && timer.elapsed() < 5000) {
    finishedSpy.wait(50);
  }

  QCOMPARE(saveRejectSpy.count(), 0);
  QCOMPARE(finishedSpy.count(), 1);
  // saveFinished signature: (bufferId, revision, ok, errorMsg)
  QCOMPARE(finishedSpy.at(0).at(0).toString(), bufferIdB);
  QCOMPARE(finishedSpy.at(0).at(1).toULongLong(), quint64(42));
  QCOMPARE(finishedSpy.at(0).at(2).toBool(), true);

  // Disk now reflects the new payload — the guard is invisible to the happy path.
  QCOMPARE(readFile(diskPath), attempted);

  m_bufferCoreService->closeBuffer(bufferIdB);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferReadOnlyGuard)
#include "test_buffer_read_only_guard.moc"
