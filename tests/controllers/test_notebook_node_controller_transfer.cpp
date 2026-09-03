#include <QFile>
#include <QtTest>

#include <controllers/notebooknodecontroller.h>

namespace tests {

vnotex::NodeTransferItemResult transferResult(vnotex::NodeTransferItemResult::Status p_status,
                                              VxCoreError p_error,
                                              const QString &p_destinationPath = QString(),
                                              const QJsonObject &p_resumeToken = QJsonObject());

class TestNotebookNodeControllerTransfer : public QObject {
  Q_OBJECT

private slots:
  void ordinaryFileClipboardConfigKeepsFileKind();
  void sameNotebookRoutingUsesOriginalIdentifiers();
  void crossNotebookRequestIsOneStructuredBatch();
  void cutApplyRemovesOnlyMovedAndStoresResume();
  void copyApplyKeepsClipboardAndUsesReturnedDestination();
  void failedBatchDoesNotCommitDestination();
  void missingSourcesAreConsolidated();
  void unresolvedEntriesAreNotMissingSources();
  void explorersForwardThroughTheInterfaceApplyPath();
};

using Controller = vnotex::NotebookNodeController;

Controller::ClipboardItem clipboardItem(const QString &p_notebookId, const QString &p_path,
                                        const QString &p_stableId, bool p_isFolder) {
  Controller::ClipboardItem item;
  item.originalId = {p_notebookId, p_path};
  item.stableNodeId = p_stableId;
  item.hasKind = true;
  item.isFolder = p_isFolder;
  return item;
}

void TestNotebookNodeControllerTransfer::ordinaryFileClipboardConfigKeepsFileKind() {
  const vnotex::NodeIdentifier original{QStringLiteral("source"), QStringLiteral("note.md")};
  const QJsonObject config{{QStringLiteral("id"), QStringLiteral("stable-note")},
                           {QStringLiteral("type"), QStringLiteral("file")}};

  const auto item = Controller::clipboardItemFromNodeConfig(original, config);
  QCOMPARE(item.originalId, original);
  QCOMPARE(item.stableNodeId, QStringLiteral("stable-note"));
  QVERIFY(item.hasKind);
  QVERIFY(!item.isFolder);

  const auto request = Controller::buildCrossNotebookRequest(
      {item}, false, {QStringLiteral("destination"), QStringLiteral("target")});
  QCOMPARE(request.m_items.size(), 1);
  QVERIFY(!request.m_items.first().m_isFolder);
}

void TestNotebookNodeControllerTransfer::sameNotebookRoutingUsesOriginalIdentifiers() {
  const QList<Controller::ClipboardItem> items{
      clipboardItem(QStringLiteral("source"), QStringLiteral("folder/note.md"),
                    QStringLiteral("stable-note"), false),
      clipboardItem(QStringLiteral("source"), QStringLiteral("external.bin"), QString(), false)};

  QVERIFY(Controller::allClipboardItemsBelongTo(items, QStringLiteral("source")));
  QVERIFY(!Controller::allClipboardItemsBelongTo(items, QStringLiteral("destination")));
  QCOMPARE(items.at(0).originalId.relativePath, QStringLiteral("folder/note.md"));
  QCOMPARE(items.at(1).originalId.relativePath, QStringLiteral("external.bin"));
}

void TestNotebookNodeControllerTransfer::crossNotebookRequestIsOneStructuredBatch() {
  const QList<Controller::ClipboardItem> items{
      clipboardItem(QStringLiteral("source"), QStringLiteral("folder"),
                    QStringLiteral("stable-folder"), true),
      clipboardItem(QStringLiteral("source"), QStringLiteral("note.md"),
                    QStringLiteral("stable-note"), false)};

  const auto request = Controller::buildCrossNotebookRequest(
      items, true, {QStringLiteral("destination"), QStringLiteral("target")});
  QCOMPARE(request.m_sourceNotebookId, QStringLiteral("source"));
  QCOMPARE(request.m_destinationNotebookId, QStringLiteral("destination"));
  QCOMPARE(request.m_destinationFolderPath, QStringLiteral("target"));
  QCOMPARE(request.m_operation, vnotex::NodeTransferOperation::Move);
  QCOMPARE(request.m_items.size(), 2);
  QCOMPARE(request.m_items.at(0).m_nodeId, QStringLiteral("stable-folder"));
  QVERIFY(request.m_items.at(0).m_isFolder);
  QCOMPARE(request.m_items.at(1).m_relativePath, QStringLiteral("note.md"));
  QVERIFY(!request.m_items.at(1).m_isFolder);
}

void TestNotebookNodeControllerTransfer::cutApplyRemovesOnlyMovedAndStoresResume() {
  const QList<Controller::ClipboardItem> items{
      clipboardItem(QStringLiteral("source-a"), QStringLiteral("one/moved.md"),
                    QStringLiteral("one"), false),
      clipboardItem(QStringLiteral("source-a"), QStringLiteral("two/retained.md"),
                    QStringLiteral("two"), false),
      clipboardItem(QStringLiteral("source-a"), QStringLiteral("three/failed.md"),
                    QStringLiteral("three"), false)};
  vnotex::NodeTransferBatchResult result;
  result.m_items = {transferResult(vnotex::NodeTransferItemResult::Status::Moved, VXCORE_OK,
                                   QStringLiteral("target/moved.md")),
                    transferResult(vnotex::NodeTransferItemResult::Status::CopiedSourceRetained,
                                   VXCORE_OK, QStringLiteral("target/retained.md"),
                                   QJsonObject{{QStringLiteral("resume"), true}}),
                    transferResult(vnotex::NodeTransferItemResult::Status::Failed, VXCORE_ERR_IO)};
  result.m_items[0].m_source.m_relativePath = QStringLiteral("renamed/moved.md");

  const auto plan = Controller::planTransferApply(items, true, result);
  QCOMPARE(plan.removeClipboardIndexes, QList<int>{0});
  QCOMPARE(plan.resumeTokens.size(), 1);
  QVERIFY(plan.resumeTokens.contains(1));
  QCOMPARE(plan.sourceParents.size(), 1);
  QCOMPARE(plan.sourceParents.first().notebookId, QStringLiteral("source-a"));
  QCOMPARE(plan.sourceParents.first().relativePath, QStringLiteral("renamed"));
  QVERIFY(plan.destinationCommitted);
  QCOMPARE(plan.firstDestination.relativePath, QStringLiteral("target/moved.md"));
}

void TestNotebookNodeControllerTransfer::copyApplyKeepsClipboardAndUsesReturnedDestination() {
  const QList<Controller::ClipboardItem> items{clipboardItem(
      QStringLiteral("source"), QStringLiteral("old.md"), QStringLiteral("old-id"), false)};
  vnotex::NodeTransferBatchResult result;
  result.m_items = {transferResult(vnotex::NodeTransferItemResult::Status::Copied, VXCORE_OK,
                                   QStringLiteral("target/old (2).md"))};

  const auto plan = Controller::planTransferApply(items, false, result);
  QVERIFY(plan.removeClipboardIndexes.isEmpty());
  QVERIFY(plan.sourceParents.isEmpty());
  QVERIFY(plan.destinationCommitted);
  QCOMPARE(plan.firstDestination.relativePath, QStringLiteral("target/old (2).md"));
}

void TestNotebookNodeControllerTransfer::failedBatchDoesNotCommitDestination() {
  const QList<Controller::ClipboardItem> items{clipboardItem(
      QStringLiteral("source"), QStringLiteral("missing.md"), QStringLiteral("missing"), false)};
  vnotex::NodeTransferBatchResult result;
  result.m_items = {
      transferResult(vnotex::NodeTransferItemResult::Status::Failed, VXCORE_ERR_INVALID_STATE)};

  const auto plan = Controller::planTransferApply(items, true, result);
  QVERIFY(!plan.destinationCommitted);
  QVERIFY(!plan.firstDestination.isValid());
  QVERIFY(plan.removeClipboardIndexes.isEmpty());
}

void TestNotebookNodeControllerTransfer::missingSourcesAreConsolidated() {
  const auto item = clipboardItem(QStringLiteral("source"), QStringLiteral("missing.md"),
                                  QStringLiteral("missing"), false);
  const QList<Controller::ClipboardItem> items{item, item};
  vnotex::NodeTransferBatchResult result;
  result.m_items = {
      transferResult(vnotex::NodeTransferItemResult::Status::Failed, VXCORE_ERR_NODE_NOT_EXISTS),
      transferResult(vnotex::NodeTransferItemResult::Status::Failed, VXCORE_ERR_NODE_NOT_EXISTS)};

  const auto plan = Controller::planTransferApply(items, true, result);
  QCOMPARE(plan.missingSources.size(), 1);
  QCOMPARE(plan.missingSources.first(), item.originalId);
}

void TestNotebookNodeControllerTransfer::unresolvedEntriesAreNotMissingSources() {
  Controller::ClipboardItem item;
  item.originalId = {QStringLiteral("source"), QStringLiteral("external.md")};
  const QList<Controller::ClipboardItem> items{item};
  vnotex::NodeTransferBatchResult result;
  result.m_items = {
      transferResult(vnotex::NodeTransferItemResult::Status::Failed, VXCORE_ERR_NODE_NOT_EXISTS)};

  const auto plan = Controller::planTransferApply(items, true, result);
  QVERIFY(plan.missingSources.isEmpty());
  QVERIFY(plan.removeClipboardIndexes.isEmpty());
}

void TestNotebookNodeControllerTransfer::explorersForwardThroughTheInterfaceApplyPath() {
  auto readSource = [](const QString &p_relativePath) {
    QFile file(QStringLiteral(VNOTE_SOURCE_DIR) + QLatin1Char('/') + p_relativePath);
    if (!file.open(QIODevice::ReadOnly)) {
      return QByteArray();
    }
    return file.readAll();
  };

  const QByteArray combined = readSource(QStringLiteral("src/views/combinednodeexplorer.cpp"));
  const QByteArray twoColumns = readSource(QStringLiteral("src/views/twocolumnsnodeexplorer.cpp"));
  const QByteArray widget = readSource(QStringLiteral("src/widgets/notebookexplorer2.cpp"));
  QVERIFY(combined.contains("&NotebookNodeController::crossNotebookPasteRequested"));
  QVERIFY(combined.contains("m_controller->executeCrossNotebookPaste"));
  QVERIFY(twoColumns.count("&NotebookNodeController::crossNotebookPasteRequested") == 1);
  QVERIFY(twoColumns.contains("controller->executeCrossNotebookPaste"));
  QVERIFY(widget.count("&NotebookExplorer2::onCrossNotebookPasteRequested") == 2);
  QVERIFY(widget.contains("m_nodeExplorer->executeCrossNotebookPaste"));
  QVERIFY(!widget.contains("get<NodeTransferService>"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeControllerTransfer)
#include "test_notebook_node_controller_transfer.moc"
