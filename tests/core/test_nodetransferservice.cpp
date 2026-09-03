#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/hookmanager.h>
#include <core/services/nodetransferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

struct TransferEventGateProbe {
  NotebookIoGate *gate = nullptr;
  QString sourceNotebookId;
  QString destinationNotebookId;
  bool called = false;
  bool gatesUnlocked = false;
};

void probeTransferEventGates(const char *, const char *, void *p_userData) {
  auto *probe = static_cast<TransferEventGateProbe *>(p_userData);
  probe->called = true;
  NotebookIoGate::ScopedTryLock sourceLock(*probe->gate, probe->sourceNotebookId, 0);
  NotebookIoGate::ScopedTryLock destinationLock(*probe->gate, probe->destinationNotebookId, 0);
  probe->gatesUnlocked = sourceLock.isLocked() && destinationLock.isLocked();
}

class TestNodeTransferService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();
  void testCoreWrapperCopyReturnsTypedResult();
  void testServiceCopyAndAggregateResult();
  void testDirtyOpenNoteIsSavedBeforeCopy();
  void testMoveRejectsOpenSourceBuffer();
  void testRunningSyncRefusesTransfer();
  void testPartialBatchPreservesOrderAndCounts();
  void testBeforeHookCancellationPublishesNothing();
  void testMaintenanceLeaseSpansBeforeHookAndDefersQueuedSync();
  void testCopyMutationAfterPreparePublishesNothing();
  void testVxcoreSubscribersRunAfterIoGatesRelease();
  void testAfterHookReportsDurableDestination();
  void testFinalizeMoveRevalidatesPendingCommentsUnderGates();
  void testFinalizeCancellationCallbackIsNotInvokedUnderIoGates();

private:
  struct NotebookPair {
    QString sourceId;
    QString destinationId;
    QString sourceRoot;
    QString destinationRoot;
  };

  NotebookPair createPair(const QString &p_name);
  QString createSourceFile(const NotebookPair &p_pair, const QString &p_name,
                           const QByteArray &p_content);
  NodeTransferRequest requestFor(const NotebookPair &p_pair, const QString &p_nodeId,
                                 const QString &p_path, NodeTransferOperation p_operation) const;

  QTemporaryDir m_tempDir;
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookManager = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferService *m_bufferService = nullptr;
  CommentService *m_commentService = nullptr;
  SyncWorkQueueManager *m_syncQueue = nullptr;
  NodeTransferService *m_transferService = nullptr;
};

void TestNodeTransferService::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_hookManager = new HookManager(this);
  m_ioGate = new NotebookIoGate();
  m_notebookService = new NotebookCoreService(m_context, this);
  m_bufferService =
      new BufferService(m_context, m_hookManager, m_ioGate, AutoSavePolicy::AutoSave, this);
  m_commentService = new CommentService(m_notebookService, m_ioGate, m_hookManager, this);
  m_syncQueue = new SyncWorkQueueManager(this);
  m_transferService = new NodeTransferService(m_notebookService, m_bufferService, m_commentService,
                                              m_syncQueue, m_ioGate, m_hookManager, this);
}

void TestNodeTransferService::cleanupTestCase() {
  delete m_transferService;
  m_transferService = nullptr;
  delete m_syncQueue;
  m_syncQueue = nullptr;
  delete m_commentService;
  m_commentService = nullptr;
  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;
  delete m_hookManager;
  m_hookManager = nullptr;
  vxcore_context_destroy(m_context);
  m_context = nullptr;
}

void TestNodeTransferService::cleanup() {
  const QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &value : buffers) {
    m_bufferService->closeBuffer(value.toObject().value(QStringLiteral("id")).toString());
  }
  const QJsonArray notebooks = m_notebookService->listNotebooks();
  for (const auto &value : notebooks) {
    m_notebookService->closeNotebook(value.toObject().value(QStringLiteral("id")).toString());
  }
}

TestNodeTransferService::NotebookPair TestNodeTransferService::createPair(const QString &p_name) {
  NotebookPair pair;
  pair.sourceRoot = m_tempDir.filePath(p_name + QStringLiteral("_source"));
  pair.destinationRoot = m_tempDir.filePath(p_name + QStringLiteral("_destination"));
  pair.sourceId = m_notebookService->createNotebook(
      pair.sourceRoot, QStringLiteral(R"({"name":"Source"})"), NotebookType::Bundled);
  pair.destinationId = m_notebookService->createNotebook(
      pair.destinationRoot, QStringLiteral(R"({"name":"Destination"})"), NotebookType::Bundled);
  return pair;
}

QString TestNodeTransferService::createSourceFile(const NotebookPair &p_pair, const QString &p_name,
                                                  const QByteArray &p_content) {
  const QString nodeId = m_notebookService->createFile(p_pair.sourceId, QString(), p_name);
  QFile file(p_pair.sourceRoot + QLatin1Char('/') + p_name);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(p_content);
    file.close();
  }
  return nodeId;
}

NodeTransferRequest TestNodeTransferService::requestFor(const NotebookPair &p_pair,
                                                        const QString &p_nodeId,
                                                        const QString &p_path,
                                                        NodeTransferOperation p_operation) const {
  NodeTransferRequest request;
  request.m_sourceNotebookId = p_pair.sourceId;
  request.m_destinationNotebookId = p_pair.destinationId;
  request.m_operation = p_operation;
  NodeTransferSourceItem item;
  item.m_nodeId = p_nodeId;
  item.m_relativePath = p_path;
  request.m_items.append(item);
  return request;
}

void TestNodeTransferService::testCoreWrapperCopyReturnsTypedResult() {
  const NotebookPair pair = createPair(QStringLiteral("core_wrapper"));
  QVERIFY(!pair.sourceId.isEmpty());
  QVERIFY(!pair.destinationId.isEmpty());
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("typed.md"), QByteArrayLiteral("typed\n"));
  QVERIFY(!nodeId.isEmpty());

  NodeTransferCoreRequest request;
  request.m_sourceNotebookId = pair.sourceId;
  request.m_sourceRelativePath = QStringLiteral("typed.md");
  request.m_destinationNotebookId = pair.destinationId;
  request.m_operation = NodeTransferOperation::Copy;
  int progressCalls = 0;
  PreparedNodeTransfer prepared = m_notebookService->prepareNodeTransfer(
      request, [&progressCalls](const NodeTransferProgress &) {
        ++progressCalls;
        return true;
      });
  QVERIFY2(prepared.isValid(), qPrintable(prepared.m_errorMessage));
  const NodeTransferCoreResult result = m_notebookService->commitNodeTransfer(prepared);
  QVERIFY2(result.isSuccess(), qPrintable(result.m_errorMessage));
  QCOMPARE(result.m_status, NodeTransferCoreResult::Status::Copied);
  QCOMPARE(result.m_sourceNotebookId, pair.sourceId);
  QCOMPARE(result.m_destinationNotebookId, pair.destinationId);
  QCOMPARE(result.m_destinationRelativePath, QStringLiteral("typed.md"));
  QVERIFY(!result.m_destinationNodeId.isEmpty());
  QVERIFY(result.m_destinationNodeId != nodeId);
  QVERIFY(progressCalls > 0);
}

void TestNodeTransferService::testServiceCopyAndAggregateResult() {
  const NotebookPair pair = createPair(QStringLiteral("service_copy"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("copy.md"), QByteArrayLiteral("current\n"));
  int phases = 0;
  NodeTransferCallbacks callbacks;
  callbacks.m_phaseChanged = [&phases](int, int, const QString &, const QString &) { ++phases; };
  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("stale.md"), NodeTransferOperation::Copy), callbacks);
  QCOMPARE(batch.m_items.size(), 1);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Copied);
  QCOMPARE(batch.m_copiedCount, 1);
  QCOMPARE(batch.m_failedCount, 0);
  QVERIFY(batch.m_firstSuccessfulDestination.isValid());
  QCOMPARE(batch.m_firstSuccessfulDestination.m_relativePath, QStringLiteral("copy.md"));
  QVERIFY(QFile::exists(pair.destinationRoot + QStringLiteral("/copy.md")));
  QVERIFY(phases > 0);
}

void TestNodeTransferService::testDirtyOpenNoteIsSavedBeforeCopy() {
  const NotebookPair pair = createPair(QStringLiteral("dirty_copy"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("dirty.md"), QByteArrayLiteral("old\n"));
  const Buffer2 buffer =
      m_bufferService->openBuffer(NodeIdentifier{pair.sourceId, QStringLiteral("dirty.md")});
  QVERIFY(buffer.isValid());
  m_bufferService->registerActiveWriter(buffer.id(), 1,
                                        []() { return QStringLiteral("current editor text\n"); });
  m_bufferService->markDirty(buffer.id());

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("dirty.md"), NodeTransferOperation::Copy));
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Copied);
  QFile copied(pair.destinationRoot + QStringLiteral("/dirty.md"));
  QVERIFY(copied.open(QIODevice::ReadOnly));
  QCOMPARE(copied.readAll(), QByteArrayLiteral("current editor text\n"));
  m_bufferService->unregisterActiveWriter(buffer.id(), 1);
}

void TestNodeTransferService::testMoveRejectsOpenSourceBuffer() {
  const NotebookPair pair = createPair(QStringLiteral("move_open"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("open.md"), QByteArrayLiteral("open\n"));
  const Buffer2 buffer =
      m_bufferService->openBuffer(NodeIdentifier{pair.sourceId, QStringLiteral("open.md")});
  QVERIFY(buffer.isValid());

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("open.md"), NodeTransferOperation::Move));
  QCOMPARE(batch.m_items.size(), 1);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Failed);
  QCOMPARE(batch.m_items.first().m_error, VXCORE_ERR_INVALID_STATE);
  QVERIFY(QFile::exists(pair.sourceRoot + QStringLiteral("/open.md")));
  QVERIFY(!QFile::exists(pair.destinationRoot + QStringLiteral("/open.md")));
}

void TestNodeTransferService::testRunningSyncRefusesTransfer() {
  const NotebookPair pair = createPair(QStringLiteral("sync_busy"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("busy.md"), QByteArrayLiteral("busy\n"));
  m_syncQueue->testForceInFlight(pair.sourceId, true);

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("busy.md"), NodeTransferOperation::Copy));
  m_syncQueue->testForceInFlight(pair.sourceId, false);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Failed);
  QCOMPARE(batch.m_items.first().m_error, VXCORE_ERR_SYNC_IN_PROGRESS);
  QVERIFY(!QFile::exists(pair.destinationRoot + QStringLiteral("/busy.md")));
}

void TestNodeTransferService::testPartialBatchPreservesOrderAndCounts() {
  const NotebookPair pair = createPair(QStringLiteral("partial"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("valid.md"), QByteArrayLiteral("valid\n"));
  NodeTransferRequest request =
      requestFor(pair, QStringLiteral("missing-id"), QStringLiteral("missing.md"),
                 NodeTransferOperation::Copy);
  NodeTransferSourceItem valid;
  valid.m_nodeId = nodeId;
  valid.m_relativePath = QStringLiteral("valid.md");
  request.m_items.append(valid);

  const NodeTransferBatchResult batch = m_transferService->transfer(request);
  QCOMPARE(batch.m_items.size(), 2);
  QCOMPARE(batch.m_items.at(0).m_status, NodeTransferItemResult::Status::Failed);
  QCOMPARE(batch.m_items.at(1).m_status, NodeTransferItemResult::Status::Copied);
  QCOMPARE(batch.m_failedCount, 1);
  QCOMPARE(batch.m_copiedCount, 1);
  QCOMPARE(batch.m_firstSuccessfulDestination.m_relativePath, QStringLiteral("valid.md"));
}

void TestNodeTransferService::testBeforeHookCancellationPublishesNothing() {
  const NotebookPair pair = createPair(QStringLiteral("hook_cancel"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("cancel.md"), QByteArrayLiteral("cancel\n"));
  const int actionId = m_hookManager->addAction<NodeTransferEvent>(
      HookNames::NodeBeforeTransfer,
      [](HookContext &p_context, const NodeTransferEvent &) { p_context.cancel(); });

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("cancel.md"), NodeTransferOperation::Copy));
  m_hookManager->removeAction(actionId);
  QCOMPARE(batch.m_items.size(), 1);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Cancelled);
  QCOMPARE(batch.m_cancelledCount, 1);
  QVERIFY(!QFile::exists(pair.destinationRoot + QStringLiteral("/cancel.md")));
}

void TestNodeTransferService::testMaintenanceLeaseSpansBeforeHookAndDefersQueuedSync() {
  const NotebookPair pair = createPair(QStringLiteral("hook_lease"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("leased.md"), QByteArrayLiteral("leased\n"));
  std::atomic<bool> queuedSyncRan{false};
  bool leaseHeldInHook = false;
  bool gatesReleasedInHook = false;
  bool enqueueAccepted = false;
  const int actionId = m_hookManager->addAction<NodeTransferEvent>(
      HookNames::NodeBeforeTransfer,
      [this, &pair, &queuedSyncRan, &leaseHeldInHook, &gatesReleasedInHook,
       &enqueueAccepted](HookContext &, const NodeTransferEvent &) {
        auto competingLease =
            m_syncQueue->tryAcquireMaintenance({pair.sourceId, pair.destinationId});
        leaseHeldInHook = !competingLease;
        NotebookIoGate::ScopedTryLock sourceLock(*m_ioGate, pair.sourceId, 0);
        NotebookIoGate::ScopedTryLock destinationLock(*m_ioGate, pair.destinationId, 0);
        gatesReleasedInHook = sourceLock.isLocked() && destinationLock.isLocked();
        enqueueAccepted = m_syncQueue->enqueue(pair.sourceId, [&queuedSyncRan]() {
          queuedSyncRan = true;
        }) == SyncWorkQueueManager::EnqueueResult::Accepted;
        QThread::msleep(50);
        if (queuedSyncRan.load()) {
          leaseHeldInHook = false;
        }
      });

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("leased.md"), NodeTransferOperation::Copy));
  m_hookManager->removeAction(actionId);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Copied);
  QVERIFY(leaseHeldInHook);
  QVERIFY(gatesReleasedInHook);
  QVERIFY(enqueueAccepted);
  QTRY_VERIFY_WITH_TIMEOUT(queuedSyncRan.load(), 1000);
  QTRY_VERIFY_WITH_TIMEOUT(!m_syncQueue->isRunning(pair.sourceId), 1000);
}

void TestNodeTransferService::testCopyMutationAfterPreparePublishesNothing() {
  const NotebookPair pair = createPair(QStringLiteral("copy_mutation"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("mutable.md"), QByteArrayLiteral("original\n"));
  const int actionId = m_hookManager->addAction<NodeTransferEvent>(
      HookNames::NodeBeforeTransfer, [&pair](HookContext &, const NodeTransferEvent &) {
        QFile source(pair.sourceRoot + QStringLiteral("/mutable.md"));
        if (source.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
          source.write(QByteArrayLiteral("mutated after prepare\n"));
        }
      });

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("mutable.md"), NodeTransferOperation::Copy));
  m_hookManager->removeAction(actionId);
  QCOMPARE(batch.m_items.size(), 1);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Failed);
  QCOMPARE(batch.m_items.first().m_error, VXCORE_ERR_INVALID_STATE);
  QVERIFY(!QFile::exists(pair.destinationRoot + QStringLiteral("/mutable.md")));
  QVERIFY(QFile::exists(pair.sourceRoot + QStringLiteral("/mutable.md")));
}

void TestNodeTransferService::testVxcoreSubscribersRunAfterIoGatesRelease() {
  const NotebookPair pair = createPair(QStringLiteral("deferred_events"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("deferred.md"), QByteArrayLiteral("deferred\n"));
  TransferEventGateProbe probe;
  probe.gate = m_ioGate;
  probe.sourceNotebookId = pair.sourceId;
  probe.destinationNotebookId = pair.destinationId;
  QCOMPARE(vxcore_on_event(m_context, "file.created", probeTransferEventGates, &probe), VXCORE_OK);

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("deferred.md"), NodeTransferOperation::Copy));
  QCOMPARE(vxcore_off_event(m_context, "file.created", probeTransferEventGates), VXCORE_OK);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Copied);
  QVERIFY(probe.called);
  QVERIFY(probe.gatesUnlocked);
}

void TestNodeTransferService::testAfterHookReportsDurableDestination() {
  const NotebookPair pair = createPair(QStringLiteral("after_hook"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("after.md"), QByteArrayLiteral("after\n"));
  NodeTransferEvent captured;
  int calls = 0;
  bool leaseReleased = false;
  bool gatesReleased = false;
  const int actionId = m_hookManager->addAction<NodeTransferEvent>(
      HookNames::NodeAfterTransfer,
      [this, &pair, &captured, &calls, &leaseReleased,
       &gatesReleased](HookContext &, const NodeTransferEvent &p_event) {
        captured = p_event;
        ++calls;
        auto lease = m_syncQueue->tryAcquireMaintenance({pair.sourceId, pair.destinationId});
        leaseReleased = lease.isValid();
        NotebookIoGate::ScopedTryLock sourceLock(*m_ioGate, pair.sourceId, 0);
        NotebookIoGate::ScopedTryLock destinationLock(*m_ioGate, pair.destinationId, 0);
        gatesReleased = sourceLock.isLocked() && destinationLock.isLocked();
      });

  const NodeTransferBatchResult batch = m_transferService->transfer(
      requestFor(pair, nodeId, QStringLiteral("after.md"), NodeTransferOperation::Copy));
  m_hookManager->removeAction(actionId);
  QCOMPARE(batch.m_items.first().m_status, NodeTransferItemResult::Status::Copied);
  QCOMPARE(calls, 1);
  QCOMPARE(captured.actualStatus, QStringLiteral("copied"));
  QCOMPARE(captured.destinationNotebookId, pair.destinationId);
  QCOMPARE(captured.destinationRelativePath, QStringLiteral("after.md"));
  QVERIFY(!captured.destinationNodeId.isEmpty());
  QVERIFY(captured.sourceRemains);
  QVERIFY(leaseReleased);
  QVERIFY(gatesReleased);
}

void TestNodeTransferService::testFinalizeMoveRevalidatesPendingCommentsUnderGates() {
  const NotebookPair pair = createPair(QStringLiteral("finalize_comments"));
  const QString nodeId =
      createSourceFile(pair, QStringLiteral("comments.md"), QByteArrayLiteral("comments\n"));
  const NodeIdentifier sourceId{pair.sourceId, QStringLiteral("comments.md")};
  CommentSet baseComments;
  baseComments.m_extraKeys[QStringLiteral("revision")] = 1;
  m_commentService->scheduleSave(sourceId, baseComments, 1);
  QTRY_VERIFY_WITH_TIMEOUT(!m_commentService->isBusy(sourceId), 1000);

  const QByteArray sourceNotebook = pair.sourceId.toUtf8();
  const QByteArray destinationNotebook = pair.destinationId.toUtf8();
  const QByteArray options = QByteArrayLiteral(
      R"({"operation":"move","conflictPolicy":"rename","timestampPolicy":"preserve","createMissingTags":true,"preserveRelativeLinks":true,"testFault":"postCommitVerification"})");
  VxCoreNodeTransferHandle handle = nullptr;
  QCOMPARE(vxcore_node_transfer_prepare(m_context, sourceNotebook.constData(), "comments.md",
                                        destinationNotebook.constData(), ".", options.constData(),
                                        nullptr, nullptr, &handle),
           VXCORE_OK);
  char *resultJson = nullptr;
  QCOMPARE(vxcore_node_transfer_commit(m_context, handle, &resultJson), VXCORE_OK);
  QVERIFY(resultJson);
  const QJsonObject retained = QJsonDocument::fromJson(resultJson).object();
  vxcore_string_free(resultJson);
  QCOMPARE(retained.value(QStringLiteral("status")).toString(),
           QStringLiteral("moveRecoveryRequired"));
  const QByteArray eventBatchId =
      retained.value(QStringLiteral("eventBatchId")).toString().toUtf8();
  QCOMPARE(vxcore_node_transfer_dispatch_events(m_context, eventBatchId.constData()), VXCORE_OK);

  auto participant = m_commentService->registerFlushParticipant(
      sourceId,
      [this, sourceId, baseComments]() {
        m_commentService->scheduleSave(sourceId, baseComments, 1);
      },
      1);
  CommentSet changedComments = baseComments;
  changedComments.m_extraKeys[QStringLiteral("revision")] = 2;
  m_transferService->testSetBeforeFinalizeGateCallback(
      [this, &participant, sourceId, changedComments]() {
        participant.setGeneration(2);
        m_commentService->scheduleSave(sourceId, changedComments, 2);
      });

  const NodeTransferItemResult finalized = m_transferService->finalizeTransferredMove(
      retained.value(QStringLiteral("resumeToken")).toObject());
  m_transferService->testSetBeforeFinalizeGateCallback({});
  QCOMPARE(finalized.m_status, NodeTransferItemResult::Status::Failed);
  QCOMPARE(finalized.m_error, VXCORE_ERR_INVALID_STATE);
  QVERIFY(QFile::exists(pair.sourceRoot + QStringLiteral("/comments.md")));
  QVERIFY(QFile::exists(pair.destinationRoot + QStringLiteral("/comments.md")));
  QVERIFY(!QFile::exists(pair.destinationRoot + QStringLiteral("/comments (2).md")));
  QTRY_VERIFY_WITH_TIMEOUT(!m_commentService->isBusy(sourceId), 1000);
  const CommentService::LoadResult loaded = m_commentService->load(sourceId);
  QCOMPARE(loaded.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(loaded.m_comments.m_extraKeys.value(QStringLiteral("revision")).toInt(), 2);
}

void TestNodeTransferService::testFinalizeCancellationCallbackIsNotInvokedUnderIoGates() {
  const NotebookPair pair = createPair(QStringLiteral("finalize_cancel_callback"));
  createSourceFile(pair, QStringLiteral("callback.md"), QByteArrayLiteral("callback\n"));

  const QByteArray sourceNotebook = pair.sourceId.toUtf8();
  const QByteArray destinationNotebook = pair.destinationId.toUtf8();
  const QByteArray options = QByteArrayLiteral(
      R"({"operation":"move","conflictPolicy":"rename","timestampPolicy":"preserve","createMissingTags":true,"preserveRelativeLinks":true,"testFault":"sourceRemoval"})");
  VxCoreNodeTransferHandle handle = nullptr;
  QCOMPARE(vxcore_node_transfer_prepare(m_context, sourceNotebook.constData(), "callback.md",
                                        destinationNotebook.constData(), ".", options.constData(),
                                        nullptr, nullptr, &handle),
           VXCORE_OK);
  char *resultJson = nullptr;
  QCOMPARE(vxcore_node_transfer_commit(m_context, handle, &resultJson), VXCORE_OK);
  QVERIFY(resultJson);
  const QJsonObject retained = QJsonDocument::fromJson(resultJson).object();
  vxcore_string_free(resultJson);
  QCOMPARE(retained.value(QStringLiteral("status")).toString(),
           QStringLiteral("copiedSourceRetained"));

  bool callbackUnderGate = false;
  int callbackCount = 0;
  const auto isCancelled = [this, &pair, &callbackUnderGate, &callbackCount]() {
    ++callbackCount;
    NotebookIoGate::ScopedTryLock sourceLock(*m_ioGate, pair.sourceId, 0);
    NotebookIoGate::ScopedTryLock destinationLock(*m_ioGate, pair.destinationId, 0);
    if (!sourceLock.isLocked() || !destinationLock.isLocked()) {
      callbackUnderGate = true;
    }
    return false;
  };
  const NodeTransferItemResult finalized = m_transferService->finalizeTransferredMove(
      retained.value(QStringLiteral("resumeToken")).toObject(), 250, isCancelled);
  QCOMPARE(finalized.m_status, NodeTransferItemResult::Status::Moved);
  QVERIFY(callbackCount > 0);
  QVERIFY(!callbackUnderGate);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNodeTransferService)
#include "test_nodetransferservice.moc"
