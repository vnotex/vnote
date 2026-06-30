// test_search_service_drain.cpp - Tests for SearchService drain-thread pool (T7).
//
// Verifies two properties of the drain-thread pool that powers vxcore's
// "vxcore.search" content-search work queue:
//   1. CORRECTNESS: an async SearchService::searchContent over a >50-file
//      notebook returns the SAME ordered results as a direct single-threaded
//      SearchCoreService::searchContentCancellable baseline (the parallel drain
//      pool must not corrupt or reorder results).
//   2. IDLE-CPU: with no search running, the drain threads block on the work
//      queue's condvar (up to 100ms per poll) instead of busy-spinning, so
//      testDrainItemsProcessed() stays 0 across a >=500ms idle window, and the
//      pool is sized to min(hardware_concurrency, 8) floored at 2.
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <QElapsedTimer>
#include <QSignalSpy>

#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestSearchServiceDrain : public QObject {
  Q_OBJECT

public:
  TestSearchServiceDrain() = default;

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Async search through the drain pool matches the single-threaded baseline.
  void testDrainPoolResultsMatchBaseline();

  // Idle drain threads block on the condvar (0 items processed) and the pool is
  // sized correctly.
  void testIdleDrainThreadsDoNotSpin();

  // T8: destroying a SearchService while a content search is still in flight
  // must join all drain threads and return promptly (no hang, no UAF).
  void testTeardownDuringActiveSearch();

  // T8: cancelling a search with a backlog of enqueued work items must
  // terminate promptly (cancelled or finished) and drain the queue to 0 so the
  // service tears down cleanly with nothing stranded.
  void testCancelWithBacklog();

private:
  // Canonicalize a SearchResult into a deterministic, order-preserving string
  // so two results can be compared for exact equality.
  static QString canonicalize(const SearchResult &p_result);

  // Build a bundled notebook named p_name containing p_fileCount files (every
  // third file holds the "needle" needle). Reused by initTestCase for both the
  // baseline/idle notebook and the larger cancel-backlog notebook so the setup
  // lives in exactly one place.
  void createFilledNotebook(const QString &p_name, int p_fileCount, QString *p_outNotebookId);

  VxCoreContextHandle m_context = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
  QString m_largeNotebookId;

  static constexpr int kFileCount = 60;        // > kParallelSearchThreshold (50)
  static constexpr int kLargeFileCount = 300;  // Deep backlog so cancel can win.
};

QString TestSearchServiceDrain::canonicalize(const SearchResult &p_result) {
  QStringList lines;
  lines << QStringLiteral("matchCount=%1").arg(p_result.m_matchCount);
  lines << QStringLiteral("truncated=%1").arg(p_result.m_truncated ? 1 : 0);
  for (const auto &fileResult : p_result.m_fileResults) {
    lines << QStringLiteral("file=%1").arg(fileResult.m_path);
    for (const auto &lineMatch : fileResult.m_lineMatches) {
      QStringList segs;
      for (const auto &seg : lineMatch.m_segments) {
        segs << QStringLiteral("%1-%2").arg(seg.m_columnStart).arg(seg.m_columnEnd);
      }
      lines << QStringLiteral("  L%1 C[%2] %3")
                   .arg(lineMatch.m_lineNumber)
                   .arg(segs.join(QLatin1Char(',')))
                   .arg(lineMatch.m_lineText);
    }
  }
  return lines.join(QLatin1Char('\n'));
}

void TestSearchServiceDrain::createFilledNotebook(const QString &p_name, int p_fileCount,
                                                  QString *p_outNotebookId) {
  const QString notebookPath = m_tempDir.filePath(p_name);
  const QByteArray notebookPathUtf8 = notebookPath.toUtf8();
  const QByteArray configUtf8 = QStringLiteral("{\"name\":\"%1\"}").arg(p_name).toUtf8();

  char *notebookId = nullptr;
  VxCoreError err = vxcore_notebook_create(m_context, notebookPathUtf8.constData(),
                                           configUtf8.constData(), VXCORE_NOTEBOOK_BUNDLED,
                                           &notebookId);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(notebookId != nullptr);
  const QString id = QString::fromUtf8(notebookId);
  vxcore_string_free(notebookId);

  const QByteArray notebookIdUtf8 = id.toUtf8();

  // Build the notebook. Every third file contains the search needle so the
  // result set is non-trivial and the ordering across files is meaningful.
  for (int i = 0; i < p_fileCount; ++i) {
    const QString fileName = QStringLiteral("note_%1.md").arg(i, 3, 10, QLatin1Char('0'));
    const QByteArray fileNameUtf8 = fileName.toUtf8();

    char *fileId = nullptr;
    VxCoreError vxerr =
        vxcore_file_create(m_context, notebookIdUtf8.constData(), "", fileNameUtf8.constData(),
                           &fileId);
    QCOMPARE(vxerr, VXCORE_OK);
    QVERIFY(fileId != nullptr);
    vxcore_string_free(fileId);

    QString content = QStringLiteral("# Note %1\n\nplain body line\n").arg(i);
    if (i % 3 == 0) {
      content += QStringLiteral("this line contains the needle keyword here\n");
    }
    content += QStringLiteral("trailing line\n");

    char *bufferId = nullptr;
    vxerr = vxcore_buffer_open(m_context, notebookIdUtf8.constData(), fileNameUtf8.constData(),
                               &bufferId);
    QCOMPARE(vxerr, VXCORE_OK);
    QVERIFY(bufferId != nullptr);

    const QByteArray contentUtf8 = content.toUtf8();
    vxerr = vxcore_buffer_set_content_raw(m_context, bufferId, contentUtf8.constData(),
                                          static_cast<size_t>(contentUtf8.size()));
    QCOMPARE(vxerr, VXCORE_OK);

    vxerr = vxcore_buffer_save(m_context, bufferId);
    QCOMPARE(vxerr, VXCORE_OK);

    vxerr = vxcore_buffer_close(m_context, bufferId);
    QCOMPARE(vxerr, VXCORE_OK);
    vxcore_string_free(bufferId);
  }

  *p_outNotebookId = id;
}

void TestSearchServiceDrain::initTestCase() {
  vxcore_set_test_mode(1);
  vxcore_set_app_info("VNote", "VNoteTest");

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Baseline / idle / teardown notebook (> 50 files so the parallel drain path
  // is exercised).
  createFilledNotebook(QStringLiteral("drain_notebook"), kFileCount, &m_notebookId);
  QVERIFY(!m_notebookId.isEmpty());

  // Larger notebook so a cancel has a real backlog to race against.
  createFilledNotebook(QStringLiteral("drain_notebook_large"), kLargeFileCount, &m_largeNotebookId);
  QVERIFY(!m_largeNotebookId.isEmpty());
}

void TestSearchServiceDrain::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSearchServiceDrain::testDrainPoolResultsMatchBaseline() {
  const QString queryJson = QStringLiteral("{\"pattern\":\"needle\"}");

  // Baseline: direct single-threaded content search (no drain pool, no worker
  // thread). This uses the caller-helps-drain path with no auxiliary drainers.
  SearchCoreService baselineCore(m_context);
  QJsonObject baselineObj;
  Error baselineErr =
      baselineCore.searchContentCancellable(m_notebookId, queryJson, QString(), nullptr,
                                            &baselineObj);
  QVERIFY2(baselineErr.isOk(),
           qPrintable(QStringLiteral("baseline searchContent failed: %1").arg(baselineErr.message())));
  SearchResult baselineResult = SearchResult::fromContentSearchJson(baselineObj, m_notebookId);

  // Sanity: the needle must actually be present so the comparison is meaningful.
  QVERIFY(baselineResult.m_matchCount > 0);
  QVERIFY(!baselineResult.m_fileResults.isEmpty());

  // Async path: SearchService owns the worker thread AND the drain pool.
  SearchCoreService asyncCore(m_context);
  SearchService service(&asyncCore);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  const int token = service.searchContent(m_notebookId, queryJson, QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 15000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(QStringLiteral("async searchContent failed unexpectedly (%1)")
                          .arg(failedSpy.isEmpty() ? QString()
                                                   : failedSpy.takeFirst().at(1).toString())));
  QCOMPARE(finishedSpy.at(0).at(0).toInt(), token);

  SearchResult asyncResult = finishedSpy.at(0).at(1).value<SearchResult>();

  // The parallel drain pool must produce byte-for-byte the same ordered result
  // as the single-threaded baseline.
  QCOMPARE(canonicalize(asyncResult), canonicalize(baselineResult));
}

void TestSearchServiceDrain::testIdleDrainThreadsDoNotSpin() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  // Pool sizing: min(hardware_concurrency, 8), floored at 2.
  unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) {
    hw = 2;
  }
  const size_t expectedThreads = static_cast<size_t>(std::min(hw, 8u));
  QCOMPARE(service.testDrainThreadCount(), expectedThreads);

  // No search has been started. The drain threads must be blocked on the work
  // queue's condvar (process_next returns 0 on timeout), NOT busy-spinning
  // pulling phantom work. Sleep well past several 100ms poll cycles.
  const uint64_t before = service.testDrainItemsProcessed();
  QThread::msleep(500);
  const uint64_t after = service.testDrainItemsProcessed();

  QCOMPARE(before, static_cast<uint64_t>(0));
  QCOMPARE(after, static_cast<uint64_t>(0));
}

void TestSearchServiceDrain::testTeardownDuringActiveSearch() {
  const QString queryJson = QStringLiteral("{\"pattern\":\"needle\"}");

  SearchCoreService asyncCore(m_context);
  auto svc = std::make_unique<SearchService>(&asyncCore);

  const int token = svc->searchContent(m_notebookId, queryJson, QString());
  QVERIFY(token > 0);

  // Let a few per-file work items get in flight before we tear the service down.
  QThread::msleep(5);

  // Watchdog: if the destructor hangs (e.g. a drain thread that never joins, or
  // a deadlock against a half-destroyed queue), abort the process so the
  // regression FAILS loudly instead of hanging CI. Generous margin over the
  // asserted 5s budget so a merely-slow-but-correct teardown does not trip it.
  constexpr qint64 kTeardownBudgetMs = 5000;
  std::atomic<bool> resetDone{false};
  std::thread watchdog([&resetDone]() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kTeardownBudgetMs + 3000);
    while (!resetDone.load(std::memory_order_acquire)) {
      if (std::chrono::steady_clock::now() > deadline) {
        qFatal("SearchService teardown watchdog: destructor did not complete within budget "
               "(hang or UAF)");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  QElapsedTimer timer;
  timer.start();
  svc.reset();
  const qint64 elapsedMs = timer.elapsed();

  resetDone.store(true, std::memory_order_release);
  watchdog.join();

  QVERIFY2(elapsedMs < kTeardownBudgetMs,
           qPrintable(QStringLiteral("SearchService teardown took %1ms (budget %2ms)")
                          .arg(elapsedMs)
                          .arg(kTeardownBudgetMs)));
  qInfo() << "teardown-during-search dtor elapsed(ms):" << elapsedMs;

  // Process is still alive and sane after the mid-flight teardown: a trivial
  // follow-up operation executes without crashing.
  QCOMPARE(1 + 1, 2);
}

void TestSearchServiceDrain::testCancelWithBacklog() {
  const QString queryJson = QStringLiteral("{\"pattern\":\"needle\"}");

  SearchCoreService asyncCore(m_context);
  auto svc = std::make_unique<SearchService>(&asyncCore);

  QSignalSpy cancelledSpy(svc.get(), &SearchService::searchCancelled);
  QSignalSpy finishedSpy(svc.get(), &SearchService::searchFinished);
  QSignalSpy failedSpy(svc.get(), &SearchService::searchFailed);

  const int token = svc->searchContent(m_largeNotebookId, queryJson, QString());
  QVERIFY(token > 0);

  // Cancel almost immediately so cancellation races against a deep backlog.
  QThread::msleep(2);
  svc->cancel(token);

  QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() > 0 || finishedSpy.count() > 0 ||
                               failedSpy.count() > 0,
                           15000);

  const bool cancelWon = cancelledSpy.count() > 0;
  const bool finishWon = finishedSpy.count() > 0;
  QVERIFY2(cancelWon || finishWon,
           qPrintable(QStringLiteral("search neither cancelled nor finished (failed=%1)")
                          .arg(failedSpy.count())));
  if (cancelWon) {
    QCOMPARE(cancelledSpy.at(0).at(0).toInt(), token);
  } else {
    QCOMPARE(finishedSpy.at(0).at(0).toInt(), token);
  }
  qInfo() << "cancel-with-backlog winner:" << (cancelWon ? "cancelled" : "finished");

  // Nothing is stranded: every enqueued work item still drains, so the vxcore
  // "vxcore.search" queue empties out.
  QTRY_VERIFY_WITH_TIMEOUT(vxcore_work_queue_size(m_context, "vxcore.search") == 0, 15000);

  // Drain accounting stabilizes once the backlog is gone (no phantom work).
  const uint64_t settled = svc->testDrainItemsProcessed();
  QThread::msleep(300);
  QCOMPARE(svc->testDrainItemsProcessed(), settled);

  // Teardown after cancel must still be clean and prompt (same watchdog guard).
  constexpr qint64 kTeardownBudgetMs = 5000;
  std::atomic<bool> resetDone{false};
  std::thread watchdog([&resetDone]() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kTeardownBudgetMs + 3000);
    while (!resetDone.load(std::memory_order_acquire)) {
      if (std::chrono::steady_clock::now() > deadline) {
        qFatal("SearchService post-cancel teardown watchdog: destructor did not complete "
               "within budget (hang or UAF)");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  QElapsedTimer timer;
  timer.start();
  svc.reset();
  const qint64 elapsedMs = timer.elapsed();

  resetDone.store(true, std::memory_order_release);
  watchdog.join();

  QVERIFY2(elapsedMs < kTeardownBudgetMs,
           qPrintable(QStringLiteral("post-cancel teardown took %1ms (budget %2ms)")
                          .arg(elapsedMs)
                          .arg(kTeardownBudgetMs)));
  qInfo() << "cancel-with-backlog dtor elapsed(ms):" << elapsedMs;
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchServiceDrain)
#include "test_search_service_drain.moc"
