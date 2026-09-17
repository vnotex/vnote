#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QThread>
#include <QThreadPool>
#include <QVector>
#include <QtTest>

#include <stdexcept>
#include <thread>

#include <core/services/buffersavequeue.h>
#include <core/services/ibuffercoreservice.h>
#include <core/services/notebookiogate.h>

namespace tests {

static bool writeFile(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_bytes) == p_bytes.size() && file.flush();
}

static QByteArray readFile(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static QByteArray sha256(const QByteArray &p_bytes) {
  return QCryptographicHash::hash(p_bytes, QCryptographicHash::Sha256);
}

class WorkerBarrier {
public:
  void arriveAndWait() {
    m_entered.release();
    m_release.acquire();
  }
  bool waitUntilEntered() { return m_entered.tryAcquire(1, 5000); }
  void release() { m_release.release(); }

private:
  QSemaphore m_entered;
  QSemaphore m_release;
};

// Declare after the queue so failed assertions cannot strand a save worker
// while the queue destructor is joining it.
class ScopedBarrierRelease {
public:
  explicit ScopedBarrierRelease(WorkerBarrier &p_barrier) : m_barrier(p_barrier) {}
  ~ScopedBarrierRelease() { m_barrier.release(); }

private:
  WorkerBarrier &m_barrier;
};

class GateBlocker {
public:
  GateBlocker(vnotex::NotebookIoGate &p_gate, const QString &p_notebookId)
      : m_worker([this, &p_gate, p_notebookId]() {
          vnotex::NotebookIoGate::ScopedLock lock(p_gate, p_notebookId);
          m_barrier.arriveAndWait();
        }) {}
  ~GateBlocker() {
    m_barrier.release();
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }
  bool waitUntilEntered() { return m_barrier.waitUntilEntered(); }
  void release() {
    m_barrier.release();
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }

private:
  WorkerBarrier m_barrier;
  std::thread m_worker;
};

class ThreadPoolMinimum {
public:
  explicit ThreadPoolMinimum(int p_minimum)
      : m_previous(QThreadPool::globalInstance()->maxThreadCount()) {
    QThreadPool::globalInstance()->setMaxThreadCount(qMax(m_previous, p_minimum));
  }
  ~ThreadPoolMinimum() { QThreadPool::globalInstance()->setMaxThreadCount(m_previous); }

private:
  int m_previous;
};

// Observable in-memory and backing-file contents, with a one-shot barrier
// between installation and persistence. No fake mutex is held while waiting.
class FakeBufferCoreService : public vnotex::IBufferCoreService {
public:
  struct Call {
    QString bufferId;
    QByteArray data;
  };

  void setSleepMs(int p_ms) {
    QMutexLocker lk(&m_mutex);
    m_sleepMs = p_ms;
  }

  void setFailSave(bool p_fail) {
    QMutexLocker lk(&m_mutex);
    m_failSave = p_fail;
  }

  void setFailSetContent(bool p_fail) {
    QMutexLocker lk(&m_mutex);
    m_failSetContent = p_fail;
  }

  void setThrowOnSave(bool p_throw) {
    QMutexLocker lk(&m_mutex);
    m_throwOnSave = p_throw;
  }

  void setReadOnly(bool p_readOnly) {
    QMutexLocker lk(&m_mutex);
    m_readOnly = p_readOnly;
  }

  bool isBufferReadOnly(const QString &) const override {
    QMutexLocker lk(&m_mutex);
    return m_readOnly;
  }

  bool attachBackingFile(const QString &p_bufferId, const QString &p_path,
                         const QByteArray &p_bytes) {
    if (!writeFile(p_path, p_bytes)) {
      return false;
    }
    QMutexLocker lk(&m_mutex);
    m_paths.insert(p_bufferId, p_path);
    m_contents.insert(p_bufferId, p_bytes);
    return true;
  }

  QByteArray content(const QString &p_bufferId) const {
    QMutexLocker lk(&m_mutex);
    return m_contents.value(p_bufferId);
  }

  void pauseNextSave(const QString &p_bufferId, const std::shared_ptr<WorkerBarrier> &p_barrier) {
    QMutexLocker lk(&m_mutex);
    m_saveBarriers.insert(p_bufferId, p_barrier);
  }

  bool setContentRaw(const QString &p_bufferId, const QByteArray &p_data) override {
    int sleepMs = 0;
    {
      QMutexLocker lk(&m_mutex);
      m_setContentCalls.append(Call{p_bufferId, p_data});
      if (m_failSetContent) {
        return false;
      }
      m_contents.insert(p_bufferId, p_data);
      sleepMs = m_sleepMs;
    }
    if (sleepMs > 0) {
      QThread::msleep(sleepMs);
    }
    return true;
  }

  bool saveBuffer(const QString &p_bufferId) override {
    QString path;
    QByteArray bytes;
    std::shared_ptr<WorkerBarrier> barrier;
    bool fail = false;
    bool throwOnSave = false;
    {
      QMutexLocker lk(&m_mutex);
      m_saveCalls.append(p_bufferId);
      path = m_paths.value(p_bufferId);
      bytes = m_contents.value(p_bufferId);
      barrier = m_saveBarriers.take(p_bufferId);
      fail = m_failSave;
      throwOnSave = m_throwOnSave;
    }
    if (barrier) {
      barrier->arriveAndWait();
    }
    if (throwOnSave) {
      throw std::runtime_error("Simulated persistence exception");
    }
    return !fail && (path.isEmpty() || writeFile(path, bytes));
  }

  QVector<Call> setContentCalls() const {
    QMutexLocker lk(&m_mutex);
    return m_setContentCalls;
  }

  QVector<QString> saveCalls() const {
    QMutexLocker lk(&m_mutex);
    return m_saveCalls;
  }

  int setContentCount() const {
    QMutexLocker lk(&m_mutex);
    return m_setContentCalls.size();
  }

private:
  mutable QMutex m_mutex;
  QVector<Call> m_setContentCalls;
  QVector<QString> m_saveCalls;
  QHash<QString, QString> m_paths;
  QHash<QString, QByteArray> m_contents;
  QHash<QString, std::shared_ptr<WorkerBarrier>> m_saveBarriers;
  int m_sleepMs = 0;
  bool m_failSave = false;
  bool m_failSetContent = false;
  bool m_throwOnSave = false;
  bool m_readOnly = false;
};

class TestBufferSaveQueue : public QObject {
  Q_OBJECT

private slots:
  void testFifoOrderDistinctBuffers();
  void testCoalescingKeepsNewest();
  void testGateContentionSerializesSameNotebook();
  void testDistinctNotebooksParallelize();
  void testShutdownDrainsInFlight();
  void testErrorPath();
  void testIsBusyReflectsPendingAndRunning();
  void testWorkerEncodesWithJobEncoding();
  void testReplacementOwnsKeyUntilDurable();
  void testReplacementRejectsBusyOrdinaryKey();
  void testReplacementRejectsReadOnlyAndShutdown();
  void testReplacementRejectsDiskConflict_data();
  void testReplacementRejectsDiskConflict();
  void testReplacementCancelledBeforeMutation();
  void testReplacementCancelledAfterMutation();
  void testReplacementFailureKeepsRecoverableContent_data();
  void testReplacementFailureKeepsRecoverableContent();
  void testReplacementAllowsOtherNotebookProgress();
  void testReplacementShutdownDrainsAcceptedWrite();
};

// Helper: wait until the QSignalSpy collects @p_target signals or timeout.
static bool waitForSignalCount(QSignalSpy &p_spy, int p_target, int p_timeoutMs = 5000) {
  QElapsedTimer t;
  t.start();
  while (p_spy.count() < p_target && t.elapsed() < p_timeoutMs) {
    p_spy.wait(50);
  }
  return p_spy.count() >= p_target;
}

void TestBufferSaveQueue::testFifoOrderDistinctBuffers() {
  FakeBufferCoreService fake;
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  // Different buffers in same notebook — composite keys differ → may parallelise
  // across buffers but each buffer's saveBuffer call must happen exactly once.
  // We assert all 3 saves complete and recorded.
  queue.enqueue("nb1", "bufA", "A", 1);
  queue.enqueue("nb1", "bufB", "B", 1);
  queue.enqueue("nb1", "bufC", "C", 1);

  QVERIFY(waitForSignalCount(spy, 3));
  QCOMPARE(spy.count(), 3);

  auto saveCalls = fake.saveCalls();
  QCOMPARE(saveCalls.size(), 3);
  QVERIFY(saveCalls.contains("bufA"));
  QVERIFY(saveCalls.contains("bufB"));
  QVERIFY(saveCalls.contains("bufC"));

  QVERIFY(queue.shutdown(2000));
}

void TestBufferSaveQueue::testCoalescingKeepsNewest() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  auto barrier = std::make_shared<WorkerBarrier>();
  fake.pauseNextSave("bufA", barrier);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  ScopedBarrierRelease release(*barrier);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);

  queue.enqueue("nb1", "bufA", "one", 1);
  QVERIFY(barrier->waitUntilEntered());
  queue.enqueue("nb1", "bufA", "two", 2);
  queue.enqueue("nb1", "bufA", "three", 3);
  QCOMPARE(readFile(path), QByteArray("original"));
  barrier->release();

  QVERIFY(waitForSignalCount(spy, 3));
  QVERIFY(queue.shutdown(3000));
  QCOMPARE(readFile(path), QByteArray("three"));
  QCOMPARE(fake.content("bufA"), QByteArray("three"));
  const auto calls = fake.setContentCalls();
  QCOMPARE(calls.size(), 2);
  QCOMPARE(calls.at(0).data, QByteArray("one"));
  QCOMPARE(calls.at(1).data, QByteArray("three"));
  QSet<quint64> finishedRevisions;
  for (const auto &args : spy) {
    QVERIFY(args.at(2).toBool());
    finishedRevisions.insert(args.at(1).toULongLong());
  }
  QCOMPARE(finishedRevisions, (QSet<quint64>{1, 2, 3}));
  QCOMPARE(exactSpy.count(), 0);
}

void TestBufferSaveQueue::testGateContentionSerializesSameNotebook() {
  FakeBufferCoreService fake;
  fake.setSleepMs(50);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  QElapsedTimer timer;
  timer.start();

  // Same notebook, different buffers → composite keys differ but the
  // NotebookIoGate forces serialisation on the notebook.
  queue.enqueue("nbX", "bufA", "a", 1);
  queue.enqueue("nbX", "bufB", "b", 1);

  QVERIFY(waitForSignalCount(spy, 2));
  qint64 elapsed = timer.elapsed();

  QVERIFY2(elapsed >= 100,
           qPrintable(QStringLiteral("Expected serialised (>=100ms), got %1ms").arg(elapsed)));
  // Generous upper bound to absorb pool scheduling jitter on CI.
  QVERIFY2(elapsed < 500, qPrintable(QStringLiteral("Expected <500ms, got %1ms").arg(elapsed)));

  QVERIFY(queue.shutdown(2000));
}

void TestBufferSaveQueue::testDistinctNotebooksParallelize() {
  FakeBufferCoreService fake;
  fake.setSleepMs(100);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  // Ensure the global pool has enough threads for the parallel test; if the
  // host CPU reports <4 cores, bump it for this test.
  const int prevMax = QThreadPool::globalInstance()->maxThreadCount();
  if (prevMax < 4) {
    QThreadPool::globalInstance()->setMaxThreadCount(4);
  }

  QElapsedTimer timer;
  timer.start();

  queue.enqueue("nb1", "bufA", "a", 1);
  queue.enqueue("nb2", "bufA", "a", 1);
  queue.enqueue("nb3", "bufA", "a", 1);
  queue.enqueue("nb4", "bufA", "a", 1);

  QVERIFY(waitForSignalCount(spy, 4));
  qint64 elapsed = timer.elapsed();

  if (prevMax < 4) {
    QThreadPool::globalInstance()->setMaxThreadCount(prevMax);
  }

  // Strictly serial would be ~400ms; parallel should be ~100ms. Allow generous
  // headroom for CI noise.
  QVERIFY2(elapsed < 300,
           qPrintable(QStringLiteral("Expected parallel (<300ms), got %1ms").arg(elapsed)));

  QVERIFY(queue.shutdown(2000));
}

void TestBufferSaveQueue::testShutdownDrainsInFlight() {
  FakeBufferCoreService fake;
  fake.setSleepMs(200);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  // 5 distinct notebooks (each parallelisable up to pool capacity).
  const int prevMax = QThreadPool::globalInstance()->maxThreadCount();
  if (prevMax < 5) {
    QThreadPool::globalInstance()->setMaxThreadCount(5);
  }

  for (int i = 0; i < 5; ++i) {
    queue.enqueue(QStringLiteral("nb%1").arg(i), "buf", QStringLiteral("c%1").arg(i),
                  static_cast<quint64>(i + 1));
  }

  QVERIFY(queue.shutdown(2000));

  // Drain queued signal emissions.
  QVERIFY(waitForSignalCount(spy, 5, 2000));
  QCOMPARE(spy.count(), 5);

  if (prevMax < 5) {
    QThreadPool::globalInstance()->setMaxThreadCount(prevMax);
  }
}

void TestBufferSaveQueue::testErrorPath() {
  FakeBufferCoreService fake;
  fake.setFailSave(true);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  queue.enqueue("nb1", "bufA", "data", 42);
  QVERIFY(waitForSignalCount(spy, 1));
  QCOMPARE(spy.count(), 1);

  const auto args = spy.at(0);
  QCOMPARE(args.at(0).toString(), QString("bufA"));
  QCOMPARE(args.at(1).toULongLong(), quint64(42));
  QCOMPARE(args.at(2).toBool(), false);
  QVERIFY(!args.at(3).toString().isEmpty());

  QVERIFY(queue.shutdown(2000));
}

void TestBufferSaveQueue::testIsBusyReflectsPendingAndRunning() {
  FakeBufferCoreService fake;
  fake.setSleepMs(200); // Hold the worker in setContentRaw so the job is "running".
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  // Idle queue: not busy.
  QVERIFY(!queue.isBusy("nb1", "bufA"));
  // A different buffer must remain unaffected throughout.
  QVERIFY(!queue.isBusy("nb1", "bufB"));

  queue.enqueue("nb1", "bufA", "payload", 1);

  // While the worker is sleeping inside setContentRaw, the job is in-flight and
  // isBusy must report true for THIS buffer only. This is the exact window the
  // external-change check must skip to avoid a self-write false positive.
  QVERIFY(queue.isBusy("nb1", "bufA"));
  QVERIFY(!queue.isBusy("nb1", "bufB"));

  // After the save drains, isBusy returns to false.
  QVERIFY(waitForSignalCount(spy, 1));
  QVERIFY(queue.isBusy("nb1", "bufA") == false);

  QVERIFY(queue.shutdown(2000));
}

// The worker must encode job.content with the per-job encoding captured on the
// UI thread — GB18030 bytes for a GB18030 job, and a UTF-8 fallback for an
// empty (default) or unknown codec name. This exercises the buffersavequeue
// encodeWith() path that the async AutoSave save route depends on.
void TestBufferSaveQueue::testWorkerEncodesWithJobEncoding() {
  FakeBufferCoreService fake;
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  const QString cjk = QString::fromUtf8("\xE4\xBD\xA0\xE5\xA5\xBD"); // 你好
  QTextCodec *gb = QTextCodec::codecForName("GB18030");
  QVERIFY(gb != nullptr);

  // Default (empty) encoding → UTF-8 bytes.
  queue.enqueue("nb1", "bufDefault", cjk, 1, QString());
  // Explicit GB18030 → GB18030 bytes.
  queue.enqueue("nb1", "bufGb", cjk, 1, QStringLiteral("GB18030"));
  // Unknown codec → UTF-8 fallback.
  queue.enqueue("nb1", "bufBad", cjk, 1, QStringLiteral("NoSuchCodec-XYZ"));

  QVERIFY(waitForSignalCount(spy, 3));
  QVERIFY(queue.shutdown(2000));

  QByteArray defaultBytes, gbBytes, badBytes;
  for (const auto &c : fake.setContentCalls()) {
    if (c.bufferId == "bufDefault")
      defaultBytes = c.data;
    else if (c.bufferId == "bufGb")
      gbBytes = c.data;
    else if (c.bufferId == "bufBad")
      badBytes = c.data;
  }

  QCOMPARE(defaultBytes, cjk.toUtf8());
  QCOMPARE(gbBytes, gb->fromUnicode(cjk));
  QVERIFY(gbBytes != cjk.toUtf8());
  QCOMPARE(badBytes, cjk.toUtf8());
}

void TestBufferSaveQueue::testReplacementOwnsKeyUntilDurable() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  const QByteArray original("original\r\n");
  const QByteArray payload = QByteArray::fromHex("efbbbf636166e90d0a");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, original));
  auto barrier = std::make_shared<WorkerBarrier>();
  fake.pauseNextSave("bufA", barrier);
  auto cancelled = std::make_shared<std::atomic_bool>(false);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  ScopedBarrierRelease release(*barrier);
  GateBlocker blocker(gate, "nb1");
  QVERIFY(blocker.waitUntilEntered());
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  QSignalSpy ordinarySpy(&queue, &vnotex::BufferSaveQueue::saveFinished);
  bool deliverySawDurableIdleState = false;
  connect(
      &queue, &vnotex::BufferSaveQueue::replacementFinished, &queue,
      [&](const QString &, quint64 p_revision, bool p_changed, bool p_ok, const QString &) {
        vnotex::NotebookIoGate::ScopedTryLock releasedGate(gate, "nb1", 0);
        deliverySawDurableIdleState = QThread::currentThread() == queue.thread() &&
                                      !queue.isBusy("nb1", "bufA") && releasedGate.isLocked() &&
                                      p_revision == 7 && p_changed && p_ok &&
                                      readFile(path) == payload;
      },
      Qt::DirectConnection);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", path, sha256(original), payload, 7, cancelled));
  QVERIFY(queue.isBusy("nb1", "bufA"));
  QVERIFY(!queue.enqueueReplacement("nb1", "bufA", path, sha256(original), "superseding", 8,
                                    cancelled));
  queue.enqueue("nb1", "bufA", "stale pending editor", 9);
  QCoreApplication::processEvents();
  QCOMPARE(exactSpy.count(), 0);
  QCOMPARE(fake.content("bufA"), original);
  blocker.release();

  QVERIFY(barrier->waitUntilEntered());
  QCOMPARE(fake.content("bufA"), payload);
  QCOMPARE(readFile(path), original);
  QVERIFY(queue.isBusy("nb1", "bufA"));
  QVERIFY(!queue.enqueueReplacement("nb1", "bufA", path, sha256(original), "superseding", 10,
                                    cancelled));
  queue.enqueue("nb1", "bufA", "stale running editor", 11);
  QCoreApplication::processEvents();
  QCOMPARE(exactSpy.count(), 0);
  QCOMPARE(ordinarySpy.count(), 0);
  barrier->release();

  QVERIFY(waitForSignalCount(exactSpy, 1));
  QVERIFY(queue.shutdown(5000));
  QCOMPARE(exactSpy.count(), 1);
  QVERIFY(deliverySawDurableIdleState);
  QCOMPARE(readFile(path), payload);
  QCOMPARE(fake.content("bufA"), payload);
  QCOMPARE(ordinarySpy.count(), 0);
}

void TestBufferSaveQueue::testReplacementRejectsBusyOrdinaryKey() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  auto barrier = std::make_shared<WorkerBarrier>();
  fake.pauseNextSave("bufA", barrier);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  ScopedBarrierRelease release(*barrier);
  QSignalSpy ordinarySpy(&queue, &vnotex::BufferSaveQueue::saveFinished);
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  queue.enqueue("nb1", "bufA", "autosave", 1);
  QVERIFY(barrier->waitUntilEntered());
  QVERIFY(!queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 2,
                                    cancelled));
  queue.enqueue("nb1", "bufA", "latest autosave", 3);
  QVERIFY(!queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 4,
                                    cancelled));
  barrier->release();
  QVERIFY(waitForSignalCount(ordinarySpy, 2));
  QVERIFY(queue.shutdown(5000));
  QCOMPARE(readFile(path), QByteArray("latest autosave"));
  QCOMPARE(fake.content("bufA"), QByteArray("latest autosave"));
  QCOMPARE(exactSpy.count(), 0);
}

void TestBufferSaveQueue::testReplacementRejectsReadOnlyAndShutdown() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  fake.setReadOnly(true);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(!queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 1,
                                    cancelled));
  QVERIFY(!queue.isBusy("nb1", "bufA"));
  fake.setReadOnly(false);
  QVERIFY(queue.shutdown(0));
  QVERIFY(!queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 2,
                                    cancelled));
  QCoreApplication::processEvents();
  QCOMPARE(exactSpy.count(), 0);
  QCOMPARE(readFile(path), QByteArray("original"));
  QCOMPARE(fake.content("bufA"), QByteArray("original"));
}

void TestBufferSaveQueue::testReplacementRejectsDiskConflict_data() {
  QTest::addColumn<bool>("missing");
  QTest::newRow("changed-after-enqueue") << false;
  QTest::newRow("removed-after-enqueue") << true;
}

void TestBufferSaveQueue::testReplacementRejectsDiskConflict() {
  QFETCH(bool, missing);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  GateBlocker blocker(gate, "nb1");
  QVERIFY(blocker.waitUntilEntered());
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 1,
                                   cancelled));
  if (missing) {
    QVERIFY(QFile::remove(path));
  } else {
    QVERIFY(writeFile(path, "external change"));
  }
  blocker.release();
  QVERIFY(waitForSignalCount(exactSpy, 1));
  QVERIFY(queue.shutdown(5000));
  QVERIFY(!exactSpy.at(0).at(2).toBool());
  QVERIFY(!exactSpy.at(0).at(3).toBool());
  QVERIFY(!exactSpy.at(0).at(4).toString().isEmpty());
  QCOMPARE(fake.content("bufA"), QByteArray("original"));
  if (missing) {
    QVERIFY(!QFile::exists(path));
  } else {
    QCOMPARE(readFile(path), QByteArray("external change"));
  }
  QVERIFY(!queue.isBusy("nb1", "bufA"));
}

void TestBufferSaveQueue::testReplacementCancelledBeforeMutation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  GateBlocker blocker(gate, "nb1");
  QVERIFY(blocker.waitUntilEntered());
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 1,
                                   cancelled));
  cancelled->store(true);
  blocker.release();
  QVERIFY(waitForSignalCount(exactSpy, 1));
  QVERIFY(queue.shutdown(5000));
  QVERIFY(!exactSpy.at(0).at(2).toBool());
  QVERIFY(!exactSpy.at(0).at(3).toBool());
  QCOMPARE(fake.content("bufA"), QByteArray("original"));
  QCOMPARE(readFile(path), QByteArray("original"));
  QVERIFY(!queue.isBusy("nb1", "bufA"));
}

void TestBufferSaveQueue::testReplacementCancelledAfterMutation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  auto barrier = std::make_shared<WorkerBarrier>();
  fake.pauseNextSave("bufA", barrier);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  ScopedBarrierRelease release(*barrier);
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 1,
                                   cancelled));
  QVERIFY(barrier->waitUntilEntered());
  QCOMPARE(fake.content("bufA"), QByteArray("replacement"));
  QCOMPARE(readFile(path), QByteArray("original"));
  cancelled->store(true);
  barrier->release();
  QVERIFY(waitForSignalCount(exactSpy, 1));
  QVERIFY(queue.shutdown(5000));
  QVERIFY(exactSpy.at(0).at(2).toBool());
  QVERIFY(exactSpy.at(0).at(3).toBool());
  QCOMPARE(readFile(path), QByteArray("replacement"));
  QCOMPARE(fake.content("bufA"), QByteArray("replacement"));
}

void TestBufferSaveQueue::testReplacementFailureKeepsRecoverableContent_data() {
  QTest::addColumn<int>("failurePhase");
  QTest::newRow("installation-refused") << 0;
  QTest::newRow("write-failed") << 1;
  QTest::newRow("write-threw") << 2;
}

void TestBufferSaveQueue::testReplacementFailureKeepsRecoverableContent() {
  QFETCH(int, failurePhase);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  fake.setFailSetContent(failurePhase == 0);
  fake.setFailSave(failurePhase == 1);
  fake.setThrowOnSave(failurePhase == 2);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  QSignalSpy ordinarySpy(&queue, &vnotex::BufferSaveQueue::saveFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 1,
                                   cancelled));
  QVERIFY(waitForSignalCount(exactSpy, 1));
  QVERIFY(queue.shutdown(5000));
  QCOMPARE(exactSpy.at(0).at(2).toBool(), failurePhase != 0);
  QVERIFY(!exactSpy.at(0).at(3).toBool());
  QVERIFY(!exactSpy.at(0).at(4).toString().isEmpty());
  QCOMPARE(readFile(path), QByteArray("original"));
  QCOMPARE(fake.content("bufA"), QByteArray(failurePhase == 0 ? "original" : "replacement"));
  QVERIFY(!queue.isBusy("nb1", "bufA"));
  QCOMPARE(exactSpy.count(), 1);
  QCOMPARE(ordinarySpy.count(), 0);
}

void TestBufferSaveQueue::testReplacementAllowsOtherNotebookProgress() {
  ThreadPoolMinimum pool(2);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString pathA = directory.filePath("a.txt");
  const QString pathB = directory.filePath("b.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", pathA, "original A"));
  QVERIFY(fake.attachBackingFile("bufB", pathB, "original B"));
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  GateBlocker blocker(gate, "nb1");
  QVERIFY(blocker.waitUntilEntered());
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", pathA, sha256("original A"), "replacement A", 1,
                                   cancelled));
  QVERIFY(queue.enqueueReplacement("nb2", "bufB", pathB, sha256("original B"), "replacement B", 2,
                                   cancelled));
  QVERIFY(waitForSignalCount(exactSpy, 1));
  QCOMPARE(exactSpy.at(0).at(0).toString(), QString("bufB"));
  QVERIFY(exactSpy.at(0).at(3).toBool());
  QCOMPARE(readFile(pathB), QByteArray("replacement B"));
  QCOMPARE(readFile(pathA), QByteArray("original A"));
  QVERIFY(queue.isBusy("nb1", "bufA"));
  blocker.release();
  QVERIFY(waitForSignalCount(exactSpy, 2));
  QVERIFY(queue.shutdown(5000));
  QCOMPARE(readFile(pathA), QByteArray("replacement A"));
}

void TestBufferSaveQueue::testReplacementShutdownDrainsAcceptedWrite() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("note.txt");
  const QString otherPath = directory.filePath("other.txt");
  FakeBufferCoreService fake;
  QVERIFY(fake.attachBackingFile("bufA", path, "original"));
  QVERIFY(fake.attachBackingFile("bufB", otherPath, "untouched"));
  auto barrier = std::make_shared<WorkerBarrier>();
  fake.pauseNextSave("bufA", barrier);
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  ScopedBarrierRelease release(*barrier);
  QSignalSpy exactSpy(&queue, &vnotex::BufferSaveQueue::replacementFinished);
  auto cancelled = std::make_shared<std::atomic_bool>(false);

  QVERIFY(queue.enqueueReplacement("nb1", "bufA", path, sha256("original"), "replacement", 1,
                                   cancelled));
  QVERIFY(barrier->waitUntilEntered());
  QVERIFY(!queue.shutdown(0));
  QVERIFY(queue.isBusy("nb1", "bufA"));
  QVERIFY(!queue.enqueueReplacement("nb2", "bufB", otherPath, sha256("untouched"), "rejected", 2,
                                    cancelled));
  queue.enqueue("nb2", "bufB", "also rejected", 3);
  QCoreApplication::processEvents();
  QCOMPARE(exactSpy.count(), 0);
  QCOMPARE(readFile(path), QByteArray("original"));
  barrier->release();

  QVERIFY(queue.shutdown(5000));
  QCOMPARE(readFile(path), QByteArray("replacement"));
  QCOMPARE(readFile(otherPath), QByteArray("untouched"));
  QCOMPARE(fake.content("bufB"), QByteArray("untouched"));
  QVERIFY(!queue.isBusy("nb1", "bufA"));
  QVERIFY(waitForSignalCount(exactSpy, 1));
  QVERIFY(exactSpy.at(0).at(2).toBool());
  QVERIFY(exactSpy.at(0).at(3).toBool());
  QCOMPARE(exactSpy.count(), 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferSaveQueue)
#include "test_buffer_save_queue.moc"
