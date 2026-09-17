#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/searchservice.h>

#define private public
#include <controllers/searchcontroller.h>
#undef private

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/searchcoreservice.h>
#include <models/searchresultmodel.h>

#include <vxcore/vxcore.h>

namespace tests {

namespace {

QByteArray readFile(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &p_path, const QByteArray &p_content) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_content) == p_content.size();
}

int replacementOccurrenceCount(const vnotex::SearchResultModel &p_model) {
  int count = 0;
  for (const auto &file : p_model.allReplacementTargets())
    count += file.m_matchCount;
  return count;
}

// Fail at the real save boundary after the exact queue has installed transformed content.
class WriteFailureBufferService : public vnotex::BufferService {
public:
  using BufferService::BufferService;
  QString failurePath;

  bool saveBuffer(const QString &p_bufferId) override {
    const auto preserved = failurePath + QStringLiteral(".preserved");
    if (!QFile::rename(failurePath, preserved)) {
      return false;
    }
    const bool obstruction = QDir().mkdir(failurePath);
    const bool saved = obstruction && BufferService::saveBuffer(p_bufferId);
    if (obstruction) {
      QDir().rmdir(failurePath);
    }
    QFile::rename(preserved, failurePath);
    return saved;
  }
};

} // namespace

class TestSearchController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testActivateResultEmitsNodeActivated();
  void testCancelDelegates();
  void testSearchWithNoNotebook();
  void testBuildQueryJsonContentSearch();
  void testBuildQueryJsonFileSearchOptions();
  void testConfiguredMaxResultsAppliedToAllModes();
  void testStaleTokenRejection();
  void testReplacementSelectedAndAll();
  void testReplacementIneligible_data();
  void testReplacementIneligible();
  void testReplacementConfirmationRevalidation_data();
  void testReplacementConfirmationRevalidation();
  void testReplacementCancelAfterFirstFile();
  void testReplacementStaleFileContinues();
  void testReplacementWriteFailureRecovery();
  void testReplacementNoopDoesNotSaveDraft();
  void testReplacementRefreshKeepsResolvedScope();
  void testExplicitSearchSupersedesRefresh();
  void testReplacementIgnoresForeignCompletion();

private:
  struct ControllerFixture {
    vnotex::ServiceLocator services;
    vnotex::SearchCoreService *searchCoreService = nullptr;
    vnotex::SearchService *searchService = nullptr;
    vnotex::ConfigCoreService *configCoreService = nullptr;
    vnotex::ConfigMgr2 *configMgr = nullptr;
    vnotex::SearchController *controller = nullptr;
    vnotex::HookManager *hooks = nullptr;
    vnotex::NotebookIoGate *gate = nullptr;
    vnotex::BufferService *bufferService = nullptr;
    vnotex::NotebookCoreService *notebookService = nullptr;
    QString notebookId;
    QString rootPath;
    QString folder;
    bool configured = false;

    ControllerFixture(VxCoreContextHandle p_ctx, const QString &p_notebookId,
                      const QString &p_rootPath, bool p_failWrites = false)
        : notebookId(p_notebookId), rootPath(p_rootPath) {
      configured =
          vxcore_context_update_config(p_ctx, R"({"search":{"backends":["simple"]}})") == VXCORE_OK;
      hooks = new vnotex::HookManager();
      gate = new vnotex::NotebookIoGate();
      bufferService =
          p_failWrites
              ? new WriteFailureBufferService(p_ctx, hooks, gate, vnotex::AutoSavePolicy::None)
              : new vnotex::BufferService(p_ctx, hooks, gate, vnotex::AutoSavePolicy::None);
      notebookService = new vnotex::NotebookCoreService(p_ctx);
      notebookService->setHookManager(hooks);
      notebookService->setNotebookIoGate(gate);
      services.registerService<vnotex::HookManager>(hooks);
      services.registerService<vnotex::NotebookIoGate>(gate);
      services.registerService<vnotex::BufferService>(bufferService);
      services.registerService<vnotex::NotebookCoreService>(notebookService);
      searchCoreService = new vnotex::SearchCoreService(p_ctx);
      searchService = new vnotex::SearchService(searchCoreService);
      services.registerService<vnotex::SearchService>(searchService);

      configCoreService = new vnotex::ConfigCoreService(p_ctx);
      configMgr = new vnotex::ConfigMgr2(configCoreService);
      configMgr->init();
      configMgr->getCoreConfig().setSearchMaxResults(1000);
      services.registerService<vnotex::ConfigMgr2>(configMgr);

      controller = new vnotex::SearchController(services);
    }

    QString relativePath(const QString &p_name) const { return folder + QLatin1Char('/') + p_name; }
    QString filePath(const QString &p_name) const {
      return QDir(rootPath).filePath(relativePath(p_name));
    }

    bool prepareFiles() {
      folder = QString::fromLatin1(QTest::currentTestFunction());
      const auto tag = QTest::currentDataTag();
      if (tag && *tag) {
        folder += QLatin1Char('-') + QString::fromLatin1(tag);
      }
      if (!configured || notebookService->createFolderPath(notebookId, folder).isEmpty() ||
          notebookService->createFile(notebookId, folder, QStringLiteral("a.md")).isEmpty() ||
          notebookService->createFile(notebookId, folder, QStringLiteral("b.txt")).isEmpty()) {
        return false;
      }
      controller->setCurrentNotebookId(notebookId);
      controller->setCurrentFolderId({notebookId, folder});
      return writeFile(filePath(QStringLiteral("a.md")), QByteArrayLiteral("foo foo\nkeep\nfoo")) &&
             writeFile(filePath(QStringLiteral("b.txt")), QByteArrayLiteral("foo"));
    }

    void search(const QString &p_keyword = QStringLiteral("foo"),
                int p_mode = vnotex::SearchController::ContentSearch) {
      controller->search(p_keyword, vnotex::SearchController::CurrentFolder, p_mode, true, false,
                         QString(), vnotex::SearchController::FileSearchOptions());
    }

    QModelIndex fileIndex(const vnotex::SearchResultModel &p_model, const QString &p_name) const {
      for (int row = 0; row < p_model.rowCount(); ++row) {
        const auto index = p_model.index(row, 0);
        if (p_model.data(index, vnotex::SearchResultModel::NodeIdRole)
                .value<vnotex::NodeIdentifier>()
                .relativePath == relativePath(p_name)) {
          return index;
        }
      }
      return QModelIndex();
    }

    ~ControllerFixture() {
      delete controller;
      delete searchService;
      delete searchCoreService;
      bufferService->shutdown();
      for (const auto &value : bufferService->listBuffers()) {
        bufferService->closeBuffer(value.toObject().value(QStringLiteral("id")).toString());
      }
      delete bufferService;
      delete notebookService;
      delete gate;
      delete hooks;
      delete configMgr;
      delete configCoreService;
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
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
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

  fixture.controller->m_activeSearchMode = vnotex::SearchController::ContentSearch;
  fixture.controller->m_lastKeyword = QStringLiteral("hello");
  fixture.controller->activateResult(lineIndex);

  QVERIFY(emitted);
  QCOMPARE(emittedId.notebookId, QStringLiteral("nb-1"));
  QCOMPARE(emittedId.relativePath, QStringLiteral("notes/demo.md"));
  QCOMPARE(emittedSettings.m_lineNumber, 5);
  QCOMPARE(emittedSettings.m_searchHighlight.m_currentMatchLine, 5);

  fixture.controller->activateResult(fileIndex);
  QCOMPARE(emittedSettings.m_lineNumber, -1);
  QCOMPARE(emittedSettings.m_searchHighlight.m_currentMatchLine, -1);
}

void TestSearchController::testCancelDelegates() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy cancelled(fixture.controller, &vnotex::SearchController::searchCancelled);
  QSignalSpy finished(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy confirmation(fixture.controller,
                          &vnotex::SearchController::replacementConfirmationRequested);
  fixture.search();
  fixture.controller->cancel();
  QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 10000);
  QCOMPARE(finished.count(), 0);
  fixture.controller->requestReplacement(QStringLiteral("bar"), true);
  fixture.controller->confirmReplacement(true);
  QCOMPARE(confirmation.count(), 0);
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));

  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 4);
  QCOMPARE(cancelled.count(), 1);
}

void TestSearchController::testSearchWithNoNotebook() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QSignalSpy failedSpy(fixture.controller, &vnotex::SearchController::searchFailed);

  fixture.controller->setCurrentNotebookId(QString());
  fixture.controller->search(QStringLiteral("abc"), vnotex::SearchController::CurrentNotebook,
                             vnotex::SearchController::ContentSearch, false, false, QString(),
                             vnotex::SearchController::FileSearchOptions());

  QCOMPARE(failedSpy.count(), 1);
  QVERIFY(failedSpy.takeFirst().at(0).toString().contains(QStringLiteral("No current notebook")));
}

void TestSearchController::testBuildQueryJsonContentSearch() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  // Set the value explicitly rather than relying on the persisted default: the
  // fixture's ConfigMgr2 shares the test-mode config file across runs, so a
  // sibling test that writes a different value would otherwise leak in here.
  fixture.configMgr->getCoreConfig().setSearchMaxResults(1000);
  fixture.controller->setCurrentNotebookId(QString());
  fixture.controller->search(QStringLiteral("hello"), vnotex::SearchController::CurrentNotebook,
                             vnotex::SearchController::ContentSearch, true, true,
                             QStringLiteral("*.md"), vnotex::SearchController::FileSearchOptions());

  const QString jsonText = fixture.controller->m_queryJson;
  QVERIFY(!jsonText.isEmpty());

  const QJsonObject obj = QJsonDocument::fromJson(jsonText.toUtf8()).object();
  QCOMPARE(obj.value(QStringLiteral("pattern")).toString(), QStringLiteral("hello"));
  QCOMPARE(obj.value(QStringLiteral("caseSensitive")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("wholeWord")).toBool(), false);
  QCOMPARE(obj.value(QStringLiteral("regex")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("maxResults")).toInt(), 1000);

  const QJsonObject scopeObj = obj.value(QStringLiteral("scope")).toObject();
  const QJsonArray patterns = scopeObj.value(QStringLiteral("filePatterns")).toArray();
  QCOMPARE(patterns.size(), 1);
  QCOMPARE(patterns.first().toString(), QStringLiteral("*.md"));
}

void TestSearchController::testBuildQueryJsonFileSearchOptions() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  fixture.configMgr->getCoreConfig().setSearchMaxResults(100);

  vnotex::SearchController::FileSearchOptions options;
  options.includeFolders = false;
  options.matchTarget = vnotex::SearchController::MatchName;
  fixture.controller->search(QStringLiteral("needle"), vnotex::SearchController::CurrentNotebook,
                             vnotex::SearchController::FileNameSearch, false, false, QString(),
                             options);

  QJsonObject obj = QJsonDocument::fromJson(fixture.controller->m_queryJson.toUtf8()).object();
  QCOMPARE(obj.value(QStringLiteral("includeFiles")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("includeFolders")).toBool(), false);
  QCOMPARE(obj.value(QStringLiteral("matchTarget")).toString(), QStringLiteral("name"));

  options.includeFolders = true;
  options.matchTarget = vnotex::SearchController::MatchPath;
  fixture.controller->search(QStringLiteral("needle"), vnotex::SearchController::CurrentNotebook,
                             vnotex::SearchController::FileNameSearch, false, false, QString(),
                             options);

  obj = QJsonDocument::fromJson(fixture.controller->m_queryJson.toUtf8()).object();
  QCOMPARE(obj.value(QStringLiteral("includeFolders")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("matchTarget")).toString(), QStringLiteral("path"));
}

void TestSearchController::testConfiguredMaxResultsAppliedToAllModes() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  fixture.configMgr->getCoreConfig().setSearchMaxResults(250);
  fixture.controller->setCurrentNotebookId(QString());

  const int modes[] = {vnotex::SearchController::FileNameSearch,
                       vnotex::SearchController::ContentSearch,
                       vnotex::SearchController::TagSearch};
  for (int mode : modes) {
    fixture.controller->search(QStringLiteral("hello"), vnotex::SearchController::CurrentNotebook,
                               mode, false, false, QString(),
                               vnotex::SearchController::FileSearchOptions());
    const QString jsonText = fixture.controller->m_queryJson;
    QVERIFY2(!jsonText.isEmpty(), qPrintable(QStringLiteral("empty query for mode %1").arg(mode)));
    const QJsonObject obj = QJsonDocument::fromJson(jsonText.toUtf8()).object();
    QCOMPARE(obj.value(QStringLiteral("maxResults")).toInt(), 250);
  }
}

void TestSearchController::testStaleTokenRejection() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy started(fixture.searchService, &vnotex::SearchService::searchStarted);
  QSignalSpy finished(fixture.controller, &vnotex::SearchController::searchFinished);
  fixture.search(QStringLiteral("foo"));
  const int oldToken = started.last().at(0).toInt();
  fixture.search(QStringLiteral("keep"));

  vnotex::SearchResult stale;
  vnotex::SearchFileResult fake;
  fake.m_path = QStringLiteral("STALE_SHOULD_NOT_APPEAR");
  fake.m_notebookId = m_notebookId;
  stale.m_fileResults.append(fake);
  stale.m_matchCount = 1;
  fixture.searchService->searchFinished(oldToken, stale);
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
  QCOMPARE(model.rowCount(), 1);
  const auto file = fixture.fileIndex(model, QStringLiteral("a.md"));
  QVERIFY(file.isValid());
  QCOMPARE(model.rowCount(file), 1);
  QCOMPARE(model.data(model.index(0, 0, file), vnotex::SearchResultModel::LineNumberRole).toInt(),
           2);
  QCOMPARE(replacementOccurrenceCount(model), 1);
}

void TestSearchController::testReplacementSelectedAndAll() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy confirmation(fixture.controller,
                          &vnotex::SearchController::replacementConfirmationRequested);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  QSignalSpy availability(fixture.controller,
                          &vnotex::SearchController::replacementAvailabilityChanged);
  auto note = fixture.bufferService->openBuffer(
      {m_notebookId, fixture.relativePath(QStringLiteral("a.md"))});
  QVERIFY(note.isValid());
  QString editor = QStringLiteral("foo foo\ndraft\nfoo");
  fixture.bufferService->registerActiveWriter(note.id(), 1, [&]() { return editor; });
  fixture.bufferService->markDirty(note.id());
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 4);
  QVERIFY(availability.last().at(1).toBool());
  QVERIFY(!availability.last().at(0).toBool());
  fixture.controller->requestReplacement(QStringLiteral("bar"), false);
  QCOMPARE(confirmation.count(), 0);
  const auto a = fixture.fileIndex(model, QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  fixture.controller->setSelectedResults({model.index(0, 0, a)});
  QVERIFY(availability.last().at(0).toBool());
  fixture.controller->requestReplacement(QStringLiteral("bar"), false);
  QCOMPARE(confirmation.count(), 1);
  QCOMPARE(confirmation.last().at(0).toInt(), 2);
  QCOMPARE(confirmation.last().at(1).toInt(), 1);
  fixture.controller->confirmReplacement(false);
  QCOMPARE(replacements.count(), 0);
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QVERIFY(fixture.bufferService->isDirty(note.id()));

  fixture.controller->requestReplacement(QStringLiteral("bar"), false);
  // Selection changes while the view is confirming cannot change the approved value snapshot.
  fixture.controller->setSelectedResults({fixture.fileIndex(model, QStringLiteral("b.txt"))});
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 2);
  QCOMPARE(replacements.last().at(1).toInt(), 1);
  QVERIFY(!replacements.last().at(2).toBool());
  QVERIFY(replacements.last().at(3).toStringList().isEmpty());
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("bar bar\ndraft\nfoo"));
  QCOMPARE(note.getContentRaw(), QByteArrayLiteral("bar bar\ndraft\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QVERIFY(!fixture.bufferService->isDirty(note.id()));
  QCOMPARE(fixture.bufferService->lastSavedRevision(note.id()),
           fixture.bufferService->currentRevision(note.id()));
  editor = QString::fromUtf8(note.getContentRaw());
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 2);
  const auto refreshedA = fixture.fileIndex(model, QStringLiteral("a.md"));
  QVERIFY(refreshedA.isValid());
  QCOMPARE(
      model.data(model.index(0, 0, refreshedA), vnotex::SearchResultModel::LineNumberRole).toInt(),
      3);

  fixture.controller->requestReplacement(QStringLiteral("baz"), true);
  QCOMPARE(confirmation.last().at(0).toInt(), 2);
  QCOMPARE(confirmation.last().at(1).toInt(), 2);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 2, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 2);
  QCOMPARE(replacements.last().at(1).toInt(), 2);
  QVERIFY(replacements.last().at(3).toStringList().isEmpty());
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("bar bar\ndraft\nbaz"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("baz"));
  QVERIFY(!fixture.bufferService->isDirty(note.id()));
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 3, 10000);
  QCOMPARE(model.rowCount(), 0);
  QCOMPARE(replacementOccurrenceCount(model), 0);
  QVERIFY(!availability.last().at(1).toBool());
  fixture.bufferService->unregisterActiveWriter(note.id(), 1);
}

void TestSearchController::testReplacementIneligible_data() {
  QTest::addColumn<QString>("reason");
  for (const char *reason :
       {"running", "criteria-during-search", "truncated", "file-name", "tag", "model-reset",
        "unsupported-current-backend", "unsupported-provenance", "failed"}) {
    QTest::newRow(reason) << QString::fromLatin1(reason);
  }
}

void TestSearchController::testReplacementIneligible() {
  QFETCH(QString, reason);
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy failures(fixture.controller, &vnotex::SearchController::searchFailed);
  QSignalSpy confirmation(fixture.controller,
                          &vnotex::SearchController::replacementConfirmationRequested);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  QSignalSpy status(fixture.controller, &vnotex::SearchController::replacementStatusChanged);
  QSignalSpy started(fixture.searchService, &vnotex::SearchService::searchStarted);
  if (reason == QStringLiteral("truncated")) {
    fixture.configMgr->getCoreConfig().setSearchMaxResults(1);
  }
  if (reason == QStringLiteral("tag")) {
    QVERIFY(fixture.notebookService->createTag(m_notebookId, QStringLiteral("controller-tag")));
    QVERIFY(fixture.notebookService->updateFileTags(m_notebookId,
                                                    fixture.relativePath(QStringLiteral("a.md")),
                                                    {QStringLiteral("controller-tag")}));
    fixture.search(QStringLiteral("controller-tag"), vnotex::SearchController::TagSearch);
  } else if (reason == QStringLiteral("file-name")) {
    fixture.search(QStringLiteral("a.md"), vnotex::SearchController::FileNameSearch);
  } else {
    fixture.search();
  }
  if (reason == QStringLiteral("criteria-during-search")) {
    fixture.controller->invalidateReplacementResults();
  }
  if (reason != QStringLiteral("running")) {
    QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  }
  if (reason == QStringLiteral("model-reset")) {
    vnotex::SearchResult identical;
    identical.m_fileResults = model.allReplacementTargets();
    identical.m_matchCount = model.totalMatchCount();
    model.setSearchResult(identical);
  } else if (reason == QStringLiteral("unsupported-current-backend")) {
    QCOMPARE(vxcore_context_update_config(m_ctx, R"({"search":{"backends":["rg","simple"]}})"),
             VXCORE_OK);
  } else if (reason == QStringLiteral("unsupported-provenance")) {
    vnotex::SearchResult unsupported;
    unsupported.m_fileResults = model.allReplacementTargets();
    unsupported.m_matchCount = model.totalMatchCount();
    for (auto &file : unsupported.m_fileResults) {
      file.m_replacementSupported = false;
    }
    fixture.search();
    const int token = started.last().at(0).toInt();
    // A final result's provenance is immutable even if the current configuration is Simple.
    fixture.searchService->searchFinished(token, unsupported);
    QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  } else if (reason == QStringLiteral("failed")) {
    fixture.controller->setCurrentFolderId({QStringLiteral("missing-notebook"), QString()});
    fixture.search();
    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 10000);
  }
  fixture.controller->requestReplacement(QStringLiteral("bar"), true);
  fixture.controller->confirmReplacement(true);
  QCOMPARE(confirmation.count(), 0);
  QCOMPARE(replacements.count(), 1);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
  QCOMPARE(replacements.last().at(1).toInt(), 0);
  QVERIFY(!replacements.last().at(3).toStringList().isEmpty());
  if (reason == QStringLiteral("truncated") || reason.startsWith(QStringLiteral("unsupported"))) {
    QVERIFY(!status.last().at(0).toString().isEmpty());
    QCOMPARE(failures.count(), 0);
  }
  if (reason == QStringLiteral("running")) {
    QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  }
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QVERIFY(fixture.bufferService->listBuffers().isEmpty());
  QTRY_VERIFY_WITH_TIMEOUT(!fixture.searchService->isSearching(), 10000);
}

void TestSearchController::testReplacementConfirmationRevalidation_data() {
  QTest::addColumn<QString>("change");
  for (const char *change : {"new-search", "criteria", "model-reset", "backend"}) {
    QTest::newRow(change) << QString::fromLatin1(change);
  }
}

void TestSearchController::testReplacementConfirmationRevalidation() {
  QFETCH(QString, change);
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy confirmations(fixture.controller,
                           &vnotex::SearchController::replacementConfirmationRequested);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  fixture.controller->requestReplacement(QStringLiteral("bar"), true);
  QCOMPARE(confirmations.count(), 1);
  if (change == QStringLiteral("new-search")) {
    fixture.search(QStringLiteral("keep"));
    QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  } else if (change == QStringLiteral("criteria")) {
    fixture.controller->invalidateReplacementResults();
  } else if (change == QStringLiteral("model-reset")) {
    model.clear();
  } else {
    QCOMPARE(vxcore_context_update_config(m_ctx, R"({"search":{"backends":["rg","simple"]}})"),
             VXCORE_OK);
  }
  fixture.controller->confirmReplacement(true);
  QCOMPARE(replacements.count(), 1);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
  QCOMPARE(replacements.last().at(1).toInt(), 0);
  QVERIFY(!replacements.last().at(3).toStringList().isEmpty());
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QVERIFY(fixture.bufferService->listBuffers().isEmpty());
}

void TestSearchController::testReplacementCancelAfterFirstFile() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  const auto targets = model.allReplacementTargets();
  QCOMPARE(targets.size(), 2);
  const auto firstPath = QDir(fixture.rootPath).filePath(targets.first().m_path);
  const auto secondPath = QDir(fixture.rootPath).filePath(targets.last().m_path);
  const auto secondOriginal = readFile(secondPath);
  auto firstExpected = readFile(firstPath);
  firstExpected.replace("foo", "bar");
  bool firstDurableAtProgress = false;
  connect(fixture.controller, &vnotex::SearchController::replacementProgress, this,
          [&](int p_completed, int) {
            if (p_completed == 1) {
              firstDurableAtProgress = readFile(firstPath) == firstExpected;
              fixture.controller->cancel();
            }
          });
  fixture.controller->requestReplacement(QStringLiteral("bar"), true);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QVERIFY(firstDurableAtProgress);
  QCOMPARE(replacements.last().at(0).toInt(), targets.first().m_matchCount);
  QCOMPARE(replacements.last().at(1).toInt(), 1);
  QVERIFY(replacements.last().at(2).toBool());
  QVERIFY(replacements.last().at(3).toStringList().isEmpty());
  QCOMPARE(readFile(firstPath), firstExpected);
  QCOMPARE(readFile(secondPath), secondOriginal);
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(replacementOccurrenceCount(model), targets.last().m_matchCount);
}

void TestSearchController::testReplacementStaleFileContinues() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  int recoveryCount = 0;
  connect(fixture.controller, &vnotex::SearchController::nodeActivated, this,
          [&]() { ++recoveryCount; });
  auto note = fixture.bufferService->openBuffer(
      {m_notebookId, fixture.relativePath(QStringLiteral("a.md"))});
  QVERIFY(note.isValid());
  const QString editor = QStringLiteral("changed foo foo\ndraft\nfoo");
  fixture.bufferService->registerActiveWriter(note.id(), 1, [&]() { return editor; });
  fixture.bufferService->markDirty(note.id());
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  fixture.controller->requestReplacement(QStringLiteral("bar"), true);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 1);
  QCOMPARE(replacements.last().at(1).toInt(), 1);
  const auto errors = replacements.last().at(3).toStringList();
  QCOMPARE(errors.size(), 1);
  QVERIFY(errors.first().startsWith(m_notebookId + QLatin1Char('/') +
                                    fixture.relativePath(QStringLiteral("a.md"))));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(note.getContentRaw(), QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("bar"));
  QVERIFY(fixture.bufferService->isDirty(note.id()));
  QString captured;
  QVERIFY(fixture.bufferService->captureActiveWriterContent(note.id(), &captured));
  QCOMPARE(captured, editor);
  QCOMPARE(recoveryCount, 0);
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 3);
  // Automatic refresh reads disk, not a rematch of the stale dirty line.
  fixture.controller->requestReplacement(QStringLiteral("baz"), true);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 2, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
  QCOMPARE(replacements.last().at(1).toInt(), 0);
  QCOMPARE(replacements.last().at(3).toStringList().size(), 1);
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 3, 10000);
  fixture.bufferService->unregisterActiveWriter(note.id(), 1);
}

void TestSearchController::testReplacementWriteFailureRecovery() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")), true);
  QVERIFY(fixture.prepareFiles());
  static_cast<WriteFailureBufferService *>(fixture.bufferService)->failurePath =
      fixture.filePath(QStringLiteral("a.md"));
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  vnotex::Buffer2 recovered;
  vnotex::FileOpenSettings recoverySettings;
  connect(fixture.controller, &vnotex::SearchController::nodeActivated, this,
          [&](const vnotex::NodeIdentifier &p_id, const vnotex::FileOpenSettings &p_settings) {
            recoverySettings = p_settings;
            recovered = fixture.bufferService->openBuffer(p_id, p_settings);
          });
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  fixture.controller->setSelectedResults({fixture.fileIndex(model, QStringLiteral("a.md"))});
  fixture.controller->requestReplacement(QStringLiteral("bar"), false);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
  QCOMPARE(replacements.last().at(1).toInt(), 0);
  const auto errors = replacements.last().at(3).toStringList();
  QCOMPARE(errors.size(), 1);
  QVERIFY(errors.first().startsWith(m_notebookId + QLatin1Char('/') +
                                    fixture.relativePath(QStringLiteral("a.md"))));
  QVERIFY(recovered.isValid());
  QCOMPARE(recovered.nodeId().relativePath, fixture.relativePath(QStringLiteral("a.md")));
  QCOMPARE(recoverySettings.m_mode, vnotex::ViewWindowMode::Edit);
  QVERIFY(recoverySettings.m_forceMode);
  QVERIFY(!recoverySettings.m_searchHighlight.m_isValid);
  QCOMPARE(recovered.getContentRaw(), QByteArrayLiteral("bar bar\nkeep\nbar"));
  QVERIFY(fixture.bufferService->isDirty(recovered.id()));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 4);
}

void TestSearchController::testReplacementNoopDoesNotSaveDraft() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  auto note = fixture.bufferService->openBuffer(
      {m_notebookId, fixture.relativePath(QStringLiteral("a.md"))});
  QVERIFY(note.isValid());
  const QString editor = QStringLiteral("foo foo\ndraft\nfoo");
  fixture.bufferService->registerActiveWriter(note.id(), 1, [&]() { return editor; });
  fixture.bufferService->markDirty(note.id());
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  fixture.controller->requestReplacement(QStringLiteral("foo"), true);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
  QCOMPARE(replacements.last().at(1).toInt(), 0);
  QVERIFY(replacements.last().at(3).toStringList().isEmpty());
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QVERIFY(fixture.bufferService->isDirty(note.id()));
  QString captured;
  QVERIFY(fixture.bufferService->captureActiveWriterContent(note.id(), &captured));
  QCOMPARE(captured, editor);
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 4);
  fixture.bufferService->unregisterActiveWriter(note.id(), 1);
}

void TestSearchController::testReplacementRefreshKeepsResolvedScope() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  QSignalSpy confirmations(fixture.controller,
                           &vnotex::SearchController::replacementConfirmationRequested);
  auto a = fixture.bufferService->openBuffer(
      {m_notebookId, fixture.relativePath(QStringLiteral("a.md"))});
  QVERIFY(a.isValid());
  fixture.controller->search(QStringLiteral("foo"), vnotex::SearchController::Buffers,
                             vnotex::SearchController::ContentSearch, true, false, QString(),
                             vnotex::SearchController::FileSearchOptions());
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 3);
  auto b = fixture.bufferService->openBuffer(
      {m_notebookId, fixture.relativePath(QStringLiteral("b.txt"))});
  QVERIFY(b.isValid());
  fixture.controller->setSelectedResults(
      {model.index(0, 0, fixture.fileIndex(model, QStringLiteral("a.md")))});
  fixture.controller->requestReplacement(QStringLiteral("bar"), false);
  fixture.controller->confirmReplacement(true);
  // New buffers and context must not be folded into either the saved selection or refresh scope.
  fixture.controller->setCurrentNotebookId(QStringLiteral("different-current-notebook"));
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("bar bar\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(replacementOccurrenceCount(model), 1);
  QVERIFY(fixture.fileIndex(model, QStringLiteral("a.md")).isValid());
  fixture.controller->requestReplacement(QStringLiteral("baz"), true);
  QCOMPARE(confirmations.count(), 1);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
}

void TestSearchController::testExplicitSearchSupersedesRefresh() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  connect(fixture.controller, &vnotex::SearchController::replacementStarted, this,
          [&](int) { fixture.search(QStringLiteral("keep")); });
  fixture.controller->requestReplacement(QStringLiteral("bar"), true);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 0);
  QVERIFY(replacements.last().at(2).toBool());
  QCOMPARE(replacementOccurrenceCount(model), 1);
  QCOMPARE(model.rowCount(), 1);
  const auto a = fixture.fileIndex(model, QStringLiteral("a.md"));
  QVERIFY(a.isValid());
  QCOMPARE(model.data(model.index(0, 0, a), vnotex::SearchResultModel::LineNumberRole).toInt(), 2);
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("foo"));
}

void TestSearchController::testReplacementIgnoresForeignCompletion() {
  ControllerFixture fixture(m_ctx, m_notebookId,
                            m_tempDir.filePath(QStringLiteral("controller_notebook")));
  QVERIFY(fixture.prepareFiles());
  vnotex::SearchResultModel model;
  fixture.controller->setModel(&model);
  QSignalSpy searches(fixture.controller, &vnotex::SearchController::searchFinished);
  QSignalSpy replacements(fixture.controller, &vnotex::SearchController::replacementFinished);
  fixture.search();
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 1, 10000);
  fixture.hooks->addAction<vnotex::BufferEvent>(
      vnotex::HookNames::FileBeforeSave, [&](vnotex::HookContext &, const vnotex::BufferEvent &) {
        const int foreignToken =
            fixture.bufferService->replaceSearchMatches(vnotex::SearchFileResult(), QString());
        fixture.bufferService->searchReplacementFinished(foreignToken, {}, QString(), 999, true,
                                                         QString());
      });
  fixture.controller->setSelectedResults({fixture.fileIndex(model, QStringLiteral("b.txt"))});
  fixture.controller->requestReplacement(QStringLiteral("bar"), false);
  fixture.controller->confirmReplacement(true);
  QTRY_COMPARE_WITH_TIMEOUT(replacements.count(), 1, 10000);
  QCOMPARE(replacements.last().at(0).toInt(), 1);
  QCOMPARE(replacements.last().at(1).toInt(), 1);
  QVERIFY(replacements.last().at(3).toStringList().isEmpty());
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("a.md"))),
           QByteArrayLiteral("foo foo\nkeep\nfoo"));
  QCOMPARE(readFile(fixture.filePath(QStringLiteral("b.txt"))), QByteArrayLiteral("bar"));
  QTRY_COMPARE_WITH_TIMEOUT(searches.count(), 2, 10000);
  QCOMPARE(replacementOccurrenceCount(model), 3);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchController)
#include "test_searchcontroller.moc"
