#include <QFile>
#include <QStringList>
#include <QVector>
#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookHooks : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Delete file (4)
  void testDeleteFile_HappyPath_FiresBeforeAndAfter();
  void testDeleteFile_BeforeHookCancels_VxcoreNotCalled_NoAfterHook();
  void testDeleteFile_VxcoreFailure_NoAfterHook();
  void testDeleteFile_EventPayloadName_ExtractsBasename();

  // Delete folder (2)
  void testDeleteFolder_HappyPath_FiresBeforeAndAfter_IsFolderTrue();
  void testDeleteFolder_BeforeHookCancels_FolderRemains();

  // Move file (5)
  void testMoveFile_HappyPath_FiresBeforeAndAfter();
  void testMoveFile_NewPath_RootTargetEmptyString();
  void testMoveFile_NewPath_RootTargetDot();
  void testMoveFile_NewPath_NestedSubfolder();
  void testMoveFile_BeforeHookCancels_NoVxcoreCallNoAfterHook();

  // Move folder (3)
  void testMoveFolder_HappyPath_FiresAfterWithIsFolderTrue();
  void testMoveFolder_NewPath_RootTarget();
  void testMoveFolder_NewPath_Nested();

  // Rename regression guards (2)
  void testRenameFile_HappyPath_FiresBeforeAndAfterWithNames();
  void testRenameFolder_HappyPath_FiresBeforeAndAfter();

private:
  QString createTestNotebook(const QString &p_path);
  void subscribeAllHooks();

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  HookManager *m_hookMgr = nullptr;
  TempDirFixture m_tempDir;
  QString m_nbId;
  int m_nbCounter = 0;

  // Capture vectors
  QVector<NodeOperationEvent> m_beforeDeletes;
  QVector<NodeOperationEvent> m_afterDeletes;
  QVector<NodeOperationEvent> m_beforeMoves;
  QVector<NodeMoveEvent> m_afterMoves;
  QVector<NodeRenameEvent> m_beforeRenames;
  QVector<NodeRenameEvent> m_afterRenames;
  QStringList m_hookOrder;
  QVector<int> m_hookIds;
};

void TestNotebookHooks::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);

  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_service = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_service->setHookManager(m_hookMgr);
}

void TestNotebookHooks::cleanupTestCase() {
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_service;
  m_service = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

QString TestNotebookHooks::createTestNotebook(const QString &p_path) {
  QString configJson = R"({"name":"NB","description":"","version":"1"})";
  return m_service->createNotebook(p_path, configJson, NotebookType::Bundled);
}

void TestNotebookHooks::subscribeAllHooks() {
  m_hookIds.append(m_hookMgr->addAction<NodeOperationEvent>(
      HookNames::NodeBeforeDelete,
      [this](HookContext &, const NodeOperationEvent &ev) {
        m_beforeDeletes.append(ev);
        m_hookOrder.append(QStringLiteral("before_delete"));
      },
      10));
  m_hookIds.append(m_hookMgr->addAction<NodeOperationEvent>(
      HookNames::NodeAfterDelete,
      [this](HookContext &, const NodeOperationEvent &ev) {
        m_afterDeletes.append(ev);
        m_hookOrder.append(QStringLiteral("after_delete"));
      },
      10));
  m_hookIds.append(m_hookMgr->addAction<NodeOperationEvent>(
      HookNames::NodeBeforeMove,
      [this](HookContext &, const NodeOperationEvent &ev) {
        m_beforeMoves.append(ev);
        m_hookOrder.append(QStringLiteral("before_move"));
      },
      10));
  m_hookIds.append(m_hookMgr->addAction<NodeMoveEvent>(
      HookNames::NodeAfterMove,
      [this](HookContext &, const NodeMoveEvent &ev) {
        m_afterMoves.append(ev);
        m_hookOrder.append(QStringLiteral("after_move"));
      },
      10));
  m_hookIds.append(m_hookMgr->addAction<NodeRenameEvent>(
      HookNames::NodeBeforeRename,
      [this](HookContext &, const NodeRenameEvent &ev) {
        m_beforeRenames.append(ev);
        m_hookOrder.append(QStringLiteral("before_rename"));
      },
      10));
  m_hookIds.append(m_hookMgr->addAction<NodeRenameEvent>(
      HookNames::NodeAfterRename,
      [this](HookContext &, const NodeRenameEvent &ev) {
        m_afterRenames.append(ev);
        m_hookOrder.append(QStringLiteral("after_rename"));
      },
      10));
}

void TestNotebookHooks::init() {
  // Fresh notebook for each test.
  m_nbCounter++;
  QString path = m_tempDir.filePath(QStringLiteral("hook_nb_%1").arg(m_nbCounter));
  m_nbId = createTestNotebook(path);
  QVERIFY(!m_nbId.isEmpty());

  // Clear capture state.
  m_beforeDeletes.clear();
  m_afterDeletes.clear();
  m_beforeMoves.clear();
  m_afterMoves.clear();
  m_beforeRenames.clear();
  m_afterRenames.clear();
  m_hookOrder.clear();
  m_hookIds.clear();

  subscribeAllHooks();
}

void TestNotebookHooks::cleanup() {
  for (int id : m_hookIds) {
    m_hookMgr->removeAction(id);
  }
  m_hookIds.clear();
  if (!m_nbId.isEmpty()) {
    m_service->closeNotebook(m_nbId);
    m_nbId.clear();
  }
}

// ===== Delete file =====

void TestNotebookHooks::testDeleteFile_HappyPath_FiresBeforeAndAfter() {
  QVERIFY(!m_service->createFile(m_nbId, "", "del.md").isEmpty());

  QVERIFY(m_service->deleteFile(m_nbId, "del.md"));

  QCOMPARE(m_beforeDeletes.size(), 1);
  QCOMPARE(m_afterDeletes.size(), 1);
  QCOMPARE(m_hookOrder, QStringList({"before_delete", "after_delete"}));

  const auto &be = m_beforeDeletes.first();
  QCOMPARE(be.notebookId, m_nbId);
  QCOMPARE(be.relativePath, QStringLiteral("del.md"));
  QCOMPARE(be.isFolder, false);
  QCOMPARE(be.name, QStringLiteral("del.md"));
  QCOMPARE(be.operation, QStringLiteral("delete"));

  const auto &ae = m_afterDeletes.first();
  QCOMPARE(ae.relativePath, QStringLiteral("del.md"));
  QCOMPARE(ae.isFolder, false);
  QCOMPARE(ae.operation, QStringLiteral("delete"));
}

void TestNotebookHooks::testDeleteFile_BeforeHookCancels_VxcoreNotCalled_NoAfterHook() {
  QVERIFY(!m_service->createFile(m_nbId, "", "keep.md").isEmpty());

  int cancelId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete, [](HookContext &ctx, const QVariantMap &) { ctx.cancel(); }, 5);

  bool result = m_service->deleteFile(m_nbId, "keep.md");
  QVERIFY(!result);

  QCOMPARE(m_afterDeletes.size(), 0);
  QVERIFY(!m_hookOrder.contains(QStringLiteral("after_delete")));

  // File still exists.
  QVERIFY(!m_service->getFileInfo(m_nbId, "keep.md").isEmpty());

  m_hookMgr->removeAction(cancelId);
}

void TestNotebookHooks::testDeleteFile_VxcoreFailure_NoAfterHook() {
  // Non-existent file. NodeBeforeDelete fires unconditionally; NodeAfterDelete only on success.
  // vxcore_node_delete will log a qWarning we must ignore so the test isn't flagged.
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression("deleteFile failed:.*"));

  bool result = m_service->deleteFile(m_nbId, "does_not_exist.md");
  QVERIFY(!result);

  QCOMPARE(m_beforeDeletes.size(), 1);
  QCOMPARE(m_afterDeletes.size(), 0);
}

void TestNotebookHooks::testDeleteFile_EventPayloadName_ExtractsBasename() {
  QVERIFY(!m_service->createFolderPath(m_nbId, "dir/sub").isEmpty());
  QVERIFY(!m_service->createFile(m_nbId, "dir/sub", "file.md").isEmpty());

  QVERIFY(m_service->deleteFile(m_nbId, "dir/sub/file.md"));

  QCOMPARE(m_beforeDeletes.size(), 1);
  QCOMPARE(m_beforeDeletes.first().name, QStringLiteral("file.md"));
  QCOMPARE(m_beforeDeletes.first().relativePath, QStringLiteral("dir/sub/file.md"));
}

// ===== Delete folder =====

void TestNotebookHooks::testDeleteFolder_HappyPath_FiresBeforeAndAfter_IsFolderTrue() {
  QVERIFY(!m_service->createFolder(m_nbId, "", "DelFolder").isEmpty());

  QVERIFY(m_service->deleteFolder(m_nbId, "DelFolder"));

  QCOMPARE(m_beforeDeletes.size(), 1);
  QCOMPARE(m_afterDeletes.size(), 1);
  QCOMPARE(m_hookOrder, QStringList({"before_delete", "after_delete"}));
  QCOMPARE(m_beforeDeletes.first().isFolder, true);
  QCOMPARE(m_afterDeletes.first().isFolder, true);
  QCOMPARE(m_beforeDeletes.first().name, QStringLiteral("DelFolder"));
}

void TestNotebookHooks::testDeleteFolder_BeforeHookCancels_FolderRemains() {
  QVERIFY(!m_service->createFolder(m_nbId, "", "StayFolder").isEmpty());

  int cancelId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete, [](HookContext &ctx, const QVariantMap &) { ctx.cancel(); }, 5);

  QVERIFY(!m_service->deleteFolder(m_nbId, "StayFolder"));
  QCOMPARE(m_afterDeletes.size(), 0);
  QVERIFY(!m_service->getFolderConfig(m_nbId, "StayFolder").isEmpty());

  m_hookMgr->removeAction(cancelId);
}

// ===== Move file =====

void TestNotebookHooks::testMoveFile_HappyPath_FiresBeforeAndAfter() {
  QVERIFY(!m_service->createFile(m_nbId, "", "mv.md").isEmpty());
  QVERIFY(!m_service->createFolder(m_nbId, "", "Dest").isEmpty());

  QVERIFY(m_service->moveFile(m_nbId, "mv.md", "Dest"));

  QCOMPARE(m_beforeMoves.size(), 1);
  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_hookOrder, QStringList({"before_move", "after_move"}));

  const auto &be = m_beforeMoves.first();
  QCOMPARE(be.relativePath, QStringLiteral("mv.md"));
  QCOMPARE(be.isFolder, false);
  QCOMPARE(be.operation, QStringLiteral("move"));

  const auto &ae = m_afterMoves.first();
  QCOMPARE(ae.notebookId, m_nbId);
  QCOMPARE(ae.oldRelativePath, QStringLiteral("mv.md"));
  QCOMPARE(ae.newRelativePath, QStringLiteral("Dest/mv.md"));
  QCOMPARE(ae.isFolder, false);
}

void TestNotebookHooks::testMoveFile_NewPath_RootTargetEmptyString() {
  QVERIFY(!m_service->createFolder(m_nbId, "", "dir").isEmpty());
  QVERIFY(!m_service->createFile(m_nbId, "dir", "a.md").isEmpty());

  QVERIFY(m_service->moveFile(m_nbId, "dir/a.md", ""));

  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_afterMoves.last().newRelativePath, QStringLiteral("a.md"));
  QCOMPARE(m_afterMoves.last().oldRelativePath, QStringLiteral("dir/a.md"));
}

void TestNotebookHooks::testMoveFile_NewPath_RootTargetDot() {
  QVERIFY(!m_service->createFolder(m_nbId, "", "dir").isEmpty());
  QVERIFY(!m_service->createFile(m_nbId, "dir", "a.md").isEmpty());

  QVERIFY(m_service->moveFile(m_nbId, "dir/a.md", "."));

  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_afterMoves.last().newRelativePath, QStringLiteral("a.md"));
}

void TestNotebookHooks::testMoveFile_NewPath_NestedSubfolder() {
  QVERIFY(!m_service->createFile(m_nbId, "", "a.md").isEmpty());
  QVERIFY(!m_service->createFolderPath(m_nbId, "dir1/dir2").isEmpty());

  QVERIFY(m_service->moveFile(m_nbId, "a.md", "dir1/dir2"));

  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_afterMoves.last().newRelativePath, QStringLiteral("dir1/dir2/a.md"));
}

void TestNotebookHooks::testMoveFile_BeforeHookCancels_NoVxcoreCallNoAfterHook() {
  QVERIFY(!m_service->createFile(m_nbId, "", "cancel_mv.md").isEmpty());
  QVERIFY(!m_service->createFolder(m_nbId, "", "Dest").isEmpty());

  int cancelId = m_hookMgr->addAction(
      HookNames::NodeBeforeMove, [](HookContext &ctx, const QVariantMap &) { ctx.cancel(); }, 5);

  QVERIFY(!m_service->moveFile(m_nbId, "cancel_mv.md", "Dest"));
  QCOMPARE(m_afterMoves.size(), 0);

  // File remains at origin; not at destination.
  QVERIFY(!m_service->getFileInfo(m_nbId, "cancel_mv.md").isEmpty());
  QVERIFY(m_service->getFileInfo(m_nbId, "Dest/cancel_mv.md").isEmpty());

  m_hookMgr->removeAction(cancelId);
}

// ===== Move folder =====

void TestNotebookHooks::testMoveFolder_HappyPath_FiresAfterWithIsFolderTrue() {
  QVERIFY(!m_service->createFolder(m_nbId, "", "MvFolder").isEmpty());
  QVERIFY(!m_service->createFolder(m_nbId, "", "Parent").isEmpty());

  QVERIFY(m_service->moveFolder(m_nbId, "MvFolder", "Parent"));

  QCOMPARE(m_beforeMoves.size(), 1);
  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_beforeMoves.first().isFolder, true);
  QCOMPARE(m_afterMoves.first().isFolder, true);
  QCOMPARE(m_afterMoves.first().newRelativePath, QStringLiteral("Parent/MvFolder"));
  QCOMPARE(m_afterMoves.first().oldRelativePath, QStringLiteral("MvFolder"));
}

void TestNotebookHooks::testMoveFolder_NewPath_RootTarget() {
  // Folder at "Outer/Inner" moved to root "" -> newRelativePath = "Inner".
  QVERIFY(!m_service->createFolderPath(m_nbId, "Outer/Inner").isEmpty());

  QVERIFY(m_service->moveFolder(m_nbId, "Outer/Inner", ""));

  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_afterMoves.last().newRelativePath, QStringLiteral("Inner"));
}

void TestNotebookHooks::testMoveFolder_NewPath_Nested() {
  // Move "a/b" into "c/d" -> "c/d/b".
  QVERIFY(!m_service->createFolderPath(m_nbId, "a/b").isEmpty());
  QVERIFY(!m_service->createFolderPath(m_nbId, "c/d").isEmpty());

  QVERIFY(m_service->moveFolder(m_nbId, "a/b", "c/d"));

  QCOMPARE(m_afterMoves.size(), 1);
  QCOMPARE(m_afterMoves.last().newRelativePath, QStringLiteral("c/d/b"));
}

// ===== Rename regression guards =====

void TestNotebookHooks::testRenameFile_HappyPath_FiresBeforeAndAfterWithNames() {
  QVERIFY(!m_service->createFile(m_nbId, "", "orig.md").isEmpty());

  QVERIFY(m_service->renameFile(m_nbId, "orig.md", "new.md"));

  QCOMPARE(m_beforeRenames.size(), 1);
  QCOMPARE(m_afterRenames.size(), 1);
  QCOMPARE(m_hookOrder, QStringList({"before_rename", "after_rename"}));

  QCOMPARE(m_beforeRenames.first().oldName, QStringLiteral("orig.md"));
  QCOMPARE(m_beforeRenames.first().newName, QStringLiteral("new.md"));
  QCOMPARE(m_beforeRenames.first().isFolder, false);

  QCOMPARE(m_afterRenames.first().oldName, QStringLiteral("orig.md"));
  QCOMPARE(m_afterRenames.first().newName, QStringLiteral("new.md"));
  QCOMPARE(m_afterRenames.first().isFolder, false);
}

void TestNotebookHooks::testRenameFolder_HappyPath_FiresBeforeAndAfter() {
  QVERIFY(!m_service->createFolder(m_nbId, "", "OrigFolder").isEmpty());

  QVERIFY(m_service->renameFolder(m_nbId, "OrigFolder", "NewFolder"));

  QCOMPARE(m_beforeRenames.size(), 1);
  QCOMPARE(m_afterRenames.size(), 1);
  QCOMPARE(m_hookOrder, QStringList({"before_rename", "after_rename"}));
  QCOMPARE(m_beforeRenames.first().isFolder, true);
  QCOMPARE(m_afterRenames.first().isFolder, true);
  QCOMPARE(m_afterRenames.first().oldName, QStringLiteral("OrigFolder"));
  QCOMPARE(m_afterRenames.first().newName, QStringLiteral("NewFolder"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookHooks)
#include "test_notebook_hooks.moc"
