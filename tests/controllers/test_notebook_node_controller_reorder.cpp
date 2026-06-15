// T7 + T8 (notebook-explorer-drag-reorder): NotebookNodeController::reorderNodes
// and NotebookNodeController::sortNodes signal emitter.
//
// T7 — Verifies the controller-layer reorder dispatch contract:
//   - Valid same-parent input → converted to QStringList names + dispatched
//   - Mixed-parent input (folders) → rejected pre-dispatch, errorOccurred emitted
//   - Mixed-parent input (files) → same
//   - Empty input → no-op (no service call, no error)
//   - Service success → nodesReordered emitted
//   - Service failure → errorOccurred emitted, nodesReordered NOT emitted
//
// T8 — Verifies sortNodes is now a thin signal emitter (no QDialog ownership):
//   - sortNodes(validParentId) emits sortRequested(parentId) exactly once
//   - sortNodes(invalidParentId) with no active notebook → no signal (no-op)
//   - sortNodes(invalidParentId) with active notebook → emits with NodeIdentifier
//     of notebook root ({currentNotebookId, ""})
//
// Mock-service strategy: subclass NotebookCoreService and override
// reorderFolderChildren (made virtual for this seam) to record call args
// without dispatching to vxcore. simulateReorderCompleted() emits the public
// reorderCompleted signal so the controller's onReorderCompleted slot fires
// through the real signal/slot wiring.
//
// T8 deferred sub-tests: tests 10/11/12 from the plan (createContextMenu
// returns a Sort... action; trigger emits sortRequested; action always
// enabled) are intentionally NOT implemented here. The menu helpers
// (addMiscActions, createContextMenu) live in notebooknodecontroller.cpp,
// which is NOT compiled into this test target (the small-TU
// notebooknodecontroller_reorder.cpp pattern keeps test scope narrow). The
// menu-trigger path is exercised by the T11 integration test against
// NotebookExplorer2 instead. Pulling the full controller TU here would drag
// in NotebookCoreService config queries, NotebookNodeModel, theme, and the
// rest of the UI dependency graph - the whole reason the small TU exists.

#include <QSignalSpy>
#include <QStringList>
#include <QtTest>

#include <vxcore/vxcore.h>

#include <controllers/notebooknodecontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <models/inodelistmodel.h>

namespace tests {

// Recording subclass: intercepts reorderFolderChildren without invoking vxcore.
class RecordingNotebookCoreService : public vnotex::NotebookCoreService {
public:
  explicit RecordingNotebookCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr)
      : vnotex::NotebookCoreService(p_context, p_parent) {}

  void reorderFolderChildren(const QString &p_notebookId, const QString &p_folderRelPath,
                             const QStringList &p_orderedFolders,
                             const QStringList &p_orderedFiles) override {
    ++callCount;
    lastNotebookId = p_notebookId;
    lastFolderRelPath = p_folderRelPath;
    lastOrderedFolders = p_orderedFolders;
    lastOrderedFiles = p_orderedFiles;
  }

  // Test helper: drive reorderCompleted to simulate worker callback.
  void simulateReorderCompleted(const QString &p_notebookId, const QString &p_folderRelPath,
                                bool p_success, const QString &p_errorMessage) {
    emit reorderCompleted(p_notebookId, p_folderRelPath, p_success, p_errorMessage);
  }

  int callCount = 0;
  QString lastNotebookId;
  QString lastFolderRelPath;
  QStringList lastOrderedFolders;
  QStringList lastOrderedFiles;
};

// T8: Minimal INodeListModel stub so the controller's currentNotebookId()
// helper returns a configurable value. Pure virtuals are stubbed (the
// sortNodes path never touches them).
class StubNodeListModel : public vnotex::INodeListModel {
public:
  explicit StubNodeListModel(QString p_notebookId) : m_notebookId(std::move(p_notebookId)) {}

  QString getNotebookId() const override { return m_notebookId; }

  vnotex::NodeIdentifier nodeIdFromIndex(const QModelIndex &) const override { return {}; }
  vnotex::NodeInfo nodeInfoFromIndex(const QModelIndex &) const override { return {}; }
  QModelIndex indexFromNodeId(const vnotex::NodeIdentifier &) const override { return {}; }

private:
  QString m_notebookId;
};

class TestNotebookNodeControllerReorder : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // 6 sub-tests called out in T7 acceptance criteria.
  void validSameParentReorder_dispatchesToService();
  void mixedParentFolderInput_rejectsBeforeDispatch();
  void mixedParentFileInput_rejectsBeforeDispatch();
  void emptyInput_isNoOp();
  void serviceSuccess_emitsNodesReordered();
  void serviceFailure_emitsErrorOccurred_andSkipsNodesReordered();

  // T8 acceptance criteria — sortNodes signal emitter contract.
  void sortNodesEmitsSortRequested();
  void sortNodesWithInvalidParentNoActiveNotebook();
  void sortNodesWithInvalidParentActiveNotebook();

private:
  VxCoreContextHandle m_ctx = nullptr;
  vnotex::ServiceLocator *m_services = nullptr;
  RecordingNotebookCoreService *m_service = nullptr;
  vnotex::NotebookNodeController *m_controller = nullptr;
};

void TestNotebookNodeControllerReorder::initTestCase() {
  vxcore_set_test_mode(1);
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);
}

void TestNotebookNodeControllerReorder::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestNotebookNodeControllerReorder::init() {
  m_services = new vnotex::ServiceLocator();
  m_service = new RecordingNotebookCoreService(m_ctx);
  m_services->registerService<vnotex::NotebookCoreService>(m_service);
  m_controller = new vnotex::NotebookNodeController(*m_services);
}

void TestNotebookNodeControllerReorder::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  delete m_service;
  m_service = nullptr;
  delete m_services;
  m_services = nullptr;
}

// Sub-test 1: valid same-parent reorder converts NodeIdentifier lists to name
// QStringLists and dispatches exactly one call with the expected arguments.
void TestNotebookNodeControllerReorder::validSameParentReorder_dispatchesToService() {
  vnotex::NodeIdentifier parentId{QStringLiteral("nb-1"), QStringLiteral("folder")};

  vnotex::NodeIdentifier folderA{QStringLiteral("nb-1"), QStringLiteral("folder/sub-a")};
  vnotex::NodeIdentifier folderB{QStringLiteral("nb-1"), QStringLiteral("folder/sub-b")};

  vnotex::NodeIdentifier fileX{QStringLiteral("nb-1"), QStringLiteral("folder/x.md")};
  vnotex::NodeIdentifier fileY{QStringLiteral("nb-1"), QStringLiteral("folder/y.md")};

  QSignalSpy errorSpy(m_controller, &vnotex::NotebookNodeController::errorOccurred);

  m_controller->reorderNodes(parentId, {folderB, folderA}, {fileY, fileX});

  QCOMPARE(m_service->callCount, 1);
  QCOMPARE(m_service->lastNotebookId, QStringLiteral("nb-1"));
  QCOMPARE(m_service->lastFolderRelPath, QStringLiteral("folder"));
  QCOMPARE(m_service->lastOrderedFolders, (QStringList{"sub-b", "sub-a"}));
  QCOMPARE(m_service->lastOrderedFiles, (QStringList{"y.md", "x.md"}));
  QCOMPARE(errorSpy.count(), 0);
}

// Sub-test 2: mixed-parent folder IDs short-circuit the dispatch and surface
// errorOccurred so the View can show a user-facing message.
void TestNotebookNodeControllerReorder::mixedParentFolderInput_rejectsBeforeDispatch() {
  vnotex::NodeIdentifier parentId{QStringLiteral("nb-1"), QStringLiteral("folder")};
  vnotex::NodeIdentifier matchingChild{QStringLiteral("nb-1"), QStringLiteral("folder/sub-a")};
  vnotex::NodeIdentifier orphanChild{QStringLiteral("nb-1"), QStringLiteral("other/sub-b")};

  QSignalSpy errorSpy(m_controller, &vnotex::NotebookNodeController::errorOccurred);

  m_controller->reorderNodes(parentId, {matchingChild, orphanChild}, {});

  QCOMPARE(m_service->callCount, 0);
  QCOMPARE(errorSpy.count(), 1);
  const QList<QVariant> args = errorSpy.takeFirst();
  QCOMPARE(args.size(), 2);
  QVERIFY(!args.at(0).toString().isEmpty()); // title
  QVERIFY(!args.at(1).toString().isEmpty()); // message
}

// Sub-test 3: same protection applies when only the file IDs straddle parents.
void TestNotebookNodeControllerReorder::mixedParentFileInput_rejectsBeforeDispatch() {
  vnotex::NodeIdentifier parentId{QStringLiteral("nb-1"), QStringLiteral("folder")};
  vnotex::NodeIdentifier matchingFolder{QStringLiteral("nb-1"), QStringLiteral("folder/sub-a")};
  vnotex::NodeIdentifier matchingFile{QStringLiteral("nb-1"), QStringLiteral("folder/x.md")};
  vnotex::NodeIdentifier orphanFile{QStringLiteral("nb-1"), QStringLiteral("other/y.md")};

  QSignalSpy errorSpy(m_controller, &vnotex::NotebookNodeController::errorOccurred);

  m_controller->reorderNodes(parentId, {matchingFolder}, {matchingFile, orphanFile});

  QCOMPARE(m_service->callCount, 0);
  QCOMPARE(errorSpy.count(), 1);
}

// Sub-test 4: empty input is a hard no-op (no service call, no error signal,
// no success signal). The View should not see anything.
void TestNotebookNodeControllerReorder::emptyInput_isNoOp() {
  vnotex::NodeIdentifier parentId{QStringLiteral("nb-1"), QStringLiteral("folder")};

  QSignalSpy errorSpy(m_controller, &vnotex::NotebookNodeController::errorOccurred);
  QSignalSpy reorderedSpy(m_controller, &vnotex::NotebookNodeController::nodesReordered);

  m_controller->reorderNodes(parentId, {}, {});

  QCOMPARE(m_service->callCount, 0);
  QCOMPARE(errorSpy.count(), 0);
  QCOMPARE(reorderedSpy.count(), 0);
}

// Sub-test 5: service success flows through the reorderCompleted → onReorderCompleted
// → nodesReordered chain wired by the controller constructor.
void TestNotebookNodeControllerReorder::serviceSuccess_emitsNodesReordered() {
  vnotex::NodeIdentifier parentId{QStringLiteral("nb-1"), QStringLiteral("folder")};

  QSignalSpy reorderedSpy(m_controller, &vnotex::NotebookNodeController::nodesReordered);
  QSignalSpy errorSpy(m_controller, &vnotex::NotebookNodeController::errorOccurred);

  m_service->simulateReorderCompleted(QStringLiteral("nb-1"), QStringLiteral("folder"), true,
                                      QString());

  QCOMPARE(reorderedSpy.count(), 1);
  QCOMPARE(errorSpy.count(), 0);

  const QList<QVariant> args = reorderedSpy.takeFirst();
  QCOMPARE(args.size(), 1);
  const auto emittedParent = args.at(0).value<vnotex::NodeIdentifier>();
  QCOMPARE(emittedParent, parentId);
}

// Sub-test 6: service failure surfaces via errorOccurred with the worker's
// translated message and MUST NOT trigger nodesReordered.
void TestNotebookNodeControllerReorder::serviceFailure_emitsErrorOccurred_andSkipsNodesReordered() {
  QSignalSpy reorderedSpy(m_controller, &vnotex::NotebookNodeController::nodesReordered);
  QSignalSpy errorSpy(m_controller, &vnotex::NotebookNodeController::errorOccurred);

  const QString failureMessage = QStringLiteral("vx.json write failed");
  m_service->simulateReorderCompleted(QStringLiteral("nb-1"), QStringLiteral("folder"), false,
                                      failureMessage);

  QCOMPARE(reorderedSpy.count(), 0);
  QCOMPARE(errorSpy.count(), 1);
  const QList<QVariant> args = errorSpy.takeFirst();
  QCOMPARE(args.size(), 2);
  QVERIFY(!args.at(0).toString().isEmpty()); // title
  QCOMPARE(args.at(1).toString(), failureMessage);
}

// T8 sub-test 7: sortNodes with a valid parent emits sortRequested with that
// exact parent. The controller MUST NOT touch the service (sort dispatch is
// the View's job after the user picks an order from SortDialog2).
void TestNotebookNodeControllerReorder::sortNodesEmitsSortRequested() {
  vnotex::NodeIdentifier parentId{QStringLiteral("nb-1"), QStringLiteral("folder")};

  QSignalSpy sortSpy(m_controller, &vnotex::NotebookNodeController::sortRequested);

  m_controller->sortNodes(parentId);

  QCOMPARE(sortSpy.count(), 1);
  QCOMPARE(m_service->callCount, 0); // sort must NOT call reorder service
  const QList<QVariant> args = sortSpy.takeFirst();
  QCOMPARE(args.size(), 1);
  const auto emittedParent = args.at(0).value<vnotex::NodeIdentifier>();
  QCOMPARE(emittedParent, parentId);
}

// T8 sub-test 8: sortNodes(invalid) with NO active notebook (no model set) is
// a hard no-op. currentNotebookId() returns empty, the function early-returns,
// no signal fires.
void TestNotebookNodeControllerReorder::sortNodesWithInvalidParentNoActiveNotebook() {
  // The fixture's init() never calls setModel(), so currentNotebookId()
  // returns QString() (default INodeListModel impl returns empty).
  vnotex::NodeIdentifier invalidParentId; // notebookId="" -> isValid() false

  QSignalSpy sortSpy(m_controller, &vnotex::NotebookNodeController::sortRequested);

  m_controller->sortNodes(invalidParentId);

  QCOMPARE(sortSpy.count(), 0);
  QCOMPARE(m_service->callCount, 0);
}

// T8 sub-test 9: sortNodes(invalid) WITH an active notebook resolves to the
// notebook root (relativePath ="") and emits sortRequested.
void TestNotebookNodeControllerReorder::sortNodesWithInvalidParentActiveNotebook() {
  StubNodeListModel model(QStringLiteral("nb-123"));
  m_controller->setModel(&model);

  vnotex::NodeIdentifier invalidParentId; // isValid() false

  QSignalSpy sortSpy(m_controller, &vnotex::NotebookNodeController::sortRequested);

  m_controller->sortNodes(invalidParentId);

  QCOMPARE(sortSpy.count(), 1);
  QCOMPARE(m_service->callCount, 0);
  const QList<QVariant> args = sortSpy.takeFirst();
  QCOMPARE(args.size(), 1);
  const auto emittedParent = args.at(0).value<vnotex::NodeIdentifier>();
  QCOMPARE(emittedParent.notebookId, QStringLiteral("nb-123"));
  QCOMPARE(emittedParent.relativePath, QString()); // notebook root

  // Detach before fixture cleanup tears down the controller.
  m_controller->setModel(nullptr);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeControllerReorder)
#include "test_notebook_node_controller_reorder.moc"
