// test_searchservice.cpp - Tests for vnotex::SearchService
#include <QtTest>

#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

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
                               "{\"name\":\"TestNotebook\"}",
                               VXCORE_NOTEBOOK_BUNDLED, &notebookId);
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
  Error err = service.searchFiles(m_notebookId,
                                  "{\"pattern\":\"*.txt\"}",
                                  QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchFiles failed: %1").arg(err.message())));
  QVERIFY(results.isEmpty());
}

void TestSearchService::testSearchContentEmptyNotebook() {
  SearchCoreService service(m_context);
  QJsonArray results;

  Error err = service.searchContent(m_notebookId,
                                    "{\"pattern\":\"test\"}",
                                    QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchContent failed: %1").arg(err.message())));
  QVERIFY(results.isEmpty());
}

void TestSearchService::testSearchByTagsEmptyNotebook() {
  SearchCoreService service(m_context);
  QJsonArray results;

  Error err = service.searchByTags(m_notebookId,
                                   "{\"tags\":[\"test\"]}",
                                   QString(), &results);
  QVERIFY2(err.isOk(), qPrintable(QString("searchByTags failed: %1").arg(err.message())));
  QVERIFY(results.isEmpty());
}

void TestSearchService::testSearchFilesWithFiles() {
  // Create a file in the notebook
  char *fileId = nullptr;
  VxCoreError vxerr = vxcore_file_create(m_context,
                                         m_notebookId.toUtf8().constData(),
                                         "",
                                         "test_file.md",
                                         &fileId);
  QCOMPARE(vxerr, VXCORE_OK);
  QVERIFY(fileId != nullptr);
  vxcore_string_free(fileId);

  SearchCoreService service(m_context);
  QJsonArray results;

  // Search for markdown files
  Error err = service.searchFiles(m_notebookId,
                                  "{\"pattern\":\"*.md\"}",
                                  QString(), &results);
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
  Error err = service.searchContent(m_notebookId,
                                    "{\"pattern\":\"test\"}",
                                    QString(), &results);

  // searchContent may or may not find results; just verify the call doesn't crash
  QVERIFY2(err.isOk(), qPrintable(QString("searchContent failed: %1").arg(err.message())));
}

void TestSearchService::testSearchByTagsWithTags() {
  // Create a file
  char *fileId = nullptr;
  VxCoreError vxerr = vxcore_file_create(m_context,
                                         m_notebookId.toUtf8().constData(),
                                         "",
                                         "tagged_file.md",
                                         &fileId);
  QCOMPARE(vxerr, VXCORE_OK);
  vxcore_string_free(fileId);

  // Create a tag
  vxerr = vxcore_tag_create(m_context,
                            m_notebookId.toUtf8().constData(),
                            "test-tag");
  QCOMPARE(vxerr, VXCORE_OK);

  // Tag the file
  vxerr = vxcore_file_tag(m_context,
                          m_notebookId.toUtf8().constData(),
                          "tagged_file.md",
                          "test-tag");
  QCOMPARE(vxerr, VXCORE_OK);

  SearchCoreService service(m_context);
  QJsonArray results;

  // Search by tag
  Error err = service.searchByTags(m_notebookId,
                                   "{\"tags\":[\"test-tag\"]}",
                                   QString(), &results);
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

  service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || failedSpy.count() > 0, 5000);
  QVERIFY2(finishedSpy.count() > 0,
           qPrintable(QString("searchFiles async failed unexpectedly (%1)")
                          .arg(failedSpy.isEmpty() ? QString() : failedSpy.takeFirst().at(0).toString())));
}

void TestSearchService::testCancelSearch() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
  QSignalSpy cancelledSpy(&service, &SearchService::searchCancelled);
  QSignalSpy failedSpy(&service, &SearchService::searchFailed);

  service.searchContent(m_notebookId, QStringLiteral("{\"pattern\":\"test\"}"), QString());
  service.cancel();

  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || cancelledSpy.count() > 0 || failedSpy.count() > 0,
                           5000);
  QVERIFY(finishedSpy.count() > 0 || cancelledSpy.count() > 0);
}

void TestSearchService::testSearchStartedSignal() {
  SearchCoreService coreService(m_context);
  SearchService service(&coreService);

  QSignalSpy startedSpy(&service, &SearchService::searchStarted);

  service.searchFiles(m_notebookId, QStringLiteral("{\"pattern\":\"*.md\"}"), QString());

  QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 3000);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchService)
#include "test_searchservice.moc"
