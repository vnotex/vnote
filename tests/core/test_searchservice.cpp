// test_searchservice.cpp - Tests for vnotex::SearchService
#include <QtTest>

#include <memory>

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
    out += QStringLiteral("[path=%1;id=%2;fmc=%3;").arg(fr.m_path, fr.m_id).arg(fr.m_matchCount);
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

  void testSimpleSearchUnicodeReplacement();
  void testReplacementCapabilityUsesBackendOrder();
  void testReplacementPreservesSourceSlices();
  void testReplacementLiteralAndDeletion();
  void testReplacementNoOps();
  void testReplacementZeroLengthAndBoundaries();
  void testReplacementInvalidRanges_data();
  void testReplacementInvalidRanges();
  void testReplacementRejectsStaleSnapshots();
  void testReplacementRejectsOverlaps();
  void testReplacementOutputPointers();

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
  QCOMPARE(vxcore_context_update_config(m_context, "{\"search\":{\"backends\":[\"simple\"]}}"),
           VXCORE_OK);

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
  QVERIFY(!service.isReplacementSupported());
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

void TestSearchService::testSimpleSearchUnicodeReplacement() {
  const QString source = QStringLiteral("\u4f60\u597d \U0001f600 foo foo\r\n");
  char *fileId = nullptr;
  QCOMPARE(
      vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "", "unicode.md", &fileId),
      VXCORE_OK);
  vxcore_string_free(fileId);

  char *bufferId = nullptr;
  QCOMPARE(
      vxcore_buffer_open(m_context, m_notebookId.toUtf8().constData(), "unicode.md", &bufferId),
      VXCORE_OK);
  const QByteArray bytes = source.toUtf8();
  QCOMPARE(vxcore_buffer_set_content_raw(m_context, bufferId, bytes.constData(),
                                         static_cast<size_t>(bytes.size())),
           VXCORE_OK);
  QCOMPARE(vxcore_buffer_save(m_context, bufferId), VXCORE_OK);
  QCOMPARE(vxcore_buffer_close(m_context, bufferId), VXCORE_OK);
  vxcore_string_free(bufferId);

  SearchCoreService coreService(m_context);
  SearchService service(&coreService);
  QVERIFY(service.isReplacementSupported());
  QSignalSpy batchSpy(&service, &SearchService::searchBatch);
  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);
  const QString query =
      QStringLiteral("{\"pattern\":\"foo\",\"caseSensitive\":true,\"maxResults\":0}");
  const QString input = QStringLiteral("{\"files\":[\"unicode.md\"]}");
  const int token = service.searchContent(m_notebookId, query, input);
  QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty() || !failedSpy.isEmpty(), 10000);
  QVERIFY(failedSpy.isEmpty());
  QCOMPARE(finishedSpy.size(), 1);
  QCOMPARE(finishedSpy.first().at(0).toInt(), token);
  const SearchResult result = finishedSpy.first().at(1).value<SearchResult>();
  QCOMPARE(result.m_fileResults.size(), 1);
  const SearchFileResult &target = result.m_fileResults.first();
  QVERIFY(target.m_replacementSupported);
  QString output;
  QString error;
  int count = -1;
  QVERIFY(SearchService::buildReplacement(source, target.m_lineMatches, QStringLiteral("bar"),
                                          &output, &count, &error));
  QCOMPARE(output, QStringLiteral("\u4f60\u597d \U0001f600 bar bar\r\n"));
  QCOMPARE(count, 2);
  QVERIFY(error.isEmpty());

  QCOMPARE(batchSpy.size(), 1);
  const SearchResult preview = batchSpy.first().at(1).value<SearchResult>();
  QCOMPARE(preview.m_fileResults.size(), 1);
  QVERIFY(!preview.m_fileResults.first().m_replacementSupported);

  QJsonObject json;
  QVERIFY(coreService.searchContentCancellable(m_notebookId, query, input, nullptr, &json).isOk());
  const SearchResult parsed = SearchResult::fromContentSearchJson(json, m_notebookId);
  QCOMPARE(parsed.m_fileResults.size(), 1);
  QVERIFY(!parsed.m_fileResults.first().m_replacementSupported);
}

void TestSearchService::testReplacementCapabilityUsesBackendOrder() {
  vxcore_set_test_mode(1);
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &context), VXCORE_OK);
  const std::unique_ptr<VxCoreContext, decltype(&vxcore_context_destroy)> ownedContext(
      context, vxcore_context_destroy);
  SearchCoreService coreService(context);
  SearchService service(&coreService);
  const struct {
    const char *m_config;
    bool m_supported;
  } cases[] = {
      {"{\"search\":{\"backends\":[\"simple\",\"rg\"]}}", true},
      {"{\"search\":{\"backends\":[\"rg\",\"simple\"]}}", false},
      {"{\"search\":{\"backends\":[\"unknown\",\"simple\",\"rg\"]}}", true},
      {"{\"search\":{\"backends\":[\"unknown\",\"rg\",\"simple\"]}}", false},
      {"{\"search\":{\"backends\":[]}}", true},
      {"{\"search\":{\"backends\":[\"unknown\"]}}", true},
  };
  // Configuration inspection only: no search is dispatched and rg is never executed.
  for (const auto &entry : cases) {
    QCOMPARE(vxcore_context_update_config(context, entry.m_config), VXCORE_OK);
    QCOMPARE(coreService.isReplacementSupported(), entry.m_supported);
    QCOMPARE(service.isReplacementSupported(), entry.m_supported);
  }
}

void TestSearchService::testReplacementPreservesSourceSlices() {
  const QString source = QStringLiteral("foo foo\r\nkeep\r\nfoo");
  const SearchLineMatch first{1, QStringLiteral("foo foo"), {{4, 7}, {0, 3}}};
  const SearchLineMatch last{3, QStringLiteral("foo"), {{0, 3}}};
  // Both line order and segment order may vary; duplicate lines/ranges apply only once.
  const QVector<SearchLineMatch> matches{last, first, first};
  QString output;
  QString error;
  int count = -1;
  QVERIFY(SearchService::buildReplacement(source, matches, QStringLiteral("bar"), &output, &count,
                                          &error));
  QCOMPARE(output, QStringLiteral("bar bar\r\nkeep\r\nbar"));
  QCOMPARE(count, 3);
  QVERIFY(error.isEmpty());

  const QVector<SearchLineMatch> onlyLast{last};
  QVERIFY(SearchService::buildReplacement(QStringLiteral("foo foo\r\ndraft\r\nfoo"), onlyLast,
                                          QStringLiteral("bar"), &output, &count, &error));
  QCOMPARE(output, QStringLiteral("foo foo\r\ndraft\r\nbar"));
  QCOMPARE(count, 1);
  QVERIFY(error.isEmpty());

  const QVector<SearchLineMatch> trailingCr{{1, QStringLiteral("foo"), {{0, 3}}}};
  QVERIFY(SearchService::buildReplacement(QStringLiteral("foo\r"), trailingCr,
                                          QStringLiteral("bar"), &output, &count, &error));
  QCOMPARE(output, QStringLiteral("bar\r"));
  QCOMPARE(count, 1);
}

void TestSearchService::testReplacementLiteralAndDeletion() {
  const QString source = QStringLiteral(" foo foo \r\n foo ");
  const QVector<SearchLineMatch> matches{{1, QStringLiteral(" foo foo "), {{1, 4}}},
                                         {2, QStringLiteral(" foo "), {{1, 4}}}};
  QString output;
  QString error;
  int count = -1;
  QVERIFY(SearchService::buildReplacement(source, matches, QString(), &output, &count, &error));
  QCOMPARE(output, QStringLiteral("  foo \r\n  "));
  QCOMPARE(count, 2);
  QVERIFY(error.isEmpty());

  // Inserted text contains both capture-like escapes and the original search text. None of it
  // is expanded or scanned again; leading/trailing replacement whitespace is also literal.
  QVERIFY(SearchService::buildReplacement(source, matches, QStringLiteral(" $1\\1 foo "), &output,
                                          &count, &error));
  QCOMPARE(output, QStringLiteral("  $1\\1 foo  foo \r\n  $1\\1 foo  "));
  QCOMPARE(count, 2);
  QVERIFY(error.isEmpty());
}

void TestSearchService::testReplacementNoOps() {
  const QString source = QStringLiteral("foo bar\r\n");
  QString output = QStringLiteral("old output");
  QString error = QStringLiteral("old error");
  int count = -1;
  QVERIFY(
      SearchService::buildReplacement(source, {}, QStringLiteral("bar"), &output, &count, &error));
  QCOMPARE(output, source);
  QCOMPARE(count, 0);
  QVERIFY(error.isEmpty());

  const QVector<SearchLineMatch> same{{1, QStringLiteral("foo bar"), {{0, 3}}}};
  QVERIFY(SearchService::buildReplacement(source, same, QStringLiteral("foo"), &output, &count,
                                          &error));
  QCOMPARE(output, source);
  QCOMPARE(count, 0);
  QVERIFY(error.isEmpty());

  const QVector<SearchLineMatch> mixed{{1, QStringLiteral("foo bar"), {{0, 3}, {4, 7}}}};
  QVERIFY(SearchService::buildReplacement(source, mixed, QStringLiteral("foo"), &output, &count,
                                          &error));
  QCOMPARE(output, QStringLiteral("foo foo\r\n"));
  QCOMPARE(count, 1);

  const QVector<SearchLineMatch> insertion{{1, QStringLiteral("foo bar"), {{3, 3}}}};
  QVERIFY(SearchService::buildReplacement(source, insertion, QString(), &output, &count, &error));
  QCOMPARE(output, source);
  QCOMPARE(count, 0);
}

void TestSearchService::testReplacementZeroLengthAndBoundaries() {
  QString output;
  QString error;
  int count = -1;
  const SearchLineMatch insertion{1, QStringLiteral("ab"), {{1, 1}, {1, 1}}};
  QVERIFY(SearchService::buildReplacement(QStringLiteral("ab\r\n"), {insertion, insertion},
                                          QStringLiteral("x"), &output, &count, &error));
  QCOMPARE(output, QStringLiteral("axb\r\n"));
  QCOMPARE(count, 1);

  const QVector<SearchLineMatch> boundaries{{1, QStringLiteral("ab"), {{2, 2}, {0, 2}, {0, 0}}}};
  QVERIFY(SearchService::buildReplacement(QStringLiteral("ab"), boundaries, QStringLiteral("x"),
                                          &output, &count, &error));
  QCOMPARE(output, QStringLiteral("xxx"));
  QCOMPARE(count, 3);

  const QVector<SearchLineMatch> emptyLine{{1, QString(), {{0, 0}}}};
  QVERIFY(SearchService::buildReplacement(QString(), emptyLine, QStringLiteral("x"), &output,
                                          &count, &error));
  QCOMPARE(output, QStringLiteral("x"));
  QCOMPARE(count, 1);

  const QVector<SearchLineMatch> finalLine{{2, QString(), {{0, 0}}}};
  QVERIFY(SearchService::buildReplacement(QStringLiteral("a\n"), finalLine, QStringLiteral("x"),
                                          &output, &count, &error));
  QCOMPARE(output, QStringLiteral("a\nx"));
  QCOMPARE(count, 1);

  const QString unicode = QStringLiteral("a\U0001f600b");
  const QVector<SearchLineMatch> wholeSurrogate{{1, unicode, {{1, 3}}}};
  QVERIFY(SearchService::buildReplacement(unicode, wholeSurrogate, QStringLiteral("x"), &output,
                                          &count, &error));
  QCOMPARE(output, QStringLiteral("axb"));
  QCOMPARE(count, 1);
  QVERIFY(error.isEmpty());
}

void TestSearchService::testReplacementInvalidRanges_data() {
  QTest::addColumn<int>("lineNumber");
  QTest::addColumn<int>("start");
  QTest::addColumn<int>("end");
  QTest::newRow("negative-column") << 1 << -1 << 1;
  QTest::newRow("reversed-range") << 1 << 3 << 2;
  QTest::newRow("past-line-end") << 1 << 0 << 5;
  QTest::newRow("surrogate-start") << 1 << 2 << 3;
  QTest::newRow("surrogate-end") << 1 << 1 << 2;
  QTest::newRow("surrogate-insertion") << 1 << 2 << 2;
  QTest::newRow("zero-line-number") << 0 << 0 << 1;
  QTest::newRow("negative-line-number") << -1 << 0 << 1;
  QTest::newRow("missing-line") << 2 << 0 << 1;
}

void TestSearchService::testReplacementInvalidRanges() {
  QFETCH(int, lineNumber);
  QFETCH(int, start);
  QFETCH(int, end);
  const QString source = QStringLiteral("a\U0001f600b");
  const QVector<SearchLineMatch> matches{{lineNumber, source, {{start, end}}}};
  QString output = QStringLiteral("old output");
  QString error;
  int count = -1;
  QVERIFY(!SearchService::buildReplacement(source, matches, QStringLiteral("x"), &output, &count,
                                           &error));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());
  QCOMPARE(source, QStringLiteral("a\U0001f600b"));
}

void TestSearchService::testReplacementRejectsStaleSnapshots() {
  const QString source = QStringLiteral("foo\nchanged");
  const QVector<SearchLineMatch> matches{{1, QStringLiteral("foo"), {{0, 3}}},
                                         {2, QStringLiteral("foo"), {{0, 3}}}};
  QString output = QStringLiteral("old output");
  QString error;
  int count = -1;
  QVERIFY(!SearchService::buildReplacement(source, matches, QStringLiteral("bar"), &output, &count,
                                           &error));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());
  QCOMPARE(source, QStringLiteral("foo\nchanged"));

  const QVector<SearchLineMatch> shifted{{2, QStringLiteral("foo"), {{0, 3}}}};
  QVERIFY(!SearchService::buildReplacement(QStringLiteral("inserted\nkeep\nfoo"), shifted,
                                           QStringLiteral("bar"), &output, &count, &error));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());

  const QVector<SearchLineMatch> inconsistent{{1, QStringLiteral("foo"), {{0, 3}}},
                                              {1, QStringLiteral("bar"), {{0, 3}}}};
  QVERIFY(!SearchService::buildReplacement(QStringLiteral("foo"), inconsistent,
                                           QStringLiteral("bar"), &output, &count, &error));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());
}

void TestSearchService::testReplacementRejectsOverlaps() {
  const QString source = QStringLiteral("abcd");
  const QVector<SearchLineMatch> overlap{{1, source, {{0, 3}, {2, 4}}}};
  QString output;
  QString error;
  int count = -1;
  QVERIFY(!SearchService::buildReplacement(source, overlap, QStringLiteral("x"), &output, &count,
                                           &error));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());

  const QVector<SearchLineMatch> interiorInsertion{{1, source, {{0, 3}, {1, 1}}}};
  QVERIFY(!SearchService::buildReplacement(source, interiorInsertion, QStringLiteral("x"), &output,
                                           &count, &error));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());

  const QVector<SearchLineMatch> adjacent{{1, source, {{2, 4}, {0, 2}}}};
  QVERIFY(SearchService::buildReplacement(source, adjacent, QStringLiteral("x"), &output, &count,
                                          &error));
  QCOMPARE(output, QStringLiteral("xx"));
  QCOMPARE(count, 2);
  QVERIFY(error.isEmpty());
}

void TestSearchService::testReplacementOutputPointers() {
  const QVector<SearchLineMatch> matches{{1, QStringLiteral("foo"), {{0, 3}}}};
  QString output = QStringLiteral("old output");
  QString error = QStringLiteral("old error");
  int count = -1;
  QVERIFY(!SearchService::buildReplacement(QStringLiteral("foo"), matches, QStringLiteral("bar"),
                                           nullptr, &count, &error));
  QCOMPARE(count, 0);
  QVERIFY(!error.isEmpty());

  QVERIFY(!SearchService::buildReplacement(QStringLiteral("foo"), matches, QStringLiteral("bar"),
                                           &output, nullptr, &error));
  QVERIFY(output.isEmpty());
  QVERIFY(!error.isEmpty());

  output = QStringLiteral("old output");
  count = -1;
  QVERIFY(!SearchService::buildReplacement(QStringLiteral("foo"), matches, QStringLiteral("bar"),
                                           &output, &count, nullptr));
  QVERIFY(output.isEmpty());
  QCOMPARE(count, 0);

  output = QStringLiteral("foo");
  QVERIFY(SearchService::buildReplacement(output, matches, QStringLiteral("bar"), &output, &count,
                                          &error));
  QCOMPARE(output, QStringLiteral("bar"));
  QCOMPARE(count, 1);
  QVERIFY(error.isEmpty());
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

  const int token = service.searchContent(
      m_notebookId, QStringLiteral("{\"pattern\":\"streamneedle\"}"), QString());
  QVERIFY(token > 0);

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 10000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(
               QString("streaming searchContent failed unexpectedly (%1)")
                   .arg(failedSpy.isEmpty() ? QString() : failedSpy.takeFirst().at(1).toString())));
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
    VxCoreError e =
        vxcore_file_create(m_context, m_notebookId.toUtf8().constData(), "", p_name, &fileId);
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

  const QString queryJson = QStringLiteral("{\"pattern\":\"capneedle\",\"maxResults\":2}");

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
           qPrintable(
               QString("streaming searchContent failed unexpectedly (%1)")
                   .arg(failedSpy.isEmpty() ? QString() : failedSpy.takeFirst().at(1).toString())));

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
           qPrintable(
               QString("streaming searchContent failed unexpectedly (%1)")
                   .arg(failedSpy.isEmpty() ? QString() : failedSpy.takeFirst().at(1).toString())));

  const SearchResult streamedResult = finishedSpy.at(0).at(1).value<SearchResult>();
  QVERIFY2(streamedResult.m_truncated,
           "streaming finished result ignored the implicit default maxResults=100 cap");
  QCOMPARE(streamedResult.m_matchCount, blobResult.m_matchCount);
  QCOMPARE(canonicalizeContentResult(streamedResult), canonicalizeContentResult(blobResult));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchService)
#include "test_searchservice.moc"
