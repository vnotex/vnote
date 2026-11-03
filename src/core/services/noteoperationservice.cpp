#include "noteoperationservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <QtConcurrent>

#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
constexpr qint64 c_maxNoteBytes = 50 * 1024 * 1024;
constexpr int c_deadlineMs = 5000;

void fail(NoteOperationResult *p_result, const QString &p_code) {
  p_result->errorCode = p_code;
  // Stable domain diagnostics deliberately contain no native error or physical path.
  p_result->errorMessage = NoteOperationService::tr(
      "The note operation could not complete. Check its error code and saved status.");
}

QString joinedPath(const QString &p_parent, const QString &p_name) {
  return p_parent.isEmpty() ? p_name : p_parent + QLatin1Char('/') + p_name;
}

bool ordinaryPath(const QString &p_path, bool p_root = false) {
  if (p_path.isEmpty())
    return p_root;
  if (QDir::isAbsolutePath(p_path) || QDir::cleanPath(p_path) != p_path ||
      p_path.contains(QLatin1Char('\\')) || p_path.contains(QLatin1Char(':')) ||
      p_path.contains(QChar::Null))
    return false;
  for (const auto &part : p_path.split(QLatin1Char('/'))) {
    if (part.isEmpty() || part.startsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char('.')) ||
        part.endsWith(QLatin1Char(' ')))
      return false;
  }
  return true;
}

bool safePhysicalPath(const QString &p_root, const QString &p_path, QString *p_canonical) {
  const QFileInfo rootInfo(p_root);
  const auto root = rootInfo.canonicalFilePath();
  const auto absoluteRoot = QDir::cleanPath(rootInfo.absoluteFilePath());
  const QFileInfo file(p_path);
  const auto canonical = file.canonicalFilePath();
  if (!rootInfo.isDir() || root.isEmpty() || canonical.isEmpty())
    return false;
  const auto relative = QDir(root).relativeFilePath(canonical);
  if (QDir::isAbsolutePath(relative) || relative == QLatin1String("..") ||
      relative.startsWith(QLatin1String("../")))
    return false;
#ifdef Q_OS_WIN
  constexpr auto sensitivity = Qt::CaseInsensitive;
#else
  constexpr auto sensitivity = Qt::CaseSensitive;
#endif
  for (QFileInfo part = file;; part = QFileInfo(part.absolutePath())) {
    if (part.isSymbolicLink() || part.isJunction())
      return false;
    if (QDir::cleanPath(part.absoluteFilePath()).compare(absoluteRoot, sensitivity) == 0)
      break;
    if (part.isRoot())
      return false;
  }
  *p_canonical = canonical;
  return true;
}

bool contentType(const FileType &p_type) {
  return p_type.m_typeName == QLatin1String("Markdown") ||
         p_type.m_typeName == QLatin1String("Text");
}

// A worker owns the gate while hashing, then requests one callback-free metadata
// commit on the owner thread. It never waits for hooks or event processing on
// shutdown: cancellation wakes the handshake and retires any queued continuation.
struct NodeCommit {
  QMutex mutex;
  QWaitCondition changed;
  bool done = false;
  bool retired = false;
  NoteOperationResult result;
};
} // namespace

struct NoteOperationService::Operation {
  NoteOperationRequest request;
  NoteOperationResult result;
  QString physicalPath;
  QString canonicalPath;
  QString parentPath;
  QString parentUuid;
  QString targetPath;
  CommentService::FlushCheckpoint comments;
  SyncWorkQueueManager::MaintenanceLease maintenance;
  QElapsedTimer timer;
  QFutureWatcher<NoteOperationResult> *watcher = nullptr;
  std::shared_ptr<NodeCommit> commit;
  bool reserved = false;
  bool afterHook = false;
};

NoteOperationService::NoteOperationService(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  qRegisterMetaType<NoteOperationKind>();
  qRegisterMetaType<NoteOperationRequest>();
  qRegisterMetaType<NoteOperationResult>();
}

NoteOperationService::~NoteOperationService() {
  shutdown();
  const auto pending = m_operations;
  for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
    if (it.value()->watcher) {
      it.value()->watcher->future().waitForFinished();
      finishNodeOperation(it.key());
    }
  }
  // Content requests are owned by BufferService through their real worker
  // completion. Its cancellation token survives this facade; its QPointer
  // callback cannot access a destroyed service or release the reservation early.
}

quint64 NoteOperationService::execute(NoteOperationRequest p_request) {
  Q_ASSERT(QThread::currentThread() == thread());
  const quint64 token = ++m_nextToken;
  auto operation = std::make_shared<Operation>();
  if (!p_request.cancelled)
    p_request.cancelled = std::make_shared<std::atomic_bool>(false);
  operation->request = std::move(p_request);
  operation->result.notebookId = operation->request.notebookId;
  operation->result.nodeId = operation->request.nodeId;
  operation->result.relativePath = operation->request.relativePath;
  operation->timer.start();
  m_operations.insert(token, operation);
  QTimer::singleShot(0, this, [this, token]() { start(token); });
  return token;
}

void NoteOperationService::cancel(quint64 p_token) {
  Q_ASSERT(QThread::currentThread() == thread());
  const auto operation = m_operations.value(p_token);
  if (!operation)
    return;
  operation->request.cancelled->store(true);
  if (operation->commit) {
    QMutexLocker lock(&operation->commit->mutex);
    operation->commit->changed.wakeAll();
  }
}

void NoteOperationService::shutdown() {
  Q_ASSERT(QThread::currentThread() == thread());
  m_stopping = true;
  const auto tokens = m_operations.keys();
  for (const auto token : tokens)
    cancel(token);
  // No GUI join with a gate holder that needs this thread. Pending node
  // continuations see cancellation; content saves publish their actual outcome.
}

bool NoteOperationService::validate(Operation &p_operation, NoteOperationResult *p_result) const {
  const auto &request = p_operation.request;
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *buffers = m_services.get<BufferService>();
  auto *types = m_services.get<FileTypeCoreService>();
  if (!notebooks || !buffers || !types || !m_services.get<NotebookIoGate>()) {
    fail(p_result, QStringLiteral("unavailable"));
    return false;
  }
  if (m_stopping || request.cancelled->load()) {
    fail(p_result, QStringLiteral("cancelled"));
    return false;
  }
  if (!request.authorized || !request.authorized()) {
    fail(p_result, QStringLiteral("forbidden"));
    return false;
  }
  const auto notebook = notebooks->getNotebookConfig(request.notebookId);
  if (notebook.isEmpty()) {
    fail(p_result, QStringLiteral("not_found"));
    return false;
  }
  if (notebooks->isNotebookReadOnly(request.notebookId)) {
    fail(p_result, QStringLiteral("read_only"));
    return false;
  }
  const bool create = request.kind == NoteOperationKind::Create;
  if (!ordinaryPath(request.relativePath, create)) {
    fail(p_result, QStringLiteral("not_found"));
    return false;
  }
  if (create || request.kind == NoteOperationKind::Rename) {
    if (!ordinaryPath(request.name) || request.name.contains(QLatin1Char('/'))) {
      fail(p_result, QStringLiteral("invalid_name"));
      return false;
    }
    const auto parent = notebooks->getFileInfo(request.notebookId, p_operation.parentPath);
    if (parent.value(QLatin1String(vxcore::kJsonKeyType)).toString() != QLatin1String("folder") ||
        parent.value(QLatin1String(vxcore::kJsonKeyId)).toString() != p_operation.parentUuid) {
      fail(p_result, QStringLiteral("precondition_failed"));
      return false;
    }
    if (!p_operation.parentPath.isEmpty() && !p_operation.parentUuid.isEmpty() &&
        notebooks->getNodePathById(request.notebookId, p_operation.parentUuid) !=
            p_operation.parentPath) {
      fail(p_result, QStringLiteral("precondition_failed"));
      return false;
    }
    if (notebooks->getAvailableName(request.notebookId, p_operation.parentPath, request.name) !=
        request.name) {
      fail(p_result, QStringLiteral("name_conflict"));
      return false;
    }
  }
  if (!create) {
    if (request.nodeId.isEmpty() ||
        notebooks->getNodePathById(request.notebookId, request.nodeId) != request.relativePath ||
        ((request.kind == NoteOperationKind::Rename || request.kind == NoteOperationKind::Trash) &&
         request.expectedPath != request.relativePath)) {
      fail(p_result, QStringLiteral("precondition_failed"));
      return false;
    }
    const auto node = notebooks->getFileInfo(request.notebookId, request.relativePath);
    if (node.isEmpty() ||
        node.value(QLatin1String(vxcore::kJsonKeyId)).toString() != request.nodeId ||
        node.value(QLatin1String(vxcore::kJsonKeyType)).toString() != QLatin1String("file")) {
      fail(p_result, QStringLiteral("not_found"));
      return false;
    }
    if (node.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
        request.relativePath.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive)) {
      fail(p_result, QStringLiteral("protected_note_unavailable"));
      return false;
    }
    const auto type = types->getFileType(request.relativePath);
    if (request.kind == NoteOperationKind::Rename) {
      if (type.m_typeName.isEmpty() ||
          types->getFileType(request.name).m_typeName != type.m_typeName) {
        fail(p_result, QStringLiteral("unsupported_note_type"));
        return false;
      }
    } else if (!contentType(type)) {
      fail(p_result, QStringLiteral("unsupported_note_type"));
      return false;
    }
    if (request.kind != NoteOperationKind::Rename && request.expectedFileSha256.size() != 32) {
      fail(p_result, QStringLiteral("precondition_failed"));
      return false;
    }
    if (request.kind == NoteOperationKind::Trash &&
        !buffers->isNotebookBundled(request.notebookId)) {
      fail(p_result, QStringLiteral("unsupported_operation"));
      return false;
    }
    for (const auto &value : buffers->listBuffers()) {
      const auto info = value.toObject();
      if (info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() != request.notebookId ||
          info.value(QStringLiteral("filePath")).toString() != request.relativePath)
        continue;
      const auto id = info.value(QLatin1String(vxcore::kJsonKeyId)).toString();
      if (buffers->isBufferReadOnly(id)) {
        fail(p_result, QStringLiteral("read_only"));
        return false;
      }
      if ((!p_operation.reserved && buffers->isContentReplacementActive(id)) ||
          buffers->isSaveQueueBusy(id)) {
        fail(p_result, QStringLiteral("resource_busy"));
        return false;
      }
      if (buffers->isDirty(id) || buffers->currentRevision(id) != buffers->lastSavedRevision(id) ||
          buffers->isModified(id)) {
        fail(p_result, QStringLiteral("unsaved_changes"));
        return false;
      }
    }
  } else if (!request.text.isEmpty() || !contentType(types->getFileType(request.name))) {
    fail(p_result, QStringLiteral("unsupported_note_type"));
    return false;
  }
  const auto path = notebooks->buildAbsolutePath(request.notebookId, request.relativePath);
  QString canonical;
  if (!safePhysicalPath(notebooks->buildAbsolutePath(request.notebookId, {}), path, &canonical) ||
      (!p_operation.canonicalPath.isEmpty() && canonical != p_operation.canonicalPath) ||
      !QFileInfo(path).isWritable()) {
    fail(p_result, QStringLiteral("precondition_failed"));
    return false;
  }
  if (!create && !QFileInfo(path).isFile()) {
    fail(p_result, QStringLiteral("not_found"));
    return false;
  }
  p_operation.physicalPath = path;
  p_operation.canonicalPath = canonical;
  return true;
}

void NoteOperationService::start(quint64 p_token) {
  const auto operation = m_operations.value(p_token);
  if (!operation)
    return;
  auto &request = operation->request;
  try {
    auto *notebooks = m_services.get<NotebookCoreService>();
    if (!notebooks) {
      fail(&operation->result, QStringLiteral("unavailable"));
      complete(p_token, operation->result);
      return;
    }
    if (request.kind == NoteOperationKind::Create) {
      const auto rootId = notebooks->getFileInfo(request.notebookId, {})
                              .value(QLatin1String(vxcore::kJsonKeyId))
                              .toString();
      operation->parentPath =
          request.parentId.isEmpty() || request.parentId == rootId
              ? QString()
              : notebooks->getNodePathById(request.notebookId, request.parentId);
      request.relativePath = operation->parentPath;
    } else {
      const int slash = request.relativePath.lastIndexOf(QLatin1Char('/'));
      operation->parentPath = slash < 0 ? QString() : request.relativePath.left(slash);
    }
    if (request.kind == NoteOperationKind::Create || request.kind == NoteOperationKind::Rename) {
      operation->parentUuid = notebooks->getFileInfo(request.notebookId, operation->parentPath)
                                  .value(QLatin1String(vxcore::kJsonKeyId))
                                  .toString();
      if (request.kind == NoteOperationKind::Create && !request.parentId.isEmpty() &&
          request.parentId != operation->parentUuid) {
        fail(&operation->result, QStringLiteral("not_found"));
        complete(p_token, operation->result);
        return;
      }
      operation->targetPath = joinedPath(operation->parentPath, request.name);
    }
    if (!validate(*operation, &operation->result)) {
      complete(p_token, operation->result);
      return;
    }
    if (request.kind == NoteOperationKind::Replace || request.kind == NoteOperationKind::Append) {
      auto *buffers = m_services.get<BufferService>();
      const QPointer<NoteOperationService> self(this);
      if (buffers->replaceContent(request, [self, p_token](NoteOperationResult result) {
            if (self)
              self->complete(p_token, std::move(result));
          }) <= 0) {
        fail(&operation->result, QStringLiteral("resource_busy"));
        complete(p_token, operation->result);
      }
      return;
    }
    if (auto *queues = m_services.get<SyncWorkQueueManager>()) {
      operation->maintenance = queues->tryAcquireMaintenance({request.notebookId});
      if (!operation->maintenance) {
        fail(&operation->result, QStringLiteral("resource_busy"));
        complete(p_token, operation->result);
        return;
      }
    }
    runNodeOperation(p_token);
  } catch (...) {
    fail(&operation->result, QStringLiteral("operation_failed"));
    complete(p_token, operation->result);
  }
}

void NoteOperationService::runNodeOperation(quint64 p_token) {
  const auto operation = m_operations.value(p_token);
  if (!operation || operation->watcher)
    return;
  auto *buffers = m_services.get<BufferService>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *comments = m_services.get<CommentService>();
  auto &request = operation->request;
  const QPointer<NoteOperationService> owner(this);
  try {
    if (!validate(*operation, &operation->result)) {
      complete(p_token, operation->result);
      return;
    }
    if (comments && request.kind != NoteOperationKind::Create) {
      // Flush callbacks are owner-thread-only and strictly outside the I/O gate.
      // A one-millisecond bound yields to the event loop instead of draining GUI work.
      const auto flushed =
          comments->flushAndWaitForIdle(request.notebookId, request.relativePath, false, 1);
      if (!owner)
        return;
      if (flushed.m_status != CommentService::FlushResult::Status::Succeeded) {
        if (flushed.m_status == CommentService::FlushResult::Status::Timeout &&
            operation->timer.elapsed() < c_deadlineMs) {
          QTimer::singleShot(25, this, [this, p_token]() { runNodeOperation(p_token); });
          return;
        }
        fail(&operation->result, QStringLiteral("comments_not_saved"));
        complete(p_token, operation->result);
        return;
      }
      operation->comments = flushed.m_checkpoint;
    }
    const bool allowed = notebooks->beforeFileMutation(request);
    if (!owner)
      return;
    if (!allowed) {
      fail(&operation->result, QStringLiteral("cancelled"));
      complete(p_token, operation->result);
      return;
    }
    if (!validate(*operation, &operation->result)) {
      complete(p_token, operation->result);
      return;
    }
    QStringList protectedPaths;
    operation->reserved = buffers->beginSyncApply(request.notebookId, &protectedPaths);
    if (!owner) {
      buffers->endSyncApply(request.notebookId, {});
      return;
    }
    if (!operation->reserved || !buffers->isSyncApplyReady(request.notebookId) ||
        (request.kind != NoteOperationKind::Create &&
         protectedPaths.contains(request.relativePath))) {
      fail(&operation->result, QStringLiteral("resource_busy"));
      complete(p_token, operation->result);
      return;
    }
    // Detect unreported editor drafts while views are frozen, before entering
    // the callback-free gate phase. Native editor capture must never run there.
    if (request.kind != NoteOperationKind::Create) {
      for (const auto &value : buffers->listBuffers()) {
        const auto info = value.toObject();
        if (info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() !=
                request.notebookId ||
            info.value(QStringLiteral("filePath")).toString() != request.relativePath)
          continue;
        const auto id = info.value(QLatin1String(vxcore::kJsonKeyId)).toString();
        QString text;
        bool hasWriter = false;
        const bool captured = buffers->captureActiveWriterContent(id, &text, &hasWriter);
        if (!hasWriter)
          continue;
        const auto buffer = buffers->getBufferHandle(id);
        auto original = buffers->decodeContent(id, buffer.getContentRaw());
        auto normalize = [](QString text) {
          if (text.startsWith(QChar(0xfeff)))
            text.remove(0, 1);
          text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
          text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
          return text;
        };
        if (!captured || normalize(text) != normalize(original)) {
          fail(&operation->result, QStringLiteral("unsaved_changes"));
          complete(p_token, operation->result);
          return;
        }
      }
    }
    operation->commit = std::make_shared<NodeCommit>();
    operation->commit->result = operation->result;
    operation->watcher = new QFutureWatcher<NoteOperationResult>(this);
    connect(operation->watcher, &QFutureWatcher<NoteOperationResult>::finished, this,
            [this, p_token]() { finishNodeOperation(p_token); });
    const auto commit = operation->commit;
    const auto cancelled = request.cancelled;
    const auto notebookId = request.notebookId;
    const auto physicalPath = operation->physicalPath;
    const auto canonicalPath = operation->canonicalPath;
    const auto expected = request.expectedFileSha256;
    const bool trash = request.kind == NoteOperationKind::Trash;
    auto *gate = m_services.get<NotebookIoGate>();
    const QPointer<NoteOperationService> self(this);
    operation->watcher->setFuture(QtConcurrent::run([self, p_token, commit, cancelled, notebookId,
                                                     physicalPath, canonicalPath, expected, trash,
                                                     gate]() {
      NotebookIoGate::ScopedTryLock lock(*gate, notebookId, 0);
      if (!lock.isLocked()) {
        fail(&commit->result, QStringLiteral("resource_busy"));
        return commit->result;
      }
      if (cancelled->load()) {
        fail(&commit->result, QStringLiteral("cancelled"));
        return commit->result;
      }
      if (trash) {
        QFile file(physicalPath);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (QFileInfo(physicalPath).canonicalFilePath() != canonicalPath ||
            !file.open(QIODevice::ReadOnly) || file.size() > c_maxNoteBytes ||
            !hash.addData(&file) || file.error() != QFileDevice::NoError ||
            hash.result() != expected) {
          fail(&commit->result, QStringLiteral("precondition_failed"));
          return commit->result;
        }
      }
      if (!self) {
        fail(&commit->result, QStringLiteral("cancelled"));
        return commit->result;
      }
      QMetaObject::invokeMethod(
          self,
          [self, p_token, commit]() {
            QMutexLocker guard(&commit->mutex);
            if (commit->retired || !self)
              return;
            const auto operation = self->m_operations.value(p_token);
            if (!operation) {
              commit->retired = true;
              commit->changed.wakeAll();
              return;
            }
            try {
              auto *comments = self->m_services.get<CommentService>();
              if (!self->validate(*operation, &commit->result)) {
                // Validation owns the stable failure code.
              } else if (operation->comments.isValid() && comments &&
                         !comments->isFlushCheckpointCurrent(operation->comments)) {
                fail(&commit->result, QStringLiteral("comments_not_saved"));
              } else {
                auto *notebooks = self->m_services.get<NotebookCoreService>();
                const auto error =
                    notebooks->commitFileMutation(operation->request, &commit->result);
                if (error != VXCORE_OK) {
                  fail(&commit->result,
                       commit->result.recoveryRequired   ? QStringLiteral("recovery_required")
                       : error == VXCORE_ERR_UNSUPPORTED ? QStringLiteral("unsupported_operation")
                       : error == VXCORE_ERR_READ_ONLY   ? QStringLiteral("read_only")
                                                         : QStringLiteral("operation_failed"));
                } else if (operation->request.kind == NoteOperationKind::Create ||
                           operation->request.kind == NoteOperationKind::Rename) {
                  commit->result.relativePath = operation->targetPath;
                }
              }
            } catch (...) {
              fail(&commit->result, QStringLiteral("operation_failed"));
            }
            commit->done = true;
            commit->changed.wakeAll();
          },
          Qt::QueuedConnection);
      QElapsedTimer deadline;
      deadline.start();
      QMutexLocker guard(&commit->mutex);
      while (!commit->done && !commit->retired && !cancelled->load() &&
             deadline.elapsed() < c_deadlineMs)
        commit->changed.wait(&commit->mutex, 25);
      if (!commit->done) {
        commit->retired = true;
        fail(&commit->result,
             cancelled->load() ? QStringLiteral("cancelled") : QStringLiteral("resource_busy"));
      }
      return commit->result;
    }));
  } catch (...) {
    fail(&operation->result, QStringLiteral("operation_failed"));
    complete(p_token, operation->result);
  }
}

void NoteOperationService::finishNodeOperation(quint64 p_token) {
  const auto operation = m_operations.value(p_token);
  if (!operation || !operation->watcher || !operation->watcher->isFinished())
    return;
  const QPointer<NoteOperationService> owner(this);
  if (!operation->afterHook) {
    operation->result = operation->watcher->result();
    operation->afterHook = true;
    operation->timer.restart();
    if (operation->result.committed && !operation->result.recoveryRequired) {
      try {
        if (!m_services.get<NotebookCoreService>()->afterFileMutation(operation->request)) {
          fail(&operation->result, QStringLiteral("after_hook_failed"));
        }
      } catch (...) {
        fail(&operation->result, QStringLiteral("after_hook_failed"));
      }
      if (!owner)
        return;
    }
  }
  if (operation->result.committed && !operation->result.recoveryRequired &&
      operation->request.kind != NoteOperationKind::Create) {
    if (auto *comments = m_services.get<CommentService>()) {
      auto flushed = comments->flushAndWaitForIdle(operation->request.notebookId,
                                                   operation->request.relativePath, false, 1);
      if (!owner)
        return;
      if (flushed.m_status == CommentService::FlushResult::Status::Succeeded &&
          operation->request.kind == NoteOperationKind::Rename)
        flushed = comments->flushAndWaitForIdle(operation->request.notebookId,
                                                operation->result.relativePath, false, 1);
      if (!owner)
        return;
      if (flushed.m_status != CommentService::FlushResult::Status::Succeeded) {
        if (!m_stopping && flushed.m_status == CommentService::FlushResult::Status::Timeout &&
            operation->timer.elapsed() < c_deadlineMs) {
          QTimer::singleShot(25, this, [this, p_token]() { finishNodeOperation(p_token); });
          return;
        }
        operation->result.saved = false;
        fail(&operation->result, QStringLiteral("comments_not_saved"));
      }
    }
  }
  complete(p_token, operation->result);
}

void NoteOperationService::complete(quint64 p_token, NoteOperationResult p_result) {
  const auto operation = m_operations.take(p_token);
  if (!operation)
    return;
  const QPointer<NoteOperationService> owner(this);
  if (operation->reserved) {
    m_services.get<BufferService>()->endSyncApply(operation->request.notebookId, {});
    if (!owner)
      return;
    m_services.get<BufferService>()->pruneClosedBuffers();
    operation->reserved = false;
  }
  operation->maintenance.release();
  if (operation->watcher) {
    disconnect(operation->watcher, nullptr, this, nullptr);
    operation->watcher->deleteLater();
    operation->watcher = nullptr;
  }
  QTimer::singleShot(
      0, this, [this, p_token, result = std::move(p_result)]() { emit finished(p_token, result); });
}
