#include <QAtomicInt>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QThread>
#include <QThreadPool>
#include <QVector>
#include <QtTest>

#include <core/services/buffersavequeue.h>
#include <core/services/ibuffercoreservice.h>
#include <core/services/notebookiogate.h>

namespace tests {

// Header-only fake of IBufferCoreService. Records call order, supports an
// optional artificial delay, and can simulate failure on saveBuffer.
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

  bool setContentRaw(const QString &p_bufferId, const QByteArray &p_data) override {
    int sleepMs = 0;
    {
      QMutexLocker lk(&m_mutex);
      m_setContentCalls.append(Call{p_bufferId, p_data});
      sleepMs = m_sleepMs;
    }
    if (sleepMs > 0) {
      QThread::msleep(sleepMs);
    }
    return true;
  }

  bool saveBuffer(const QString &p_bufferId) override {
    QMutexLocker lk(&m_mutex);
    m_saveCalls.append(p_bufferId);
    return !m_failSave;
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
  int m_sleepMs = 0;
  bool m_failSave = false;
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
  FakeBufferCoreService fake;
  fake.setSleepMs(150); // Slow the in-flight job so subsequent enqueues coalesce.
  vnotex::NotebookIoGate gate;
  vnotex::BufferSaveQueue queue(fake, gate);
  QSignalSpy spy(&queue, &vnotex::BufferSaveQueue::saveFinished);

  // First enqueue starts the worker (which will sleep 150ms in setContentRaw).
  queue.enqueue("nb1", "bufA", "one", 1);
  // Briefly yield so the worker grabs job v1 and is busy in setContentRaw.
  QThread::msleep(30);
  // These two arrive while v1 is in-flight; v2 inserts pending, v3 replaces it
  // (and supersedes v2 → emits saveFinished for v2 immediately).
  queue.enqueue("nb1", "bufA", "two", 2);
  queue.enqueue("nb1", "bufA", "three", 3);

  // Expect three saveFinished signals total (v1 real, v2 superseded, v3 real).
  QVERIFY(waitForSignalCount(spy, 3));
  QCOMPARE(spy.count(), 3);

  // Drop the sleep so any extra jobs (shouldn't be any) drain fast.
  fake.setSleepMs(0);
  QVERIFY(queue.shutdown(3000));

  // Exactly two real setContentRaw calls landed for bufA: v1 ("one") and v3 ("three").
  // v2 must have been coalesced out.
  int contentCallsForA = 0;
  bool sawOne = false, sawThree = false, sawTwo = false;
  for (const auto &c : fake.setContentCalls()) {
    if (c.bufferId == "bufA") {
      ++contentCallsForA;
      if (c.data == "one")
        sawOne = true;
      if (c.data == "two")
        sawTwo = true;
      if (c.data == "three")
        sawThree = true;
    }
  }
  QCOMPARE(contentCallsForA, 2);
  QVERIFY(sawOne);
  QVERIFY(sawThree);
  QVERIFY(!sawTwo);

  // Find the v3 emission and assert ok+revision.
  bool sawV3Ok = false;
  for (int i = 0; i < spy.count(); ++i) {
    const auto &args = spy.at(i);
    const quint64 rev = args.at(1).toULongLong();
    const bool ok = args.at(2).toBool();
    if (rev == 3 && ok) {
      sawV3Ok = true;
    }
  }
  QVERIFY(sawV3Ok);
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBufferSaveQueue)
#include "test_buffer_save_queue.moc"
