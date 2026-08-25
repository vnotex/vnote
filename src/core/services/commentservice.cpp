#include "commentservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThreadPool>

#include <vxcore/notebook_json_keys.h>

#include <core/hookevents.h>
#include <core/hooknames.h>

#include "hookmanager.h"
#include "notebookcoreservice.h"
#include "notebookiogate.h"

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(commentServiceLog, "vnote.comments")

bool isRawNotebookConfig(const QJsonObject &p_config) {
  // Canonical key, the same test NotebookNodeController uses to gate
  // bundled-only actions. Anything that is not explicitly "bundled" is treated
  // as raw, which fails safe: a sibling sidecar works for every layout, a UUID
  // assets folder does not.
  return p_config.value(QLatin1String(vxcore::kJsonKeyType)).toString() !=
         QStringLiteral("bundled");
}

} // namespace

CommentService::CommentService(NotebookCoreService *p_notebookService, NotebookIoGate *p_ioGate,
                               HookManager *p_hookMgr, QObject *p_parent)
    : QObject(p_parent), m_notebookService(p_notebookService), m_ioGate(p_ioGate),
      m_hookMgr(p_hookMgr) {
  installLifecycleHooks();
}

CommentService::~CommentService() {
  if (m_hookMgr) {
    for (int id : m_hookIds) {
      m_hookMgr->removeAction(id);
    }
  }

  // Bounded first, so a normal teardown gets a diagnostic rather than an
  // indefinite hang if something is genuinely stuck.
  if (!shutdown(5000)) {
    qCWarning(commentServiceLog)
        << "comment writes did not drain in 5s; waiting for them before teardown";
  }

  // Then UNBOUNDED. A worker holds a raw `this` and touches m_mutex/m_queues
  // and QMetaObject::invokeMethod(this, ...) after its write, so returning from
  // the destructor while one is still running is a use-after-free. A bounded
  // wait here would trade a rare hang for a rare crash, which is the wrong way
  // round. (Any queued completion still in the event loop is discarded by
  // ~QObject, which removes posted events for its receiver.)
  QMutexLocker locker(&m_mutex);
  while (m_inFlightCount > 0) {
    m_drained.wait(&m_mutex);
  }
}

// ============ Location ============

bool CommentService::isSiblingNotebook(const QString &p_notebookId) const {
  if (!m_notebookService || p_notebookId.isEmpty()) {
    return false;
  }
  const auto config = m_notebookService->getNotebookConfig(p_notebookId);
  return !config.isEmpty() && isRawNotebookConfig(config);
}

CommentService::Location CommentService::resolveLocation(const NodeIdentifier &p_nodeId) const {
  Location location;

  if (p_nodeId.relativePath.isEmpty() || p_nodeId.isVirtual()) {
    return location;
  }

  // External file: no notebook, and relativePath already holds the absolute
  // on-disk path (the same convention PdfViewWindowController::buildAbsolutePath
  // short-circuits on).
  if (p_nodeId.notebookId.isEmpty()) {
    location.m_kind = Location::Kind::Sibling;
    location.m_storePath = p_nodeId.relativePath + siblingSuffix();
    return location;
  }

  if (!m_notebookService) {
    return location;
  }

  const auto config = m_notebookService->getNotebookConfig(p_nodeId.notebookId);
  if (config.isEmpty()) {
    return location;
  }

  const auto absPath =
      m_notebookService->buildAbsolutePath(p_nodeId.notebookId, p_nodeId.relativePath);
  if (absPath.isEmpty()) {
    return location;
  }

  if (isRawNotebookConfig(config)) {
    location.m_kind = Location::Kind::Sibling;
    location.m_notebookId = p_nodeId.notebookId;
    location.m_storePath = absPath + siblingSuffix();
    return location;
  }

  // Bundled. getAttachmentsFolder ALREADY appends the file record's id, so this
  // is <assets-root>/<file-uuid> - do NOT reconstruct it from getFileInfo()'s
  // id, and do not expect it to exist yet (it is created under the gate at
  // write time).
  const auto assetsFolder =
      m_notebookService->getAttachmentsFolder(p_nodeId.notebookId, p_nodeId.relativePath);
  if (assetsFolder.isEmpty()) {
    qCWarning(commentServiceLog) << "no attachments folder for" << p_nodeId.relativePath
                                 << "in notebook" << p_nodeId.notebookId;
    return location;
  }

  location.m_kind = Location::Kind::Attachments;
  location.m_notebookId = p_nodeId.notebookId;
  location.m_storePath = QDir(assetsFolder).filePath(storeFileName());
  return location;
}

// ============ Load ============

CommentService::LoadResult CommentService::load(const NodeIdentifier &p_nodeId) const {
  LoadResult result;

  const auto location = resolveLocation(p_nodeId);
  if (!location.isValid()) {
    result.m_status = LoadResult::Status::Error;
    result.m_error = tr("Cannot locate the comment store for this file.");
    return result;
  }

  QFile file(location.m_storePath);
  if (!file.exists()) {
    // The normal case for a file nobody has commented on.
    result.m_status = LoadResult::Status::Missing;
    return result;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qCWarning(commentServiceLog) << "cannot read" << location.m_storePath;
    result.m_status = LoadResult::Status::Error;
    result.m_error = tr("Cannot read %1.").arg(location.m_storePath);
    return result;
  }

  QJsonParseError parseError{};
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qCWarning(commentServiceLog) << "malformed comments store" << location.m_storePath
                                 << parseError.errorString();
    // Status::Error, NOT an empty set: the caller must go read-only so the next
    // edit cannot overwrite a file the user could still recover by hand.
    result.m_status = LoadResult::Status::Error;
    result.m_error =
        tr("%1 is not valid JSON (%2).").arg(location.m_storePath, parseError.errorString());
    return result;
  }

  result.m_status = LoadResult::Status::Loaded;
  result.m_comments = CommentSet::fromJson(doc.object());
  return result;
}

// ============ Queue ============

QString CommentService::jobKey(const QString &p_notebookId, const QString &p_relativePath) {
  return p_notebookId + QStringLiteral("::") + p_relativePath;
}

QString CommentService::jobKey(const NodeIdentifier &p_nodeId) {
  return jobKey(p_nodeId.notebookId, p_nodeId.relativePath);
}

void CommentService::enqueue(const QString &p_key, const Job &p_job) {
  bool needLaunch = false;

  {
    QMutexLocker locker(&m_mutex);
    if (m_stopping) {
      qCWarning(commentServiceLog) << "job after shutdown ignored for" << p_key;
      return;
    }

    auto &queue = m_queues[p_key];

    // Coalesce a Save with the tail ONLY when the tail is itself a Save. A
    // queued Move/Remove is a barrier: jumping it would reorder the write
    // relative to the file operation and reintroduce the race this FIFO exists
    // to remove.
    if (p_job.m_kind == Job::Kind::Save && !queue.isEmpty() &&
        queue.back().m_kind == Job::Kind::Save) {
      queue.back() = p_job;
    } else {
      queue.enqueue(p_job);
    }

    if (!m_running.value(p_key, false)) {
      m_running.insert(p_key, true);
      ++m_inFlightCount;
      needLaunch = true;
    }
  }

  if (needLaunch) {
    const QString keyCopy = p_key;
    QThreadPool::globalInstance()->start([this, keyCopy]() { runWorker(keyCopy); });
  }
}

void CommentService::scheduleSave(const NodeIdentifier &p_nodeId, const CommentSet &p_comments,
                                  quint64 p_generation) {
  const auto location = resolveLocation(p_nodeId);
  if (!location.isValid()) {
    emit saveFinished(p_nodeId, p_generation, false,
                      tr("Cannot locate the comment store for this file."));
    return;
  }

  // vxcore rejects asset writes on a read-only notebook BEFORE touching disk; a
  // direct QSaveFile would bypass that guard, so re-apply it here. Checked
  // before any mutex, queue insertion or worker dispatch, so nothing is written.
  if (!location.m_notebookId.isEmpty() && m_notebookService &&
      m_notebookService->isNotebookReadOnly(location.m_notebookId)) {
    qCWarning(commentServiceLog) << "save rejected: notebook is read-only" << location.m_notebookId;
    emit saveRejectedReadOnly(p_nodeId);
    return;
  }

  Job job;
  job.m_kind = Job::Kind::Save;
  job.m_nodeId = p_nodeId;
  job.m_location = location;
  job.m_generation = p_generation;
  // Serialized on the CALLING thread, so the worker never touches the caller's
  // CommentSet and a later mutation cannot race the pending write.
  job.m_payload = QJsonDocument(p_comments.toJson()).toJson(QJsonDocument::Indented);

  enqueue(jobKey(p_nodeId), job);
}

void CommentService::runWorker(const QString &p_key) {
  for (;;) {
    Job job;
    {
      QMutexLocker locker(&m_mutex);
      auto it = m_queues.find(p_key);
      if (it == m_queues.end() || it.value().isEmpty()) {
        if (it != m_queues.end()) {
          m_queues.erase(it);
        }
        m_running.insert(p_key, false);
        if (m_inFlightCount > 0) {
          --m_inFlightCount;
        }
        if (m_inFlightCount == 0) {
          m_drained.wakeAll();
        }
        return;
      }
      job = it.value().dequeue();
    }

    QString error;
    try {
      // Worker thread only. Serializes against BufferSaveQueue workers and the
      // git-stage phase of SyncOps on the same working tree.
      const bool gated = job.m_location.needsIoGate() && m_ioGate;
      QScopedPointer<NotebookIoGate::ScopedLock> lock;
      if (gated) {
        lock.reset(new NotebookIoGate::ScopedLock(*m_ioGate, job.m_location.m_notebookId));
      }

      switch (job.m_kind) {
      case Job::Kind::Save:
        error = writeStore(job.m_location, job.m_payload);
        break;

      case Job::Kind::Move:
        error = moveStore(job.m_location.m_storePath, job.m_destPath);
        break;

      case Job::Kind::Remove:
        if (QFile::exists(job.m_location.m_storePath) &&
            !QFile::remove(job.m_location.m_storePath)) {
          error = QStringLiteral("cannot remove %1").arg(job.m_location.m_storePath);
        }
        break;
      }
    } catch (const std::exception &e) {
      error = QStringLiteral("exception: ") + QString::fromUtf8(e.what());
    } catch (...) {
      error = QStringLiteral("unknown exception");
    }

    const bool ok = error.isEmpty();
    if (!ok) {
      qCWarning(commentServiceLog) << "comment store job failed:" << error;
    }

    // Only a Save reports back: Move/Remove are internal bookkeeping with no
    // caller waiting on them, and reporting them would look like a save result.
    if (job.m_kind != Job::Kind::Save) {
      continue;
    }

    const auto nodeId = job.m_nodeId;
    const auto generation = job.m_generation;
    const auto notebookId = job.m_location.m_notebookId;
    const bool notifyDirty = ok && !notebookId.isEmpty();

    QMetaObject::invokeMethod(
        this,
        [this, nodeId, generation, ok, error, notebookId, notifyDirty]() {
          emit saveFinished(nodeId, generation, ok, error);
          if (notifyDirty) {
            emit storeDirty(notebookId);
          }
        },
        Qt::QueuedConnection);
  }
}

QString CommentService::writeStore(const Location &p_location, const QByteArray &p_payload) {
  const QFileInfo info(p_location.m_storePath);

  // Created under the gate: getAttachmentsFolder() does NOT create the UUID
  // directory, so the very first comment on a bundled file lands in a folder
  // that does not exist yet.
  if (!info.dir().exists() && !QDir().mkpath(info.absolutePath())) {
    return QStringLiteral("cannot create %1").arg(info.absolutePath());
  }

  QSaveFile file(p_location.m_storePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return QStringLiteral("cannot open %1: %2").arg(p_location.m_storePath, file.errorString());
  }
  if (file.write(p_payload) != p_payload.size()) {
    file.cancelWriting();
    return QStringLiteral("short write to %1").arg(p_location.m_storePath);
  }
  if (!file.commit()) {
    return QStringLiteral("cannot commit %1: %2").arg(p_location.m_storePath, file.errorString());
  }
  return QString();
}

QString CommentService::moveStore(const QString &p_from, const QString &p_to) {
  if (p_from == p_to || !QFile::exists(p_from)) {
    return QString();
  }

  // Destination already present: the mover wins the file name, and the orphan is
  // LEFT IN PLACE rather than silently destroying someone's comments.
  if (QFile::exists(p_to)) {
    qCWarning(commentServiceLog) << "comment store already exists at" << p_to << "- leaving"
                                 << p_from << "in place";
    return QString();
  }

  const QFileInfo info(p_to);
  if (!info.dir().exists() && !QDir().mkpath(info.absolutePath())) {
    return QStringLiteral("cannot create %1").arg(info.absolutePath());
  }

  if (!QFile::rename(p_from, p_to)) {
    return QStringLiteral("cannot move %1 -> %2").arg(p_from, p_to);
  }
  return QString();
}

bool CommentService::isBusy(const NodeIdentifier &p_nodeId) const {
  const auto key = jobKey(p_nodeId);
  QMutexLocker locker(&m_mutex);
  const auto it = m_queues.constFind(key);
  return (it != m_queues.constEnd() && !it.value().isEmpty()) || m_running.value(key, false);
}

bool CommentService::shutdown(int p_timeoutMs) {
  QMutexLocker locker(&m_mutex);
  if (m_stopping && m_inFlightCount == 0) {
    return true;
  }
  m_stopping = true;
  // Do NOT clear m_queues: a dispatched worker is counted as in-flight and must
  // still commit its newest snapshot, or the user's last edit is lost.

  if (m_inFlightCount == 0) {
    return true;
  }

  QDeadlineTimer deadline(p_timeoutMs);
  while (m_inFlightCount > 0) {
    if (!m_drained.wait(&m_mutex, deadline)) {
      return m_inFlightCount == 0;
    }
  }
  return true;
}

// ============ Sibling lifecycle ============

void CommentService::installLifecycleHooks() {
  if (!m_hookMgr) {
    return;
  }

  m_hookIds.append(m_hookMgr->addAction<NodeRenameEvent>(
      HookNames::NodeAfterRename,
      [this](HookContext &, const NodeRenameEvent &p_event) {
        if (p_event.isFolder) {
          // A folder rename moves the file with its sibling sidecar in one
          // directory operation, so there is nothing to do.
          return;
        }
        onNodeRenamed(p_event.notebookId, p_event.relativePath, p_event.newName);
      },
      10));

  m_hookIds.append(m_hookMgr->addAction<NodeMoveEvent>(
      HookNames::NodeAfterMove,
      [this](HookContext &, const NodeMoveEvent &p_event) {
        if (p_event.isFolder) {
          return;
        }
        onNodeMoved(p_event.notebookId, p_event.oldRelativePath, p_event.newRelativePath);
      },
      10));

  m_hookIds.append(m_hookMgr->addAction<NodeOperationEvent>(
      HookNames::NodeAfterDelete,
      [this](HookContext &, const NodeOperationEvent &p_event) {
        if (p_event.isFolder) {
          return;
        }
        onNodeDeleted(p_event.notebookId, p_event.relativePath);
      },
      10));
}

void CommentService::onNodeRenamed(const QString &p_notebookId, const QString &p_oldRelativePath,
                                   const QString &p_newName) {
  const int lastSlash = p_oldRelativePath.lastIndexOf(QLatin1Char('/'));
  const QString newRelativePath =
      lastSlash >= 0 ? p_oldRelativePath.left(lastSlash + 1) + p_newName : p_newName;
  onNodeMoved(p_notebookId, p_oldRelativePath, newRelativePath);
}

void CommentService::onNodeMoved(const QString &p_notebookId, const QString &p_oldRelativePath,
                                 const QString &p_newRelativePath) {
  // A bundled store needs no help: its UUID folder is stable and travels with
  // the file record.
  if (!isSiblingNotebook(p_notebookId) || !m_notebookService) {
    return;
  }

  // The node has ALREADY moved, so both ends are rebuilt from the notebook root
  // rather than from the (now stale) index entry.
  const auto oldAbs = m_notebookService->buildAbsolutePath(p_notebookId, p_oldRelativePath);
  const auto newAbs = m_notebookService->buildAbsolutePath(p_notebookId, p_newRelativePath);
  if (oldAbs.isEmpty() || newAbs.isEmpty()) {
    return;
  }

  const QString oldKey = jobKey(p_notebookId, p_oldRelativePath);
  const QString newKey = jobKey(p_notebookId, p_newRelativePath);

  Job job;
  job.m_kind = Job::Kind::Move;
  job.m_location.m_kind = Location::Kind::Sibling;
  job.m_location.m_notebookId = p_notebookId;
  job.m_location.m_storePath = oldAbs + siblingSuffix();
  job.m_destPath = newAbs + siblingSuffix();

  // Queued on the OLD key, so it runs strictly AFTER any save still pending for
  // the old name. That is what stops a pending worker from recreating the old
  // sidecar behind the move.
  enqueue(oldKey, job);

  // Anything already queued under the NEW key (rare, but possible when the user
  // renames onto a file that also has pending work) keeps its own ordering.
  Q_UNUSED(newKey);
}

void CommentService::onNodeDeleted(const QString &p_notebookId, const QString &p_relativePath) {
  if (!isSiblingNotebook(p_notebookId) || !m_notebookService) {
    return;
  }

  const auto abs = m_notebookService->buildAbsolutePath(p_notebookId, p_relativePath);
  if (abs.isEmpty()) {
    return;
  }

  Job job;
  job.m_kind = Job::Kind::Remove;
  job.m_location.m_kind = Location::Kind::Sibling;
  job.m_location.m_notebookId = p_notebookId;
  job.m_location.m_storePath = abs + siblingSuffix();

  // Behind any pending save, so an in-flight write cannot resurrect the sidecar
  // after the delete.
  enqueue(jobKey(p_notebookId, p_relativePath), job);
}
