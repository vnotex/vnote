#include "nodetransferservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QThread>

#include <algorithm>
#include <memory>
#include <utility>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
const int c_operationTimeoutMs = 5000;
const int c_gateTimeoutMs = 250;

QString normalizedPath(const QString &p_path) {
  QString path = QDir::fromNativeSeparators(p_path);
  while (path.startsWith(QLatin1String("./"))) {
    path.remove(0, 2);
  }
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  return path == QLatin1String(".") ? QString() : path;
}

QString statusString(NodeTransferItemResult::Status p_status) {
  switch (p_status) {
  case NodeTransferItemResult::Status::Copied:
    return QStringLiteral("copied");
  case NodeTransferItemResult::Status::Moved:
    return QStringLiteral("moved");
  case NodeTransferItemResult::Status::CopiedSourceRetained:
    return QStringLiteral("copiedSourceRetained");
  case NodeTransferItemResult::Status::Failed:
    return QStringLiteral("failed");
  case NodeTransferItemResult::Status::Cancelled:
    return QStringLiteral("cancelled");
  }
  return QStringLiteral("failed");
}
} // namespace

NodeTransferService::NodeTransferService(NotebookCoreService *p_notebookService,
                                         BufferService *p_bufferService,
                                         CommentService *p_commentService,
                                         SyncWorkQueueManager *p_syncWorkQueueManager,
                                         NotebookIoGate *p_ioGate, HookManager *p_hookManager,
                                         QObject *p_parent)
    : QObject(p_parent), m_notebookService(p_notebookService), m_bufferService(p_bufferService),
      m_commentService(p_commentService), m_syncWorkQueueManager(p_syncWorkQueueManager),
      m_ioGate(p_ioGate), m_hookManager(p_hookManager) {}

NodeTransferBatchResult NodeTransferService::transfer(const NodeTransferRequest &p_request,
                                                      const NodeTransferCallbacks &p_callbacks) {
  NodeTransferBatchResult batch;
  VxCoreError validationError = VXCORE_OK;
  QString validationMessage;
  if (!validateNotebookPair(p_request, validationError, validationMessage)) {
    for (const auto &item : p_request.m_items) {
      NodeTransferItemResult result;
      result.m_source = item;
      result.m_error = validationError;
      result.m_errorMessage = validationMessage;
      appendResult(batch, std::move(result));
    }
    return batch;
  }

  const int totalItems = p_request.m_items.size();
  for (int itemIndex = 0; itemIndex < totalItems; ++itemIndex) {
    NodeTransferSourceItem source = p_request.m_items.at(itemIndex);
    if (p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_status = NodeTransferItemResult::Status::Cancelled;
      result.m_error = VXCORE_ERR_CANCELLED;
      result.m_errorMessage = tr("Node transfer was cancelled.");
      appendResult(batch, std::move(result));
      break;
    }

    const QString currentPath =
        m_notebookService->getNodePathById(p_request.m_sourceNotebookId, source.m_nodeId);
    if (currentPath.isEmpty()) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = VXCORE_ERR_NODE_NOT_EXISTS;
      result.m_errorMessage = tr("The source item no longer exists.");
      appendResult(batch, std::move(result));
      continue;
    }
    source.m_relativePath = currentPath;
    VxCoreError kindError = VXCORE_OK;
    const QJsonObject nodeConfig =
        m_notebookService->getFolderConfig(p_request.m_sourceNotebookId, currentPath, &kindError);
    const bool actualIsFolder =
        nodeConfig.value(QLatin1String(vxcore::kJsonKeyType)).toString() == QLatin1String("folder");
    if (kindError != VXCORE_OK || actualIsFolder != source.m_isFolder) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = kindError == VXCORE_OK ? VXCORE_ERR_INVALID_STATE : kindError;
      result.m_errorMessage = tr("The source item kind no longer matches the clipboard entry.");
      appendResult(batch, std::move(result));
      continue;
    }
    if (p_callbacks.m_phaseChanged) {
      p_callbacks.m_phaseChanged(itemIndex, totalItems, QStringLiteral("preflight"), currentPath);
    }

    QVector<BufferSnapshot> bufferCheckpoint =
        buffersInScope(p_request.m_sourceNotebookId, currentPath, source.m_isFolder);
    if (p_request.m_operation == NodeTransferOperation::Move && !bufferCheckpoint.isEmpty()) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = VXCORE_ERR_INVALID_STATE;
      result.m_errorMessage = tr("Close all notes inside this item before moving it.");
      appendResult(batch, std::move(result));
      continue;
    }

    CommentService::FlushCheckpoint commentCheckpoint;
    if (p_request.m_operation == NodeTransferOperation::Copy) {
      if (!waitForSaveQueues(bufferCheckpoint, c_operationTimeoutMs, p_callbacks.m_isCancelled)) {
        NodeTransferItemResult result;
        result.m_source = source;
        result.m_error = p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()
                             ? VXCORE_ERR_CANCELLED
                             : VXCORE_ERR_SYNC_IN_PROGRESS;
        result.m_status = result.m_error == VXCORE_ERR_CANCELLED
                              ? NodeTransferItemResult::Status::Cancelled
                              : NodeTransferItemResult::Status::Failed;
        result.m_errorMessage = result.m_error == VXCORE_ERR_CANCELLED
                                    ? tr("Node transfer was cancelled.")
                                    : tr("An open note is still being saved.");
        appendResult(batch, std::move(result));
        if (batch.m_items.back().m_status == NodeTransferItemResult::Status::Cancelled) {
          break;
        }
        continue;
      }

      bool saveFailed = false;
      QString saveError;
      for (const auto &buffer : bufferCheckpoint) {
        if (!m_bufferService->saveForSnapshot(buffer.m_bufferId, c_gateTimeoutMs, &saveError)) {
          saveFailed = true;
          break;
        }
      }
      if (saveFailed) {
        NodeTransferItemResult result;
        result.m_source = source;
        result.m_error = VXCORE_ERR_IO;
        result.m_errorMessage = saveError;
        appendResult(batch, std::move(result));
        continue;
      }
      if (!validateBuffers(bufferCheckpoint, p_request.m_sourceNotebookId, currentPath,
                           source.m_isFolder)) {
        NodeTransferItemResult result;
        result.m_source = source;
        result.m_error = VXCORE_ERR_INVALID_STATE;
        result.m_errorMessage = tr("The open-note set changed while it was being saved.");
        appendResult(batch, std::move(result));
        continue;
      }
    }

    const auto flushResult = m_commentService->flushAndWaitForIdle(
        p_request.m_sourceNotebookId, currentPath, source.m_isFolder, c_operationTimeoutMs,
        p_callbacks.m_isCancelled);
    if (flushResult.m_status != CommentService::FlushResult::Status::Succeeded) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_status = flushResult.m_status == CommentService::FlushResult::Status::Cancelled
                            ? NodeTransferItemResult::Status::Cancelled
                            : NodeTransferItemResult::Status::Failed;
      result.m_error = result.m_status == NodeTransferItemResult::Status::Cancelled
                           ? VXCORE_ERR_CANCELLED
                           : VXCORE_ERR_IO;
      result.m_errorMessage = flushResult.m_error.isEmpty()
                                  ? tr("Pending comments could not be saved.")
                                  : flushResult.m_error;
      appendResult(batch, std::move(result));
      if (batch.m_items.back().m_status == NodeTransferItemResult::Status::Cancelled) {
        break;
      }
      continue;
    }
    commentCheckpoint = flushResult.m_checkpoint;
    if ((p_request.m_operation == NodeTransferOperation::Copy &&
         !validateBuffers(bufferCheckpoint, p_request.m_sourceNotebookId, currentPath,
                          source.m_isFolder)) ||
        !m_commentService->isFlushCheckpointCurrent(commentCheckpoint)) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = VXCORE_ERR_INVALID_STATE;
      result.m_errorMessage = tr("The source changed while it was being prepared.");
      appendResult(batch, std::move(result));
      continue;
    }

    QStringList notebookIds{p_request.m_sourceNotebookId, p_request.m_destinationNotebookId};
    auto maintenanceLease = m_syncWorkQueueManager->tryAcquireMaintenance(notebookIds);
    if (!maintenanceLease) {
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = VXCORE_ERR_SYNC_IN_PROGRESS;
      result.m_errorMessage = tr("A source or destination notebook is busy syncing.");
      appendResult(batch, std::move(result));
      continue;
    }

    NodeTransferCoreRequest coreRequest;
    coreRequest.m_sourceNotebookId = p_request.m_sourceNotebookId;
    coreRequest.m_sourceRelativePath = currentPath;
    coreRequest.m_destinationNotebookId = p_request.m_destinationNotebookId;
    coreRequest.m_destinationFolderPath = p_request.m_destinationFolderPath;
    coreRequest.m_operation = p_request.m_operation;
    const auto progress = [&](const NodeTransferProgress &p_progress) {
      if (p_callbacks.m_phaseChanged) {
        p_callbacks.m_phaseChanged(itemIndex, totalItems, p_progress.m_phase, currentPath);
      }
      if (p_callbacks.m_byteProgress) {
        p_callbacks.m_byteProgress(p_progress.m_completedBytes, p_progress.m_totalBytes);
      }
      return !p_callbacks.m_isCancelled || !p_callbacks.m_isCancelled();
    };
    PreparedNodeTransfer prepared = m_notebookService->prepareNodeTransfer(coreRequest, progress);
    if (!prepared.isValid()) {
      maintenanceLease.release();
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = prepared.m_error;
      result.m_status = prepared.m_error == VXCORE_ERR_CANCELLED
                            ? NodeTransferItemResult::Status::Cancelled
                            : NodeTransferItemResult::Status::Failed;
      result.m_errorMessage = prepared.m_errorMessage;
      appendResult(batch, std::move(result));
      if (batch.m_items.back().m_status == NodeTransferItemResult::Status::Cancelled) {
        break;
      }
      continue;
    }

    const bool postPrepareCurrent =
        m_notebookService->getNodePathById(p_request.m_sourceNotebookId, source.m_nodeId) ==
            currentPath &&
        (p_request.m_operation == NodeTransferOperation::Move
             ? buffersInScope(p_request.m_sourceNotebookId, currentPath, source.m_isFolder)
                   .isEmpty()
             : validateBuffers(bufferCheckpoint, p_request.m_sourceNotebookId, currentPath,
                               source.m_isFolder)) &&
        m_commentService->isFlushCheckpointCurrent(commentCheckpoint);
    if (!postPrepareCurrent) {
      m_notebookService->freePreparedNodeTransfer(prepared);
      maintenanceLease.release();
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_error = VXCORE_ERR_INVALID_STATE;
      result.m_errorMessage = tr("The source changed while it was being prepared.");
      appendResult(batch, std::move(result));
      continue;
    }

    // Hooks may re-enter services, so no IO gate is held while plugin code runs.
    // Keep sync paused continuously from snapshot preparation through commit.
    NodeTransferEvent beforeEvent;
    beforeEvent.sourceNotebookId = p_request.m_sourceNotebookId;
    beforeEvent.sourceRelativePath = currentPath;
    beforeEvent.destinationNotebookId = p_request.m_destinationNotebookId;
    beforeEvent.destinationRelativePath = p_request.m_destinationFolderPath;
    beforeEvent.requestedOperation = p_request.m_operation == NodeTransferOperation::Move
                                         ? QStringLiteral("move")
                                         : QStringLiteral("copy");
    beforeEvent.isFolder = source.m_isFolder;
    if (m_hookManager && m_hookManager->doAction(HookNames::NodeBeforeTransfer, beforeEvent)) {
      m_notebookService->freePreparedNodeTransfer(prepared);
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_status = NodeTransferItemResult::Status::Cancelled;
      result.m_error = VXCORE_ERR_CANCELLED;
      result.m_errorMessage = tr("Node transfer was cancelled by a hook.");
      appendResult(batch, std::move(result));
      break;
    }
    if (p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()) {
      m_notebookService->freePreparedNodeTransfer(prepared);
      NodeTransferItemResult result;
      result.m_source = source;
      result.m_status = NodeTransferItemResult::Status::Cancelled;
      result.m_error = VXCORE_ERR_CANCELLED;
      result.m_errorMessage = tr("Node transfer was cancelled.");
      appendResult(batch, std::move(result));
      break;
    }

    QStringList sortedIds = notebookIds;
    std::sort(sortedIds.begin(), sortedIds.end());
    NodeTransferCoreResult coreResult;
    {
      QElapsedTimer gateTimer;
      gateTimer.start();
      std::unique_ptr<NotebookIoGate::ScopedTryLock> firstLock(
          new NotebookIoGate::ScopedTryLock(*m_ioGate, sortedIds.at(0), c_gateTimeoutMs));
      std::unique_ptr<NotebookIoGate::ScopedTryLock> secondLock;
      if (firstLock->isLocked()) {
        const int remaining = qMax(0, c_gateTimeoutMs - static_cast<int>(gateTimer.elapsed()));
        secondLock.reset(new NotebookIoGate::ScopedTryLock(*m_ioGate, sortedIds.at(1), remaining));
      }
      if (!firstLock->isLocked() || !secondLock || !secondLock->isLocked()) {
        coreResult.m_error = VXCORE_ERR_SYNC_IN_PROGRESS;
        coreResult.m_errorMessage = tr("A source or destination notebook is busy.");
      } else {
        const bool finalCurrent =
            m_notebookService->getNodePathById(p_request.m_sourceNotebookId, source.m_nodeId) ==
                currentPath &&
            (p_request.m_operation == NodeTransferOperation::Move
                 ? buffersInScope(p_request.m_sourceNotebookId, currentPath, source.m_isFolder)
                       .isEmpty()
                 : validateBuffers(bufferCheckpoint, p_request.m_sourceNotebookId, currentPath,
                                   source.m_isFolder)) &&
            m_commentService->isFlushCheckpointCurrent(commentCheckpoint);
        if (!finalCurrent) {
          coreResult.m_error = VXCORE_ERR_INVALID_STATE;
          coreResult.m_errorMessage = tr("The source changed before transfer commit.");
        } else {
          coreResult = m_notebookService->commitNodeTransfer(prepared);
        }
      }
    }
    const VxCoreError dispatchError = m_notebookService->dispatchNodeTransferEvents(coreResult);
    if (dispatchError != VXCORE_OK) {
      qCritical() << "Failed to dispatch committed node transfer events:" << dispatchError;
    }
    if (prepared.isValid()) {
      m_notebookService->freePreparedNodeTransfer(prepared);
    }
    maintenanceLease.release();

    NodeTransferItemResult itemResult = fromCoreResult(source, coreResult);
    if (itemResult.destinationCommitted() && m_hookManager) {
      NodeTransferEvent afterEvent = beforeEvent;
      afterEvent.destinationRelativePath = itemResult.m_destination.m_relativePath;
      afterEvent.destinationNodeId = itemResult.m_destination.m_nodeId;
      afterEvent.actualStatus = itemResult.m_recoveryRequired
                                    ? QStringLiteral("moveRecoveryRequired")
                                    : statusString(itemResult.m_status);
      afterEvent.sourceRemains = itemResult.m_status != NodeTransferItemResult::Status::Moved;
      m_hookManager->doAction(HookNames::NodeAfterTransfer, afterEvent);
    }
    appendResult(batch, std::move(itemResult));
  }
  return batch;
}

NodeTransferItemResult
NodeTransferService::finalizeTransferredMove(const QJsonObject &p_resumeToken, int p_gateTimeoutMs,
                                             const std::function<bool()> &p_isCancelled) {
  NodeTransferSourceItem source;
  source.m_nodeId = p_resumeToken.value(QStringLiteral("sourceNodeId")).toString();
  source.m_relativePath = p_resumeToken.value(QStringLiteral("sourceRelativePath")).toString();
  source.m_isFolder = p_resumeToken.value(QStringLiteral("isFolder")).toBool();
  NodeTransferItemResult result;
  result.m_source = source;
  const QString sourceNotebook = p_resumeToken.value(QStringLiteral("sourceNotebookId")).toString();
  const QString destinationNotebook =
      p_resumeToken.value(QStringLiteral("destinationNotebookId")).toString();
  if (sourceNotebook.isEmpty() || destinationNotebook.isEmpty() ||
      source.m_relativePath.isEmpty()) {
    result.m_error = VXCORE_ERR_INVALID_PARAM;
    result.m_errorMessage = tr("The move resume token is invalid.");
    return result;
  }
  if (!buffersInScope(sourceNotebook, source.m_relativePath, source.m_isFolder).isEmpty()) {
    result.m_error = VXCORE_ERR_INVALID_STATE;
    result.m_errorMessage = tr("Close all notes inside this item before finishing the move.");
    return result;
  }

  const auto flushResult =
      m_commentService->flushAndWaitForIdle(sourceNotebook, source.m_relativePath,
                                            source.m_isFolder, c_operationTimeoutMs, p_isCancelled);
  if (flushResult.m_status != CommentService::FlushResult::Status::Succeeded) {
    result.m_status = flushResult.m_status == CommentService::FlushResult::Status::Cancelled
                          ? NodeTransferItemResult::Status::Cancelled
                          : NodeTransferItemResult::Status::Failed;
    result.m_error = result.m_status == NodeTransferItemResult::Status::Cancelled
                         ? VXCORE_ERR_CANCELLED
                         : VXCORE_ERR_IO;
    result.m_errorMessage = flushResult.m_error.isEmpty()
                                ? tr("Pending comments could not be saved.")
                                : flushResult.m_error;
    return result;
  }
  const CommentService::FlushCheckpoint commentCheckpoint = flushResult.m_checkpoint;
  if (p_isCancelled && p_isCancelled()) {
    result.m_status = NodeTransferItemResult::Status::Cancelled;
    result.m_error = VXCORE_ERR_CANCELLED;
    result.m_errorMessage = tr("Move finalization was cancelled.");
    return result;
  }
  if (m_testBeforeFinalizeGateCallback) {
    m_testBeforeFinalizeGateCallback();
  }

  QStringList notebookIds{sourceNotebook, destinationNotebook};
  auto lease = m_syncWorkQueueManager->tryAcquireMaintenance(notebookIds);
  if (!lease) {
    result.m_error = VXCORE_ERR_SYNC_IN_PROGRESS;
    result.m_errorMessage = tr("A source or destination notebook is busy syncing.");
    return result;
  }
  std::sort(notebookIds.begin(), notebookIds.end());
  NodeTransferCoreResult coreResult;
  {
    QElapsedTimer timer;
    timer.start();
    std::unique_ptr<NotebookIoGate::ScopedTryLock> firstLock(
        new NotebookIoGate::ScopedTryLock(*m_ioGate, notebookIds.at(0), p_gateTimeoutMs));
    std::unique_ptr<NotebookIoGate::ScopedTryLock> secondLock;
    if (firstLock->isLocked()) {
      const int remaining = qMax(0, p_gateTimeoutMs - static_cast<int>(timer.elapsed()));
      secondLock.reset(new NotebookIoGate::ScopedTryLock(*m_ioGate, notebookIds.at(1), remaining));
    }
    if (!firstLock->isLocked() || !secondLock || !secondLock->isLocked()) {
      coreResult.m_error = VXCORE_ERR_SYNC_IN_PROGRESS;
      coreResult.m_errorMessage = tr("A source or destination notebook is busy.");
    } else if (!buffersInScope(sourceNotebook, source.m_relativePath, source.m_isFolder)
                    .isEmpty() ||
               !m_commentService->isFlushCheckpointCurrent(commentCheckpoint)) {
      coreResult.m_error = VXCORE_ERR_INVALID_STATE;
      coreResult.m_errorMessage = tr("The source changed before move finalization.");
    } else {
      coreResult = m_notebookService->finalizeTransferredMove(p_resumeToken);
    }
  }
  const VxCoreError dispatchError = m_notebookService->dispatchNodeTransferEvents(coreResult);
  if (dispatchError != VXCORE_OK) {
    qCritical() << "Failed to dispatch finalized node transfer events:" << dispatchError;
  }
  lease.release();
  NodeTransferItemResult itemResult = fromCoreResult(source, coreResult);
  if (coreResult.m_error == VXCORE_ERR_CANCELLED) {
    itemResult.m_status = NodeTransferItemResult::Status::Cancelled;
  }
  return itemResult;
}

QVector<NodeTransferService::BufferSnapshot>
NodeTransferService::buffersInScope(const QString &p_notebookId, const QString &p_relativePath,
                                    bool p_isFolder) const {
  QVector<BufferSnapshot> buffers;
  const QJsonArray openBuffers = m_bufferService->listBuffers();
  for (const auto &value : openBuffers) {
    const QJsonObject object = value.toObject();
    if (object.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() != p_notebookId) {
      continue;
    }
    const QString filePath = object.value(QStringLiteral("filePath")).toString();
    if (!pathMatches(filePath, p_relativePath, p_isFolder)) {
      continue;
    }
    BufferSnapshot buffer;
    buffer.m_bufferId = object.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    buffer.m_relativePath = filePath;
    buffer.m_revision = m_bufferService->currentRevision(buffer.m_bufferId);
    buffers.append(std::move(buffer));
  }
  std::sort(buffers.begin(), buffers.end(),
            [](const BufferSnapshot &p_left, const BufferSnapshot &p_right) {
              if (p_left.m_relativePath != p_right.m_relativePath) {
                return p_left.m_relativePath < p_right.m_relativePath;
              }
              return p_left.m_bufferId < p_right.m_bufferId;
            });
  return buffers;
}

bool NodeTransferService::waitForSaveQueues(const QVector<BufferSnapshot> &p_buffers,
                                            int p_timeoutMs,
                                            const std::function<bool()> &p_isCancelled) const {
  QElapsedTimer timer;
  timer.start();
  for (;;) {
    if (p_isCancelled && p_isCancelled()) {
      return false;
    }
    bool busy = false;
    for (const auto &buffer : p_buffers) {
      if (m_bufferService->isSaveQueueBusy(buffer.m_bufferId)) {
        busy = true;
        break;
      }
    }
    if (!busy) {
      return true;
    }
    if (timer.elapsed() >= p_timeoutMs) {
      return false;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}

bool NodeTransferService::validateBuffers(const QVector<BufferSnapshot> &p_expected,
                                          const QString &p_notebookId,
                                          const QString &p_relativePath, bool p_isFolder) const {
  return p_expected == buffersInScope(p_notebookId, p_relativePath, p_isFolder);
}

bool NodeTransferService::validateNotebookPair(const NodeTransferRequest &p_request,
                                               VxCoreError &p_error,
                                               QString &p_errorMessage) const {
  if (!m_notebookService || !m_bufferService || !m_commentService || !m_syncWorkQueueManager ||
      !m_ioGate || p_request.m_sourceNotebookId.isEmpty() ||
      p_request.m_destinationNotebookId.isEmpty()) {
    p_error = VXCORE_ERR_INVALID_PARAM;
    p_errorMessage = tr("The node transfer request is incomplete.");
    return false;
  }
  if (p_request.m_sourceNotebookId == p_request.m_destinationNotebookId) {
    p_error = VXCORE_ERR_INVALID_PARAM;
    p_errorMessage = tr("Cross-notebook transfer requires two different notebooks.");
    return false;
  }

  QJsonObject sourceRecord;
  QJsonObject destinationRecord;
  for (const auto &value : m_notebookService->listNotebooks()) {
    const QJsonObject record = value.toObject();
    const QString id = record.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (id == p_request.m_sourceNotebookId) {
      sourceRecord = record;
    } else if (id == p_request.m_destinationNotebookId) {
      destinationRecord = record;
    }
  }
  if (sourceRecord.isEmpty() || destinationRecord.isEmpty()) {
    p_error = VXCORE_ERR_NOT_FOUND;
    p_errorMessage = tr("A source or destination notebook is not open.");
    return false;
  }
  if (sourceRecord.value(QLatin1String(vxcore::kJsonKeyType)).toString() !=
          QLatin1String("bundled") ||
      destinationRecord.value(QLatin1String(vxcore::kJsonKeyType)).toString() !=
          QLatin1String("bundled")) {
    p_error = VXCORE_ERR_UNSUPPORTED;
    p_errorMessage = tr("Cross-notebook transfer supports bundled notebooks only.");
    return false;
  }
  if (destinationRecord.value(QLatin1String(vxcore::kJsonKeyReadOnly)).toBool() ||
      (p_request.m_operation == NodeTransferOperation::Move &&
       sourceRecord.value(QLatin1String(vxcore::kJsonKeyReadOnly)).toBool())) {
    p_error = VXCORE_ERR_READ_ONLY;
    p_errorMessage = tr("The selected transfer requires a writable notebook.");
    return false;
  }
  return true;
}

bool NodeTransferService::pathMatches(const QString &p_candidate, const QString &p_scope,
                                      bool p_isFolder) {
  const QString candidate = normalizedPath(p_candidate);
  const QString scope = normalizedPath(p_scope);
  return candidate == scope || (p_isFolder && candidate.startsWith(scope + QLatin1Char('/')));
}

NodeTransferItemResult
NodeTransferService::fromCoreResult(const NodeTransferSourceItem &p_source,
                                    const NodeTransferCoreResult &p_coreResult) {
  NodeTransferItemResult result;
  result.m_source = p_source;
  result.m_error = p_coreResult.m_error;
  result.m_errorMessage = p_coreResult.m_errorMessage;
  result.m_destination.m_notebookId = p_coreResult.m_destinationNotebookId;
  result.m_destination.m_relativePath = p_coreResult.m_destinationRelativePath;
  result.m_destination.m_nodeId = p_coreResult.m_destinationNodeId;
  result.m_resumeToken = p_coreResult.m_resumeToken;
  switch (p_coreResult.m_status) {
  case NodeTransferCoreResult::Status::Copied:
    result.m_status = NodeTransferItemResult::Status::Copied;
    break;
  case NodeTransferCoreResult::Status::Moved:
    result.m_status = NodeTransferItemResult::Status::Moved;
    break;
  case NodeTransferCoreResult::Status::CopiedSourceRetained:
    result.m_status = NodeTransferItemResult::Status::CopiedSourceRetained;
    break;
  case NodeTransferCoreResult::Status::MoveRecoveryRequired:
    result.m_status = NodeTransferItemResult::Status::Failed;
    result.m_recoveryRequired = true;
    break;
  case NodeTransferCoreResult::Status::Invalid:
    result.m_status = p_coreResult.m_error == VXCORE_ERR_CANCELLED
                          ? NodeTransferItemResult::Status::Cancelled
                          : NodeTransferItemResult::Status::Failed;
    break;
  }
  return result;
}

void NodeTransferService::appendResult(NodeTransferBatchResult &p_batch,
                                       NodeTransferItemResult p_result) {
  if (p_result.destinationCommitted() && !p_batch.m_firstSuccessfulDestination.isValid()) {
    p_batch.m_firstSuccessfulDestination = p_result.m_destination;
  }
  switch (p_result.m_status) {
  case NodeTransferItemResult::Status::Copied:
    ++p_batch.m_copiedCount;
    break;
  case NodeTransferItemResult::Status::Moved:
    ++p_batch.m_movedCount;
    break;
  case NodeTransferItemResult::Status::CopiedSourceRetained:
    ++p_batch.m_sourceRetainedCount;
    break;
  case NodeTransferItemResult::Status::Failed:
    ++p_batch.m_failedCount;
    if (p_result.m_recoveryRequired) {
      ++p_batch.m_recoveryRequiredCount;
    }
    break;
  case NodeTransferItemResult::Status::Cancelled:
    ++p_batch.m_cancelledCount;
    break;
  }
  p_batch.m_items.append(std::move(p_result));
}
