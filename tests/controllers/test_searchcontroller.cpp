#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <core/services/searchservice.h>

#define private public
#include <controllers/searchcontroller.h>
#undef private

#include <core/servicelocator.h>
#include <core/services/searchcoreservice.h>
#include <models/searchresultmodel.h>

#include <vxcore/vxcore.h>

namespace tests {

class TestSearchController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testActivateResultEmitsNodeActivated();
  void testCancelDelegates();
  void testSearchWithNoNotebook();
  void testBuildQueryJsonContentSearch();

private:
  struct ControllerFixture {
    vnotex::ServiceLocator services;
    vnotex::SearchCoreService *searchCoreService = nullptr;
    vnotex::SearchService *searchService = nullptr;
    vnotex::SearchController *controller = nullptr;

    explicit ControllerFixture(VxCoreContextHandle p_ctx) {
      searchCoreService = new vnotex::SearchCoreService(p_ctx);
      searchService = new vnotex::SearchService(searchCoreService);
      services.registerService<vnotex::SearchService>(searchService);
      controller = new vnotex::SearchController(services);
    }

    ~ControllerFixture() {
      delete controller;
      delete searchService;
      delete searchCoreService;
    }
  };

  VxCoreContextHandle m_ctx = nullptr;
  QTemporaryDir m_tempDir;
  QString m_notebookId;
};

void TestSearchController::initTestCase() {
  vxcore_set_test_mode(1);
  vxcore_set_app_info("VNote", "VNoteTestSearchController");

  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_ctx != nullptr);
  QVERIFY(m_tempDir.isValid());

  char *notebookId = nullptr;
  QByteArray notebookPath = m_tempDir.filePath(QStringLiteral("controller_notebook")).toUtf8();
  err = vxcore_notebook_create(m_ctx, notebookPath.constData(), "{\"name\":\"ControllerNotebook\"}",
                               VXCORE_NOTEBOOK_BUNDLED, &notebookId);
  QVERIFY2(err == VXCORE_OK, "Failed to create test notebook");
  QVERIFY(notebookId != nullptr);
  m_notebookId = QString::fromUtf8(notebookId);
  vxcore_string_free(notebookId);
}

void TestSearchController::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestSearchController::testActivateResultEmitsNodeActivated() {
  ControllerFixture fixture(m_ctx);
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);

  vnotex::SearchLineMatch lineMatch;
  lineMatch.m_lineNumber = 6;
  lineMatch.m_lineText = QStringLiteral("hello");

  vnotex::SearchFileResult fileResult;
  fileResult.m_path = QStringLiteral("notes/demo.md");
  fileResult.m_notebookId = QStringLiteral("nb-1");
  fileResult.m_lineMatches.append(lineMatch);

  vnotex::SearchResult result;
  result.m_fileResults.append(fileResult);
  result.m_matchCount = 1;
  model.setSearchResult(result);

  vnotex::NodeIdentifier emittedId;
  vnotex::FileOpenSettings emittedSettings;
  bool emitted = false;
  connect(fixture.controller, &vnotex::SearchController::nodeActivated, this,
          [&](const vnotex::NodeIdentifier &p_nodeId, const vnotex::FileOpenSettings &p_settings) {
            emitted = true;
            emittedId = p_nodeId;
            emittedSettings = p_settings;
          });

  const QModelIndex fileIndex = model.index(0, 0);
  const QModelIndex lineIndex = model.index(0, 0, fileIndex);
  QVERIFY(lineIndex.isValid());

  fixture.controller->activateResult(lineIndex);

  QVERIFY(emitted);
  QCOMPARE(emittedId.notebookId, QStringLiteral("nb-1"));
  QCOMPARE(emittedId.relativePath, QStringLiteral("notes/demo.md"));
  QCOMPARE(emittedSettings.m_lineNumber, 6);
}

void TestSearchController::testCancelDelegates() {
  ControllerFixture fixture(m_ctx);
  QSignalSpy cancelledSpy(fixture.controller, &vnotex::SearchController::searchCancelled);
  QSignalSpy finishedSpy(fixture.controller, &vnotex::SearchController::searchFinished);

  fixture.controller->setCurrentNotebookId(m_notebookId);
  fixture.controller->search(QStringLiteral("test"), vnotex::SearchController::CurrentNotebook,
                       vnotex::SearchController::ContentSearch, false, false, QString());
  fixture.controller->cancel();

  QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() > 0 || finishedSpy.count() > 0, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(!fixture.searchService->isSearching(), 3000);
}

void TestSearchController::testSearchWithNoNotebook() {
  ControllerFixture fixture(m_ctx);
  QSignalSpy failedSpy(fixture.controller, &vnotex::SearchController::searchFailed);

  fixture.controller->setCurrentNotebookId(QString());
  fixture.controller->search(QStringLiteral("abc"), vnotex::SearchController::CurrentNotebook,
                       vnotex::SearchController::ContentSearch, false, false, QString());

  QCOMPARE(failedSpy.count(), 1);
  QVERIFY(failedSpy.takeFirst().at(0).toString().contains(QStringLiteral("No current notebook")));
}

void TestSearchController::testBuildQueryJsonContentSearch() {
  ControllerFixture fixture(m_ctx);
  fixture.controller->setCurrentNotebookId(QString());
  fixture.controller->search(QStringLiteral("hello"), vnotex::SearchController::CurrentNotebook,
                       vnotex::SearchController::ContentSearch, true, true,
                       QStringLiteral("*.md"));

  const QString jsonText = fixture.controller->m_queryJson;
  QVERIFY(!jsonText.isEmpty());

  const QJsonObject obj = QJsonDocument::fromJson(jsonText.toUtf8()).object();
  QCOMPARE(obj.value(QStringLiteral("pattern")).toString(), QStringLiteral("hello"));
  QCOMPARE(obj.value(QStringLiteral("caseSensitive")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("wholeWord")).toBool(), false);
  QCOMPARE(obj.value(QStringLiteral("regex")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("maxResults")).toInt(), 500);

  const QJsonObject scopeObj = obj.value(QStringLiteral("scope")).toObject();
  const QJsonArray patterns = scopeObj.value(QStringLiteral("pathPatterns")).toArray();
  QCOMPARE(patterns.size(), 1);
  QCOMPARE(patterns.first().toString(), QStringLiteral("*.md"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchController)
#include "test_searchcontroller.moc"
