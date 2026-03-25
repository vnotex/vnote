#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTagService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // ===== TagCoreService tests =====
  void testCreateAndDeleteTag();
  void testCreateTagPath();
  void testListTags();
  void testMoveTag();
  void testTagFile();
  void testUntagFile();
  void testUpdateFileTags();
  void testSearchByTags();

  // ===== TagService hook tests =====
  void testCreateTagHookFires();
  void testCreateTagCancelledByHook();
  void testDeleteTagHookFires();
  void testMoveTagHookFires();
  void testTagFileHookFires();
  void testUntagFileHookFires();
  void testListTagsNoHook();
  void testSearchByTagsNoHook();

private:
  // Helper to create a bundled test notebook and return its ID.
  QString createTestNotebook(const QString &p_path);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TagCoreService *m_tagCoreService = nullptr;
  HookManager *m_hookMgr = nullptr;
  TagService *m_tagService = nullptr;
  TempDirFixture m_tempDir;
};

void TestTagService::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // Enable test mode to use isolated temp directories instead of real AppData.
  vxcore_set_test_mode(1);

  // Initialize VxCore context.
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create NotebookCoreService (needed to create test notebooks).
  m_notebookService = new NotebookCoreService(m_context, this);
  QVERIFY(m_notebookService != nullptr);

  // Create TagCoreService for direct API tests.
  m_tagCoreService = new TagCoreService(m_context, this);
  QVERIFY(m_tagCoreService != nullptr);

  // Create HookManager and TagService for hook tests.
  m_hookMgr = new HookManager(this);
  m_tagService = new TagService(m_context, m_hookMgr, this);
  QVERIFY(m_tagService != nullptr);
}

void TestTagService::cleanupTestCase() {
  delete m_tagService;
  m_tagService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  delete m_tagCoreService;
  m_tagCoreService = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestTagService::cleanup() {
  // Close all notebooks after each test.
  QJsonArray notebooks = m_notebookService->listNotebooks();
  for (const auto &notebookVal : notebooks) {
    QJsonObject notebook = notebookVal.toObject();
    QString id = notebook["id"].toString();
    if (!id.isEmpty()) {
      m_notebookService->closeNotebook(id);
    }
  }
}

QString TestTagService::createTestNotebook(const QString &p_path) {
  QString configJson = R"({
    "name": "Tag Test Notebook",
    "description": "Test notebook for tag tests",
    "version": "1"
  })";
  return m_notebookService->createNotebook(p_path, configJson, NotebookType::Bundled);
}

// ===== TagCoreService tests =====

void TestTagService::testCreateAndDeleteTag() {
  QString nbPath = m_tempDir.filePath("tc_create_del_tag_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create tag.
  QVERIFY(m_tagCoreService->createTag(nbId, "TestTag"));

  // Verify tag appears in listTags.
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool found = false;
  for (const auto &tagVal : tags) {
    if (tagVal.toObject()["name"].toString() == "TestTag") {
      found = true;
      break;
    }
  }
  QVERIFY(found);

  // Delete tag.
  QVERIFY(m_tagCoreService->deleteTag(nbId, "TestTag"));

  // Verify tag is gone.
  tags = m_tagCoreService->listTags(nbId);
  found = false;
  for (const auto &tagVal : tags) {
    if (tagVal.toObject()["name"].toString() == "TestTag") {
      found = true;
      break;
    }
  }
  QVERIFY(!found);
}

void TestTagService::testCreateTagPath() {
  QString nbPath = m_tempDir.filePath("tc_create_tag_path_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create a nested tag path "Parent/Child".
  QVERIFY(m_tagCoreService->createTagPath(nbId, "Parent/Child"));

  // Verify both tags appear in listTags.
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool foundParent = false;
  bool foundChild = false;
  for (const auto &tagVal : tags) {
    QString name = tagVal.toObject()["name"].toString();
    if (name == "Parent") {
      foundParent = true;
    }
    if (name == "Child") {
      foundChild = true;
    }
  }
  QVERIFY(foundParent);
  QVERIFY(foundChild);
}

void TestTagService::testListTags() {
  QString nbPath = m_tempDir.filePath("tc_list_tags_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create multiple tags.
  QVERIFY(m_tagCoreService->createTag(nbId, "Alpha"));
  QVERIFY(m_tagCoreService->createTag(nbId, "Beta"));
  QVERIFY(m_tagCoreService->createTag(nbId, "Gamma"));

  // Verify all are returned.
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  QVERIFY(tags.size() >= 3);

  QSet<QString> tagNames;
  for (const auto &tagVal : tags) {
    tagNames.insert(tagVal.toObject()["name"].toString());
  }
  QVERIFY(tagNames.contains("Alpha"));
  QVERIFY(tagNames.contains("Beta"));
  QVERIFY(tagNames.contains("Gamma"));
}

void TestTagService::testMoveTag() {
  QString nbPath = m_tempDir.filePath("tc_move_tag_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create parent and child tags.
  QVERIFY(m_tagCoreService->createTag(nbId, "ParentTag"));
  QVERIFY(m_tagCoreService->createTag(nbId, "ChildTag"));

  // Move child under parent.
  QVERIFY(m_tagCoreService->moveTag(nbId, "ChildTag", "ParentTag"));

  // Verify both tags still exist (move doesn't delete).
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool foundParent = false;
  bool foundChild = false;
  for (const auto &tagVal : tags) {
    QJsonObject tagObj = tagVal.toObject();
    QString name = tagObj["name"].toString();
    if (name == "ParentTag") {
      foundParent = true;
    }
    if (name == "ChildTag") {
      foundChild = true;
      // Verify parent field changed.
      QCOMPARE(tagObj["parent"].toString(), QStringLiteral("ParentTag"));
    }
  }
  QVERIFY(foundParent);
  QVERIFY(foundChild);
}

void TestTagService::testTagFile() {
  QString nbPath = m_tempDir.filePath("tc_tag_file_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file and tag.
  QString fileId = m_notebookService->createFile(nbId, "", "tagged.md");
  QVERIFY(!fileId.isEmpty());
  QVERIFY(m_tagCoreService->createTag(nbId, "FileTag"));

  // Tag the file.
  QVERIFY(m_tagCoreService->tagFile(nbId, "tagged.md", "FileTag"));

  // Verify file info reflects the tag.
  QJsonObject fileInfo = m_notebookService->getFileInfo(nbId, "tagged.md");
  QVERIFY(!fileInfo.isEmpty());
}

void TestTagService::testUntagFile() {
  QString nbPath = m_tempDir.filePath("tc_untag_file_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file and tag, then tag the file.
  QString fileId = m_notebookService->createFile(nbId, "", "untagged.md");
  QVERIFY(!fileId.isEmpty());
  QVERIFY(m_tagCoreService->createTag(nbId, "RemoveTag"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "untagged.md", "RemoveTag"));

  // Untag the file.
  QVERIFY(m_tagCoreService->untagFile(nbId, "untagged.md", "RemoveTag"));

  // Verify the file still exists but tag association is removed.
  QJsonObject fileInfo = m_notebookService->getFileInfo(nbId, "untagged.md");
  QVERIFY(!fileInfo.isEmpty());

  // Search by the removed tag should NOT find the file.
  QJsonArray results = m_tagCoreService->searchByTags(nbId, QStringList() << "RemoveTag");
  bool found = false;
  for (const auto &resultVal : results) {
    if (resultVal.toObject()["path"].toString() == "untagged.md") {
      found = true;
      break;
    }
  }
  QVERIFY(!found);
}

void TestTagService::testUpdateFileTags() {
  QString nbPath = m_tempDir.filePath("tc_update_filetags_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file and multiple tags.
  QString fileId = m_notebookService->createFile(nbId, "", "multi.md");
  QVERIFY(!fileId.isEmpty());
  QVERIFY(m_tagCoreService->createTag(nbId, "TagA"));
  QVERIFY(m_tagCoreService->createTag(nbId, "TagB"));

  // Update file tags via JSON.
  QString tagsJson = R"(["TagA","TagB"])";
  QVERIFY(m_tagCoreService->updateFileTags(nbId, "multi.md", tagsJson));

  // Search by TagA should find the file.
  QJsonArray results = m_tagCoreService->searchByTags(nbId, QStringList() << "TagA");
  bool found = false;
  for (const auto &resultVal : results) {
    if (resultVal.toObject()["path"].toString() == "multi.md") {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestTagService::testSearchByTags() {
  QString nbPath = m_tempDir.filePath("tc_search_tags_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create two files and two tags.
  m_notebookService->createFile(nbId, "", "file1.md");
  m_notebookService->createFile(nbId, "", "file2.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "SearchTag1"));
  QVERIFY(m_tagCoreService->createTag(nbId, "SearchTag2"));

  // Tag files.
  QVERIFY(m_tagCoreService->tagFile(nbId, "file1.md", "SearchTag1"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "file2.md", "SearchTag2"));

  // Search by SearchTag1.
  QJsonArray results = m_tagCoreService->searchByTags(nbId, QStringList() << "SearchTag1");
  bool foundFile1 = false;
  bool foundFile2 = false;
  for (const auto &resultVal : results) {
    QString path = resultVal.toObject()["path"].toString();
    if (path == "file1.md") {
      foundFile1 = true;
    }
    if (path == "file2.md") {
      foundFile2 = true;
    }
  }
  QVERIFY(foundFile1);
  QVERIFY(!foundFile2);

  // Search by SearchTag2.
  results = m_tagCoreService->searchByTags(nbId, QStringList() << "SearchTag2");
  foundFile1 = false;
  foundFile2 = false;
  for (const auto &resultVal : results) {
    QString path = resultVal.toObject()["path"].toString();
    if (path == "file1.md") {
      foundFile1 = true;
    }
    if (path == "file2.md") {
      foundFile2 = true;
    }
  }
  QVERIFY(!foundFile1);
  QVERIFY(foundFile2);
}

// ===== TagService hook tests =====

void TestTagService::testCreateTagHookFires() {
  QString nbPath = m_tempDir.filePath("th_create_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  bool beforeFired = false;
  bool afterFired = false;
  QVariantMap capturedBeforeArgs;
  QVariantMap capturedAfterArgs;

  int beforeId = m_hookMgr->addAction(
      HookNames::TagBeforeCreate,
      [&beforeFired, &capturedBeforeArgs](HookContext &, const QVariantMap &p_args) {
        beforeFired = true;
        capturedBeforeArgs = p_args;
      },
      10);

  int afterId = m_hookMgr->addAction(
      HookNames::TagAfterCreate,
      [&afterFired, &capturedAfterArgs](HookContext &, const QVariantMap &p_args) {
        afterFired = true;
        capturedAfterArgs = p_args;
      },
      10);

  QVERIFY(m_tagService->createTag(nbId, "HookTag"));

  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(capturedBeforeArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedBeforeArgs[QStringLiteral("tagName")].toString(), QStringLiteral("HookTag"));
  QCOMPARE(capturedBeforeArgs[QStringLiteral("operation")].toString(), QStringLiteral("create"));
  QCOMPARE(capturedAfterArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedAfterArgs[QStringLiteral("tagName")].toString(), QStringLiteral("HookTag"));

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

void TestTagService::testCreateTagCancelledByHook() {
  QString nbPath = m_tempDir.filePath("th_create_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  int hookId = m_hookMgr->addAction(
      HookNames::TagBeforeCreate,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

  bool result = m_tagService->createTag(nbId, "CancelledTag");
  QVERIFY(!result);

  // Verify tag was NOT created.
  QJsonArray tags = m_tagService->listTags(nbId);
  bool found = false;
  for (const auto &tagVal : tags) {
    if (tagVal.toObject()["name"].toString() == "CancelledTag") {
      found = true;
      break;
    }
  }
  QVERIFY(!found);

  m_hookMgr->removeAction(hookId);
}

void TestTagService::testDeleteTagHookFires() {
  QString nbPath = m_tempDir.filePath("th_delete_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // First create a tag via core service (no hooks).
  QVERIFY(m_tagCoreService->createTag(nbId, "DeleteHookTag"));

  bool beforeFired = false;
  bool afterFired = false;
  QVariantMap capturedArgs;

  int beforeId = m_hookMgr->addAction(
      HookNames::TagBeforeDelete,
      [&beforeFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        beforeFired = true;
        capturedArgs = p_args;
      },
      10);

  int afterId = m_hookMgr->addAction(
      HookNames::TagAfterDelete,
      [&afterFired](HookContext &, const QVariantMap &) { afterFired = true; }, 10);

  QVERIFY(m_tagService->deleteTag(nbId, "DeleteHookTag"));

  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("tagName")].toString(), QStringLiteral("DeleteHookTag"));
  QCOMPARE(capturedArgs[QStringLiteral("operation")].toString(), QStringLiteral("delete"));

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

void TestTagService::testMoveTagHookFires() {
  QString nbPath = m_tempDir.filePath("th_move_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create tags via core service.
  QVERIFY(m_tagCoreService->createTag(nbId, "MoveParent"));
  QVERIFY(m_tagCoreService->createTag(nbId, "MoveChild"));

  bool beforeFired = false;
  bool afterFired = false;
  QVariantMap capturedArgs;

  int beforeId = m_hookMgr->addAction(
      HookNames::TagBeforeMove,
      [&beforeFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        beforeFired = true;
        capturedArgs = p_args;
      },
      10);

  int afterId = m_hookMgr->addAction(
      HookNames::TagAfterMove,
      [&afterFired](HookContext &, const QVariantMap &) { afterFired = true; }, 10);

  QVERIFY(m_tagService->moveTag(nbId, "MoveChild", "MoveParent"));

  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("tagName")].toString(), QStringLiteral("MoveChild"));
  QCOMPARE(capturedArgs[QStringLiteral("parentTag")].toString(), QStringLiteral("MoveParent"));
  QCOMPARE(capturedArgs[QStringLiteral("operation")].toString(), QStringLiteral("move"));

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

void TestTagService::testTagFileHookFires() {
  QString nbPath = m_tempDir.filePath("th_tagfile_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file and tag via core services.
  m_notebookService->createFile(nbId, "", "hooktagged.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "FileHookTag"));

  bool beforeFired = false;
  bool afterFired = false;
  QVariantMap capturedArgs;

  int beforeId = m_hookMgr->addAction(
      HookNames::FileBeforeTag,
      [&beforeFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        beforeFired = true;
        capturedArgs = p_args;
      },
      10);

  int afterId = m_hookMgr->addAction(
      HookNames::FileAfterTag,
      [&afterFired](HookContext &, const QVariantMap &) { afterFired = true; }, 10);

  QVERIFY(m_tagService->tagFile(nbId, "hooktagged.md", "FileHookTag"));

  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("filePath")].toString(), QStringLiteral("hooktagged.md"));
  QCOMPARE(capturedArgs[QStringLiteral("tagName")].toString(), QStringLiteral("FileHookTag"));
  QCOMPARE(capturedArgs[QStringLiteral("operation")].toString(), QStringLiteral("tag"));

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

void TestTagService::testUntagFileHookFires() {
  QString nbPath = m_tempDir.filePath("th_untagfile_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file, tag, and apply tag via core services.
  m_notebookService->createFile(nbId, "", "hookuntagged.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "UntagHookTag"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "hookuntagged.md", "UntagHookTag"));

  bool beforeFired = false;
  bool afterFired = false;
  QVariantMap capturedArgs;

  int beforeId = m_hookMgr->addAction(
      HookNames::FileBeforeUntag,
      [&beforeFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        beforeFired = true;
        capturedArgs = p_args;
      },
      10);

  int afterId = m_hookMgr->addAction(
      HookNames::FileAfterUntag,
      [&afterFired](HookContext &, const QVariantMap &) { afterFired = true; }, 10);

  QVERIFY(m_tagService->untagFile(nbId, "hookuntagged.md", "UntagHookTag"));

  QVERIFY(beforeFired);
  QVERIFY(afterFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("filePath")].toString(),
           QStringLiteral("hookuntagged.md"));
  QCOMPARE(capturedArgs[QStringLiteral("tagName")].toString(), QStringLiteral("UntagHookTag"));
  QCOMPARE(capturedArgs[QStringLiteral("operation")].toString(), QStringLiteral("untag"));

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

void TestTagService::testListTagsNoHook() {
  QString nbPath = m_tempDir.filePath("th_listtags_nohook_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(m_tagCoreService->createTag(nbId, "NoHookListTag"));

  // Register hooks on all tag-related hook names; none should fire.
  int hookCallCount = 0;
  auto counter = [&hookCallCount](HookContext &, const QVariantMap &) { ++hookCallCount; };

  int id1 = m_hookMgr->addAction(HookNames::TagBeforeCreate, counter, 10);
  int id2 = m_hookMgr->addAction(HookNames::TagAfterCreate, counter, 10);
  int id3 = m_hookMgr->addAction(HookNames::TagBeforeDelete, counter, 10);
  int id4 = m_hookMgr->addAction(HookNames::TagAfterDelete, counter, 10);
  int id5 = m_hookMgr->addAction(HookNames::TagBeforeMove, counter, 10);
  int id6 = m_hookMgr->addAction(HookNames::TagAfterMove, counter, 10);

  QJsonArray tags = m_tagService->listTags(nbId);
  QVERIFY(tags.size() > 0);
  QCOMPARE(hookCallCount, 0);

  m_hookMgr->removeAction(id1);
  m_hookMgr->removeAction(id2);
  m_hookMgr->removeAction(id3);
  m_hookMgr->removeAction(id4);
  m_hookMgr->removeAction(id5);
  m_hookMgr->removeAction(id6);
}

void TestTagService::testSearchByTagsNoHook() {
  QString nbPath = m_tempDir.filePath("th_search_nohook_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(m_tagCoreService->createTag(nbId, "NoHookSearchTag"));
  m_notebookService->createFile(nbId, "", "nohooksearch.md");
  QVERIFY(m_tagCoreService->tagFile(nbId, "nohooksearch.md", "NoHookSearchTag"));

  // Register hooks on all tag and file-tag hook names; none should fire.
  int hookCallCount = 0;
  auto counter = [&hookCallCount](HookContext &, const QVariantMap &) { ++hookCallCount; };

  int id1 = m_hookMgr->addAction(HookNames::TagBeforeCreate, counter, 10);
  int id2 = m_hookMgr->addAction(HookNames::TagAfterCreate, counter, 10);
  int id3 = m_hookMgr->addAction(HookNames::FileBeforeTag, counter, 10);
  int id4 = m_hookMgr->addAction(HookNames::FileAfterTag, counter, 10);
  int id5 = m_hookMgr->addAction(HookNames::FileBeforeUntag, counter, 10);
  int id6 = m_hookMgr->addAction(HookNames::FileAfterUntag, counter, 10);

  QJsonArray results = m_tagService->searchByTags(nbId, QStringList() << "NoHookSearchTag");
  // The search itself should work.
  QVERIFY(results.size() > 0);
  // But no hooks should have fired.
  QCOMPARE(hookCallCount, 0);

  m_hookMgr->removeAction(id1);
  m_hookMgr->removeAction(id2);
  m_hookMgr->removeAction(id3);
  m_hookMgr->removeAction(id4);
  m_hookMgr->removeAction(id5);
  m_hookMgr->removeAction(id6);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTagService)
#include "test_tagservice.moc"
