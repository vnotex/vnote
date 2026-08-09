#include "foldersharecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

// Upper bound on how long we wait for a previously-queued async auto-save of a
// note to drain before giving up. The queue normally clears in milliseconds;
// this only guards against a pathologically slow disk.
constexpr int c_saveQueueDrainTimeoutMs = 15000;

// Upper bound on how long a single share-triggered save waits for the notebook
// I/O gate. Kept short because it is taken on the GUI thread: a sync stage that
// holds it for longer should surface as "try again", not as a frozen window.
constexpr int c_gateTimeoutMs = 5000;

FolderSharePackager::Result makeFailure(const QString &p_message) {
  FolderSharePackager::Result result;
  result.m_status = FolderSharePackager::Status::Failed;
  result.m_errorMessage = p_message;
  return result;
}

FolderSharePackager::Result makeCancelled() {
  FolderSharePackager::Result result;
  result.m_status = FolderSharePackager::Status::Cancelled;
  return result;
}

// Canonical path when the node exists, cleaned absolute path otherwise. The
// canonical form resolves symlinks/junctions, which is exactly what the
// containment check needs: a destination REACHED through a link is fine, a
// destination that RESOLVES inside the notebook is not.
QString canonicalOrClean(const QString &p_path) {
  const QFileInfo info(p_path);
  const QString canonical = info.canonicalFilePath();
  if (!canonical.isEmpty()) {
    return QDir::cleanPath(canonical);
  }
  return QDir::cleanPath(info.absoluteFilePath());
}

bool isInsideOrEqual(const QString &p_root, const QString &p_target) {
  const QString root = QDir::cleanPath(p_root);
  const QString target = QDir::cleanPath(p_target);
#ifdef Q_OS_WIN
  const auto cs = Qt::CaseInsensitive;
#else
  const auto cs = Qt::CaseSensitive;
#endif
  // The SAME sensitivity for equality and for the descendant-prefix test: on a
  // case-sensitive filesystem "/data/Foo" and "/data/foo" are genuinely
  // different directories.
  if (target.compare(root, cs) == 0) {
    return true;
  }
  return target.startsWith(root + QLatin1Char('/'), cs);
}

} // namespace

FolderShareController::FolderShareController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

FolderShareController::~FolderShareController() = default;

QString FolderShareController::phaseLabel(FolderSharePackager::Phase p_phase) {
  switch (p_phase) {
  case FolderSharePackager::Phase::Validating:
    return tr("Checking the folder…");
  case FolderSharePackager::Phase::Copying:
    return tr("Copying files…");
  case FolderSharePackager::Phase::Verifying:
    return tr("Verifying the copy…");
  case FolderSharePackager::Phase::Publishing:
    return tr("Finishing…");
  }
  return tr("Working…");
}

FolderSharePackager::Result FolderShareController::shareFolder(const NodeIdentifier &p_nodeId,
                                                               const QString &p_destinationParent,
                                                               const Callbacks &p_callbacks) {
  // The callbacks pump the event loop, so a second invocation really is
  // reachable (a shortcut, a queued menu action) even though this is a blocking
  // call. Refuse rather than run two copies over the same temp namespace.
  if (m_busy) {
    return makeFailure(tr("Another folder is already being shared."));
  }
  m_busy = true;
  struct BusyGuard {
    bool *flag;
    ~BusyGuard() { *flag = false; }
  } guard{&m_busy};

  if (!p_nodeId.isValid()) {
    return makeFailure(tr("No folder is selected."));
  }
  if (p_destinationParent.isEmpty()) {
    return makeFailure(tr("No destination folder was chosen."));
  }

  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!notebooks) {
    return makeFailure(tr("This folder cannot be shared."));
  }

  // Resolve the storage roots through vxcore. This proves the selection is a
  // real, indexed, non-root folder of a BUNDLED notebook, with full
  // root-to-selected index reachability and no symlinked component.
  const FolderSharePaths paths =
      notebooks->getFolderSharePaths(p_nodeId.notebookId, p_nodeId.relativePath);
  if (!paths.isValid()) {
    return makeFailure(paths.m_errorMessage.isEmpty()
                           ? tr("This folder cannot be shared.")
                           : tr("This folder cannot be shared: %1").arg(paths.m_errorMessage));
  }

  const QString folderName = QFileInfo(paths.m_contentRoot).fileName();
  if (folderName.isEmpty()) {
    return makeFailure(tr("This folder cannot be shared."));
  }

  const QString destination = canonicalOrClean(p_destinationParent);
  const QFileInfo destinationInfo(destination);
  if (!destinationInfo.exists() || !destinationInfo.isDir()) {
    return makeFailure(tr("The destination folder does not exist."));
  }
  if (!destinationInfo.isWritable()) {
    return makeFailure(tr("The destination folder is not writable."));
  }

  // A destination REACHED through a symlink/junction is fine; a destination
  // that RESOLVES inside the source notebook is not — sharing into the notebook
  // would feed the copy its own output.
  const QString canonicalNotebookRoot = canonicalOrClean(paths.m_notebookRoot);
  if (isInsideOrEqual(canonicalNotebookRoot, destination)) {
    return makeFailure(tr("Choose a destination outside the notebook folder."));
  }

  // A legal NESTED folder may be named "vx_notebook", but it cannot occupy the
  // package root beside the bundle's own metadata directory.
  const bool caseSensitive = FolderSharePackager::destinationIsCaseSensitive(destination);
  const QString reserved = FolderSharePackager::reservedPackageDirName();
  if (folderName.compare(reserved, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0) {
    return makeFailure(
        tr("A folder named \"%1\" cannot be shared: that name is reserved inside the bundle.")
            .arg(reserved));
  }

  // Force every open modified note in the subtree to disk BEFORE copying.
  if (p_callbacks.m_labelChanged) {
    p_callbacks.m_labelChanged(tr("Saving open notes…"));
  }
  m_barrierRevisions.clear();
  QString saveError;
  if (!saveOpenNotesUnder(p_nodeId.notebookId, p_nodeId.relativePath, p_callbacks, &saveError)) {
    if (p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()) {
      return makeCancelled();
    }
    return makeFailure(saveError);
  }
  if (p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()) {
    return makeCancelled();
  }

  FolderSharePackager::Request request;
  // NOT the canonical form: the packager derives the content/metadata paths
  // RELATIVE to this root and then walks that chain looking for a symlinked
  // component. Canonicalizing only the root would (a) make the ROOT'S OWN link
  // check vacuous, since a canonical path no longer names the link, and (b) on
  // a platform where the notebook lives under a symlinked prefix (macOS:
  // /var -> /private/var) mix a resolved root with unresolved children, so
  // QDir::relativeFilePath yields a "../../.." escape that walks the check
  // straight out of the notebook and onto a system symlink. All three source
  // paths must come from the SAME (unresolved) namespace. The destination
  // stays canonical: it is not part of that chain, and the containment test
  // above must compare real locations rather than spellings.
  request.m_notebookRoot = QDir::cleanPath(paths.m_notebookRoot);
  request.m_contentRoot = QDir::cleanPath(paths.m_contentRoot);
  request.m_metadataRoot = QDir::cleanPath(paths.m_metadataRoot);
  request.m_destinationParent = destination;
  request.m_folderName = folderName;
  request.m_failureInjection = m_failureInjection;

  FolderSharePackager::Callbacks packagerCallbacks;
  packagerCallbacks.m_progress = p_callbacks.m_progress;
  packagerCallbacks.m_isCancelled = p_callbacks.m_isCancelled;
  if (p_callbacks.m_labelChanged) {
    auto labelChanged = p_callbacks.m_labelChanged;
    packagerCallbacks.m_phaseChanged = [labelChanged](FolderSharePackager::Phase p_phase) {
      labelChanged(phaseLabel(p_phase));
    };
  }
  // Runs in the packager's no-event-pumping section, right before the rename.
  const QString notebookId = p_nodeId.notebookId;
  const QString folderPath = p_nodeId.relativePath;
  packagerCallbacks.m_finalPrecondition = [this, notebookId, folderPath](QString *p_outError) {
    return openNotesAreStillDurable(notebookId, folderPath, p_outError);
  };

  return FolderSharePackager::run(request, packagerCallbacks);
}
QStringList FolderShareController::collectOpenNotesUnder(const QString &p_notebookId,
                                                         const QString &p_folderPath) const {
  QStringList candidates;
  auto *buffers = m_services.get<BufferService>();
  if (!buffers) {
    return candidates;
  }

  const QString prefix = p_folderPath + QLatin1Char('/');
  for (const QJsonValue &value : buffers->listBuffers()) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("isVirtual")).toBool()) {
      continue;
    }
    if (object.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() != p_notebookId) {
      continue;
    }
    if (!object.value(QStringLiteral("filePath")).toString().startsWith(prefix)) {
      continue;
    }
    const QString bufferId = object.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (!bufferId.isEmpty()) {
      candidates.append(bufferId);
    }
  }
  candidates.sort();
  return candidates;
}

bool FolderShareController::openNotesAreStillDurable(const QString &p_notebookId,
                                                     const QString &p_folderPath,
                                                     QString *p_outError) const {
  auto *buffers = m_services.get<BufferService>();
  if (!buffers) {
    return true;
  }

  const QStringList current = collectOpenNotesUnder(p_notebookId, p_folderPath);

  // A note opened (or moved) into the subtree after the barrier was never made
  // durable, so its content may only exist in memory.
  for (const QString &bufferId : current) {
    if (!m_barrierRevisions.contains(bufferId)) {
      *p_outError = tr("A note in this folder was opened while it was being prepared. Try "
                       "sharing again.");
      return false;
    }
  }

  for (auto it = m_barrierRevisions.constBegin(); it != m_barrierRevisions.constEnd(); ++it) {
    const QString &bufferId = it.key();
    if (!current.contains(bufferId)) {
      *p_outError = tr("A note in this folder was closed while it was being prepared. Try "
                       "sharing again.");
      return false;
    }
    // An edit made while the progress callbacks were pumping the event loop
    // bumps the revision and/or the modified flag, and under the "None"
    // auto-save policy it never reaches disk — so no amount of source
    // re-hashing would notice it.
    if (buffers->currentRevision(bufferId) != it.value() || buffers->isModified(bufferId) ||
        buffers->isDirty(bufferId) || buffers->isSaveQueueBusy(bufferId)) {
      *p_outError = tr("A note in this folder changed while it was being prepared. Try "
                       "sharing again.");
      return false;
    }
  }

  return true;
}

bool FolderShareController::saveOpenNotesUnder(const QString &p_notebookId,
                                               const QString &p_folderPath,
                                               const Callbacks &p_callbacks, QString *p_outError) {
  auto *buffers = m_services.get<BufferService>();
  if (!buffers) {
    return true; // No buffer layer: nothing can be unsaved.
  }

  const QStringList candidates = collectOpenNotesUnder(p_notebookId, p_folderPath);

  for (const QString &bufferId : candidates) {
    if (p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()) {
      return false;
    }

    // 1. DRAIN FIRST. An async auto-save the queue already holds owns an OLDER
    //    snapshot of this buffer; letting it run after we installed the
    //    editor's newer text would overwrite our content and then leave the
    //    buffer unmodified, so the durability check would pass while the disk
    //    held stale bytes. Draining first also avoids racing the worker on the
    //    mutex-less vxcore Buffer.
    //
    //    Pumping the event loop here is safe: we hold no lock and the buffer is
    //    not being mutated by us yet.
    QElapsedTimer timer;
    timer.start();
    while (buffers->isSaveQueueBusy(bufferId)) {
      if (p_callbacks.m_isCancelled && p_callbacks.m_isCancelled()) {
        return false;
      }
      if (timer.elapsed() > c_saveQueueDrainTimeoutMs) {
        *p_outError = tr("An open note is still being saved. Try again in a moment.");
        return false;
      }
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    // 2. Pull + gate + write. The service does not pump, and it re-checks the
    //    queue under the gate, so a save enqueued by one of its own callbacks
    //    cannot land after our write.
    //
    //    Any failure fails the whole share. A bundle carrying stale note
    //    content would be worse than no bundle at all.
    QString saveError;
    if (!buffers->saveForSnapshot(bufferId, c_gateTimeoutMs, &saveError)) {
      *p_outError = saveError.isEmpty()
                        ? tr("Could not save an open note before sharing the folder.")
                        : saveError;
      return false;
    }

    // Record what we made durable so the final precondition can detect a later
    // in-memory edit.
    m_barrierRevisions.insert(bufferId, buffers->currentRevision(bufferId));
  }

  return true;
}
