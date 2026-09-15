#include <QtTest>

#include <QPersistentModelIndex>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTreeView>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookNodeModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Regression test: empty-state contract
  void testEmptyStateAfterSetNotebookId();
  void testRenameExpandedSubtree();

private:
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  ServiceLocator m_services;
};

void TestNotebookNodeModel::initTestCase() {
  // CRITICAL: Enable test mode BEFORE creating vxcore context
  // to prevent tests from corrupting real user data
  vxcore_set_test_mode(1);

  // Initialize VxCore context
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create NotebookCoreService and register it
  m_service = new NotebookCoreService(m_context, this);
  QVERIFY(m_service != nullptr);
  m_services.registerService<NotebookCoreService>(m_service);
}

void TestNotebookNodeModel::cleanupTestCase() {
  // Destroy service before context (service depends on context)
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotebookNodeModel::testEmptyStateAfterSetNotebookId() {
  // Create a temporary directory for the test notebook
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  // Create a test notebook with a child file to ensure populated state
  QString configJson = R"({
    "name": "Test",
    "description": "Test notebook",
    "version": "1"
  })";
  QString nbId =
      m_service->createNotebook(tmpDir.filePath("nb"), configJson, NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Add a child file so the populated assertion is meaningful
  QString fileId = m_service->createFile(nbId, QString(), QStringLiteral("note.md"));
  QVERIFY(!fileId.isEmpty());

  // Create the model and set it to the populated notebook
  NotebookNodeModel model(m_services);
  model.setNotebookId(nbId);

  // Fetch children to populate the model
  QModelIndex root; // invalid == notebook root
  if (model.canFetchMore(root)) {
    model.fetchMore(root);
  }

  // PRE-RESET SANITY: Verify the model is populated before the reset
  int rowCountBefore = model.rowCount(root);
  QVERIFY2(rowCountBefore > 0, "Fixture must be non-empty for a meaningful regression test. "
                               "Model should have at least one child (the created file).");

  // THE TEST: Set notebook ID to empty string
  model.setNotebookId(QString());

  // ASSERTION 1: rowCount must be 0 after reset
  int rowCountAfter = model.rowCount(QModelIndex());
  QCOMPARE(rowCountAfter, 0);

  // ASSERTION 2: getNotebookId must return empty string
  QVERIFY(model.getNotebookId().isEmpty());

  // Cleanup: close the notebook
  m_service->closeNotebook(nbId);
}

void TestNotebookNodeModel::testRenameExpandedSubtree() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());
  const QString nbId = m_service->createNotebook(tmpDir.filePath("nb"), R"({"name":"Rename tree"})",
                                                 NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());
  const auto closeNotebook = qScopeGuard([&]() { m_service->closeNotebook(nbId); });
  for (const auto &path : {"before/child/grandchild", "before/lazy", "before-other"}) {
    QVERIFY(!m_service->createFolderPath(nbId, QLatin1String(path)).isEmpty());
  }
  for (const auto &path : {"before/root.md", "before/child/grandchild/note.md",
                           "before/lazy/later.md", "before-other/untouched.md"}) {
    const QString filePath = QLatin1String(path);
    QVERIFY(!m_service->createFile(nbId, filePath.section('/', 0, -2), filePath.section('/', -1))
                 .isEmpty());
  }
  const auto idFor = [&](const QString &p_path) { return NodeIdentifier{nbId, p_path}; };

  NotebookNodeModel model(m_services);
  model.setNotebookId(nbId);
  model.fetchMore({});
  const QPersistentModelIndex folder = model.indexFromNodeId(idFor("before"));
  const QPersistentModelIndex child = model.indexFromNodeId(idFor("before/child"));
  QVERIFY(folder.isValid());
  QVERIFY(child.isValid());
  model.fetchMore(child);
  const QPersistentModelIndex grandchild = model.indexFromNodeId(idFor("before/child/grandchild"));
  const QPersistentModelIndex note =
      model.indexFromNodeId(idFor("before/child/grandchild/note.md"));
  const QPersistentModelIndex sibling = model.indexFromNodeId(idFor("before-other"));
  QVERIFY(grandchild.isValid());
  QVERIFY(note.isValid());
  QCOMPARE(model.rowCount(folder), 3);

  NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&model);
  proxy.setViewOrder(ViewOrder::OrderedByName);
  proxy.sort(0);
  QTreeView view;
  view.setModel(&proxy);
  view.setUniformRowHeights(true);
  view.setHeaderHidden(true);
  view.resize(480, 400);
  view.show();
  for (const auto &index : {folder, child, grandchild}) {
    view.expand(proxy.mapFromSource(index));
  }
  view.setCurrentIndex(proxy.mapFromSource(note));
  QCoreApplication::processEvents();

  // The inline editor commits through the proxy into setData(). Existing
  // descendant indexes must keep describing the same rows at their new paths.
  QVERIFY(proxy.setData(proxy.mapFromSource(folder), QStringLiteral("renamed"), Qt::EditRole));
  QVERIFY(folder.isValid());
  QVERIFY(note.isValid());
  QCOMPARE(model.rowCount(folder), 3);
  QCOMPARE(model.rowCount(child), 1);
  QCOMPARE(model.rowCount(grandchild), 1);
  QCOMPARE(child.parent(), QModelIndex(folder));
  QCOMPARE(grandchild.parent(), QModelIndex(child));
  QCOMPARE(note.parent(), QModelIndex(grandchild));
  QCOMPARE(model.nodeIdFromIndex(note).relativePath,
           QStringLiteral("renamed/child/grandchild/note.md"));
  QCOMPARE(note.data(NotebookNodeModel::PathRole).toString(),
           QStringLiteral("renamed/child/grandchild/note.md"));
  QVERIFY(!model.indexFromNodeId(idFor("before/child/grandchild/note.md")).isValid());
  QCOMPARE(model.indexFromNodeId(idFor("renamed/child/grandchild/note.md")), QModelIndex(note));
  QCOMPARE(model.nodeIdFromIndex(sibling).relativePath, QStringLiteral("before-other"));
  QCOMPARE(proxy.mapToSource(view.currentIndex()), QModelIndex(note));
  for (const auto &index : {folder, child, grandchild}) {
    QVERIFY(view.isExpanded(proxy.mapFromSource(index)));
  }

  // A conflicting rename must leave the live subtree unchanged.
  QVERIFY(
      !proxy.setData(proxy.mapFromSource(folder), QStringLiteral("before-other"), Qt::EditRole));
  QCOMPARE(model.nodeIdFromIndex(folder).relativePath, QStringLiteral("renamed"));
  QCOMPARE(note.parent(), QModelIndex(grandchild));

  // Also rename an expanded nested folder, then expand a branch first visited
  // after the parent rename. Both cached and newly fetched paths must agree.
  QVERIFY(proxy.setData(proxy.mapFromSource(child), QStringLiteral("nested"), Qt::EditRole));
  QCOMPARE(model.nodeIdFromIndex(note).relativePath,
           QStringLiteral("renamed/nested/grandchild/note.md"));
  const QModelIndex lazy = model.indexFromNodeId(idFor("renamed/lazy"));
  QVERIFY(lazy.isValid());
  view.expand(proxy.mapFromSource(lazy));
  QCoreApplication::processEvents();
  QCOMPARE(model.rowCount(lazy), 1);
  QCOMPARE(model.nodeIdFromIndex(model.index(0, 0, lazy)).relativePath,
           QStringLiteral("renamed/lazy/later.md"));

  // File renames must also retain the selected descendant's identity.
  QVERIFY(proxy.setData(proxy.mapFromSource(note), QStringLiteral("renamed.md"), Qt::EditRole));
  QCOMPARE(note.data().toString(), QStringLiteral("renamed.md"));

  // Repeated folding must not accumulate invisible rows or blank vertical space.
  for (int cycle = 0; cycle < 3; ++cycle) {
    const QModelIndex proxyFolder = proxy.mapFromSource(folder);
    view.collapse(proxyFolder);
    view.expand(proxyFolder);
    QCoreApplication::processEvents();
    view.doItemsLayout();
    QStringList visiblePaths;
    int bottom = -1;
    for (QModelIndex index = proxy.index(0, 0); index.isValid(); index = view.indexBelow(index)) {
      const QRect rect = view.visualRect(index);
      QVERIFY(rect.isValid());
      if (bottom >= 0) {
        QCOMPARE(rect.top(), bottom + 1);
      }
      QCOMPARE(view.indexAt(rect.center()), index);
      bottom = rect.bottom();
      visiblePaths.append(index.data(NotebookNodeModel::PathRole).toString());
    }
    QCOMPARE(visiblePaths,
             QStringList({"before-other", "renamed", "renamed/lazy", "renamed/lazy/later.md",
                          "renamed/nested", "renamed/nested/grandchild",
                          "renamed/nested/grandchild/renamed.md", "renamed/root.md"}));
    QCOMPARE(proxy.mapToSource(view.currentIndex()), QModelIndex(note));
  }
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookNodeModel)
#include "test_notebooknodemodel.moc"
