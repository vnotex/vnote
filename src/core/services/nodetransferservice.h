#ifndef NODETRANSFERSERVICE_H
#define NODETRANSFERSERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <utility>

#include <core/services/notebookcoreservice.h>

namespace vnotex {

class BufferService;
class CommentService;
class HookManager;
class NotebookIoGate;
class SyncWorkQueueManager;

struct NodeTransferSourceItem {
  QString m_nodeId;
  QString m_relativePath;
  bool m_isFolder = false;
};

struct NodeTransferRequest {
  QString m_sourceNotebookId;
  QVector<NodeTransferSourceItem> m_items;
  QString m_destinationNotebookId;
  QString m_destinationFolderPath;
  NodeTransferOperation m_operation = NodeTransferOperation::Copy;
};

struct NodeTransferDestination {
  QString m_notebookId;
  QString m_relativePath;
  QString m_nodeId;

  bool isValid() const { return !m_notebookId.isEmpty() && !m_relativePath.isEmpty(); }
};

struct NodeTransferItemResult {
  enum class Status { Copied, Moved, CopiedSourceRetained, Failed, Cancelled };

  bool destinationCommitted() const {
    return m_status == Status::Copied || m_status == Status::Moved ||
           m_status == Status::CopiedSourceRetained || m_recoveryRequired;
  }

  NodeTransferSourceItem m_source;
  NodeTransferDestination m_destination;
  Status m_status = Status::Failed;
  VxCoreError m_error = VXCORE_ERR_UNKNOWN;
  QString m_errorMessage;
  QJsonObject m_resumeToken;
  bool m_recoveryRequired = false;
};

struct NodeTransferBatchResult {
  QVector<NodeTransferItemResult> m_items;
  NodeTransferDestination m_firstSuccessfulDestination;
  int m_copiedCount = 0;
  int m_movedCount = 0;
  int m_sourceRetainedCount = 0;
  int m_recoveryRequiredCount = 0;
  int m_failedCount = 0;
  int m_cancelledCount = 0;
};

struct NodeTransferCallbacks {
  std::function<void(int, int, const QString &, const QString &)> m_phaseChanged;
  std::function<void(quint64, quint64)> m_byteProgress;
  std::function<bool()> m_isCancelled;
};

class NodeTransferService : public QObject {
  Q_OBJECT

public:
  NodeTransferService(NotebookCoreService *p_notebookService, BufferService *p_bufferService,
                      CommentService *p_commentService,
                      SyncWorkQueueManager *p_syncWorkQueueManager, NotebookIoGate *p_ioGate,
                      HookManager *p_hookManager, QObject *p_parent = nullptr);

  NodeTransferBatchResult transfer(const NodeTransferRequest &p_request,
                                   const NodeTransferCallbacks &p_callbacks = {});

  // Retries source deletion from an existing durable destination. This route
  // invokes only vxcore finalization and can never import another destination.
  NodeTransferItemResult finalizeTransferredMove(const QJsonObject &p_resumeToken,
                                                 int p_gateTimeoutMs = 250,
                                                 const std::function<bool()> &p_isCancelled = {});

  void testSetBeforeFinalizeGateCallback(std::function<void()> p_callback) {
    m_testBeforeFinalizeGateCallback = std::move(p_callback);
  }

private:
  struct BufferSnapshot {
    QString m_bufferId;
    QString m_relativePath;
    quint64 m_revision = 0;

    bool operator==(const BufferSnapshot &p_other) const {
      return m_bufferId == p_other.m_bufferId && m_relativePath == p_other.m_relativePath &&
             m_revision == p_other.m_revision;
    }
  };

  QVector<BufferSnapshot> buffersInScope(const QString &p_notebookId, const QString &p_relativePath,
                                         bool p_isFolder) const;
  bool waitForSaveQueues(const QVector<BufferSnapshot> &p_buffers, int p_timeoutMs,
                         const std::function<bool()> &p_isCancelled) const;
  bool validateBuffers(const QVector<BufferSnapshot> &p_expected, const QString &p_notebookId,
                       const QString &p_relativePath, bool p_isFolder) const;
  bool validateNotebookPair(const NodeTransferRequest &p_request, VxCoreError &p_error,
                            QString &p_errorMessage) const;
  static bool pathMatches(const QString &p_candidate, const QString &p_scope, bool p_isFolder);
  static NodeTransferItemResult fromCoreResult(const NodeTransferSourceItem &p_source,
                                               const NodeTransferCoreResult &p_coreResult);
  static void appendResult(NodeTransferBatchResult &p_batch, NodeTransferItemResult p_result);

  NotebookCoreService *m_notebookService = nullptr;
  BufferService *m_bufferService = nullptr;
  CommentService *m_commentService = nullptr;
  SyncWorkQueueManager *m_syncWorkQueueManager = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  HookManager *m_hookManager = nullptr;
  std::function<void()> m_testBeforeFinalizeGateCallback;
};

} // namespace vnotex

#endif // NODETRANSFERSERVICE_H
