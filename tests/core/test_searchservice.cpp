// test_searchservice.cpp - Tests for vnotex::SearchService
#include <QtTest>

#include <QJsonObject>

#include <core/searchresulttypes.h>
#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

// Order-preserving canonical serialization of a content SearchResult. Both the blob baseline
// and the streaming finished payload are produced in input-file order, so a direct string
// compare of this canonical form is a full byte-for-byte structural equality check (paths, ids,
// per-file match count, per-line number/text, and every match segment's column span).
static QString canonicalizeContentResult(const SearchResult &p_result) {
  QString out;
  out += QStringLiteral("matchCount=%1;truncated=%2|")
             .arg(p_result.m_matchCount)
             .arg(p_result.m_truncated ? 1 : 0);
  for (const SearchFileResult &fr : p_result.m_fileResults) {
    out += QStringLiteral("[path=%1;id=%2;fmc=%3;")
               .arg(fr.m_path, fr.m_id)
               .arg(fr.m_matchCount);
    for (const SearchLineMatch &lm : fr.m_lineMatches) {
      out += QStringLiteral("(ln=%1;txt=%2;").arg(lm.m_lineNumber).arg(lm.m_lineText);
      for (const SearchMatchSegment &seg : lm.m_segments) {
        out += QStringLiteral("<%1,%2>").arg(seg.m_columnStart).arg(seg.m_columnEnd);
      }
      out += QStringLiteral(")");
    }
    out += QStringLiteral("]");
  }
  return out;
}

class TestSearchService : public QObject {
  Q_OBJECT

public:
  TestSearchService() = default;

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Test null context
  void testNullContext();

  // Test null results parameter
  void testNullResultsParameter();

  // Test searchFiles with empty notebook
  void testSearchFilesEmptyNotebook();

  // Test searchContent with empty notebook
  void testSearchContentEmptyNotebook();

  // Test searchByTags with empty notebook
  void testSearchByTagsEmptyNotebook();

  // Test searchFiles with files
  void testSearchFilesWithFiles();

  // Test searchContent with content
  void testSearchContentWithContent();

  // Test searchByTags with tags
  void testSearchByTagsWithTags();

  // Test async wrapper initial searching state
  void testIsSearchingInitiallyFalse();

  // Test async wrapper file search finished signal
  void testSearchFilesAsync();

  // Test async wrapper cancellation
  void testCancelSearch();

  // Test async wrapper started signal
  void testSearchStartedSignal();

  // Test async wrapper token uniqueness
  void testTokenUniqueness();

  // Test finished signal token matches returned token
  void testSignalTokenMatching();

  // Test per-token cancellation behavior
  void testIndividualCancellation();

  // Test cancel all in-flight searches
  void testCancelAll();

  // Test per-token searching state
  void testIsSearchingPerToken();

  // Test safe destruction with in-flight search
  void testDestructionSafety();

  // Test streaming content search: incremental batch union equals the
  // authoritative finished result.
  void testSearchContentStreamingBatchUnion();

  // Test that the streaming async searchFinished payload is byte-for-byte equal to the blob
  // searchContentCancellable baseline, including a maxResults cap that cuts inside a file.
  void testStreamingFinishedMatchesBlobBaselineWithCap();

  // Test that a query OMITTING "maxResults" applies vxcore's default cap of 100 on the async
  // streaming path, matching the blob baseline (absent key != uncapped).
  void testStreamingFinishedMatchesBlobBaselineDefaultCap();

private:
  VxCoreContextHandle m_context = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestSearchService::initTestCase() {
  // Enable test mode for vxcore
  vxcore_set_test_mode(1);
  vxcore_set_app_info("VNote", "VNoteTest");

  // Create context
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create a test notebook
  QString notebookPath = m_tempDir.filePath("test_notebook");
  QByteArray notebookPathUtf8 = notebookPath.toUtf8();

  char *notebookId = nullptr;
  err = vxcore_notebook_create(m_context, notebookPathUtf8.constData(),
                               "{\"name\":\"TestNotebook\"}", VXCORE_NOTEBOOK_BUNDLED, &notebookId);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(notebookId != nullptr);
  m_notebookId = QString::fromUtf8(notebookId);
  vxcore_string_free(notebookId);
}

void TestSearchService::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSearchService::testNullContext() {
  SearchCoreService service(nullptr);
  QJsonArray results;

  Error err = service.searchFiles(m_notebookId, "{}", QString(), &results);
  QVERIFY(!err.isOk());
  QCOMPARE(err.code(), ErrorCode::InvalidArgument);
}

void TestSearchService::testNullResultsParameter() {
  SearchCoreService service(m_context);

  Error err = service.searchFiles(m_notebookId, "{}", QString(), nullptr);
  QVERIFY(!err.isOk());
  QCOMPARE(err.code(), ErrorCode::InvalidArgument);
}

void TestSearchService::testSearchFilesEmptyNotebook() {
  SearchCoreService service(m_context);
  QJsonArray results;

  // Search with empty pattern should return empty results
  Error err = service.searchFiles(m_notebookId, "{\"pattern\":\"*.txt\"}", QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchFiles failed: %1").arg(err.message())));
  QVERIFY(results.isEmpty());
}

void TestSearchService::testSearchContentEmptyNotebook() {
  SearchCoreService service(m_context);
  QJsonArray results;

  Error err = service.searchContent(m_notebookId, "{\"pattern\":\"test\"}", QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchContent failed: %1").arg(err.message())));
  QVERIFY(results.isEmpty());
}

void TestSearchService::testSearchByTagsEmptyNotebook() {
  SearchCoreService service(m_context);
  QJsonArray results;

  Error err = service.searchByTags(m_notebookId, "{\"tags\":[\"test\"]}", QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchByTags failed: %1").arg(err.message())));
  QVERIFY(results.isEmpty());
}

void TestSearchService::testSearchFilesWithFiles() {
  // Create a file in the notebook
  char *fileId = nullptr;
  VxCoreError vxerr =
      vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "", "test_file.md", &fileId);
  QCOMPARE(vxerr, VXCORE_OK);
  QVERIFY(fileId != nullptr);
  vxcore_string_free(fileId);

  SearchCoreService service(m_context);
  QJsonArray results;

  // Search for markdown files
  Error err = service.searchFiles(m_notebookId, "{\"pattern\":\"*.md\"}", QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchFiles failed: %1").arg(err.message())));
  QCOMPARE(results.size(), 1);

  // Verify result structure
  QJsonObject result = results.first().toObject();
  QVERIFY(result.contains("path"));
  QVERIFY(result.contains("id"));
}

void TestSearchService::testSearchContentWithContent() {
  SearchCoreService service(m_context);
  QJsonArray results;

  // Search for content (may be empty if no indexing)
  Error err = service.searchContent(m_notebookId, "{\"pattern\":\"test\"}", QString(), &results);

  // searchContent may or may not find results; just verify the call doesn't crash
  QVERIFY2(err.isOk(), qPrintable(QString("searchContent failed: %1").arg(err.message())));
}

void TestSearchService::testSearchByTagsWithTags() {
  // Create a file
  char *fileId = nullptr;
  VxCoreError vxerr = vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "",
                                         "tagged_file.md", &fileId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxcore_string_free(fileId);

  // Create a tag
  vxerr = vxcore_tag_create(m_context, m_notebookId.toUtf8().constData(), "test-tag");
  QCOMPARE(vxerr, VXCORE_OK);

  // Tag the file
  vxerr =
      vxcore_file_tag(m_context, m_notebookId.toUtf8().constData(), "tagged_file.md", "test-tag");
  QCOMPARE(vxerr, VXCORE_OK);

  SearchCoreService service(m_context);
  QJsonArray results;

  // Search by tag
  Error err = service.searchByTags(m_notebookId, "{\"tags\":[\"test-tag\"]}", QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchByTags failed: %1").arg(err.message())));
  // Note: May need to rebuild cache or wait for indexing
}

void TestSearchService::testIsSearchingInitiallyFalse() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QCOMPARE(service.isSearching(), false);
}

void TestSearchService::testSearchFilesAsync() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  int token =
      service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 5000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(
               QString("searchFiles async failed unexpectedly (%1)")
                   .arg(failedSpy.isEmpty() ? QString() : failedSpy.takeFirst().at(1).toString())));
  QCOMPARE(finishedSpy.at(0).at(0).toInt(), token);
}

void TestSearchService::testCancelSearch() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy cancelledSpy(&service, &SearchService::searchCancelled);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  int token =
      service.searchContent(m_notebookId, QStringLiteral("{\"pattern\":\"test\"}"), QString());
  QVERIFY(token > 0);
  service.cancel();

  QTRY_VERIFY_WITH_TIMEOUT(
      finishedSpy.count() > 0 || cancelledSpy.count() > 0 || failedSpy.count() > 0, 5000);
  QVERIFY(finishedSpy.count() > 0 || cancelledSpy.count() > 0);
}

void TestSearchService::testSearchStartedSignal() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy startedSpy(&service, &SearchService::searchStarted);

  int token =
      service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 3000);
  QCOMPARE(startedSpy.at(0).at(0).toInt(), token);
}

void TestSearchService::testTokenUniqueness() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  int t1 = service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  int t2 = service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.txt\"}"), QString());

  QVERIFY(t1 > 0);
  QVERIFY(t2 > 0);
  QVERIFY(t1 != t2);

  QTRY_VERIFY_WITH_TIMEOUT(!service.isSearching(), 5000);
}

void TestSearchService::testSignalTokenMatching() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  int token =
      service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 5000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(
               QString("search failed unexpectedly (%1)")
                   .arg(failedSpy.isEmpty() ? QString() : failedSpy.takeFirst().at(1).toString())));
  QCOMPARE(finishedSpy.at(0).at(0).toInt(), token);
}

void TestSearchService::testIndividualCancellation() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy cancelledSpy(&service, &SearchService::searchCancelled);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  int tokenA =
      service.searchContent(m_notebookId, QStringLiteral("{\"pattern\":\"token-a\"}"), QString());
  int tokenB =
      service.searchContent(m_notebookId, QStringLiteral("{\"pattern\":\"token-b\"}"), QString());
  QVERIFY(tokenA > 0);
  QVERIFY(tokenB > 0);
  QVERIFY(tokenA != tokenB);

  service.cancel(tokenA);

  auto hasTokenSignal = [](const QSignalSpy &p_spy, int p_token) {
    for (const auto &args : p_spy) {
      if (!args.isEmpty() && args.at(0).toInt() == p_token) {
        return true;
      }
    }
    return false;
  };

  QTRY_VERIFY_WITH_TIMEOUT(
      (hasTokenSignal(finishedSpy, tokenA) || hasTokenSignal(cancelledSpy, tokenA) ||
       hasTokenSignal(failedSpy, tokenA)) &&
          (hasTokenSignal(finishedSpy, tokenB) || hasTokenSignal(cancelledSpy, tokenB) ||
           hasTokenSignal(failedSpy, tokenB)),
      5000);

  const bool tokenAFinished = hasTokenSignal(finishedSpy, tokenA);
  const bool tokenACancelled = hasTokenSignal(cancelledSpy, tokenA);
  QVERIFY(tokenAFinished || tokenACancelled);
  QVERIFY(!hasTokenSignal(failedSpy, tokenA));

  QVERIFY(hasTokenSignal(finishedSpy, tokenB));
  QVERIFY(!hasTokenSignal(cancelledSpy, tokenB));
  QVERIFY(!hasTokenSignal(failedSpy, tokenB));

  SearchResult tokenBResult;
  bool foundTokenBResult = false;
  for (const auto &args : finishedSpy) {
    if (args.size() >= 2 && args.at(0).toInt() == tokenB) {
      tokenBResult = args.at(1).value<SearchResult>();
      foundTokenBResult = true;
      break;
    }
  }
  QVERIFY(foundTokenBResult);
  QVERIFY(tokenBResult.m_matchCount >= 0);
}

void TestSearchService::testCancelAll() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy cancelledSpy(&service, &SearchService::searchCancelled);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  int token1 =
      service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  int token2 = service.searchContent(m_notebookId, QStringLiteral("{\"pattern\":\"cancel-all\"}"),
                                     QString());
  int token3 =
      service.searchByTags(m_notebookId, QStringLiteral("{\"tags\":[\"cancel-all\"]}"), QString());

  QVERIFY(token1 > 0);
  QVERIFY(token2 > 0);
  QVERIFY(token3 > 0);

  service.cancel();

  QSet<int> tokens{token1, token2, token3};
  auto resolvedCount = [&]() {
    int count = 0;
    for (int token : tokens) {
      bool resolved = false;
      for (const auto &args : finishedSpy) {
        if (!args.isEmpty() && args.at(0).toInt() == token) {
          resolved = true;
          break;
        }
      }
      if (!resolved) {
        for (const auto &args : cancelledSpy) {
          if (!args.isEmpty() && args.at(0).toInt() == token) {
            resolved = true;
            break;
          }
        }
      }
      if (!resolved) {
        for (const auto &args : failedSpy) {
          if (!args.isEmpty() && args.at(0).toInt() == token) {
            resolved = true;
            break;
          }
        }
      }
      if (resolved) {
        ++count;
      }
    }
    return count;
  };

  QTRY_VERIFY_WITH_TIMEOUT(resolvedCount() >= 3, 5000);
}

void TestSearchService::testIsSearchingPerToken() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  int token =
      service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(service.isSearching() && service.isSearching(token), 2000);
  QTRY_VERIFY_WITH_TIMEOUT(!service.isSearching(token), 5000);
  QVERIFY(!service.isSearching());
}

void TestSearchService::testDestructionSafety() {
  SearchCoreService coreService(m_context);

  {
    SearchService service(&coreService);
    service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());
  }

  QVERIFY(true);
}

void TestSearchService::testSearchContentStreamingBatchUnion() {
  // Seed a file with distinctive content so streaming content search yields a
  // non-empty result set through the async batch path.
  char *fileId = nullptr;
  VxCoreError vxerr = vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "",
                                         "streaming_note.md", &fileId);
  QCOMPARE(vxerr, VXCORE_OK);
  QVERIFY(fileId != nullptr);
  vxcore_string_free(fileId);

  char *bufferId = nullptr;
  vxerr = vxcore_buffer_open(m_context, m_notebookId.toUtf8().constData(), "streaming_note.md",
                             &bufferId);
  QCOMPARE(vxerr, VXCORE_OK);
  QVERIFY(bufferId != nullptr);

  const QByteArray contentUtf8 =
      QByteArrayLiteral("# Streaming\n\nthis line has streamneedle inside\nanother streamneedle "
                        "line here\n");
  vxerr = vxcore_buffer_set_content_raw(m_context, bufferId, contentUtf8.constData(),
                                        static_cast<size_t>(contentUtf8.size()));
  QCOMPARE(vxerr, VXCORE_OK);
  vxerr = vxcore_buffer_save(m_context, bufferId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxerr = vxcore_buffer_close(m_context, bufferId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxcore_string_free(bufferId);

  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy batchSpy(&service, &SearchService::searchBatch);
  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  const int token =
      service.searchContent(m_notebookId, QStringLiteral("{\"pattern\":\"streamneedle\"}"),
                            QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 10000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(QString("streaming searchContent failed unexpectedly (%1)")
                          .arg(failedSpy.isEmpty() ? QString()
                                                   : failedSpy.takeFirst().at(1).toString())));
  QCOMPARE(finishedSpy.at(0).at(0).toInt(), token);

  const SearchResult finishedResult = finishedSpy.at(0).at(1).value<SearchResult>();
  QVERIFY(finishedResult.m_matchCount > 0);
  QVERIFY(!finishedResult.m_fileResults.isEmpty());

  // Small notebook (single matching file) => exactly one non-empty batch, whose
  // contents equal the authoritative finished result.
  QCOMPARE(batchSpy.count(), 1);
  int streamedMatchCount = 0;
  int streamedFiles = 0;
  for (const auto &args : batchSpy) {
    QCOMPARE(args.at(0).toInt(), token);
    const SearchResult batchResult = args.at(1).value<SearchResult>();
    streamedMatchCount += batchResult.m_matchCount;
    streamedFiles += batchResult.m_fileResults.size();
  }
  QCOMPARE(streamedMatchCount, finishedResult.m_matchCount);
  QCOMPARE(streamedFiles, finishedResult.m_fileResults.size());
}

void TestSearchService::testStreamingFinishedMatchesBlobBaselineWithCap() {
  // Seed two files. capfile_a.md has THREE occurrences on three distinct lines; capfile_b.md
  // has more. With maxResults=2 the deterministic file-boundary cap lands INSIDE a file
  // (keeps the first two occurrences, drops the third line and every later file), exactly the
  // case the async streaming worker must reproduce byte-for-byte from the blob baseline.
  auto seedFile = [&](const char *p_name, const QByteArray &p_content) {
    char *fileId = nullptr;
    VxCoreError e = vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "", p_name,
                                       &fileId);
    QCOMPARE(e, VXCORE_OK);
    vxcore_string_free(fileId);

    char *bufferId = nullptr;
    e = vxcore_buffer_open(m_context, m_notebookId.toUtf8().constData(), p_name, &bufferId);
    QCOMPARE(e, VXCORE_OK);
    e = vxcore_buffer_set_content_raw(m_context, bufferId, p_content.constData(),
                                      static_cast<size_t>(p_content.size()));
    QCOMPARE(e, VXCORE_OK);
    e = vxcore_buffer_save(m_context, bufferId);
    QCOMPARE(e, VXCORE_OK);
    e = vxcore_buffer_close(m_context, bufferId);
    QCOMPARE(e, VXCORE_OK);
    vxcore_string_free(bufferId);
  };

  seedFile("capfile_a.md",
           QByteArrayLiteral("capneedle one\nfiller\ncapneedle two\ncapneedle three\n"));
  seedFile("capfile_b.md", QByteArrayLiteral("capneedle b1\ncapneedle b2\n"));

  const QString queryJson =
      QStringLiteral("{\"pattern\":\"capneedle\",\"maxResults\":2}");

  // Blob baseline: authoritative capped result the async path must match exactly.
  SearchCoreService coreService(m_context);
  QJsonObject blobObj;
  const Error blobErr =
      coreService.searchContentCancellable(m_notebookId, queryJson, QString(), nullptr, &blobObj);
  QVERIFY(!blobErr);
  const SearchResult blobResult = SearchResult::fromContentSearchJson(blobObj, m_notebookId);
  // Precondition: the cap actually truncated (otherwise the test would not exercise the path).
  QVERIFY2(blobResult.m_truncated, "expected blob baseline to be truncated by maxResults=2");

  // Streaming async path.
  SearchService service(&coreService);
  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  const int token = service.searchContent(m_notebookId, queryJson, QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 10000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(QString("streaming searchContent failed unexpectedly (%1)")
                          .arg(failedSpy.isEmpty() ? QString()
                                                   : failedSpy.takeFirst().at(1).toString())));

  const SearchResult streamedResult = finishedSpy.at(0).at(1).value<SearchResult>();

  // The async streaming finished payload must honor the same cap and be structurally identical
  // to the blob baseline: same truncated flag, same file/line/segment shape, same order.
  QVERIFY2(streamedResult.m_truncated,
           "streaming finished result dropped the maxResults truncation indicator");
  QCOMPARE(streamedResult.m_matchCount, blobResult.m_matchCount);
  QCOMPARE(canonicalizeContentResult(streamedResult), canonicalizeContentResult(blobResult));
}

void TestSearchService::testStreamingFinishedMatchesBlobBaselineDefaultCap() {
  // Seed a single file with 150 occurrences (one per line). vxcore's SearchContentQuery defaults
  // maxResults to 100 when the key is ABSENT, so a query that omits "maxResults" must still cap
  // at 100 and report truncated=true. This guards the async streaming worker against treating a
  // missing key as "uncapped" (which would diverge from the blob baseline).
  QByteArray content;
  for (int i = 0; i < 150; ++i) {
    content += QByteArrayLiteral("defcapneedle line\n");
  }

  char *fileId = nullptr;
  VxCoreError vxerr =
      vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "", "defcap.md", &fileId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxcore_string_free(fileId);

  char *bufferId = nullptr;
  vxerr = vxcore_buffer_open(m_context, m_notebookId.toUtf8().constData(), "defcap.md", &bufferId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxerr = vxcore_buffer_set_content_raw(m_context, bufferId, content.constData(),
                                        static_cast<size_t>(content.size()));
  QCOMPARE(vxerr, VXCORE_OK);
  vxerr = vxcore_buffer_save(m_context, bufferId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxerr = vxcore_buffer_close(m_context, bufferId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxcore_string_free(bufferId);

  // Query deliberately OMITS "maxResults".
  const QString queryJson = QStringLiteral("{\"pattern\":\"defcapneedle\"}");

  SearchCoreService coreService(m_context);
  QJsonObject blobObj;
  const Error blobErr =
      coreService.searchContentCancellable(m_notebookId, queryJson, QString(), nullptr, &blobObj);
  QVERIFY(!blobErr);
  const SearchResult blobResult = SearchResult::fromContentSearchJson(blobObj, m_notebookId);
  // Precondition: the implicit default cap of 100 actually truncated the 150 occurrences.
  QVERIFY2(blobResult.m_truncated,
           "expected blob baseline to be truncated by the implicit default maxResults=100");

  SearchService service(&coreService);
  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  const int token = service.searchContent(m_notebookId, queryJson, QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 10000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(QString("streaming searchContent failed unexpectedly (%1)")
                          .arg(failedSpy.isEmpty() ? QString()
                                                   : failedSpy.takeFirst().at(1).toString())));

  const SearchResult streamedResult = finishedSpy.at(0).at(1).value<SearchResult>();
  QVERIFY2(streamedResult.m_truncated,
           "streaming finished result ignored the implicit default maxResults=100 cap");
  QCOMPARE(streamedResult.m_matchCount, blobResult.m_matchCount);
  QCOMPARE(canonicalizeContentResult(streamedResult), canonicalizeContentResult(blobResult));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchService)
#include "test_searchservice.moc"
