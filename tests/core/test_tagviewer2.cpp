#include <QtTest>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTemporaryDir>

#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <temp_dir_fixture.h>
#include <widgets/tagviewer2.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTagViewer2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // Construction.
  void testConstruction();

  // setNodeId / tag population.
  void testSetNodeIdBundledNotebook();
  void testSetNodeIdRawNotebook();
  void testTagListPopulation();
  void testCurrentFileTagsSelected();
  void testEmptyTagList();

  // Tag support.
  void testIsTagSupportedBundled();
  void testIsTagSupportedRaw();

  // Toggle selection.
  void testToggleSelection();

  // Search / filter.
  void testSearchFilter();
  void testSearchClear();

  // Save.
  void testSaveNoChanges();
  void testSaveWithChanges();

  // Inline create / select.
  void testInlineCreateTag();
  void testInlineSelectExistingTag();

private:
  // Helper to create a bundled test notebook and return its ID.
  QString createTestNotebook(const QString &p_path);

  // Helper to create a raw test notebook and return its ID.
  QString createRawTestNotebook(const QString &p_path);

  // Retrieve the internal QListWidget from TagViewer2.
  QListWidget *tagList(TagViewer2 *p_viewer) const;

  // Retrieve the internal QLineEdit from TagViewer2.
  QLineEdit *searchEdit(TagViewer2 *p_viewer) const;

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TagCoreService *m_tagCoreService = nullptr;
  HookManager *m_hookMgr = nullptr;
  TagService *m_tagService = nullptr;
  ServiceLocator m_serviceLocator;
  TempDirFixture m_tempDir;
};

void TestTagViewer2::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // Enable test mode to use isolated temp directories instead of real AppData.
  vxcore_set_test_mode(1);

  // Initialize VxCore context.
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create services.
  m_notebookService = new NotebookCoreService(m_context, this);
  QVERIFY(m_notebookService != nullptr);

  m_tagCoreService = new TagCoreService(m_context, this);
  QVERIFY(m_tagCoreService != nullptr);

  m_hookMgr = new HookManager(this);
  m_tagService = new TagService(m_context, m_hookMgr, this);
  QVERIFY(m_tagService != nullptr);

  // Register services in ServiceLocator.
  m_serviceLocator.registerService<NotebookCoreService>(m_notebookService);
  m_serviceLocator.registerService<TagCoreService>(m_tagCoreService);
  m_serviceLocator.registerService<TagService>(m_tagService);
  m_serviceLocator.registerService<HookManager>(m_hookMgr);
  // ThemeService NOT registered — TagViewer2 handles null gracefully (icons will be empty).
}

void TestTagViewer2::cleanupTestCase() {
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

void TestTagViewer2::cleanup() {
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

QString TestTagViewer2::createTestNotebook(const QString &p_path) {
  QString configJson = R"({
    "name": "TagViewer Test Notebook",
    "description": "Test notebook for TagViewer2 tests",
    "version": "1"
  })";
  return m_notebookService->createNotebook(p_path, configJson, NotebookType::Bundled);
}

QString TestTagViewer2::createRawTestNotebook(const QString &p_path) {
  QString configJson = R"({
    "name": "Raw Test Notebook",
    "description": "Raw notebook for TagViewer2 tests",
    "version": "1"
  })";
  return m_notebookService->createNotebook(p_path, configJson, NotebookType::Raw);
}

QListWidget *TestTagViewer2::tagList(TagViewer2 *p_viewer) const {
  auto lists = p_viewer->findChildren<QListWidget *>();
  return lists.isEmpty() ? nullptr : lists.first();
}

QLineEdit *TestTagViewer2::searchEdit(TagViewer2 *p_viewer) const {
  auto edits = p_viewer->findChildren<QLineEdit *>();
  return edits.isEmpty() ? nullptr : edits.first();
}

// ===== Construction =====

void TestTagViewer2::testConstruction() {
  TagViewer2 viewer(m_serviceLocator);
  QVERIFY(tagList(&viewer) != nullptr);
  QVERIFY(searchEdit(&viewer) != nullptr);
  QCOMPARE(tagList(&viewer)->count(), 0);
}

// ===== setNodeId / tag population =====

void TestTagViewer2::testSetNodeIdBundledNotebook() {
  QString nbPath = m_tempDir.filePath("tv2_bundled_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create a file and some tags.
  QString fileId = m_notebookService->createFile(nbId, "", "test.md");
  QVERIFY(!fileId.isEmpty());
  QVERIFY(m_tagCoreService->createTag(nbId, "alpha"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "test.md", "alpha"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "test.md";
  viewer.setNodeId(nodeId);

  QVERIFY(tagList(&viewer)->count() > 0);
}

void TestTagViewer2::testSetNodeIdRawNotebook() {
  QString nbPath = m_tempDir.filePath("tv2_raw_nb");
  QString nbId = createRawTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "";

  // isTagSupported should be false for raw notebooks.
  viewer.setNodeId(nodeId);
  QVERIFY(!viewer.isTagSupported());
}

void TestTagViewer2::testTagListPopulation() {
  QString nbPath = m_tempDir.filePath("tv2_taglist_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create a file and 3 tags.
  m_notebookService->createFile(nbId, "", "note.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "tag1"));
  QVERIFY(m_tagCoreService->createTag(nbId, "tag2"));
  QVERIFY(m_tagCoreService->createTag(nbId, "tag3"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "note.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  QVERIFY(list != nullptr);
  QCOMPARE(list->count(), 3);

  // Verify all 3 tags are present (check via Qt::UserRole data).
  QSet<QString> foundTags;
  for (int i = 0; i < list->count(); ++i) {
    foundTags.insert(list->item(i)->data(Qt::UserRole).toString());
  }
  QVERIFY(foundTags.contains("tag1"));
  QVERIFY(foundTags.contains("tag2"));
  QVERIFY(foundTags.contains("tag3"));
}

void TestTagViewer2::testCurrentFileTagsSelected() {
  QString nbPath = m_tempDir.filePath("tv2_selected_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file and tags, tag the file with "tag1".
  m_notebookService->createFile(nbId, "", "tagged.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "tag1"));
  QVERIFY(m_tagCoreService->createTag(nbId, "tag2"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "tagged.md", "tag1"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "tagged.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  QVERIFY(list != nullptr);

  // Find tag1 and tag2 items, check their UserRole2 selected state.
  bool foundTag1Selected = false;
  bool foundTag2NotSelected = false;
  for (int i = 0; i < list->count(); ++i) {
    auto *item = list->item(i);
    QString tag = item->data(Qt::UserRole).toString();
    bool selected = item->data(UserRole2).toBool();
    if (tag == "tag1" && selected) {
      foundTag1Selected = true;
    }
    if (tag == "tag2" && !selected) {
      foundTag2NotSelected = true;
    }
  }
  QVERIFY(foundTag1Selected);
  QVERIFY(foundTag2NotSelected);
}

void TestTagViewer2::testEmptyTagList() {
  QString nbPath = m_tempDir.filePath("tv2_empty_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create file but no tags.
  m_notebookService->createFile(nbId, "", "empty.md");

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "empty.md";
  viewer.setNodeId(nodeId);

  QCOMPARE(tagList(&viewer)->count(), 0);
}

// ===== Tag support =====

void TestTagViewer2::testIsTagSupportedBundled() {
  QString nbPath = m_tempDir.filePath("tv2_support_bundled_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "file.md");

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "file.md";
  viewer.setNodeId(nodeId);

  QVERIFY(viewer.isTagSupported());
}

void TestTagViewer2::testIsTagSupportedRaw() {
  QString nbPath = m_tempDir.filePath("tv2_support_raw_nb");
  QString nbId = createRawTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "";
  viewer.setNodeId(nodeId);

  QVERIFY(!viewer.isTagSupported());
}

// ===== Toggle selection =====

void TestTagViewer2::testToggleSelection() {
  QString nbPath = m_tempDir.filePath("tv2_toggle_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "toggle.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "toggler"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "toggle.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  QVERIFY(list != nullptr);
  QVERIFY(list->count() > 0);

  auto *item = list->item(0);
  bool wasSel = item->data(UserRole2).toBool();
  QVERIFY(!wasSel); // "toggler" not selected initially.

  // Simulate click to toggle.
  QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                    list->visualItemRect(item).center());

  bool nowSel = item->data(UserRole2).toBool();
  QVERIFY(nowSel); // Should now be selected.

  // Toggle again.
  QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                    list->visualItemRect(item).center());
  QVERIFY(!item->data(UserRole2).toBool());
}

// ===== Search / filter =====

void TestTagViewer2::testSearchFilter() {
  QString nbPath = m_tempDir.filePath("tv2_filter_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "filter.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "apple"));
  QVERIFY(m_tagCoreService->createTag(nbId, "banana"));
  QVERIFY(m_tagCoreService->createTag(nbId, "cherry"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "filter.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  auto *search = searchEdit(&viewer);
  QVERIFY(list != nullptr);
  QVERIFY(search != nullptr);
  QCOMPARE(list->count(), 3);

  // Type search text — only "apple" should match.
  QTest::keyClicks(search, "app");

  int visibleCount = 0;
  for (int i = 0; i < list->count(); ++i) {
    if (!list->item(i)->isHidden()) {
      ++visibleCount;
    }
  }
  QCOMPARE(visibleCount, 1);
}

void TestTagViewer2::testSearchClear() {
  QString nbPath = m_tempDir.filePath("tv2_clear_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "clear.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "dog"));
  QVERIFY(m_tagCoreService->createTag(nbId, "cat"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "clear.md";
  viewer.setNodeId(nodeId);

  auto *search = searchEdit(&viewer);
  auto *list = tagList(&viewer);

  // Filter.
  QTest::keyClicks(search, "dog");
  // Clear.
  search->clear();

  // All items should be visible again.
  int visibleCount = 0;
  for (int i = 0; i < list->count(); ++i) {
    if (!list->item(i)->isHidden()) {
      ++visibleCount;
    }
  }
  QCOMPARE(visibleCount, 2);
}

// ===== Save =====

void TestTagViewer2::testSaveNoChanges() {
  QString nbPath = m_tempDir.filePath("tv2_save_nochange_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "nochange.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "stable"));
  QVERIFY(m_tagCoreService->tagFile(nbId, "nochange.md", "stable"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "nochange.md";
  viewer.setNodeId(nodeId);

  // Save without changes.
  QVERIFY(viewer.save());
}

void TestTagViewer2::testSaveWithChanges() {
  QString nbPath = m_tempDir.filePath("tv2_save_change_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "change.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "newtag"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "change.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  QVERIFY(list != nullptr);
  QVERIFY(list->count() > 0);

  // Toggle "newtag" on.
  auto *item = list->item(0);
  QVERIFY(!item->data(UserRole2).toBool());
  QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                    list->visualItemRect(item).center());
  QVERIFY(item->data(UserRole2).toBool());

  // Save.
  QVERIFY(viewer.save());

  // Verify via getFileInfo.
  auto fileInfo = m_notebookService->getFileInfo(nbId, "change.md");
  auto tagsArray = fileInfo.value("tags").toArray();
  QSet<QString> savedTags;
  for (const auto &t : tagsArray) {
    savedTags.insert(t.toString());
  }
  QVERIFY(savedTags.contains("newtag"));
}

// ===== Inline create / select =====

void TestTagViewer2::testInlineCreateTag() {
  QString nbPath = m_tempDir.filePath("tv2_inline_create_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "inline.md");

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "inline.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  auto *search = searchEdit(&viewer);
  QCOMPARE(list->count(), 0);

  // Type a new tag name and press Enter.
  QTest::keyClicks(search, "brandnew");
  QTest::keyClick(search, Qt::Key_Return);

  // Tag should be created and added to the list with selected state.
  QCOMPARE(list->count(), 1);
  auto *item = list->item(0);
  QCOMPARE(item->data(Qt::UserRole).toString(), QStringLiteral("brandnew"));
  QVERIFY(item->data(UserRole2).toBool());

  // Search edit should be cleared after Return.
  QVERIFY(search->text().isEmpty());

  // Verify tag was actually created in the service.
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool found = false;
  for (const auto &tagVal : tags) {
    if (tagVal.toObject()["name"].toString() == "brandnew") {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestTagViewer2::testInlineSelectExistingTag() {
  QString nbPath = m_tempDir.filePath("tv2_inline_select_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_notebookService->createFile(nbId, "", "select.md");
  QVERIFY(m_tagCoreService->createTag(nbId, "existing"));

  TagViewer2 viewer(m_serviceLocator);
  NodeIdentifier nodeId;
  nodeId.notebookId = nbId;
  nodeId.relativePath = "select.md";
  viewer.setNodeId(nodeId);

  auto *list = tagList(&viewer);
  auto *search = searchEdit(&viewer);
  QCOMPARE(list->count(), 1);

  // Verify tag not selected initially.
  QVERIFY(!list->item(0)->data(UserRole2).toBool());

  // Type exact tag name and press Enter — should select it, not create a new one.
  QTest::keyClicks(search, "existing");
  QTest::keyClick(search, Qt::Key_Return);

  // Still only 1 item (no duplicate created).
  QCOMPARE(list->count(), 1);
  // It should now be selected.
  QVERIFY(list->item(0)->data(UserRole2).toBool());
  // Search cleared.
  QVERIFY(search->text().isEmpty());
}

} // namespace tests

QTEST_MAIN(tests::TestTagViewer2)
#include "test_tagviewer2.moc"
