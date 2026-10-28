#include "viewareacontroller.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>
#include <QtConcurrent>
#include <QtGlobal>

#include <controllers/workspacewrapper.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/fileopensettings.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/iviewwindowcontent.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <gui/services/viewwindowfactory.h>
#include <utils/processutils.h>
#include <widgets/dashboard/dashboardcontent.h>
#include <widgets/settingswidget.h>
#include <widgets/viewwindow2.h>

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
VxCoreError runEncryptionWorker(const std::function<VxCoreError()> &p_work) {
  QEventLoop loop;
  QFutureWatcher<VxCoreError> watcher;
  QObject::connect(&watcher, &QFutureWatcher<VxCoreError>::finished, &loop, &QEventLoop::quit);
  watcher.setFuture(QtConcurrent::run(p_work));
  if (!watcher.isFinished()) {
    loop.exec();
  }
  watcher.waitForFinished();
  return watcher.result();
}

void erasePassword(QByteArray &p_password) {
  volatile char *bytes = p_password.data();
  for (int index = 0; index < p_password.size(); ++index) {
    bytes[index] = 0;
  }
  p_password.clear();
}

} // namespace

struct NoteEncryptionConversion::State {
  bool encrypt = false;
  bool hasBodyOverride = false;
  QString editorType;
  QString fileId;
  QString encoding = QStringLiteral("UTF-8");
  QByteArray capturedBody;
  QStringList bufferIds;
  QHash<QString, quint64> revisions;
  QVector<ID> windowIds;
  QString saveError;
  CommentService::FlushCheckpoint comments;
  SyncWorkQueueManager::MaintenanceLease maintenance;
  bool active = false;
  bool blocked = false;
  QByteArray sourceSha256;
};

NoteEncryptionConversion::NoteEncryptionConversion() : m_state(new State) {}
NoteEncryptionConversion::~NoteEncryptionConversion() {
  // Qt/editor allocations cannot be securely erased; wipe our captured bytes.
  m_state->capturedBody.fill('\0');
}

PreparedNotebookEncryption ViewAreaController::prepareNoteEncryption(
    const QString &p_notebookId, const QString &p_sourceNotebookId, QByteArray &p_password) {
  const auto passwordGuard = qScopeGuard([&]() { erasePassword(p_password); });
  PreparedNotebookEncryption prepared;
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (notebooks) {
    runEncryptionWorker([&]() {
      prepared = notebooks->prepareNotebookEncryption(p_notebookId, p_sourceNotebookId, p_password);
      return prepared.m_error;
    });
  }
  return prepared;
}

VxCoreError ViewAreaController::commitNoteEncryption(const QString &p_notebookId,
                                                     PreparedNotebookEncryption &p_setup) {
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *queues = m_services.get<SyncWorkQueueManager>();
  auto *gate = m_services.get<NotebookIoGate>();
  if (!notebooks || !queues || !gate)
    return VXCORE_ERR_NOT_INITIALIZED;
  auto maintenance = queues->tryAcquireMaintenance({p_notebookId});
  if (!maintenance)
    return VXCORE_ERR_SYNC_IN_PROGRESS;
  return runEncryptionWorker([&]() {
    NotebookIoGate::ScopedTryLock lock(*gate, p_notebookId, 5000);
    if (!lock.isLocked())
      return VXCORE_ERR_SYNC_IN_PROGRESS;
    return notebooks->commitNotebookEncryption(p_setup);
  });
}

VxCoreError ViewAreaController::reconcileNoteEncryption(const QString &p_notebookId,
                                                        bool p_confirmUninitialized) {
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *queues = m_services.get<SyncWorkQueueManager>();
  auto *gate = m_services.get<NotebookIoGate>();
  if (!notebooks || !queues || !gate) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  auto maintenance = queues->tryAcquireMaintenance({p_notebookId});
  if (!maintenance) {
    return VXCORE_ERR_SYNC_IN_PROGRESS;
  }
  // The worker releases its gate before the UI-thread maintenance lease ends.
  return runEncryptionWorker([&]() {
    NotebookIoGate::ScopedTryLock lock(*gate, p_notebookId, 5000);
    if (!lock.isLocked()) {
      return VXCORE_ERR_SYNC_IN_PROGRESS;
    }
    return notebooks->reconcileNotebookEncryption(p_notebookId, p_confirmUninitialized);
  });
}

VxCoreError ViewAreaController::unlockNoteEncryption(const QString &p_notebookId,
                                                     const QString &p_filePath,
                                                     QByteArray &p_password) {
  const auto passwordGuard = qScopeGuard([&]() { erasePassword(p_password); });
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!notebooks)
    return VXCORE_ERR_NOT_INITIALIZED;
  if (p_filePath.isEmpty())
    return VXCORE_ERR_INVALID_PARAM;
  VxCoreError error = VXCORE_OK;
  const auto status = notebooks->encryptionStatus(p_notebookId, p_filePath, &error);
  if (error != VXCORE_OK)
    return error;
  if (!status.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool()) {
    return VXCORE_ERR_ENCRYPTION_FORMAT;
  }
  // Exact selected-note source; password hashing never holds the IO gate.
  return runEncryptionWorker(
      [&]() { return notebooks->unlockNoteEncryption(p_notebookId, p_filePath, p_password); });
}

VxCoreError ViewAreaController::unlockNotebookEncryption(const QString &p_notebookId,
                                                         QByteArray &p_password) {
  const auto passwordGuard = qScopeGuard([&]() { erasePassword(p_password); });
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!notebooks)
    return VXCORE_ERR_NOT_INITIALIZED;
  return runEncryptionWorker(
      [&]() { return notebooks->unlockNotebookEncryption(p_notebookId, p_password); });
}

std::shared_ptr<NoteEncryptionConversion>
ViewAreaController::prepareNoteConversion(const NodeIdentifier &p_nodeId, bool p_encrypt) {
  auto conversion = std::make_shared<NoteEncryptionConversion>();
  conversion->m_nodeId = p_nodeId;
  auto &state = *conversion->m_state;
  state.encrypt = p_encrypt;
  const auto logPreparation = qScopeGuard([&state, result = conversion.get()]() {
    auto log = result->m_error == VXCORE_OK ? qInfo() : qWarning();
    log << "NoteConversion phase=prepare event=finish" << vxcore::kJsonKeyNotebookId
        << result->m_nodeId.notebookId << "fileId" << state.fileId << "encrypt" << state.encrypt
        << "code" << result->m_error << "buffers" << state.bufferIds.size() << "views"
        << state.windowIds.size();
  });
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *buffers = m_services.get<BufferService>();
  auto *comments = m_services.get<CommentService>();
  if (!m_view || !notebooks || !buffers || !comments || !m_services.get<SyncWorkQueueManager>() ||
      !m_services.get<NotebookIoGate>()) {
    conversion->m_errorMessage = tr("The note encryption services are unavailable.");
    return conversion;
  }
  if (!p_nodeId.isValid() || p_nodeId.isRoot() ||
      !buffers->isNotebookBundled(p_nodeId.notebookId) ||
      notebooks->isNotebookReadOnly(p_nodeId.notebookId) || isNoteConversionBlocked()) {
    conversion->m_error = VXCORE_ERR_READ_ONLY;
    conversion->m_errorMessage = tr("Note conversion requires a writable managed note.");
    return conversion;
  }
  for (const auto &pending : m_noteConversions) {
    if (pending->m_nodeId == p_nodeId) {
      conversion->m_errorMessage = tr("This note is already being converted.");
      return conversion;
    }
  }
  VxCoreError error = VXCORE_OK;
  const auto nodeConfig =
      notebooks->getFolderConfig(p_nodeId.notebookId, p_nodeId.relativePath, &error);
  const auto metadata = nodeConfig.value(QLatin1String(vxcore::kJsonKeyMetadata)).toObject();
  state.fileId = nodeConfig.value(QLatin1String(vxcore::kJsonKeyId)).toString();
  const bool encrypted = metadata.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool();
  const bool protectedSuffix =
      p_nodeId.relativePath.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive);
  if (error != VXCORE_OK || state.fileId.isEmpty() ||
      nodeConfig.value(QLatin1String(vxcore::kJsonKeyType)).toString() == QLatin1String("folder") ||
      encrypted == p_encrypt || protectedSuffix == p_encrypt) {
    conversion->m_error = error == VXCORE_OK ? VXCORE_ERR_INVALID_STATE : error;
    conversion->m_errorMessage = tr("The selected note's encryption state changed.");
    return conversion;
  }
  if (!p_encrypt) {
    state.editorType = metadata.value(QLatin1String(vxcore::kJsonKeyEditorType)).toString();
    if (state.editorType != QLatin1String("markdown") &&
        state.editorType != QLatin1String("text") && state.editorType != QLatin1String("mindmap")) {
      conversion->m_error = VXCORE_ERR_ENCRYPTION_FORMAT;
      conversion->m_errorMessage = QString::fromUtf8(vxcore_error_message(conversion->m_error));
      return conversion;
    }
  }
  // Consent must run while source editors and their previews are still editable.
  // Modal callbacks may close tabs, so only the recollected live IDs may freeze.
  if (!m_view->closePreviewsForSources(m_view->findWindowIdsByNode(p_nodeId, false))) {
    conversion->m_errorMessage = tr("Closing a link preview was cancelled.");
    return conversion;
  }
  if (buffers->isProtectedLocking() || isNoteConversionBlocked()) {
    conversion->m_errorMessage = tr("A protected operation is in progress. Retry conversion.");
    return conversion;
  }
  for (const auto &pending : m_noteConversions) {
    if (pending->m_nodeId == p_nodeId) {
      conversion->m_errorMessage = tr("This note is already being converted.");
      return conversion;
    }
  }
  state.windowIds = m_view->findWindowIdsByNode(p_nodeId, false);
  if (!m_view->setNoteConversionFrozen(state.windowIds, true)) {
    conversion->m_errorMessage = tr("The note's views changed. Retry conversion.");
    return conversion;
  }
  if (m_noteConversions.isEmpty()) {
    m_fileCheckInProgress = true;
    connect(buffers->asQObject(), SIGNAL(saveError(QString, QString)), this,
            SLOT(onNoteConversionSaveError(QString, QString)));
  }
  state.active = true;
  m_noteConversions.append(conversion);
  const auto fail = [&](VxCoreError p_error, const QString &p_message) {
    conversion->m_error = p_error;
    conversion->m_errorMessage = p_message;
    cancelNoteConversion(conversion);
    return conversion;
  };
  for (const auto &value : buffers->listBuffers()) {
    const auto info = value.toObject();
    if (info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() == p_nodeId.notebookId &&
        info.value(QStringLiteral("filePath")).toString() == p_nodeId.relativePath) {
      const QString id = info.value(QLatin1String(vxcore::kJsonKeyId)).toString();
      state.bufferIds.append(id);
      state.revisions.insert(id, buffers->currentRevision(id));
    }
  }
  // The core normally shares one handle across split views. Refuse ambiguous
  // competing writers instead of silently choosing one document.
  if (state.bufferIds.size() > 1) {
    return fail(VXCORE_ERR_INVALID_STATE, tr("The note has conflicting open buffers."));
  }
  QElapsedTimer timer;
  timer.start();
  for (;;) {
    bool busy = false;
    for (const auto &id : state.bufferIds) {
      busy = busy || buffers->isSaveQueueBusy(id) ||
             (!p_encrypt && !buffers->protectedBufferOperationsIdle(id));
    }
    if (!busy) {
      break;
    }
    if (timer.elapsed() >= 10000) {
      return fail(VXCORE_ERR_SYNC_IN_PROGRESS, tr("The note is still being saved. Retry later."));
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::sendPostedEvents(buffers->asQObject(), QEvent::MetaCall);
  if (!state.saveError.isEmpty()) {
    return fail(VXCORE_ERR_IO, state.saveError);
  }
  if (!state.bufferIds.isEmpty()) {
    const QString id = state.bufferIds.first();
    const auto handle = buffers->getBufferHandle(id);
    if (!handle.isValid() || handle.isEncrypted() == p_encrypt || handle.isReadOnly()) {
      return fail(VXCORE_ERR_INVALID_STATE, tr("The selected note's encryption state changed."));
    }
    QByteArray *capture = &state.capturedBody;
    if (!p_encrypt) {
      if (handle.editorType().isEmpty()) {
        capture = nullptr;
      } else {
        if (handle.editorType() != state.editorType || !buffers->checkExternalChanges(handle) ||
            handle.getState() == BufferState::FileChanged ||
            handle.getState() == BufferState::FileMissing) {
          return fail(VXCORE_ERR_FILE_CHANGED_OUTSIDE,
                      tr("The editor changed while preparing conversion."));
        }
        state.hasBodyOverride = true;
      }
    }
    if (buffers->currentRevision(id) != state.revisions.value(id) ||
        !buffers->beginNoteConversion(id, capture)) {
      return fail(VXCORE_ERR_INVALID_STATE, tr("The editor changed while preparing conversion."));
    }
    state.encoding = buffers->bufferEncoding(id);
  }
  const auto flushed =
      comments->flushAndWaitForIdle(p_nodeId.notebookId, p_nodeId.relativePath, false, 10000);
  if (flushed.m_status != CommentService::FlushResult::Status::Succeeded) {
    return fail(VXCORE_ERR_IO, flushed.m_error.isEmpty()
                                   ? tr("Pending comments could not be saved.")
                                   : flushed.m_error);
  }
  state.comments = flushed.m_checkpoint;
  const auto sourcePath = notebooks->buildAbsolutePath(p_nodeId.notebookId, p_nodeId.relativePath);
  const auto readError = runEncryptionWorker([&]() {
    NotebookIoGate::ScopedTryLock lock(*m_services.get<NotebookIoGate>(), p_nodeId.notebookId,
                                       5000);
    if (!lock.isLocked())
      return VXCORE_ERR_SYNC_IN_PROGRESS;
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
      return VXCORE_ERR_IO;
    if (!p_encrypt) {
      if (!state.hasBodyOverride && QFileInfo::exists(sourcePath + QLatin1String(".vswp")))
        return VXCORE_ERR_INVALID_STATE;
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!hash.addData(&source) || source.error() != QFileDevice::NoError)
        return VXCORE_ERR_IO;
      state.sourceSha256 = hash.result().toHex();
      return VXCORE_OK;
    }
    QByteArray bytes = source.readAll();
    if (source.error() != QFileDevice::NoError) {
      bytes.fill('\0');
      return VXCORE_ERR_IO;
    }
    state.sourceSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    if (state.bufferIds.isEmpty())
      state.capturedBody.swap(bytes);
    bytes.fill('\0');
    return VXCORE_OK;
  });
  if (readError != VXCORE_OK) {
    return fail(readError, !p_encrypt && readError == VXCORE_ERR_INVALID_STATE
                               ? tr("Open this note and recover or discard its pending backup "
                                    "before decrypting it.")
                               : tr("The note could not be read for conversion."));
  }
  conversion->m_error = VXCORE_OK;
  return conversion;
}

void ViewAreaController::onNoteConversionSaveError(const QString &p_bufferId,
                                                   const QString &p_error) {
  for (const auto &conversion : m_noteConversions) {
    if (conversion->m_state->bufferIds.contains(p_bufferId)) {
      conversion->m_state->saveError = p_error;
    }
  }
}

bool ViewAreaController::isNoteConversionBlocked() const {
  for (const auto &conversion : m_noteConversions) {
    if (conversion->m_state->blocked) {
      return true;
    }
  }
  return false;
}

void ViewAreaController::cancelNoteConversion(
    const std::shared_ptr<NoteEncryptionConversion> &p_conversion) {
  if (!p_conversion || !p_conversion->m_state->active || p_conversion->m_state->blocked) {
    return;
  }
  auto &state = *p_conversion->m_state;
  qInfo() << "NoteConversion phase=cancel event=start" << vxcore::kJsonKeyNotebookId
          << p_conversion->m_nodeId.notebookId << "fileId" << state.fileId << "encrypt"
          << state.encrypt << "code" << p_conversion->m_error;
  auto *buffers = m_services.get<BufferService>();
  for (const auto &id : state.bufferIds) {
    buffers->endNoteConversion(id, false);
  }
  if (m_view) {
    m_view->setNoteConversionFrozen(state.windowIds, false);
  }
  state.active = false;
  state.maintenance.release();
  m_noteConversions.removeAll(p_conversion);
  if (m_noteConversions.isEmpty()) {
    disconnect(buffers->asQObject(), SIGNAL(saveError(QString, QString)), this,
               SLOT(onNoteConversionSaveError(QString, QString)));
    m_fileCheckInProgress = false;
  }
}

VxCoreError ViewAreaController::applyNoteConversion(
    const std::shared_ptr<NoteEncryptionConversion> &p_conversion,
    PreparedNotebookEncryption *p_setup) {
  if (!p_conversion || p_conversion->m_error != VXCORE_OK || !p_conversion->m_state->active ||
      p_conversion->m_state->blocked) {
    qWarning() << "NoteConversion phase=validate event=reject reason=inactive-conversion"
               << "code" << VXCORE_ERR_INVALID_STATE;
    return VXCORE_ERR_INVALID_STATE;
  }
  auto &state = *p_conversion->m_state;
  const NodeIdentifier nodeId = p_conversion->m_nodeId;
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *buffers = m_services.get<BufferService>();
  auto *comments = m_services.get<CommentService>();
  auto *queues = m_services.get<SyncWorkQueueManager>();
  auto *gate = m_services.get<NotebookIoGate>();
  auto *workspaces = m_services.get<WorkspaceCoreService>();
  const char *phase = "validate";
  qInfo() << "NoteConversion phase=apply event=start" << vxcore::kJsonKeyNotebookId
          << nodeId.notebookId << "fileId" << state.fileId << "encrypt" << state.encrypt;
  const auto fail = [&](VxCoreError p_error, const QString &p_message, bool p_blocked) {
    qWarning() << "NoteConversion event=fail" << "phase" << phase << vxcore::kJsonKeyNotebookId
               << nodeId.notebookId << "fileId" << state.fileId << "encrypt" << state.encrypt
               << "code" << p_error << "recoveryRequired" << p_blocked;
    p_conversion->m_error = p_error;
    p_conversion->m_errorMessage = p_message;
    if (p_blocked) {
      p_conversion->m_errorMessage +=
          QLatin1Char('\n') +
          tr("Note conversion needs recovery. Restart VNote before editing or syncing.");
    }
    state.blocked = p_blocked;
    if (!p_blocked) {
      cancelNoteConversion(p_conversion);
    }
    return p_error;
  };
  if (!state.encrypt && p_setup) {
    return fail(VXCORE_ERR_INVALID_PARAM,
                QString::fromUtf8(vxcore_error_message(VXCORE_ERR_INVALID_PARAM)), false);
  }
  if (!workspaces) {
    return fail(VXCORE_ERR_INVALID_STATE, tr("The workspace service is unavailable."), false);
  }
  phase = "maintenance";
  state.maintenance = queues->tryAcquireMaintenance({nodeId.notebookId});
  if (!state.maintenance) {
    return fail(VXCORE_ERR_SYNC_IN_PROGRESS,
                tr("The notebook is busy syncing. Retry after sync finishes."), false);
  }
  const auto rejectCheckpoint = [&](const char *p_reason, const QString &p_bufferId = QString()) {
    qWarning() << "NoteConversion phase=checkpoint event=reject" << "reason" << p_reason
               << vxcore::kJsonKeyNotebookId << nodeId.notebookId << "fileId" << state.fileId
               << "bufferId" << p_bufferId << "encrypt" << state.encrypt;
    return false;
  };
  const auto checkpointCurrent = [&]() {
    VxCoreError configError = VXCORE_OK;
    const auto nodeConfig =
        notebooks->getFolderConfig(nodeId.notebookId, nodeId.relativePath, &configError);
    const auto metadata = nodeConfig.value(QLatin1String(vxcore::kJsonKeyMetadata)).toObject();
    if (configError != VXCORE_OK) {
      qWarning() << "NoteConversion phase=checkpoint" << "configCode" << configError;
      return rejectCheckpoint("node-config");
    }
    if (nodeConfig.value(QLatin1String(vxcore::kJsonKeyId)).toString() != state.fileId) {
      return rejectCheckpoint("file-identity");
    }
    if (nodeConfig.value(QLatin1String(vxcore::kJsonKeyType)).toString() ==
        QLatin1String("folder")) {
      return rejectCheckpoint("node-type");
    }
    if (metadata.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() == state.encrypt ||
        nodeId.relativePath.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive) == state.encrypt) {
      return rejectCheckpoint("encryption-state");
    }
    if (!state.encrypt &&
        metadata.value(QLatin1String(vxcore::kJsonKeyEditorType)).toString() != state.editorType) {
      return rejectCheckpoint("editor-type");
    }
    if (notebooks->getNodePathById(nodeId.notebookId, state.fileId) != nodeId.relativePath) {
      return rejectCheckpoint("node-path");
    }
    if (notebooks->isNotebookReadOnly(nodeId.notebookId)) {
      return rejectCheckpoint("notebook-read-only");
    }
    if (m_view->findWindowIdsByNode(nodeId, false) != state.windowIds) {
      return rejectCheckpoint("view-participants");
    }
    if (!comments->isFlushCheckpointCurrent(state.comments)) {
      return rejectCheckpoint("comments");
    }
    QStringList currentIds;
    for (const auto &value : buffers->listBuffers()) {
      const auto info = value.toObject();
      if (info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() == nodeId.notebookId &&
          info.value(QStringLiteral("filePath")).toString() == nodeId.relativePath) {
        currentIds.append(info.value(QLatin1String(vxcore::kJsonKeyId)).toString());
      }
    }
    if (currentIds != state.bufferIds) {
      return rejectCheckpoint("buffer-participants");
    }
    for (const auto &id : state.bufferIds) {
      const auto handle = buffers->getBufferHandle(id);
      if (!handle.isValid()) {
        return rejectCheckpoint("buffer-missing", id);
      }
      if (handle.isEncrypted() == state.encrypt) {
        return rejectCheckpoint("buffer-encryption-state", id);
      }
      if (!buffers->isNoteConversionWritable(id)) {
        return rejectCheckpoint("buffer-reservation-or-read-only", id);
      }
      const auto revision = buffers->currentRevision(id);
      if (revision != state.revisions.value(id)) {
        qWarning() << "NoteConversion phase=checkpoint" << "bufferId" << id << "expectedRevision"
                   << state.revisions.value(id) << "revision" << revision;
        return rejectCheckpoint("buffer-revision", id);
      }
      if (buffers->isSaveQueueBusy(id)) {
        return rejectCheckpoint("save-queue", id);
      }
      if (!state.encrypt && !buffers->protectedBufferOperationsIdle(id)) {
        return rejectCheckpoint("protected-operations", id);
      }
      if (!state.encrypt && state.hasBodyOverride && handle.editorType() != state.editorType) {
        return rejectCheckpoint("buffer-editor-type", id);
      }
    }
    return true;
  };
  phase = "io-gate";
  const VxCoreError error = runEncryptionWorker([&]() {
    NotebookIoGate::ScopedTryLock lock(*gate, nodeId.notebookId, 5000);
    if (!lock.isLocked()) {
      return VXCORE_ERR_SYNC_IN_PROGRESS;
    }
    // Read the GUI-owned writer/revision/participant state on its own thread.
    // No dialogs, nested event processing, or KDF occurs while this gate is held.
    phase = "checkpoint";
    bool current = false;
    if (!QMetaObject::invokeMethod(
            this, [&]() { current = checkpointCurrent(); }, Qt::BlockingQueuedConnection)) {
      qWarning() << "NoteConversion phase=checkpoint event=reject reason=dispatch-failed";
    }
    if (!current) {
      return VXCORE_ERR_INVALID_STATE;
    }
    if (!state.encrypt) {
      phase = "unprotect";
      return notebooks->unprotectNote(nodeId, state.hasBodyOverride ? &state.capturedBody : nullptr,
                                      state.sourceSha256, &p_conversion->m_targetPath);
    }
    if (p_setup && p_setup->isValid()) {
      phase = "key-setup";
      const VxCoreError setupError = notebooks->commitNotebookEncryption(*p_setup);
      if (setupError != VXCORE_OK) {
        return setupError;
      }
    }
    phase = "protect";
    return notebooks->protectNote(nodeId, state.capturedBody, state.sourceSha256,
                                  &p_conversion->m_targetPath);
  });
  if (error != VXCORE_OK) {
    QString message = QString::fromUtf8(vxcore_error_message(error));
    if (!state.encrypt && error == VXCORE_ERR_ALREADY_EXISTS) {
      message = tr("The destination note or its backup already exists. "
                   "Rename it before decrypting this note.");
    } else if (!state.encrypt && error == VXCORE_ERR_UNSUPPORTED) {
      message = tr("The restored filename does not match this note's editor type. Rename the "
                   "encrypted note or restore the file type configuration before decrypting it.");
    } else if (!state.encrypt && !state.hasBodyOverride && error == VXCORE_ERR_INVALID_STATE &&
               QFileInfo::exists(
                   notebooks->buildAbsolutePath(nodeId.notebookId, nodeId.relativePath) +
                   QLatin1String(".vswp"))) {
      message =
          tr("Open this note and recover or discard its pending backup before decrypting it.");
    } else if (error == VXCORE_ERR_INVALID_STATE) {
      message = tr("The note changed after confirmation. Nothing was converted; retry.");
    }
    return fail(error, message, error == VXCORE_ERR_ENCRYPTION_RECOVERY_REQUIRED);
  }

  // Conversion succeeded only after authenticated publication and cleanup.
  // Closing before storage success would discard the original pending backup.
  phase = "release-buffer";
  const auto workspaceSnapshot = workspaces->listWorkspaces();
  for (const auto &id : state.bufferIds) {
    buffers->endNoteConversion(id, true);
    if (!buffers->closeBuffer(id)) {
      return fail(VXCORE_ERR_INVALID_STATE,
                  tr("The converted note is durable, but its old buffer could not be released."),
                  true);
    }
  }
  phase = "reopen-buffer";
  const QString targetPath = notebooks->getNodePathById(nodeId.notebookId, state.fileId);
  if (targetPath != p_conversion->m_targetPath) {
    return fail(VXCORE_ERR_INVALID_STATE, tr("The converted note's identity changed."), true);
  }
  FileOpenSettings settings;
  settings.m_focus = false;
  Buffer2 reopened;
  {
    const QScopedValueRollback<bool> reopening(m_noteConversionReopen, true);
    reopened = buffers->openBuffer({nodeId.notebookId, targetPath}, settings);
  }
  if (!reopened.isValid() || reopened.isEncrypted() != state.encrypt) {
    return fail(VXCORE_ERR_INVALID_STATE,
                tr("The converted note is durable, but could not be reopened."), true);
  }
  buffers->setBufferEncoding(reopened.id(), state.encoding);
  phase = "restore-workspace";
  for (const auto &value : workspaceSnapshot) {
    const auto object = value.toObject();
    const QString workspaceId = object.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    QStringList order;
    bool affected = false;
    for (const auto &bufferId : object.value(QStringLiteral("bufferIds")).toArray()) {
      const QString oldId = bufferId.toString();
      const bool replace = state.bufferIds.contains(oldId);
      order.append(replace ? reopened.id() : oldId);
      affected = affected || replace;
    }
    if (!affected) {
      continue;
    }
    if (!workspaces->addBuffer(workspaceId, reopened.id())) {
      return fail(VXCORE_ERR_INVALID_STATE,
                  tr("The converted note is durable, but its workspace could not be restored."),
                  true);
    }
    for (const auto &oldId : state.bufferIds) {
      if (oldId != reopened.id()) {
        if (!workspaces->removeBuffer(workspaceId, oldId)) {
          return fail(VXCORE_ERR_INVALID_STATE,
                      tr("The converted note is durable, but its old workspace entry remains."),
                      true);
        }
      }
    }
    if (!workspaces->setBufferOrder(workspaceId, order)) {
      return fail(VXCORE_ERR_INVALID_STATE,
                  tr("The converted note is durable, but its tab order could not be restored."),
                  true);
    }
    if (state.bufferIds.contains(object.value(QStringLiteral("currentBufferId")).toString())) {
      if (!workspaces->setCurrentBuffer(workspaceId, reopened.id())) {
        return fail(VXCORE_ERR_INVALID_STATE,
                    tr("The converted note is durable, but its active tab could not be restored."),
                    true);
      }
    }
  }
  const auto replaceHidden = [&](QObject *p_old, QObject *p_new) {
    for (auto *wrapper : m_workspaces) {
      if (wrapper->isVisible() || !wrapper->viewWindows().contains(p_old)) {
        continue;
      }
      const int currentIndex = wrapper->currentIndex();
      auto windows = wrapper->takeAllViewWindows();
      windows[windows.indexOf(p_old)] = p_new;
      wrapper->receiveViewWindows(windows, currentIndex);
      break;
    }
  };
  phase = "restore-views";
  if (!m_view->recreateNoteViews(state.windowIds, reopened,
                                 state.encrypt ? reopened.editorType() : state.editorType,
                                 replaceHidden)) {
    return fail(VXCORE_ERR_INVALID_STATE,
                tr("The converted note is durable, but its views could not be recreated."), true);
  }
  if (state.windowIds.isEmpty()) {
    settings.m_focus = true;
    settings.m_alwaysNewWindow = true;
    openBuffer(reopened, settings);
    if (m_view->findWindowIdsByNode(reopened.nodeId(), false).isEmpty()) {
      return fail(VXCORE_ERR_INVALID_STATE,
                  tr("The converted note is durable, but its view could not be opened."), true);
    }
  }
  if (state.encrypt && !state.windowIds.isEmpty()) {
    if (!m_protectedWindows)
      m_protectedWindows.reset(new QSet<ID>);
    for (const ID id : state.windowIds)
      m_protectedWindows->insert(id);
  } else if (!state.encrypt) {
    for (const ID id : state.windowIds) {
      if (m_protectedWindows)
        m_protectedWindows->remove(id);
      if (m_lockedWindows)
        m_lockedWindows->remove(id);
    }
    if (m_protectedWindows && m_protectedWindows->isEmpty())
      m_protectedWindows.reset();
    if (m_lockedWindows && m_lockedWindows->isEmpty())
      m_lockedWindows.reset();
  }
  state.active = false;
  state.maintenance.release();
  m_noteConversions.removeAll(p_conversion);
  if (m_noteConversions.isEmpty()) {
    disconnect(buffers->asQObject(), SIGNAL(saveError(QString, QString)), this,
               SLOT(onNoteConversionSaveError(QString, QString)));
    m_fileCheckInProgress = false;
  }
  p_conversion->m_error = VXCORE_OK;
  qInfo() << "NoteConversion phase=apply event=finish" << vxcore::kJsonKeyNotebookId
          << nodeId.notebookId << "fileId" << state.fileId << "encrypt" << state.encrypt << "code"
          << VXCORE_OK;
  emit windowsChanged();
  emit currentViewWindowChanged();
  return VXCORE_OK;
}

bool ViewAreaController::lockAllProtectedNotes(QString *p_error) {
  auto *buffers = m_services.get<BufferService>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *comments = m_services.get<CommentService>();
  const auto fail = [&](const QString &p_message) {
    if (p_error)
      *p_error = p_message;
    return false;
  };
  if (!buffers || !notebooks || !m_view || !m_noteConversions.isEmpty() ||
      buffers->isProtectedLocking()) {
    return fail(tr("A protected operation is in progress. Finish it before locking."));
  }
  const QVector<ID> sources =
      m_protectedWindows ? m_protectedWindows->values().toVector() : QVector<ID>();
  if (!m_view->closePreviewsForSources(sources)) {
    return fail(tr("Closing a link preview was cancelled."));
  }
  // The consent loop may start another conversion or lock. Enter the locking
  // state only after consent and recheck exclusion before freezing any editor.
  if (!m_noteConversions.isEmpty() || !buffers->beginProtectedLocking()) {
    return fail(tr("A protected operation is in progress. Finish it before locking."));
  }
  QVector<ID> windowIds;
  for (const auto &buffer : buffers->protectedBuffers()) {
    for (const ID id : m_view->findWindowIdsByNode(buffer.nodeId(), false)) {
      // Protected registrations outlive admitted closes. Retiring views have
      // left the live view registry and must not enter this new freeze list.
      if (m_protectedWindows && m_protectedWindows->contains(id) && !windowIds.contains(id)) {
        windowIds.append(id);
      }
    }
  }
  const auto cancel = [&](const QString &p_message) {
    m_view->setNoteConversionFrozen(windowIds, false);
    buffers->cancelProtectedLocking();
    return fail(p_message);
  };
  if (!m_view->setNoteConversionFrozen(windowIds, true)) {
    return cancel(tr("Protected views changed while locking. Try again."));
  }
  // Retiring previews are no longer frozen participants. Drain their admitted
  // deleteLater completions before waiting for protected-resource operations.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QElapsedTimer timeout;
  timeout.start();
  while (!buffers->protectedOperationsIdle()) {
    if (timeout.elapsed() >= 30000) {
      return cancel(tr("Protected work did not finish. Notes remain unlocked."));
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    QThread::msleep(1);
  }
  // Recollect after retirement: an earlier handle snapshot must not save a
  // discarded preview whose final owner has just been destroyed.
  for (const auto &buffer : buffers->protectedBuffers()) {
    if (comments) {
      const auto flush = comments->flushAndWaitForIdle(buffer.nodeId().notebookId,
                                                       buffer.nodeId().relativePath, false, 30000);
      if (flush.m_status != CommentService::FlushResult::Status::Succeeded) {
        return cancel(flush.m_error.isEmpty() ? tr("Comments could not be saved.") : flush.m_error);
      }
    }
    if (buffers->isDirty(buffer.id()) || buffer.isModified()) {
      QString error;
      if (!buffers->saveForSnapshot(buffer.id(), 1000, &error)) {
        return cancel(error);
      }
    }
  }
  // No hook may refuse a later window after earlier documents were destroyed.
  auto *workspaces = m_services.get<WorkspaceCoreService>();
  for (const ID id : windowIds) {
    ViewWindowCloseEvent event;
    event.windowId = id;
    event.force = true;
    if (workspaces && workspaces->fireViewWindowBeforeClose(event)) {
      return cancel(tr("Closing a protected note was cancelled."));
    }
  }
  if (!buffers->protectedOperationsIdle()) {
    return cancel(tr("Protected work changed while locking. Try again."));
  }
  const QScopedValueRollback<bool> suppress(m_suppressAutoRemove, true);
  // Mutation is still refused by BufferService; lift the close-time freeze only.
  m_view->setNoteConversionFrozen(windowIds, false);
  for (const ID id : windowIds) {
    if (!m_view->closeViewWindow(id, true)) {
      buffers->cancelProtectedLocking();
      return fail(tr("A protected view could not be closed. Keys remain available."));
    }
  }
  // closeViewWindow uses deleteLater; the core close callback is deferred until
  // QObject destruction, after documents, undo, comments and profiles are gone.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (m_protectedWindows && !m_protectedWindows->isEmpty()) {
    buffers->cancelProtectedLocking();
    return fail(tr("A protected view is still closing. Keys remain available."));
  }
  for (const auto &buffer : buffers->protectedBuffers()) {
    if (!buffers->closeBuffer(buffer.id())) {
      buffers->cancelProtectedLocking();
      return fail(tr("A protected buffer is busy. Keys remain available."));
    }
  }
  const auto error = notebooks->lockAllEncryption();
  buffers->cancelProtectedLocking();
  if (error != VXCORE_OK) {
    return fail(QString::fromUtf8(vxcore_error_message(error)));
  }
  return true;
}

QString ViewAreaController::releaseHiddenViewWindow(QObject *p_window) {
  for (auto *wrapper : m_workspaces) {
    if (wrapper->isVisible())
      continue;
    const int index = wrapper->viewWindows().indexOf(p_window);
    if (index < 0)
      continue;
    int current = wrapper->currentIndex();
    auto windows = wrapper->takeAllViewWindows();
    windows.removeAt(index);
    if (index < current)
      --current;
    wrapper->receiveViewWindows(windows, qBound(0, current, qMax(0, int(windows.size()) - 1)));
    return wrapper->workspaceId();
  }
  return QString();
}

ViewAreaController::ViewAreaController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  // External file change polling timer (2-second interval).
  m_fileCheckTimer = new QTimer(this);
  m_fileCheckTimer->setInterval(2000);
  connect(m_fileCheckTimer, &QTimer::timeout, this, &ViewAreaController::onFileCheckTimerTick);

  // Monitor app focus state for file check scheduling.
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          &ViewAreaController::onAppStateChanged);
}

ViewAreaController::~ViewAreaController() {
  // Drop suspended callbacks before hidden view objects are destroyed. This
  // does not close their core buffers or remove recovery backups.
  if (auto *buffers = m_services.get<BufferService>()) {
    for (const auto &conversion : m_noteConversions) {
      for (const auto &id : conversion->m_state->bufferIds) {
        buffers->endNoteConversion(id, true);
      }
    }
  }
  // Clean up workspace wrappers. Hidden workspaces may still cache ViewWindows
  // (unparented QWidgets). Delete those before destroying the wrapper.
  for (auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it) {
    auto *wrapper = it.value();
    if (!wrapper->isVisible()) {
      auto windows = wrapper->takeAllViewWindows();
      for (auto *obj : windows) {
        auto *widget = qobject_cast<QWidget *>(obj);
        delete widget;
      }
    }
    delete wrapper;
  }
  m_workspaces.clear();
}

void ViewAreaController::setView(ViewAreaView *p_view) { m_view = p_view; }

void ViewAreaController::openBuffer(const Buffer2 &p_buffer, const FileOpenSettings &p_settings) {
  const QPointer<ViewAreaController> guard(this);
  QString fileType;
  if (p_buffer.isEncrypted()) {
    const QString editor = p_buffer.editorType();
    if (editor == QLatin1String("markdown")) {
      fileType = QStringLiteral("Markdown");
    } else if (editor == QLatin1String("text")) {
      fileType = QStringLiteral("Text");
    } else if (editor == QLatin1String("mindmap")) {
      fileType = QStringLiteral("MindMap");
    } else {
      return;
    }
  } else {
    auto *ftSvc = m_services.get<FileTypeCoreService>();
    if (ftSvc) {
      FileType ft = ftSvc->getFileType(p_buffer.nodeId().relativePath);
      fileType = ft.m_typeName;
    }
    if (fileType.isEmpty()) {
      fileType = QStringLiteral("Text");
    }
  }

  auto settings = p_settings;
  if (fileType.compare(QLatin1String("Typst"), Qt::CaseInsensitive) == 0 && !settings.m_forceMode) {
    settings.m_mode = ViewWindowMode::Edit;
  }

  // External program dispatch: check if this file suffix has a configured external program.
  if (!p_buffer.isEncrypted()) {
    const QString suffix = QFileInfo(p_buffer.nodeId().relativePath).suffix().toLower();
    if (!suffix.isEmpty()) {
      auto *configMgr = m_services.get<ConfigMgr2>();
      if (configMgr) {
        const auto *program = configMgr->getSessionConfig().findExternalProgramBySuffix(suffix);
        if (program) {
          const QString absolutePath = p_buffer.resolvedPath();
          if (program->isSystemProgram()) {
            qInfo() << "ViewAreaController::openBuffer: opening with system default app:"
                    << absolutePath;
            QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
            closeUnownedOpenBuffer(p_buffer.id());
            return;
          }
          const QStringList argv = program->fetchCommandArgs(absolutePath);
          if (!argv.isEmpty()) {
            qInfo() << "ViewAreaController::openBuffer: launching external program for"
                    << absolutePath << "command:" << argv;
            ProcessUtils::startDetached(argv.first(), argv.mid(1));
            closeUnownedOpenBuffer(p_buffer.id());
            return;
          }
        }
      }
    }
  }

  ViewWindowOpenEvent event;
  event.fileType = fileType;
  event.bufferId = p_buffer.id();
  event.nodeId = p_buffer.nodeId().relativePath;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  const bool openVetoed = wsSvc && wsSvc->fireViewWindowBeforeOpen(event);
  if (!guard) {
    return;
  }
  if (openVetoed || !p_buffer.isValid() || p_buffer.getBuffer().isEmpty()) {
    closeUnownedOpenBuffer(p_buffer.id());
    return;
  }
  if (!m_view) {
    closeUnownedOpenBuffer(p_buffer.id());
    return;
  }

  // System default fallback: if no built-in editor exists, open with system
  // default. Decided BEFORE creating any workspace so a detached open does not
  // leave an empty DetachedWindow behind for a non-editable file type.
  auto *factory = m_services.get<ViewWindowFactory>();
  if (!factory || !factory->hasCreator(fileType)) {
    if (p_buffer.isEncrypted()) {
      closeUnownedOpenBuffer(p_buffer.id());
      return;
    }
    const QString absolutePath = p_buffer.resolvedPath();
    qDebug() << "ViewAreaController::openBuffer: no built-in editor for type" << fileType
             << "- opening with system default:" << absolutePath;
    QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
    closeUnownedOpenBuffer(p_buffer.id());
    return;
  }

  // An explicit destination names an existing hosted split, never a request to
  // create or reveal a workspace. Resolve after before-open hooks can close it.
  QString targetWs = settings.m_targetWorkspaceId;
  if (!targetWs.isEmpty()) {
    // Explicit destinations take precedence even over stale preview intent.
    settings.m_previewSourceWindowId = 0;
    settings.m_previewSourceGeneration = 0;
    const auto *wrapper = m_workspaces.value(targetWs, nullptr);
    if (!wrapper || !wrapper->isVisible() ||
        (wrapper->isPreview() &&
         (isPreviewWorkspaceClosing(targetWs) || !m_view->isPreviewWorkspaceAvailable(targetWs)))) {
      qWarning() << "ViewAreaController::openBuffer: target workspace is not hosted:" << targetWs;
      closeUnownedOpenBuffer(p_buffer.id());
      return;
    }
  } else if (settings.m_previewSourceWindowId != InvalidViewWindowId) {
    targetWs = ensurePreviewWorkspace(settings.m_previewSourceWindowId,
                                      settings.m_previewSourceGeneration);
    if (!guard) {
      return;
    }
    if (targetWs.isEmpty()) {
      closeUnownedOpenBuffer(p_buffer.id());
      return;
    }
    settings.m_targetWorkspaceId = targetWs;
  } else if (settings.m_detachedView) {
    targetWs = ensureCliDetachedWorkspace();
    if (targetWs.isEmpty()) {
      qWarning() << "ViewAreaController::openBuffer: failed to create detached workspace";
      return;
    }
  } else {
    if (m_currentWorkspaceId.isEmpty()) {
      QString wsId;
      if (wsSvc) {
        wsId = wsSvc->createWorkspace(generateWorkspaceName());
      }
      if (wsId.isEmpty()) {
        qWarning() << "ViewAreaController::openBuffer: failed to create workspace";
        return;
      }
      auto *wrapper = new WorkspaceWrapper(wsId, this);
      wrapper->setVisible(true);
      m_workspaces.insert(wsId, wrapper);
      m_view->addFirstViewSplit(wsId);
    }

    if (m_currentWorkspaceId.isEmpty()) {
      qWarning() << "ViewAreaController: no current workspace";
      return;
    }
    targetWs = m_currentWorkspaceId;
  }
  const auto *targetWrapper = m_workspaces.value(targetWs, nullptr);
  const bool preview = targetWrapper && targetWrapper->isPreview();
  const bool detached = m_detachedWorkspaceIds.contains(targetWs);
  settings.m_detachedView = detached;
  if (preview) {
    ++m_previewAdmissionsInProgress[targetWs];
  }
  const auto previewAdmission = qScopeGuard([guard, preview, targetWs]() {
    if (guard && preview) {
      if (--guard->m_previewAdmissionsInProgress[targetWs] == 0) {
        guard->m_previewAdmissionsInProgress.remove(targetWs);
      }
      guard->removeEmptyPreviewWorkspace(targetWs);
    }
  });
  if (preview && (!isPreviewRequestCurrent(settings, targetWs) || !p_buffer.isValid() ||
                  p_buffer.getBuffer().isEmpty())) {
    closeUnownedOpenBuffer(p_buffer.id());
    return;
  }
  const auto activateExistingWindow = [&](ID p_windowId) {
    if (preview && !isPreviewRequestCurrent(settings, targetWs)) {
      closeUnownedOpenBuffer(p_buffer.id());
      return;
    }
    if (settings.m_focus) {
      if (preview) {
        setActivePreviewWindow(targetWs, p_windowId, p_buffer.id());
      } else {
        clearActivePreviewWindow();
        if (guard && !detached) {
          setCurrentViewSplit(targetWs, false);
        }
      }
      if (!guard || !m_view || (preview && !isPreviewRequestCurrent(settings, targetWs))) {
        return;
      }
      m_view->setCurrentBuffer(targetWs, p_buffer.id(), true);
      if (!guard || !m_view || (preview && !isPreviewRequestCurrent(settings, targetWs))) {
        return;
      }
      if (!preview && m_shouldPropagateToCore && wsSvc) {
        wsSvc->setCurrentBuffer(targetWs, p_buffer.id());
      }
      if (!preview && !detached && m_currentWorkspaceId == targetWs) {
        setCurrentViewWindow(p_windowId, p_buffer.id());
      }
    }
    if (guard && m_view && m_view->findWindowIdByBufferId(targetWs, p_buffer.id()) == p_windowId &&
        (!preview || isPreviewRequestCurrent(settings, targetWs))) {
      m_view->applyFileOpenSettings(p_windowId, settings);
    }
  };

  // Check if the buffer is already open in the target workspace.
  // If so, just activate the existing tab instead of creating a duplicate.
  ID existingWindowId = m_view->findWindowIdByBufferId(targetWs, p_buffer.id());
  if (existingWindowId != InvalidViewWindowId && p_buffer.isEncrypted() && m_lockedWindows &&
      m_lockedWindows->contains(existingWindowId)) {
    const QVector<ID> ids{existingWindowId};
    if (!m_view->closePreviewsForSources(ids) || !guard) {
      return;
    }
    if (!m_view || m_view->findWindowIdByBufferId(targetWs, p_buffer.id()) != existingWindowId ||
        (preview && !isPreviewRequestCurrent(settings, targetWs))) {
      closeUnownedOpenBuffer(p_buffer.id());
      return;
    }
    if (!m_view->setNoteConversionFrozen(ids, true) ||
        !m_view->recreateNoteViews(ids, p_buffer, p_buffer.editorType(), {})) {
      m_view->setNoteConversionFrozen(ids, false);
      return;
    }
    m_lockedWindows->remove(existingWindowId);
  }
  if (existingWindowId != InvalidViewWindowId) {
    qDebug() << "ViewAreaController::openBuffer: buffer already open in workspace,"
             << "activating existing window:" << existingWindowId;
    activateExistingWindow(existingWindowId);
    return;
  }

  const QString incomingId = p_buffer.id();
  if (m_replacementCandidates.contains(targetWs, incomingId)) {
    // A modal/hook reentry must not create a second view of the candidate
    // between workspace admission and the retained window's identity switch.
    return;
  }
  const auto openNewWindow = [&]() {
    if (preview && !isPreviewRequestCurrent(settings, targetWs)) {
      closeUnownedOpenBuffer(incomingId);
      return;
    }
    // A BufferService handle is not an acknowledgement: the view must still
    // be registered after construction, admission, and the after-open hook.
    m_view->openBuffer(p_buffer, fileType, targetWs, settings);
    if (guard &&
        (!m_view || m_view->findWindowIdByBufferId(targetWs, incomingId) == InvalidViewWindowId)) {
      closeUnownedOpenBuffer(incomingId);
    }
  };
  const auto *configMgr = m_services.get<ConfigMgr2>();
  if (settings.m_alwaysNewWindow || !configMgr ||
      configMgr->getCoreConfig().getAlwaysOpenNotesInNewTabs() || detached || preview ||
      !settings.m_focus || !m_shouldPropagateToCore || !wsSvc) {
    openNewWindow();
    return;
  }

  ID oldWindowId = InvalidViewWindowId;
  QString oldBufferId;
  m_view->getCurrentWindowInfoForWorkspace(targetWs, oldWindowId, oldBufferId);
  auto *bufferSvc = m_services.get<BufferService>();
  const auto oldBuffer =
      bufferSvc && !oldBufferId.isEmpty() ? bufferSvc->getBufferHandle(oldBufferId) : Buffer2();
  // Settings and non-lone Home tabs are never replacement candidates. The view
  // alone owns its existing lone-Home retirement after successful creation.
  if (oldWindowId == InvalidViewWindowId || !oldBuffer.isValid() ||
      oldBuffer.nodeId().isVirtual()) {
    openNewWindow();
    return;
  }
  if (m_transitioningWindows.contains(oldWindowId)) {
    closeUnownedOpenBuffer(incomingId);
    return;
  }
  m_transitioningWindows.insert(oldWindowId);
  const auto transitionGuard = qScopeGuard([guard, oldWindowId]() {
    if (guard) {
      guard->m_transitioningWindows.remove(oldWindowId);
    }
  });
  const auto oldWindowIsCurrent = [&]() {
    return guard && m_view && m_view->findWindowIdByBufferId(targetWs, oldBufferId) == oldWindowId;
  };

  const QJsonObject oldWorkspace = wsSvc->getWorkspace(targetWs);
  QStringList oldOrder;
  const auto oldBufferIds = oldWorkspace.value(QStringLiteral("bufferIds")).toArray();
  for (const auto &id : oldBufferIds) {
    oldOrder.append(id.toString());
  }
  const QString oldCurrentBuffer =
      oldWorkspace.value(QStringLiteral("currentBufferId")).toString(oldBufferId);

  if (!m_view->canReuseViewWindow(oldWindowId, fileType)) {
    // Creating a different viewer must succeed before the old tab sees any
    // close hook or Save/Discard/Cancel prompt. Cancellation keeps both tabs.
    openNewWindow();
    if (!guard || m_view->findWindowIdByBufferId(targetWs, incomingId) == InvalidViewWindowId ||
        !oldWindowIsCurrent()) {
      return;
    }
    // closeViewWindow takes over the exact-ID guard before firing its hook.
    m_transitioningWindows.remove(oldWindowId);
    closeViewWindow(oldWindowId, false);
    return;
  }

  ViewWindowCloseEvent closeEvent;
  closeEvent.windowId = oldWindowId;
  closeEvent.bufferId = oldBufferId;
  closeEvent.force = false;
  const bool vetoed = wsSvc->fireViewWindowBeforeClose(closeEvent);
  if (!guard) {
    return;
  }
  if (vetoed || !oldWindowIsCurrent() || !p_buffer.isValid() || p_buffer.getBuffer().isEmpty() ||
      !m_view->canReuseViewWindow(oldWindowId, fileType)) {
    closeUnownedOpenBuffer(incomingId);
    return;
  }
  // A hook may have opened the requested buffer itself. Preserve deduplication
  // without touching the captured outgoing tab in that case.
  existingWindowId = m_view->findWindowIdByBufferId(targetWs, incomingId);
  if (existingWindowId != InvalidViewWindowId) {
    activateExistingWindow(existingWindowId);
    return;
  }

  const bool addedMembership = !wsSvc->getWorkspace(targetWs)
                                    .value(QStringLiteral("bufferIds"))
                                    .toArray()
                                    .contains(incomingId);
  if (addedMembership && !wsSvc->addBuffer(targetWs, incomingId)) {
    closeUnownedOpenBuffer(incomingId);
    return;
  }
  m_replacementCandidates.insert(targetWs, incomingId);
  const auto candidateGuard = qScopeGuard([guard, targetWs, incomingId]() {
    if (guard) {
      guard->m_replacementCandidates.remove(targetWs, incomingId);
    }
  });

  ClosedTabRecord closedTab;
  bool replaced = false;
  {
    // Qt focus and modal callbacks may run while the retained window has its
    // new buffer but vxcore still owns both. Defer controller notifications,
    // not editor mode signals needed by the concrete resource bind.
    const bool wasReplacing = m_replacingBuffer;
    const bool wereSignalsBlocked = blockSignals(true);
    m_replacingBuffer = true;
    const auto publicationGuard = qScopeGuard([guard, wasReplacing, wereSignalsBlocked]() {
      if (guard) {
        guard->m_replacingBuffer = wasReplacing;
        guard->blockSignals(wereSignalsBlocked);
      }
    });
    replaced = m_view->replaceViewWindowBuffer(oldWindowId, p_buffer, settings, &closedTab);
    if (!guard) {
      return;
    }
    if (!replaced) {
      // A nested open can have acquired the candidate while the prompt ran.
      // Remove only this attempt's membership, never another live view's.
      if (addedMembership &&
          m_view->findWindowIdByBufferId(targetWs, incomingId) == InvalidViewWindowId) {
        wsSvc->removeBuffer(targetWs, incomingId);
      }
      if (oldWindowIsCurrent()) {
        wsSvc->setBufferOrder(targetWs, oldOrder);
        wsSvc->setCurrentBuffer(targetWs, oldCurrentBuffer);
        m_view->setCurrentBuffer(targetWs, oldBufferId, false);
        if (!guard) {
          return;
        }
        if (m_currentWorkspaceId == targetWs) {
          m_currentWindowId = oldWindowId;
        }
      }
      closeUnownedOpenBuffer(incomingId);
    } else {
      // Keep the workspace nonempty throughout the switch and leave sibling
      // views' buffers/resources alive. This is not a terminal window close.
      wsSvc->removeBuffer(targetWs, oldBufferId);
      bufferSvc->forgetBufferIfClosed(oldBufferId);
      oldOrder.removeAll(incomingId);
      const auto oldIndex = oldOrder.indexOf(oldBufferId);
      if (oldIndex >= 0) {
        oldOrder[oldIndex] = incomingId;
      } else {
        oldOrder.append(incomingId);
      }
      wsSvc->setBufferOrder(targetWs, oldOrder);
      wsSvc->setCurrentBuffer(targetWs, incomingId);
      if (m_lockedWindows) {
        m_lockedWindows->remove(oldWindowId);
      }
      if (p_buffer.isEncrypted()) {
        if (!m_protectedWindows) {
          m_protectedWindows.reset(new QSet<ID>);
        }
        m_protectedWindows->insert(oldWindowId);
      } else if (m_protectedWindows) {
        m_protectedWindows->remove(oldWindowId);
      }
      if (closedTab.nodeId.isValid()) {
        if (m_closedTabStack.size() >= c_maxClosedTabRecords) {
          m_closedTabStack.removeFirst();
        }
        m_closedTabStack.append(closedTab);
      }
      m_view->setCurrentBuffer(targetWs, incomingId, false);
      if (!guard) {
        return;
      }
      if (m_currentWorkspaceId == targetWs) {
        m_currentWindowId = oldWindowId;
      }
    }
  }
  if (!replaced) {
    emit windowsChanged();
    if (guard) {
      emit currentViewWindowChanged();
    }
    return;
  }

  wsSvc->fireViewWindowAfterClose(closeEvent);
  if (!guard) {
    return;
  }
  wsSvc->fireViewWindowAfterOpen(event);
  if (!guard) {
    return;
  }
  // Hooks may focus another tab/split; the captured ID still identifies the
  // replaced tab. Publish even when that ID did not change.
  if (m_view->findWindowIdByBufferId(targetWs, incomingId) == oldWindowId) {
    setCurrentViewSplit(targetWs, false);
    if (!guard) {
      return;
    }
    m_view->setCurrentBuffer(targetWs, incomingId, true);
    if (!guard) {
      return;
    }
    if (m_currentWorkspaceId == targetWs) {
      m_currentWindowId = oldWindowId;
    }
  }
  emit windowsChanged();
  if (guard) {
    emit currentViewWindowChanged();
  }
}

void ViewAreaController::closeUnownedOpenBuffer(const QString &p_bufferId) {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  auto *bufferSvc = m_services.get<BufferService>();
  if (p_bufferId.isEmpty() || !wsSvc || !bufferSvc) {
    return;
  }
  for (const auto &admission : m_failedPreviewAdmissions) {
    if (admission.bufferId == p_bufferId) {
      return;
    }
  }
  const auto workspaces = wsSvc->listWorkspaces();
  for (const auto &workspace : workspaces) {
    if (workspace.toObject().value(QStringLiteral("bufferIds")).toArray().contains(p_bufferId)) {
      return;
    }
  }
  if (!bufferSvc->forgetBufferIfClosed(p_bufferId) && !bufferSvc->closeBuffer(p_bufferId)) {
    qWarning() << "ViewAreaController: unable to close unowned buffer after failed open:"
               << p_bufferId;
  }
}

bool ViewAreaController::isPreviewRequestCurrent(const FileOpenSettings &p_settings,
                                                 const QString &p_workspaceId) const {
  const auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
  if (!wrapper || !wrapper->isPreview() || !wrapper->isVisible() || !m_view ||
      isPreviewWorkspaceClosing(p_workspaceId) ||
      !m_view->isPreviewWorkspaceAvailable(p_workspaceId)) {
    return false;
  }
  for (const auto &admission : m_failedPreviewAdmissions) {
    if (admission.workspaceId == p_workspaceId) {
      return false;
    }
  }
  return p_settings.m_previewSourceWindowId == InvalidViewWindowId ||
         (m_view->isPreviewSourceCurrent(p_settings.m_previewSourceWindowId,
                                         p_settings.m_previewSourceGeneration) &&
          m_view->previewWorkspaceForWindow(p_settings.m_previewSourceWindowId) == p_workspaceId);
}

QString ViewAreaController::ensurePreviewWorkspace(ID p_sourceId, quint64 p_generation) {
  if (!m_view || m_creatingPreviewSources.contains(p_sourceId) ||
      !m_view->isPreviewSourceCurrent(p_sourceId, p_generation)) {
    return QString();
  }
  const QString existing = m_view->previewWorkspaceForWindow(p_sourceId);
  if (!existing.isEmpty()) {
    FileOpenSettings settings;
    settings.m_previewSourceWindowId = p_sourceId;
    settings.m_previewSourceGeneration = p_generation;
    return isPreviewRequestCurrent(settings, existing) ? existing : QString();
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return QString();
  }

  const QPointer<ViewAreaController> guard(this);
  m_creatingPreviewSources.insert(p_sourceId);
  const auto creationGuard = qScopeGuard([guard, p_sourceId]() {
    if (guard) {
      guard->m_creatingPreviewSources.remove(p_sourceId);
    }
  });
  ViewSplitCreateEvent event;
  event.direction = static_cast<int>(Direction::Right);
  const bool vetoed = wsSvc->fireViewSplitBeforeCreate(event);
  if (!guard || vetoed || !m_view || !m_view->isPreviewSourceCurrent(p_sourceId, p_generation)) {
    return QString();
  }

  const QString workspaceId = wsSvc->createWorkspace(tr("Preview"), true);
  if (workspaceId.isEmpty()) {
    return QString();
  }
  auto *wrapper = new WorkspaceWrapper(workspaceId, this, true);
  wrapper->setVisible(true);
  m_workspaces.insert(workspaceId, wrapper);
  m_previewCloseStates.insert(workspaceId, PreviewCloseState::Open);
  ++m_previewAdmissionsInProgress[workspaceId];
  bool ready = false;
  const auto rollback = qScopeGuard([guard, workspaceId, &ready]() {
    if (guard) {
      if (--guard->m_previewAdmissionsInProgress[workspaceId] == 0) {
        guard->m_previewAdmissionsInProgress.remove(workspaceId);
      }
      if (!ready || guard->isPreviewWorkspaceClosing(workspaceId)) {
        guard->removeEmptyPreviewWorkspace(workspaceId);
      }
    }
  });
  const bool hosted = m_view->hostPreviewWorkspace(p_sourceId, p_generation, workspaceId);
  if (!guard || !hosted) {
    return QString();
  }
  FileOpenSettings settings;
  settings.m_previewSourceWindowId = p_sourceId;
  settings.m_previewSourceGeneration = p_generation;
  if (!isPreviewRequestCurrent(settings, workspaceId)) {
    return QString();
  }
  event.newWorkspaceId = workspaceId;
  wsSvc->fireViewSplitAfterCreate(event);
  if (!guard || !isPreviewRequestCurrent(settings, workspaceId)) {
    return QString();
  }
  ready = true;
  return workspaceId;
}

void ViewAreaController::removeEmptyPreviewWorkspace(const QString &p_workspaceId) {
  const auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wrapper || !wrapper->isPreview() || !wsSvc ||
      m_previewCloseStates.value(p_workspaceId) == PreviewCloseState::Checking ||
      m_previewAdmissionsInProgress.value(p_workspaceId) != 0 ||
      (m_view && !m_view->getViewWindowIdsForWorkspace(p_workspaceId).isEmpty())) {
    return;
  }
  for (const auto &admission : m_failedPreviewAdmissions) {
    if (admission.workspaceId == p_workspaceId) {
      return;
    }
  }
  // A protected tab leaves the widget registry before destruction, but keeps
  // its core membership until onViewWindowClosed. Never retire its host early.
  const auto workspace = wsSvc->getWorkspace(p_workspaceId);
  if (!workspace.value(QStringLiteral("bufferIds")).toArray().isEmpty()) {
    return;
  }
  m_previewCloseStates.insert(p_workspaceId, PreviewCloseState::Accepted);
  if (!workspace.isEmpty() && !wsSvc->deleteWorkspace(p_workspaceId)) {
    qWarning() << "ViewAreaController: failed to remove empty preview workspace:" << p_workspaceId;
    return;
  }
  const QPointer<ViewAreaController> guard(this);
  if (m_activePreviewWorkspaceId == p_workspaceId) {
    clearActivePreviewWindow();
    if (!guard) {
      return;
    }
  }
  delete m_workspaces.take(p_workspaceId);
  if (m_view) {
    // The view clears only this host's source callback and uses deleteLater.
    m_view->removePreviewWorkspaceHost(p_workspaceId);
  }
  if (guard) {
    m_previewCloseStates.remove(p_workspaceId);
  }
}

void ViewAreaController::rejectPreviewWindow(ID p_windowId, const QString &p_bufferId,
                                             const QString &p_workspaceId) {
  if (!m_view || m_view->findWindowIdByBufferId(p_workspaceId, p_bufferId) != p_windowId ||
      m_failedPreviewAdmissions.contains(p_windowId)) {
    return;
  }
  // This is failed admission, not a user tab close. Do not let a close hook
  // veto removal of a view that never acquired valid preview ownership.
  m_failedPreviewAdmissions.insert(p_windowId, {p_workspaceId, p_bufferId});
  if (!m_view->closeViewWindow(p_windowId, true)) {
    qWarning() << "ViewAreaController: failed to retire rejected preview window:" << p_windowId;
  }
}

QString ViewAreaController::ensureCliDetachedWorkspace() {
  if (!m_cliDetachedWorkspaceId.isEmpty()) {
    return m_cliDetachedWorkspaceId;
  }
  if (!m_view) {
    return QString();
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();

  // Model the detached window as a view-split creation so plugins observe it
  // through the same hooks as a manual detach (parity with detachViewWindow).
  ViewSplitCreateEvent createEvent;
  createEvent.direction = static_cast<int>(Direction::Right);
  if (wsSvc && wsSvc->fireViewSplitBeforeCreate(createEvent)) {
    return QString();
  }

  QString newWsId;
  if (wsSvc) {
    newWsId = wsSvc->createWorkspace(generateWorkspaceName());
  }
  if (newWsId.isEmpty()) {
    qWarning() << "ViewAreaController::ensureCliDetachedWorkspace: failed to create workspace";
    return QString();
  }

  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  // The detached workspace hosts a real (visible) ViewSplit2 inside the
  // DetachedWindow; mark the wrapper visible (mirrors detachViewWindow).
  wrapper->setVisible(true);
  m_workspaces.insert(newWsId, wrapper);
  m_detachedWorkspaceIds.insert(newWsId);

  m_view->hostWorkspaceInDetachedWindow(newWsId);

  if (wsSvc) {
    createEvent.newWorkspaceId = newWsId;
    wsSvc->fireViewSplitAfterCreate(createEvent);
  }

  m_cliDetachedWorkspaceId = newWsId;
  // Reset after the current synchronous detached-open batch completes so the
  // next invocation opens into a fresh detached window. Between queued startup
  // batches MainWindow2 also calls resetCliDetachedBatch() synchronously; this
  // singleShot then becomes a harmless no-op (id already cleared).
  QTimer::singleShot(0, this, [this]() { m_cliDetachedWorkspaceId.clear(); });
  return newWsId;
}

void ViewAreaController::resetCliDetachedBatch() { m_cliDetachedWorkspaceId.clear(); }

void ViewAreaController::openWidgetContent(vnotex::IViewWindowContent *p_content,
                                           const QStringList &p_pathSegments,
                                           const QString &p_fragment) {
  if (!p_content) {
    return;
  }

  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "ViewAreaController::openWidgetContent: no BufferService";
    delete p_content;
    return;
  }

  Buffer2 buffer = bufferSvc->openVirtualBuffer(p_content->virtualAddress());
  if (!buffer.isValid()) {
    qWarning() << "ViewAreaController::openWidgetContent: failed to create virtual buffer for"
               << p_content->virtualAddress();
    delete p_content;
    return;
  }

  if (m_view) {
    QStringList workspaceIds = m_view->getVisibleWorkspaceIds();
    for (const auto &wsId : workspaceIds) {
      ID existingWindowId = m_view->findWindowIdByBufferId(wsId, buffer.id());
      if (existingWindowId != InvalidViewWindowId) {
        qDebug() << "ViewAreaController::openWidgetContent: content already open in workspace"
                 << wsId << "- activating existing window:" << existingWindowId;
        m_view->setCurrentViewSplit(wsId, true);
        m_view->setCurrentBuffer(wsId, buffer.id(), true);
        setCurrentViewWindow(existingWindowId, buffer.id());
        m_view->navigateWidgetContent(existingWindowId, p_pathSegments, p_fragment);
        delete p_content;
        return;
      }
    }
  }

  // Also dedup against HIDDEN workspaces: their windows are detached from any
  // ViewSplit2 and held in the WorkspaceWrapper, so findWindowIdByBufferId
  // (which only scans visible splits) cannot see them. Without this, reopening
  // e.g. vx://home while its tab lives in a hidden workspace would build a second
  // content instance for the same virtual buffer (two DashboardBoards that
  // independently persist and can clobber each other).
  if (m_view) {
    for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
      WorkspaceWrapper *wrapper = it.value();
      if (!wrapper || wrapper->isVisible()) {
        continue;
      }
      for (QObject *obj : wrapper->viewWindows()) {
        auto *win = qobject_cast<ViewWindow2 *>(obj);
        if (win && win->getBuffer().id() == buffer.id()) {
          qDebug() << "ViewAreaController::openWidgetContent: content open in hidden workspace"
                   << it.key() << "- surfacing it";
          focusWindow(it.key(), buffer.id());
          const ID existingWindowId = win->getViewWindowId();
          if (existingWindowId != InvalidViewWindowId) {
            m_view->navigateWidgetContent(existingWindowId, p_pathSegments, p_fragment);
          }
          delete p_content;
          return;
        }
      }
    }
  }

  if (m_currentWorkspaceId.isEmpty()) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    QString wsId;
    if (wsSvc) {
      wsId = wsSvc->createWorkspace(generateWorkspaceName());
    }
    if (wsId.isEmpty()) {
      qWarning() << "ViewAreaController::openWidgetContent: failed to create workspace";
      delete p_content;
      return;
    }
    auto *wrapper = new WorkspaceWrapper(wsId, this);
    wrapper->setVisible(true);
    m_workspaces.insert(wsId, wrapper);
    if (!m_view) {
      delete p_content;
      return;
    }
    m_view->addFirstViewSplit(wsId);
  }

  if (m_currentWorkspaceId.isEmpty() || !m_view) {
    qWarning() << "ViewAreaController::openWidgetContent: no current workspace";
    delete p_content;
    return;
  }

  m_view->openWidgetContent(p_content, buffer, m_currentWorkspaceId);
  p_content->navigateTo(p_pathSegments, p_fragment);
}

void ViewAreaController::onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                                            const FileOpenSettings &p_settings,
                                            const QString &p_workspaceId) {
  if (p_windowId == InvalidViewWindowId) {
    return;
  }

  // The view supplies the actual owner: construction or hooks may change the
  // current workspace, but cannot retarget this buffer's registration.
  if (p_buffer.isEncrypted()) {
    if (!m_protectedWindows)
      m_protectedWindows.reset(new QSet<ID>);
    m_protectedWindows->insert(p_windowId);
    if (p_buffer.editorType().isEmpty()) {
      if (!m_lockedWindows)
        m_lockedWindows.reset(new QSet<ID>);
      m_lockedWindows->insert(p_windowId);
    }
  }
  const bool detached = m_detachedWorkspaceIds.contains(p_workspaceId);
  const auto &registerWs = p_workspaceId;

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  const auto *wrapper = m_workspaces.value(registerWs, nullptr);
  if (!p_settings.m_targetWorkspaceId.isEmpty() && (!wrapper || !wrapper->isVisible())) {
    rejectPreviewWindow(p_windowId, p_buffer.id(), registerWs);
    return;
  }
  if ((wrapper && wrapper->isPreview()) ||
      p_settings.m_previewSourceWindowId != InvalidViewWindowId) {
    const QPointer<ViewAreaController> guard(this);
    const QString bufferId = p_buffer.id();
    const auto admissionCurrent = [&]() {
      return m_view && isPreviewRequestCurrent(p_settings, registerWs) && p_buffer.isValid() &&
             !p_buffer.getBuffer().isEmpty() &&
             m_view->findWindowIdByBufferId(registerWs, bufferId) == p_windowId;
    };
    if (!admissionCurrent() || !wsSvc || !wsSvc->addBuffer(registerWs, bufferId)) {
      rejectPreviewWindow(p_windowId, bufferId, registerWs);
      return;
    }
    ViewWindowOpenEvent event;
    event.bufferId = bufferId;
    event.nodeId = p_buffer.nodeId().relativePath;
    wsSvc->fireViewWindowAfterOpen(event);
    if (!guard) {
      return;
    }
    if (!admissionCurrent() || !wsSvc->getWorkspace(registerWs)
                                    .value(QStringLiteral("bufferIds"))
                                    .toArray()
                                    .contains(bufferId)) {
      rejectPreviewWindow(p_windowId, bufferId, registerWs);
      return;
    }
    if (p_settings.m_focus) {
      setActivePreviewWindow(registerWs, p_windowId, bufferId);
    }
    if (!guard) {
      return;
    }
    if (!admissionCurrent()) {
      rejectPreviewWindow(p_windowId, bufferId, registerWs);
      return;
    }
    emit windowsChanged();
    if (!guard) {
      return;
    }
    // setActivePreviewWindow already publishes focused opens; background
    // admission must leave both the ordinary and preview command focus alone.
    if (!admissionCurrent()) {
      rejectPreviewWindow(p_windowId, bufferId, registerWs);
      return;
    }
    return;
  }
  const bool membershipReady =
      !m_shouldPropagateToCore || !wsSvc ||
      (!registerWs.isEmpty() && wsSvc->addBuffer(registerWs, p_buffer.id()));
  if (membershipReady && p_settings.m_focus) {
    const QPointer<ViewAreaController> guard(this);
    clearActivePreviewWindow();
    if (!guard) {
      return;
    }
  }
  // addTab can publish the first current tab before its workspace membership
  // exists. Reconcile the actual selected tab after admission, even if its
  // window ID is unchanged. Background opens must not select the new tab;
  // restore keeps its persisted current buffer until all tabs are rebuilt.
  if (m_shouldPropagateToCore && membershipReady) {
    if (!detached && m_view && registerWs == m_currentWorkspaceId) {
      ID currentId = InvalidViewWindowId;
      QString currentBufferId;
      m_view->getCurrentWindowInfoForWorkspace(registerWs, currentId, currentBufferId);
      if (currentId == p_windowId) {
        setCurrentViewWindow(currentId, currentBufferId);
      }
    } else if (detached && p_settings.m_focus && wsSvc) {
      wsSvc->setCurrentBuffer(registerWs, p_buffer.id());
    }
  }

  if (wsSvc) {
    ViewWindowOpenEvent ha;
    ha.bufferId = p_buffer.id();
    ha.nodeId = p_buffer.nodeId().relativePath;
    wsSvc->fireViewWindowAfterOpen(ha);
  }

  if (membershipReady && p_settings.m_focus && !detached) {
    setCurrentViewSplit(registerWs, true);
  }
  emit windowsChanged();
  emit currentViewWindowChanged();
}

void ViewAreaController::onViewWindowClosed(ID p_windowId, const QString &p_bufferId,
                                            const QString &p_workspaceId,
                                            const ClosedTabRecord &p_closedTab) {
  const QPointer<ViewAreaController> guard(this);
  if (m_protectedWindows)
    m_protectedWindows->remove(p_windowId);
  if (m_lockedWindows)
    m_lockedWindows->remove(p_windowId);
  const bool failedPreviewAdmission = m_failedPreviewAdmissions.remove(p_windowId) > 0;
  const auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
  const bool preview = failedPreviewAdmission || (wrapper && wrapper->isPreview());
  if (wrapper && wrapper->isPreview() && m_view &&
      m_previewCloseStates.value(p_workspaceId) != PreviewCloseState::Checking &&
      m_view->getViewWindowIdsForWorkspace(p_workspaceId).isEmpty()) {
    // Failed admission and protected locking can close directly through the
    // view. Seal an emptied host before after-close hooks can reenter admission.
    m_previewCloseStates.insert(p_workspaceId, PreviewCloseState::Accepted);
  }
  // Preview tabs never enter ordinary reopen history, including normal closes.
  // Session restore/shutdown and virtual buffers likewise have no history.
  if (!preview && m_shouldPropagateToCore && p_closedTab.nodeId.isValid()) {
    if (m_closedTabStack.size() >= c_maxClosedTabRecords) {
      m_closedTabStack.removeFirst();
    }
    m_closedTabStack.append(p_closedTab);
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (m_shouldPropagateToCore || preview) {
    // Workspace membership is set-like. A promoted preview may already own the
    // same buffer while the outgoing protected view finishes deferred retirement.
    const bool hasSurvivor =
        m_view && m_view->findWindowIdByBufferId(p_workspaceId, p_bufferId) != InvalidViewWindowId;
    if (wsSvc && !p_workspaceId.isEmpty() && !p_bufferId.isEmpty() && !hasSurvivor) {
      wsSvc->removeBuffer(p_workspaceId, p_bufferId);
      // NOTE: vxcore auto-closes the buffer when it's removed from all workspaces
      // (see workspace_manager.cpp), which bypasses BufferService::closeBuffer.
      // Tell the service so it can drop its per-buffer state (dirty flags,
      // revisions, encoding override, the resolved read-only fact); it no-ops
      // while the buffer is still open in another workspace.
      if (auto *bufferSvc = m_services.get<BufferService>()) {
        bufferSvc->forgetBufferIfClosed(p_bufferId);
      }
    }
  }
  if (m_currentWindowId == p_windowId) {
    m_currentWindowId = InvalidViewWindowId;
  }
  if (m_activePreviewWindowId == p_windowId) {
    clearActivePreviewWindow();
    if (!guard) {
      return;
    }
  }
  if (wsSvc) {
    ViewWindowCloseEvent ha;
    ha.bufferId = p_bufferId;
    wsSvc->fireViewWindowAfterClose(ha);
    if (!guard) {
      return;
    }
  }
  emit windowsChanged();
  if (!guard) {
    return;
  }
  checkCurrentViewWindowChange(p_workspaceId);
  if (!guard) {
    return;
  }

  if (preview) {
    // Protected completions arrive only after resources retire. Established
    // and failed-admission previews share this path, never main/Home removal.
    removeEmptyPreviewWorkspace(p_workspaceId);
    if (guard && failedPreviewAdmission) {
      closeUnownedOpenBuffer(p_bufferId);
    }
    return;
  }

  // If workspace is now empty and there are other splits, remove it.
  // If single split, try to switch to a hidden workspace instead.
  // Suppressed during bulk close operations (removeWorkspace, closeAll).
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (!m_suppressAutoRemove && m_shouldPropagateToCore && !p_workspaceId.isEmpty() &&
      !m_detachedWorkspaceIds.contains(p_workspaceId)) {
    if (wsSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      if (bufferIds.isEmpty()) {
        if (totalSplitCount > 1) {
          // Multiple splits: remove the empty workspace and its split.
          qDebug() << "ViewAreaController: workspace" << p_workspaceId
                   << "is empty, removing split";
          removeViewSplit(p_workspaceId, false, true);
        } else {
          // Single split: try to switch to a hidden workspace.
          QStringList visibleIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
          QJsonArray allWs = wsSvc->listWorkspaces();
          QString hiddenWsId;
          for (int i = 0; i < allWs.size(); ++i) {
            const QJsonObject ws = allWs[i].toObject();
            if (ws.value(QLatin1String(vxcore::kJsonKeyIsPreview)).toBool()) {
              continue;
            }
            QString wsId = ws.value(QStringLiteral("id")).toString();
            if (!visibleIds.contains(wsId) && wsId != p_workspaceId) {
              hiddenWsId = wsId;
              break;
            }
          }
          if (!hiddenWsId.isEmpty()) {
            qDebug() << "ViewAreaController: workspace" << p_workspaceId
                     << "is empty (last split), switching to hidden workspace:" << hiddenWsId;
            // Update the split identity to point to the hidden workspace.
            // No windows to take/place since the workspace is empty.
            if (m_view) {
              m_view->updateSplitWorkspaceId(p_workspaceId, hiddenWsId);
            }
            // Show the hidden workspace's cached windows (if any).
            auto *targetWrapper = m_workspaces.value(hiddenWsId, nullptr);
            if (targetWrapper && targetWrapper->viewWindowCount() > 0) {
              int idx = targetWrapper->currentIndex();
              QVector<QObject *> wins = targetWrapper->takeAllViewWindows();
              if (m_view) {
                m_view->placeViewWindowsInSplit(hiddenWsId, wins, idx);
              }
              targetWrapper->setVisible(true);
            } else if (targetWrapper) {
              // Fresh hidden workspace — open its buffers from vxcore.
              targetWrapper->setVisible(true);
              auto *bufferSvc = m_services.get<BufferService>();
              QJsonObject wsConf = wsSvc->getWorkspace(hiddenWsId);
              QJsonArray hiddenBufIds = wsConf.value(QStringLiteral("bufferIds")).toArray();
              QString curBuf = wsConf.value(QStringLiteral("currentBufferId")).toString();
              m_currentWorkspaceId = hiddenWsId;
              for (int j = 0; j < hiddenBufIds.size(); ++j) {
                QString bid = hiddenBufIds[j].toString();
                if (!bid.isEmpty() && bufferSvc) {
                  if (!openRestoredBuffer(bufferSvc, hiddenWsId, bid, false) && bid == curBuf) {
                    // No window exists for it; activating it would only warn.
                    curBuf.clear();
                  }
                }
              }
              if (!curBuf.isEmpty() && m_view) {
                m_view->setCurrentBuffer(hiddenWsId, curBuf, true);
              }
            }
            // Delete the empty workspace from vxcore and its wrapper.
            wsSvc->deleteWorkspace(p_workspaceId);
            if (m_workspaces.contains(p_workspaceId)) {
              auto *wrapper = m_workspaces.take(p_workspaceId);
              delete wrapper;
            }
            setCurrentViewSplit(hiddenWsId, true);
          }
          // Else: no hidden workspaces, single split with empty workspace.
          // This is the "home screen" state — nothing more to do.
        }
      }
    }
  }
}

bool ViewAreaController::closeViewWindow(ID p_windowId, bool p_force) {
  if (p_windowId == InvalidViewWindowId || !m_view || m_transitioningWindows.contains(p_windowId)) {
    return false;
  }
  const QPointer<ViewAreaController> guard(this);
  m_transitioningWindows.insert(p_windowId);
  const auto transitionGuard = qScopeGuard([guard, p_windowId]() {
    if (guard) {
      guard->m_transitioningWindows.remove(p_windowId);
    }
  });
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  QString workspaceId;
  QString bufferId;
  if (wsSvc) {
    const auto workspaces = wsSvc->listWorkspaces();
    for (const auto &workspace : workspaces) {
      const auto config = workspace.toObject();
      const auto wsId = config.value(QStringLiteral("id")).toString();
      const auto ids = config.value(QStringLiteral("bufferIds")).toArray();
      for (const auto &id : ids) {
        if (m_view->findWindowIdByBufferId(wsId, id.toString()) == p_windowId) {
          workspaceId = wsId;
          bufferId = id.toString();
          break;
        }
      }
      if (!workspaceId.isEmpty()) {
        break;
      }
    }
  }
  const bool ownsPreviewCheck = isPreviewWorkspace(workspaceId) &&
                                m_previewCloseStates.value(workspaceId) == PreviewCloseState::Open;
  const bool wasActivePreview = m_activePreviewWorkspaceId == workspaceId;
  if (ownsPreviewCheck) {
    m_previewCloseStates.insert(workspaceId, PreviewCloseState::Checking);
  }
  // Individual tab closes also exclude admission during modal close hooks.
  // An enclosing closePreviewWorkspace owns its Checking state until all tabs
  // accept, so synchronous last-tab completion cannot dismantle that loop.
  const auto previewCheck = qScopeGuard([guard, workspaceId, ownsPreviewCheck, wasActivePreview]() {
    if (!guard || !ownsPreviewCheck || !guard->m_view) {
      return;
    }
    const bool empty = guard->m_view->getViewWindowIdsForWorkspace(workspaceId).isEmpty();
    guard->m_previewCloseStates.insert(workspaceId, empty ? PreviewCloseState::Accepted
                                                          : PreviewCloseState::Open);
    if (empty && guard->m_activePreviewWorkspaceId == workspaceId) {
      guard->clearActivePreviewWindow();
    } else if (!empty && wasActivePreview) {
      ID currentId = InvalidViewWindowId;
      QString bufferId;
      guard->m_view->getCurrentWindowInfoForWorkspace(workspaceId, currentId, bufferId);
      guard->setActivePreviewWindow(workspaceId, currentId, bufferId);
    }
    if (guard) {
      guard->removeEmptyPreviewWorkspace(workspaceId);
    }
  });
  ViewWindowCloseEvent event;
  event.windowId = p_windowId;
  event.bufferId = bufferId;
  event.force = p_force;
  if (wsSvc && wsSvc->fireViewWindowBeforeClose(event)) {
    return false;
  }
  if (!guard || !m_view ||
      (!workspaceId.isEmpty() &&
       m_view->findWindowIdByBufferId(workspaceId, bufferId) != p_windowId)) {
    return false;
  }
  return m_view->closeViewWindow(p_windowId, p_force);
}

bool ViewAreaController::isPreviewWorkspaceClosing(const QString &p_workspaceId) const {
  return m_previewCloseStates.value(p_workspaceId, PreviewCloseState::Open) !=
         PreviewCloseState::Open;
}

void ViewAreaController::setPreviewWorkspaceVisible(const QString &p_workspaceId, bool p_visible) {
  auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
  if (!wrapper || !wrapper->isPreview()) {
    return;
  }
  wrapper->setVisible(p_visible);
  // Effective getters already fall back after a hide. Inspect stored activation
  // so providers still receive the notification needed to leave the preview.
  if (!p_visible && m_activePreviewWorkspaceId == p_workspaceId) {
    clearActivePreviewWindow();
  }
}

bool ViewAreaController::closePreviewWorkspace(const QString &p_workspaceId) {
  const auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
  if (!wrapper) {
    return !p_workspaceId.isEmpty(); // A previous completion already removed it.
  }
  if (!wrapper->isPreview() || !m_view) {
    return false;
  }
  const auto state = m_previewCloseStates.value(p_workspaceId, PreviewCloseState::Open);
  if (state == PreviewCloseState::Checking) {
    return false; // Never authorize source destruction inside a consent loop.
  }
  if (state == PreviewCloseState::Accepted) {
    removeEmptyPreviewWorkspace(p_workspaceId);
    return true; // Pending protected destruction is not a second consent gate.
  }
  const QPointer<ViewAreaController> guard(this);
  const bool wasActive = m_activePreviewWorkspaceId == p_workspaceId;
  m_previewCloseStates.insert(p_workspaceId, PreviewCloseState::Checking);
  const auto windowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
  for (const ID id : windowIds) {
    // A close hook may already have closed a later tab. Its admitted retirement
    // still belongs to the core workspace, but must not be closed a second time.
    if (!m_view->getViewWindowIdsForWorkspace(p_workspaceId).contains(id)) {
      continue;
    }
    const bool accepted = closeViewWindow(id, false);
    if (!guard) {
      return false;
    }
    if (!accepted) {
      m_previewCloseStates.insert(p_workspaceId, PreviewCloseState::Open);
      if (wasActive && m_view) {
        ID currentId = InvalidViewWindowId;
        QString bufferId;
        m_view->getCurrentWindowInfoForWorkspace(p_workspaceId, currentId, bufferId);
        setActivePreviewWindow(p_workspaceId, currentId, bufferId);
      }
      if (guard) {
        removeEmptyPreviewWorkspace(p_workspaceId);
      }
      return false;
    }
  }
  m_previewCloseStates.insert(p_workspaceId, PreviewCloseState::Accepted);
  if (m_activePreviewWorkspaceId == p_workspaceId) {
    clearActivePreviewWindow();
  }
  if (guard) {
    removeEmptyPreviewWorkspace(p_workspaceId);
  }
  return true;
}

bool ViewAreaController::openPreviewInNewTabs(const QString &p_previewWorkspaceId,
                                              const QString &p_targetWorkspaceId) {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  const QPointer<ViewAreaController> guard(this);
  const QPointer<WorkspaceWrapper> preview(m_workspaces.value(p_previewWorkspaceId));
  const QPointer<WorkspaceWrapper> target(m_workspaces.value(p_targetWorkspaceId));
  auto *const view = m_view;
  if (!wsSvc || !m_services.get<BufferService>() || !view || p_previewWorkspaceId.isEmpty() ||
      p_targetWorkspaceId.isEmpty() || p_previewWorkspaceId == p_targetWorkspaceId || !preview ||
      !preview->isPreview() || !target || target->isPreview() || !target->isVisible() ||
      isPreviewWorkspaceClosing(p_previewWorkspaceId) ||
      m_previewAdmissionsInProgress.value(p_previewWorkspaceId) != 0 ||
      !view->isPreviewWorkspaceAvailable(p_previewWorkspaceId)) {
    return false;
  }
  const bool detached = m_detachedWorkspaceIds.contains(p_targetWorkspaceId);
  const auto current = [&] {
    return guard && m_view == view && preview && target &&
           m_workspaces.value(p_previewWorkspaceId) == preview &&
           m_workspaces.value(p_targetWorkspaceId) == target && preview->isPreview() &&
           !target->isPreview() && target->isVisible() &&
           m_detachedWorkspaceIds.contains(p_targetWorkspaceId) == detached &&
           (detached || view->getVisibleWorkspaceIds().contains(p_targetWorkspaceId)) &&
           !wsSvc->getWorkspace(p_previewWorkspaceId).isEmpty() &&
           !wsSvc->getWorkspace(p_targetWorkspaceId).isEmpty();
  };
  if (!current()) {
    return false;
  }
  for (const auto &admission : m_failedPreviewAdmissions) {
    if (admission.workspaceId == p_previewWorkspaceId) {
      return false;
    }
  }
  const auto incoming = view->getViewWindowNavInfos(p_previewWorkspaceId);
  const auto membership =
      wsSvc->getWorkspace(p_previewWorkspaceId).value(QStringLiteral("bufferIds")).toArray();
  QSet<QString> buffers;
  for (const auto &info : incoming) {
    if (info.windowId == InvalidViewWindowId || info.bufferId.isEmpty() ||
        buffers.contains(info.bufferId) || !membership.contains(info.bufferId) ||
        m_transitioningWindows.contains(info.windowId)) {
      return false;
    }
    buffers.insert(info.bufferId);
  }
  // Hidden protected retirements still own membership, even after leaving the tab bar.
  if (incoming.isEmpty() || buffers.size() != membership.size()) {
    return false;
  }
  ID selectedId = InvalidViewWindowId;
  QString selectedBuffer;
  view->getCurrentWindowInfoForWorkspace(p_previewWorkspaceId, selectedId, selectedBuffer);
  m_previewCloseStates.insert(p_previewWorkspaceId, PreviewCloseState::Checking);
  for (const auto &info : incoming) {
    m_transitioningWindows.insert(info.windowId);
  }
  const auto release = [&] {
    if (!guard) {
      return;
    }
    for (const auto &info : incoming) {
      m_transitioningWindows.remove(info.windowId);
    }
    if (preview && m_workspaces.value(p_previewWorkspaceId) == preview &&
        m_previewCloseStates.value(p_previewWorkspaceId) == PreviewCloseState::Checking) {
      m_previewCloseStates.insert(p_previewWorkspaceId, PreviewCloseState::Open);
    }
  };
  auto transaction = qScopeGuard(release);
  ID lastMovedId = InvalidViewWindowId;
  QString lastMovedBuffer;
  bool selectedMoved = false;
  int moved = 0;
  for (const auto &info : incoming) {
    const auto stillInPreview = [&] {
      return current() &&
             view->findWindowIdByBufferId(p_previewWorkspaceId, info.bufferId) == info.windowId &&
             wsSvc->getWorkspace(p_previewWorkspaceId)
                 .value(QStringLiteral("bufferIds"))
                 .toArray()
                 .contains(info.bufferId);
    };
    if (!stillInPreview()) {
      break;
    }
    ViewWindowMoveEvent event;
    event.windowId = info.windowId;
    event.bufferId = info.bufferId;
    event.srcWorkspaceId = p_previewWorkspaceId;
    event.dstWorkspaceId = p_targetWorkspaceId;
    event.direction = static_cast<int>(Direction::Right);
    const bool vetoed = wsSvc->fireViewWindowBeforeMove(event);
    if (!guard) {
      return false;
    }
    if (vetoed || !stillInPreview()) {
      break;
    }
    const ID duplicate = view->findWindowIdByBufferId(p_targetWorkspaceId, info.bufferId);
    // addBuffer is idempotent: snapshot BEFORE adding to know what rollback owns.
    const bool alreadyRegistered = wsSvc->getWorkspace(p_targetWorkspaceId)
                                       .value(QStringLiteral("bufferIds"))
                                       .toArray()
                                       .contains(info.bufferId);
    if (!wsSvc->addBuffer(p_targetWorkspaceId, info.bufferId)) {
      break;
    }
    std::function<bool()> closeDuplicate;
    if (duplicate != InvalidViewWindowId) {
      closeDuplicate = [guard, duplicate] {
        return guard && guard->closeViewWindow(duplicate, false);
      };
    }
    const bool transferred = view->promotePreviewViewWindow(info.windowId, p_previewWorkspaceId,
                                                            p_targetWorkspaceId, closeDuplicate);
    if (!guard || m_view != view) {
      return false;
    }
    if (!transferred || !current() ||
        view->findWindowIdByBufferId(p_targetWorkspaceId, info.bufferId) != info.windowId ||
        !wsSvc->getWorkspace(p_targetWorkspaceId)
             .value(QStringLiteral("bufferIds"))
             .toArray()
             .contains(info.bufferId)) {
      if (!alreadyRegistered &&
          view->findWindowIdByBufferId(p_targetWorkspaceId, info.bufferId) == InvalidViewWindowId) {
        wsSvc->removeBuffer(p_targetWorkspaceId, info.bufferId);
      }
      break;
    }
    // Keep add-before-remove, but do not surrender preview ownership before consent.
    wsSvc->removeBuffer(p_previewWorkspaceId, info.bufferId);
    lastMovedId = info.windowId;
    lastMovedBuffer = info.bufferId;
    selectedMoved = selectedMoved || info.windowId == selectedId;
    ++moved;
    wsSvc->fireViewWindowAfterMove(event);
    if (!current()) {
      break;
    }
  }
  release();
  transaction.dismiss();
  if (!guard || m_view != view) {
    return false;
  }
  const auto syncWorkspace = [&](const QString &p_workspaceId) {
    const auto infos = view->getViewWindowNavInfos(p_workspaceId);
    QStringList order;
    order.reserve(infos.size());
    for (const auto &info : infos) {
      order.append(info.bufferId);
    }
    setBufferOrder(p_workspaceId, order);
    wsSvc->setCurrentBuffer(p_workspaceId, view->getCurrentBufferIdForWorkspace(p_workspaceId));
  };
  if (target && m_workspaces.value(p_targetWorkspaceId) == target) {
    syncWorkspace(p_targetWorkspaceId);
  }
  const bool complete = moved == incoming.size() && current();
  if (preview && m_workspaces.value(p_previewWorkspaceId) == preview) {
    syncWorkspace(p_previewWorkspaceId);
    removeEmptyPreviewWorkspace(p_previewWorkspaceId);
  }
  if (!guard || m_view != view) {
    return false;
  }
  if (complete && target && m_workspaces.value(p_targetWorkspaceId) == target) {
    const ID id = selectedMoved ? selectedId : lastMovedId;
    const QString bufferId = selectedMoved ? selectedBuffer : lastMovedBuffer;
    if (view->findWindowIdByBufferId(p_targetWorkspaceId, bufferId) == id) {
      clearActivePreviewWindow();
      if (!guard || m_view != view) {
        return false;
      }
      if (!detached) {
        setCurrentViewSplit(p_targetWorkspaceId, false);
        if (!guard || m_view != view) {
          return false;
        }
        setCurrentViewWindow(id, bufferId);
      } else {
        wsSvc->setCurrentBuffer(p_targetWorkspaceId, bufferId);
      }
      if (!guard || m_view != view) {
        return false;
      }
      view->setCurrentBuffer(p_targetWorkspaceId, bufferId, true);
    }
  } else if (preview && m_workspaces.value(p_previewWorkspaceId) == preview) {
    const auto remaining = view->getViewWindowNavInfos(p_previewWorkspaceId);
    if (!remaining.isEmpty()) {
      const bool selectionRemains =
          view->findWindowIdByBufferId(p_previewWorkspaceId, selectedBuffer) == selectedId;
      const auto bufferId = selectionRemains ? selectedBuffer : remaining.first().bufferId;
      const auto id = selectionRemains ? selectedId : remaining.first().windowId;
      view->setCurrentBuffer(p_previewWorkspaceId, bufferId, true);
      if (!guard || m_view != view) {
        return false;
      }
      // Revealing a rebound source changes detached selection without a core
      // current-buffer notification. Publish that actual selection after reveal.
      if (detached && current()) {
        wsSvc->setCurrentBuffer(p_targetWorkspaceId,
                                view->getCurrentBufferIdForWorkspace(p_targetWorkspaceId));
      }
      setActivePreviewWindow(p_previewWorkspaceId, id, bufferId);
    }
  }
  if (!guard) {
    return false;
  }
  emit windowsChanged();
  return complete;
}

bool ViewAreaController::closeAllPreviewWorkspaces() {
  const QPointer<ViewAreaController> guard(this);
  // Recollect after each consent loop: hooks can create an overlay on another
  // source. Accepted workspaces may remain only for deferred close completion.
  for (;;) {
    QString workspaceId;
    for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
      if (it.value()->isPreview() &&
          m_previewCloseStates.value(it.key()) != PreviewCloseState::Accepted) {
        workspaceId = it.key();
        break;
      }
    }
    if (workspaceId.isEmpty()) {
      return true;
    }
    if (!closePreviewWorkspace(workspaceId) || !guard) {
      return false;
    }
  }
}

bool ViewAreaController::closeAll(const QVector<QString> &p_workspaceIds, bool p_force) {
  if (!m_view) {
    return true;
  }

  m_suppressAutoRemove = true;

  for (const auto &wsId : p_workspaceIds) {
    if (isPreviewWorkspace(wsId)) {
      if (!closePreviewWorkspace(wsId)) {
        m_suppressAutoRemove = false;
        return false;
      }
      continue;
    }
    QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(wsId);
    for (ID winId : windowIds) {
      qDebug() << "ViewAreaController::closeAll: closing window:" << winId
               << "in workspace:" << wsId;
      if (!closeViewWindow(winId, p_force)) {
        // Close was cancelled. Abort entire closeAll.
        qDebug() << "ViewAreaController::closeAll: close cancelled, aborting";
        m_suppressAutoRemove = false;
        return false;
      }
    }
  }

  m_suppressAutoRemove = false;
  return true;
}

bool ViewAreaController::closeAllBuffersForQuit() {
  // ViewArea2 normally gates previews before saving the session; retain this
  // cancellable gate for direct quit callers and accepted deferred retirements.
  if (!closeAllPreviewWorkspaces()) {
    return false;
  }
  if (!isNoteConversionBlocked()) {
    auto *buffers = m_services.get<BufferService>();
    if (buffers && !buffers->protectedBuffers().isEmpty()) {
      auto *config = m_services.get<ConfigCoreService>();
      if (!config || !config->prepareShutdown())
        return false;
    }
    QString error;
    if (!lockAllProtectedNotes(&error)) {
      emit protectedLockFailed(error);
      return false;
    }
  }
  // Prepared ciphertext is durable on a blocked conversion. Leave its old
  // windows/core handles intact until process teardown: ordinary close would
  // save plaintext or discard a backup still named by the recovery journal.
  const auto recoveryWindow = [&](ID p_id) {
    for (const auto &conversion : m_noteConversions) {
      if (conversion->m_state->blocked && conversion->m_state->windowIds.contains(p_id)) {
        return true;
      }
    }
    return false;
  };
  for (const auto &conversion : m_noteConversions) {
    if (!conversion->m_state->blocked) {
      return false;
    }
  }
  // Phase 1: Close all visible workspace windows with save prompts.
  QStringList visibleIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
  QVector<QString> visibleIdsVec;
  for (const auto &id : visibleIds) {
    visibleIdsVec.append(id);
  }
  if (isNoteConversionBlocked()) {
    const QScopedValueRollback<bool> suppress(m_suppressAutoRemove, true);
    for (const auto &workspaceId : visibleIdsVec) {
      for (const ID id : m_view->getViewWindowIdsForWorkspace(workspaceId)) {
        if (!recoveryWindow(id) && !closeViewWindow(id, false)) {
          return false;
        }
      }
    }
  } else if (!closeAll(visibleIdsVec, false)) {
    return false;
  }

  // Phase 2: Close hidden workspace windows.
  // Note: If user cancels during this phase, visible windows from Phase 1
  // are already closed (non-transactional). This is acceptable per spec.
  for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
    auto *wrapper = it.value();
    if (wrapper->isPreview() || wrapper->isVisible()) {
      continue;
    }
    const auto &windows = wrapper->viewWindows();
    for (auto *obj : windows) {
      auto *win = qobject_cast<ViewWindow2 *>(obj);
      if (win && !recoveryWindow(win->getViewWindowId()) && !win->aboutToClose(false)) {
        qInfo() << "ViewAreaController::closeAllBuffersForQuit:"
                << "hidden window cancel, aborting quit";
        return false;
      }
    }
  }

  return true;
}

void ViewAreaController::closeTabs(const QString &p_workspaceId, int p_referenceTabIndex,
                                   CloseTabMode p_mode) {
  if (!m_view || p_workspaceId.isEmpty()) {
    return;
  }
  if (p_mode == CloseTabMode::All && isPreviewWorkspace(p_workspaceId)) {
    closePreviewWorkspace(p_workspaceId);
    return;
  }

  QVector<ID> allWindowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
  if (allWindowIds.isEmpty()) {
    return;
  }

  if (p_referenceTabIndex < 0 || p_referenceTabIndex >= allWindowIds.size()) {
    if (p_mode != CloseTabMode::All) {
      return;
    }
  }

  // Determine which windows to close.
  QVector<ID> toClose;
  switch (p_mode) {
  case CloseTabMode::All:
    toClose = allWindowIds;
    break;
  case CloseTabMode::Others:
    for (int i = 0; i < allWindowIds.size(); ++i) {
      if (i != p_referenceTabIndex) {
        toClose.append(allWindowIds[i]);
      }
    }
    break;
  case CloseTabMode::ToTheLeft:
    for (int i = 0; i < p_referenceTabIndex; ++i) {
      toClose.append(allWindowIds[i]);
    }
    break;
  case CloseTabMode::ToTheRight:
    for (int i = p_referenceTabIndex + 1; i < allWindowIds.size(); ++i) {
      toClose.append(allWindowIds[i]);
    }
    break;
  }

  if (toClose.isEmpty()) {
    return;
  }

  // Close in reverse order to preserve indices.
  m_suppressAutoRemove = true;
  for (int i = toClose.size() - 1; i >= 0; --i) {
    if (!closeViewWindow(toClose[i], false)) {
      break; // Cancelled.
    }
  }
  m_suppressAutoRemove = false;
}

void ViewAreaController::openLastClosedFile() {
  if (m_closedTabStack.isEmpty()) {
    return;
  }

  ClosedTabRecord record = m_closedTabStack.takeLast();

  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "ViewAreaController::openLastClosedFile: BufferService unavailable";
    return;
  }

  FileOpenSettings settings;
  settings.m_alwaysNewWindow = true;
  settings.m_mode = record.mode;
  settings.m_forceMode = true;
  settings.m_lineNumber = record.cursorPosition;
  settings.m_focus = true;

  qDebug() << "ViewAreaController::openLastClosedFile: reopening" << record.nodeId.notebookId
           << record.nodeId.relativePath << "mode:" << static_cast<int>(record.mode)
           << "cursor:" << record.cursorPosition;

  // BufferService::openBuffer fires FileAfterOpen hook, which triggers
  // onFileAfterOpen() -> openBuffer() to create the ViewWindow2.
  VxCoreError openErr = VXCORE_OK;
  Buffer2 buf = bufferSvc->openBuffer(record.nodeId, settings, &openErr);
  if (!buf.isValid()) {
    if (openErr == VXCORE_ERR_NODE_NOT_EXISTS) {
      // The file was indexed but its content was deleted on disk since the tab
      // was closed. Surface a specific, user-meaningful message. ViewAreaController
      // has no user-notification signal, so log a distinct warning.
      qWarning() << "ViewAreaController::openLastClosedFile: file no longer exists on disk:"
                 << record.nodeId.notebookId << record.nodeId.relativePath;
    } else {
      qWarning() << "ViewAreaController::openLastClosedFile: failed to reopen buffer";
    }
  }
}

bool ViewAreaController::isPreviewWorkspace(const QString &p_workspaceId) const {
  if (p_workspaceId.isEmpty()) {
    return false;
  }
  const auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
  if (wrapper) {
    return wrapper->isPreview();
  }
  const auto *wsSvc = m_services.get<WorkspaceCoreService>();
  return wsSvc && wsSvc->getWorkspace(p_workspaceId)
                      .value(QLatin1String(vxcore::kJsonKeyIsPreview))
                      .toBool();
}

void ViewAreaController::splitViewSplit(const QString &p_workspaceId, Direction p_direction,
                                        bool p_openCurrentBuffer) {
  if (p_workspaceId.isEmpty() || isPreviewWorkspace(p_workspaceId)) {
    return;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewSplitCreateEvent event;
  event.workspaceId = p_workspaceId;
  event.direction = static_cast<int>(p_direction);
  if (wsSvc && wsSvc->fireViewSplitBeforeCreate(event)) {
    return;
  }

  // Get the actual current buffer from the view (ground truth, not vxcore which may be stale).
  QString currentBufferId;
  if (m_view) {
    currentBufferId = m_view->getCurrentBufferIdForWorkspace(p_workspaceId);
  }

  QString newWsId;
  if (wsSvc) {
    newWsId = wsSvc->createWorkspace(generateWorkspaceName());
  }
  if (newWsId.isEmpty()) {
    qWarning() << "ViewAreaController::splitViewSplit: failed to create workspace";
    return;
  }
  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  wrapper->setVisible(true);
  m_workspaces.insert(newWsId, wrapper);
  if (m_view) {
    m_view->split(p_workspaceId, p_direction, newWsId);
  }

  // Open the current buffer of the source workspace in the new split.
  // Set the new workspace as current — the new split becomes active (natural UX).
  if (p_openCurrentBuffer && !currentBufferId.isEmpty() && !newWsId.isEmpty()) {
    auto *bufferSvc = m_services.get<BufferService>();
    if (bufferSvc) {
      Buffer2 buf = bufferSvc->getBufferHandle(currentBufferId);
      if (buf.isValid()) {
        m_currentWorkspaceId = newWsId;
        FileOpenSettings settings;
        settings.m_alwaysNewWindow = true;
        settings.m_focus = true;
        openBuffer(buf, settings);
      }
    }
  }

  // Distribute all splits evenly so the new split gets fair space.
  if (m_view) {
    m_view->distributeViewSplits();
  }

  if (wsSvc) {
    event.newWorkspaceId = newWsId;
    wsSvc->fireViewSplitAfterCreate(event);
  }
  emit viewSplitsCountChanged();
}

bool ViewAreaController::removeViewSplit(const QString &p_workspaceId, bool p_keepWorkspace,
                                         bool p_force) {
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (p_workspaceId.isEmpty() || isPreviewWorkspace(p_workspaceId) || totalSplitCount <= 1) {
    return false;
  }
  if (m_view && !m_transitioningWindows.isEmpty()) {
    const auto windows = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
    for (const ID id : windows) {
      if (m_transitioningWindows.contains(id)) {
        return false;
      }
    }
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewSplitRemoveEvent event;
  event.workspaceId = p_workspaceId;
  event.keepWorkspace = p_keepWorkspace;
  if (wsSvc && wsSvc->fireViewSplitBeforeRemove(event)) {
    return false;
  }

  if (p_keepWorkspace) {
    // Hide-only mode: transfer ViewWindows to WorkspaceWrapper, remove split widget.
    // Workspace becomes hidden but remains in vxcore.
    auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
    if (wrapper && wrapper->isVisible() && m_view) {
      int currentIndex = 0;
      QVector<QObject *> windows = m_view->takeViewWindowsFromSplit(p_workspaceId, &currentIndex);
      wrapper->receiveViewWindows(windows, currentIndex);
      wrapper->setVisible(false);
    }
  } else {
    // Full removal mode: close all windows, delete workspace, then remove split.
    // Close windows one-by-one with abort-on-cancel.
    if (m_view) {
      m_suppressAutoRemove = true;

      QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
      for (ID winId : windowIds) {
        qDebug() << "ViewAreaController::removeViewSplit: closing window:" << winId
                 << "in workspace:" << p_workspaceId;
        if (!closeViewWindow(winId, p_force)) {
          qDebug() << "ViewAreaController::removeViewSplit: close cancelled, aborting";
          m_suppressAutoRemove = false;
          return false;
        }
      }

      m_suppressAutoRemove = false;
    }

    // Remove remaining buffers and delete workspace in vxcore.
    if (wsSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray remainingBuffers = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      for (int i = 0; i < remainingBuffers.size(); ++i) {
        QString bufferId = remainingBuffers[i].toString();
        if (!bufferId.isEmpty()) {
          wsSvc->removeBuffer(p_workspaceId, bufferId);
        }
      }
      wsSvc->deleteWorkspace(p_workspaceId);
    }

    // Destroy the WorkspaceWrapper.
    if (m_workspaces.contains(p_workspaceId)) {
      auto *wrapper = m_workspaces.take(p_workspaceId);
      auto leftover = wrapper->takeAllViewWindows();
      for (auto *obj : leftover) {
        delete qobject_cast<QWidget *>(obj);
      }
      delete wrapper;
    }
  }

  if (m_currentWorkspaceId == p_workspaceId) {
    m_currentWorkspaceId.clear();
    m_currentWindowId = InvalidViewWindowId;
  }
  if (m_view) {
    m_view->removeViewSplit(p_workspaceId);
  }
  if (wsSvc) {
    wsSvc->fireViewSplitAfterRemove(event);
  }
  emit viewSplitsCountChanged();
  return true;
}

void ViewAreaController::maximizeViewSplit(const QString &p_workspaceId) {
  if (m_view && !isPreviewWorkspace(p_workspaceId)) {
    m_view->maximizeViewSplit(p_workspaceId);
  }
}

void ViewAreaController::distributeViewSplits() {
  if (m_view) {
    m_view->distributeViewSplits();
  }
}

void ViewAreaController::moveViewWindowOneSplit(const QString &p_srcWorkspaceId, ID p_windowId,
                                                Direction p_direction,
                                                const QString &p_dstWorkspaceId,
                                                const QString &p_bufferId) {
  if (p_srcWorkspaceId.isEmpty() || p_windowId == InvalidViewWindowId ||
      p_dstWorkspaceId.isEmpty() || isPreviewWorkspace(p_srcWorkspaceId) ||
      isPreviewWorkspace(p_dstWorkspaceId) || m_transitioningWindows.contains(p_windowId)) {
    return;
  }

  // Cannot move to the same workspace.
  if (p_srcWorkspaceId == p_dstWorkspaceId) {
    return;
  }

  // Check if the buffer is already open in the destination workspace.
  // If so, just activate the existing tab instead of creating a duplicate.
  if (m_view && !p_bufferId.isEmpty()) {
    ID existingId = m_view->findWindowIdByBufferId(p_dstWorkspaceId, p_bufferId);
    if (existingId != InvalidViewWindowId) {
      qDebug() << "ViewAreaController::moveViewWindowOneSplit: buffer already in destination,"
               << "activating existing window:" << existingId;
      setCurrentViewSplit(p_dstWorkspaceId, true);
      setCurrentViewWindow(existingId, p_bufferId);
      return;
    }
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewWindowMoveEvent event;
  event.windowId = p_windowId;
  event.srcWorkspaceId = p_srcWorkspaceId;
  event.dstWorkspaceId = p_dstWorkspaceId;
  event.direction = static_cast<int>(p_direction);
  event.bufferId = p_bufferId;
  if (wsSvc && wsSvc->fireViewWindowBeforeMove(event)) {
    return;
  }

  // Transfer buffer registration between workspaces in vxcore (add-before-remove
  // to avoid vxcore orphan auto-close; see transferBufferRegistration).
  transferBufferRegistration(p_srcWorkspaceId, p_dstWorkspaceId, p_bufferId);

  if (m_view) {
    m_view->moveViewWindowToSplit(p_windowId, p_srcWorkspaceId, p_dstWorkspaceId);
  }
  setCurrentViewSplit(p_dstWorkspaceId, true);
  if (wsSvc) {
    wsSvc->fireViewWindowAfterMove(event);
  }
  emit windowsChanged();
}

bool ViewAreaController::transferBufferRegistration(const QString &p_srcWorkspaceId,
                                                    const QString &p_dstWorkspaceId,
                                                    const QString &p_bufferId) {
  // Order matters: addBuffer(dst) BEFORE removeBuffer(src) so the buffer never
  // becomes orphan. WorkspaceManager::RemoveBufferFromWorkspace auto-closes a
  // buffer that is no longer in any workspace (workspace_manager.cpp:244-249);
  // a removeBuffer-first ordering would destroy the buffer mid-move and leave
  // the view window with a dead ID. addBuffer is idempotent so the case where
  // dst already holds the buffer (legacy clone-on-split path) remains safe.
  if (!m_shouldPropagateToCore || p_bufferId.isEmpty()) {
    return true;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return true;
  }
  if (!wsSvc->addBuffer(p_dstWorkspaceId, p_bufferId)) {
    qWarning() << "ViewAreaController::transferBufferRegistration: addBuffer to" << p_dstWorkspaceId
               << "failed for buffer" << p_bufferId << "- leaving source registration intact";
    return false;
  }
  wsSvc->removeBuffer(p_srcWorkspaceId, p_bufferId);
  return true;
}

bool ViewAreaController::detachViewWindow(const QString &p_srcWorkspaceId, ID p_windowId,
                                          const QString &p_bufferId) {
  if (p_srcWorkspaceId.isEmpty() || p_windowId == InvalidViewWindowId || !m_view ||
      isPreviewWorkspace(p_srcWorkspaceId) || m_transitioningWindows.contains(p_windowId)) {
    return false;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();

  // Let plugins veto the detach up-front (models it as a view-split creation).
  ViewSplitCreateEvent createEvent;
  createEvent.workspaceId = p_srcWorkspaceId;
  createEvent.direction = static_cast<int>(Direction::Right);
  if (wsSvc && wsSvc->fireViewSplitBeforeCreate(createEvent)) {
    return false;
  }

  QString newWsId;
  if (wsSvc) {
    newWsId = wsSvc->createWorkspace(generateWorkspaceName());
  }
  if (newWsId.isEmpty()) {
    qWarning() << "ViewAreaController::detachViewWindow: failed to create workspace";
    return false;
  }

  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  // A detached workspace hosts a real (visible) ViewSplit2 inside the
  // DetachedWindow, so mark the wrapper visible; its cached-window list stays
  // empty because the split owns the windows directly.
  wrapper->setVisible(true);
  m_workspaces.insert(newWsId, wrapper);
  m_detachedWorkspaceIds.insert(newWsId);

  // Lambda to fully undo the just-created workspace on any failure below.
  auto rollback = [this, wsSvc, newWsId]() {
    if (wsSvc) {
      wsSvc->deleteWorkspace(newWsId);
    }
    if (m_workspaces.contains(newWsId)) {
      delete m_workspaces.take(newWsId);
    }
    m_detachedWorkspaceIds.remove(newWsId);
  };

  // Register the buffer in the destination BEFORE removing it from the source so
  // vxcore never sees the buffer as orphaned (which would auto-close it).
  if (m_shouldPropagateToCore && wsSvc && !p_bufferId.isEmpty()) {
    if (!wsSvc->addBuffer(newWsId, p_bufferId)) {
      qWarning() << "ViewAreaController::detachViewWindow: addBuffer failed, aborting";
      rollback();
      return false;
    }
  }

  // Model the tab transfer as a ViewWindow move so move hooks can veto it.
  ViewWindowMoveEvent moveEvent;
  moveEvent.windowId = p_windowId;
  moveEvent.srcWorkspaceId = p_srcWorkspaceId;
  moveEvent.dstWorkspaceId = newWsId;
  moveEvent.direction = static_cast<int>(Direction::Right);
  moveEvent.bufferId = p_bufferId;
  if (wsSvc && wsSvc->fireViewWindowBeforeMove(moveEvent)) {
    if (m_shouldPropagateToCore && wsSvc && !p_bufferId.isEmpty()) {
      wsSvc->removeBuffer(newWsId, p_bufferId);
    }
    rollback();
    return false;
  }

  m_view->hostWorkspaceInDetachedWindow(newWsId);
  if (!m_view->moveViewWindowToDetached(p_windowId, p_srcWorkspaceId, newWsId)) {
    // The view destroyed the empty detached-window shell; undo core state. The
    // window is still in the source split, so leave the source registration.
    if (m_shouldPropagateToCore && wsSvc && !p_bufferId.isEmpty()) {
      wsSvc->removeBuffer(newWsId, p_bufferId);
    }
    rollback();
    return false;
  }

  // Transfer succeeded: drop the source registration.
  if (m_shouldPropagateToCore && wsSvc && !p_bufferId.isEmpty()) {
    wsSvc->removeBuffer(p_srcWorkspaceId, p_bufferId);
  }

  if (wsSvc) {
    createEvent.newWorkspaceId = newWsId;
    wsSvc->fireViewSplitAfterCreate(createEvent);
    wsSvc->fireViewWindowAfterMove(moveEvent);
  }

  emit windowsChanged();
  emit viewSplitsCountChanged();
  return true;
}

void ViewAreaController::reattachDetachedWorkspace(const QString &p_detachedWorkspaceId,
                                                   const QString &p_targetWorkspaceId,
                                                   const QStringList &p_bufferIds) {
  if (p_detachedWorkspaceId.isEmpty() || isPreviewWorkspace(p_detachedWorkspaceId) ||
      isPreviewWorkspace(p_targetWorkspaceId)) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();

  // Detached teardown is not user-cancellable (the window is already gone), so we
  // fire the remove hooks observe-only rather than honoring a veto.
  ViewSplitRemoveEvent removeEvent;
  removeEvent.workspaceId = p_detachedWorkspaceId;
  removeEvent.keepWorkspace = false;
  if (wsSvc) {
    wsSvc->fireViewSplitBeforeRemove(removeEvent);
  }

  // Transfer buffer registration to the reattach target (add before remove to
  // avoid vxcore orphan auto-close). Skipped when there is no target (last-tab
  // close teardown), in which case the buffers were already removed by
  // onViewWindowClosed.
  bool allTransferred = true;
  if (m_shouldPropagateToCore && wsSvc && !p_targetWorkspaceId.isEmpty()) {
    for (const auto &bufferId : p_bufferIds) {
      if (!bufferId.isEmpty()) {
        if (!transferBufferRegistration(p_detachedWorkspaceId, p_targetWorkspaceId, bufferId)) {
          allTransferred = false;
        }
      }
    }
  }

  if (!allTransferred) {
    // At least one buffer could not be registered in the target. Deleting the
    // detached workspace now would strand that buffer (DeleteWorkspace does NOT
    // run the orphan auto-close path), so keep the detached workspace + wrapper
    // + tracking intact to avoid losing/leaking the buffer.
    qCritical() << "ViewAreaController::reattachDetachedWorkspace: some buffers failed to"
                << "transfer to target" << p_targetWorkspaceId << "- keeping detached workspace"
                << p_detachedWorkspaceId << "to avoid buffer loss";
    emit windowsChanged();
    return;
  }

  if (wsSvc) {
    wsSvc->deleteWorkspace(p_detachedWorkspaceId);
  }

  if (m_workspaces.contains(p_detachedWorkspaceId)) {
    auto *wrapper = m_workspaces.take(p_detachedWorkspaceId);
    delete wrapper;
  }
  m_detachedWorkspaceIds.remove(p_detachedWorkspaceId);

  if (m_currentWorkspaceId == p_detachedWorkspaceId) {
    m_currentWorkspaceId.clear();
    m_currentWindowId = InvalidViewWindowId;
  }

  if (wsSvc) {
    wsSvc->fireViewSplitAfterRemove(removeEvent);
  }

  emit windowsChanged();
  emit viewSplitsCountChanged();
}

bool ViewAreaController::hasActivePreviewWindow() const {
  if (m_activePreviewWindowId == InvalidViewWindowId || !m_view ||
      m_failedPreviewAdmissions.contains(m_activePreviewWindowId)) {
    return false;
  }
  FileOpenSettings settings;
  if (!isPreviewRequestCurrent(settings, m_activePreviewWorkspaceId)) {
    return false;
  }
  const auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return false;
  }
  const auto workspace = wsSvc->getWorkspace(m_activePreviewWorkspaceId);
  const QString bufferId = workspace.value(QStringLiteral("currentBufferId")).toString();
  return !bufferId.isEmpty() &&
         workspace.value(QStringLiteral("bufferIds")).toArray().contains(bufferId) &&
         m_view->findWindowIdByBufferId(m_activePreviewWorkspaceId, bufferId) ==
             m_activePreviewWindowId;
}

ID ViewAreaController::getCurrentWindowId() const {
  return hasActivePreviewWindow() ? m_activePreviewWindowId : m_currentWindowId;
}

QString ViewAreaController::getCurrentWorkspaceId() const {
  return hasActivePreviewWindow() ? m_activePreviewWorkspaceId : m_currentWorkspaceId;
}

void ViewAreaController::setActivePreviewWindow(const QString &p_workspaceId, ID p_windowId,
                                                const QString &p_bufferId) {
  if (p_windowId == InvalidViewWindowId || p_bufferId.isEmpty() || !m_view ||
      m_failedPreviewAdmissions.contains(p_windowId)) {
    return;
  }
  FileOpenSettings settings;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc || !isPreviewRequestCurrent(settings, p_workspaceId) ||
      m_view->findWindowIdByBufferId(p_workspaceId, p_bufferId) != p_windowId ||
      !wsSvc->getWorkspace(p_workspaceId)
           .value(QStringLiteral("bufferIds"))
           .toArray()
           .contains(p_bufferId)) {
    return;
  }
  if (m_shouldPropagateToCore && !wsSvc->setCurrentBuffer(p_workspaceId, p_bufferId)) {
    return;
  }
  const bool changed =
      m_activePreviewWorkspaceId != p_workspaceId || m_activePreviewWindowId != p_windowId;
  m_activePreviewWorkspaceId = p_workspaceId;
  m_activePreviewWindowId = p_windowId;
  if (changed) {
    scheduleLockedWindowReactivation(p_workspaceId, p_windowId, p_bufferId);
    emit currentViewWindowChanged();
  }
}

void ViewAreaController::clearActivePreviewWindow() {
  if (m_activePreviewWindowId == InvalidViewWindowId && m_activePreviewWorkspaceId.isEmpty()) {
    return;
  }
  m_activePreviewWorkspaceId.clear();
  m_activePreviewWindowId = InvalidViewWindowId;
  emit currentViewWindowChanged();
}

QVector<OpenWindowEntry> ViewAreaController::listOpenWindows() const {
  QVector<OpenWindowEntry> entries;

  for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
    auto *wrapper = it.value();
    if (!wrapper || wrapper->isPreview()) {
      continue;
    }

    const QString workspaceId = wrapper->workspaceId();
    const bool visible = wrapper->isVisible();
    const QString workspaceName = getWorkspaceName(workspaceId);

    QVector<OpenWindowEntry> wsEntries;

    if (visible) {
      // Visible workspace: windows live in the split. Resolve display info
      // (name/icon) via the view layer.
      if (m_view) {
        const auto infos = m_view->getViewWindowNavInfos(workspaceId);
        for (const auto &info : infos) {
          OpenWindowEntry entry;
          entry.workspaceId = workspaceId;
          entry.workspaceName = workspaceName;
          entry.workspaceVisible = true;
          entry.windowId = info.windowId;
          entry.bufferId = info.bufferId;
          entry.windowName = info.name;
          entry.windowTitle = info.title;
          entry.windowIcon = info.icon;
          entry.windowCurrent = (info.windowId == m_currentWindowId);
          wsEntries.append(entry);
        }
      }
    } else {
      // Hidden workspace: windows are cached in the wrapper as QObject*.
      const auto &windows = wrapper->viewWindows();
      for (auto *obj : windows) {
        auto *win = qobject_cast<ViewWindow2 *>(obj);
        if (!win) {
          continue;
        }
        OpenWindowEntry entry;
        entry.workspaceId = workspaceId;
        entry.workspaceName = workspaceName;
        entry.workspaceVisible = false;
        entry.windowId = win->getViewWindowId();
        entry.bufferId = win->getBuffer().id();
        entry.windowName = win->getName();
        entry.windowTitle = win->getTitle();
        entry.windowIcon = win->getIcon();
        entry.windowCurrent = (entry.windowId == m_currentWindowId);
        wsEntries.append(entry);
      }
    }

    // Skip workspaces that end up with zero windows.
    if (!wsEntries.isEmpty()) {
      entries.append(wsEntries);
    }
  }

  return entries;
}

void ViewAreaController::focusWindow(const QString &p_workspaceId, const QString &p_bufferId) {
  if (p_workspaceId.isEmpty() || !m_view || isPreviewWorkspace(p_workspaceId)) {
    return;
  }

  // Validate the workspace is known this session.
  if (!m_workspaces.contains(p_workspaceId)) {
    qWarning() << "ViewAreaController::focusWindow: unknown workspace" << p_workspaceId;
    return;
  }

  const QStringList visibleIds = m_view->getVisibleWorkspaceIds();

  if (visibleIds.contains(p_workspaceId)) {
    // Target visible: focus its split, then activate the buffer's tab.
    setCurrentViewSplit(p_workspaceId, true);
    if (!p_bufferId.isEmpty()) {
      m_view->setCurrentBuffer(p_workspaceId, p_bufferId, true);
    }
    return;
  }

  // Target hidden: surface it by switching a visible split to it.
  if (visibleIds.isEmpty()) {
    // No visible split to switch from. switchWorkspace cannot create the first
    // split, so this is an intentional no-op (the entry is only reachable from
    // a running main window that always has >=1 split).
    qWarning() << "ViewAreaController::focusWindow: no visible workspace to surface"
               << p_workspaceId;
    return;
  }

  QString sourceWs = (!m_currentWorkspaceId.isEmpty() && visibleIds.contains(m_currentWorkspaceId))
                         ? m_currentWorkspaceId
                         : visibleIds.first();

  switchWorkspace(sourceWs, p_workspaceId);

  if (!p_bufferId.isEmpty()) {
    m_view->setCurrentBuffer(p_workspaceId, p_bufferId, true);
  }
}

void ViewAreaController::setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) {
  if (isPreviewWorkspace(p_workspaceId)) {
    return;
  }
  const QPointer<ViewAreaController> guard(this);
  clearActivePreviewWindow();
  if (!guard) {
    return;
  }
  if (m_currentWorkspaceId == p_workspaceId && !p_workspaceId.isEmpty()) {
    if (p_focus && m_view) {
      m_view->focusViewSplit(p_workspaceId);
    }
    return;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewSplitActivateEvent event;
  event.workspaceId = p_workspaceId;
  if (wsSvc && wsSvc->fireViewSplitBeforeActivate(event)) {
    return;
  }
  m_currentWorkspaceId = p_workspaceId;
  if (m_shouldPropagateToCore) {
    if (wsSvc && !p_workspaceId.isEmpty()) {
      wsSvc->setCurrentWorkspace(p_workspaceId);
    }
  }
  if (m_view) {
    m_view->setCurrentViewSplit(p_workspaceId, p_focus);
  }
  if (p_focus && m_view) {
    m_view->focusViewSplit(p_workspaceId);
  }
  if (wsSvc) {
    wsSvc->fireViewSplitAfterActivate(event);
  }
  // Propagate split change to current-window tracking so docks bound to
  // currentViewWindowChanged (e.g., Outline) rebind to the newly active split.
  if (m_view) {
    ID winId = InvalidViewWindowId;
    QString bufferId;
    m_view->getCurrentWindowInfoForWorkspace(p_workspaceId, winId, bufferId);
    setCurrentViewWindow(winId, bufferId);
  }
}

void ViewAreaController::setCurrentViewWindow(ID p_windowId, const QString &p_bufferId) {
  if (m_replacingBuffer && m_transitioningWindows.contains(p_windowId)) {
    return;
  }
  if (m_view && p_windowId != InvalidViewWindowId) {
    const QString previewWorkspace = m_view->previewWorkspaceForWindow(p_windowId);
    if (!previewWorkspace.isEmpty()) {
      // A source also resolves to its overlay. Reject only the overlay's own
      // tab, not the ordinary source receiving focus again.
      if ((!p_bufferId.isEmpty() &&
           m_view->findWindowIdByBufferId(previewWorkspace, p_bufferId) == p_windowId) ||
          (p_bufferId.isEmpty() &&
           m_view->getViewWindowIdsForWorkspace(previewWorkspace).contains(p_windowId))) {
        return;
      }
    }
  }
  if (isPreviewWorkspace(m_currentWorkspaceId)) {
    return;
  }
  const QPointer<ViewAreaController> guard(this);
  clearActivePreviewWindow();
  if (!guard) {
    return;
  }
  // The first addTab notification may precede workspace admission, and buffer
  // replacement deliberately retains the window ID. Keep core identity in
  // sync even when no new current-window notification is needed.
  if (m_shouldPropagateToCore && !p_bufferId.isEmpty() && !m_currentWorkspaceId.isEmpty()) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      wsSvc->setCurrentBuffer(m_currentWorkspaceId, p_bufferId);
    }
  }
  if (m_currentWindowId == p_windowId) {
    return;
  }
  qDebug() << "ViewAreaController::setCurrentViewWindow: windowId:" << p_windowId
           << "bufferId:" << p_bufferId << "workspace:" << m_currentWorkspaceId;
  m_currentWindowId = p_windowId;
  scheduleLockedWindowReactivation(m_currentWorkspaceId, p_windowId, p_bufferId);

  emit currentViewWindowChanged();
}

void ViewAreaController::scheduleLockedWindowReactivation(const QString &p_workspaceId,
                                                          ID p_windowId,
                                                          const QString &p_bufferId) {
  if (!m_shouldPropagateToCore || !m_lockedWindows || !m_lockedWindows->contains(p_windowId)) {
    return;
  }
  QTimer::singleShot(0, this, [this, p_workspaceId, p_windowId, p_bufferId]() {
    if (!m_view || getCurrentWindowId() != p_windowId || getCurrentWorkspaceId() != p_workspaceId ||
        !m_lockedWindows || !m_lockedWindows->contains(p_windowId) ||
        m_view->findWindowIdByBufferId(p_workspaceId, p_bufferId) != p_windowId) {
      return;
    }
    for (const auto &conversion : m_noteConversions) {
      if (conversion->m_state->active && conversion->m_state->windowIds.contains(p_windowId)) {
        return;
      }
    }
    auto *buffers = m_services.get<BufferService>();
    if (!buffers || buffers->isProtectedLocking()) {
      return;
    }
    const auto buffer = buffers->getBufferHandle(p_bufferId);
    if (buffer.isValid()) {
      FileOpenSettings settings;
      settings.m_targetWorkspaceId = p_workspaceId;
      buffers->openBuffer(buffer.nodeId(), settings);
    }
  });
}

void ViewAreaController::focus() {
  const QString workspaceId = getCurrentWorkspaceId();
  if (!workspaceId.isEmpty() && m_view) {
    m_view->focusViewSplit(workspaceId);
  }
}

void ViewAreaController::setShouldPropagateToCore(bool p_enabled) {
  m_shouldPropagateToCore = p_enabled;
}

bool ViewAreaController::shouldPropagateToCore() const { return m_shouldPropagateToCore; }

void ViewAreaController::newWorkspace(const QString &p_currentWorkspaceId, const QString &p_name) {
  if (p_currentWorkspaceId.isEmpty() || isPreviewWorkspace(p_currentWorkspaceId)) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  QString newWsId = wsSvc->createWorkspace(p_name);
  if (newWsId.isEmpty()) {
    return;
  }

  // Create wrapper (not visible yet — switchWorkspace will manage visibility).
  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  m_workspaces.insert(newWsId, wrapper);

  switchWorkspace(p_currentWorkspaceId, newWsId);
}

void ViewAreaController::renameWorkspace(const QString &p_workspaceId, const QString &p_newName) {
  if (p_workspaceId.isEmpty() || p_newName.isEmpty() || isPreviewWorkspace(p_workspaceId)) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  wsSvc->renameWorkspace(p_workspaceId, p_newName);
}

QString ViewAreaController::getWorkspaceName(const QString &p_workspaceId) const {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return QString();
  }

  QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
  return wsConfig.value(QStringLiteral("name")).toString();
}

bool ViewAreaController::removeWorkspace(QString p_workspaceId, bool p_force) {
  if (p_workspaceId.isEmpty() || isPreviewWorkspace(p_workspaceId)) {
    return false;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return false;
  }

  // Step 1: Get workspace's buffer list from vxcore.
  QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
  QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();

  // Step 2: Close each ViewWindow one-by-one with abort-on-cancel.
  // Suppress auto-remove-empty-workspace during the close loop since we manage
  // the workspace lifecycle explicitly.
  if (!bufferIds.isEmpty() && m_view) {
    m_suppressAutoRemove = true;

    QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
    for (ID winId : windowIds) {
      qDebug() << "ViewAreaController::removeWorkspace: closing window:" << winId
               << "in workspace:" << p_workspaceId;
      if (!closeViewWindow(winId, p_force)) {
        // Close was cancelled (hook or aboutToClose returned false). Abort.
        qDebug() << "ViewAreaController::removeWorkspace: close cancelled, aborting";
        m_suppressAutoRemove = false;
        return false;
      }
    }

    m_suppressAutoRemove = false;
  }

  // Step 3: Remove remaining buffers from vxcore workspace.
  // onViewWindowClosed already called wsSvc->removeBuffer() for each closed window,
  // but there may be buffers without ViewWindows (e.g., hidden workspace case).
  QJsonObject updatedConfig = wsSvc->getWorkspace(p_workspaceId);
  QJsonArray remainingBuffers = updatedConfig.value(QStringLiteral("bufferIds")).toArray();
  for (int i = 0; i < remainingBuffers.size(); ++i) {
    QString bufferId = remainingBuffers[i].toString();
    if (!bufferId.isEmpty()) {
      qDebug() << "ViewAreaController::removeWorkspace: removing orphan buffer:" << bufferId;
      wsSvc->removeBuffer(p_workspaceId, bufferId);
    }
  }

  // Step 4: Post-removal — find a replacement for the split.
  QStringList visibleWorkspaceIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  bool splitStillShowsWorkspace = visibleWorkspaceIds.contains(p_workspaceId);

  if (splitStillShowsWorkspace) {
    // The split is still showing this workspace's identity. Find a replacement.
    QJsonArray allWorkspaces = wsSvc->listWorkspaces();
    QString targetWsId;
    for (int i = 0; i < allWorkspaces.size(); ++i) {
      QJsonObject ws = allWorkspaces[i].toObject();
      if (ws.value(QLatin1String(vxcore::kJsonKeyIsPreview)).toBool()) {
        continue;
      }
      QString wsId = ws.value(QStringLiteral("id")).toString();
      if (wsId != p_workspaceId && !visibleWorkspaceIds.contains(wsId)) {
        targetWsId = wsId;
        break;
      }
    }

    if (!targetWsId.isEmpty()) {
      // Switch to a hidden workspace.
      qDebug() << "ViewAreaController::removeWorkspace: switching to hidden workspace:"
               << targetWsId;
      switchWorkspace(p_workspaceId, targetWsId);
    } else if (totalSplitCount > 1) {
      // No hidden workspace. Remove the split entirely.
      // Clear current tracking first so removeViewSplit doesn't try to re-enter
      // removeWorkspace (we're already handling the workspace deletion below).
      if (m_currentWorkspaceId == p_workspaceId) {
        m_currentWorkspaceId.clear();
        m_currentWindowId = InvalidViewWindowId;
      }
      qDebug() << "ViewAreaController::removeWorkspace: no hidden workspace, removing split";
      if (m_view) {
        m_view->removeViewSplit(p_workspaceId);
      }
      emit viewSplitsCountChanged();
    } else {
      // Last split. Create a new empty workspace.
      qDebug() << "ViewAreaController::removeWorkspace: last split, creating new workspace";
      QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
      if (!newWsId.isEmpty()) {
        auto *newWrapper = new WorkspaceWrapper(newWsId, this);
        m_workspaces.insert(newWsId, newWrapper);
        switchWorkspace(p_workspaceId, newWsId);
      }
    }
  }

  // Step 5: Delete the workspace in vxcore.
  qDebug() << "ViewAreaController::removeWorkspace: deleting workspace:" << p_workspaceId;
  wsSvc->deleteWorkspace(p_workspaceId);

  // Step 6: Destroy the WorkspaceWrapper.
  if (m_workspaces.contains(p_workspaceId)) {
    auto *wrapper = m_workspaces.take(p_workspaceId);
    auto leftover = wrapper->takeAllViewWindows();
    // For a hidden workspace the windows were parked in the wrapper and never
    // lived in a ViewSplit2, so Step 2's getViewWindowIdsForWorkspace() (which
    // only searches visible splits) returned nothing and they were never closed
    // through the view. Route them through closeViewWindow() now so they are
    // dropped from ViewArea2::m_windows and updateScreenVisibility() runs — this
    // is what reopens the home dashboard (maybeOpenHome) once the area is empty.
    // Deleting them directly here would skip that path and leave the view area
    // blank with no home screen.
    for (auto *obj : leftover) {
      auto *win = qobject_cast<ViewWindow2 *>(obj);
      if (win && m_view && win->getViewWindowId() != InvalidViewWindowId) {
        m_view->closeViewWindow(win->getViewWindowId(), /*p_force=*/true);
      } else {
        delete qobject_cast<QWidget *>(obj);
      }
    }
    delete wrapper;
  }

  return true;
}

void ViewAreaController::removeOtherWorkspaces(const QString &p_keepWorkspaceId) {
  if (isPreviewWorkspace(p_keepWorkspaceId)) {
    return;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  // Snapshot the target IDs first — the workspace list mutates as each removal runs.
  QStringList targetIds;
  QJsonArray allWorkspaces = wsSvc->listWorkspaces();
  for (int i = 0; i < allWorkspaces.size(); ++i) {
    const QJsonObject ws = allWorkspaces[i].toObject();
    if (ws.value(QLatin1String(vxcore::kJsonKeyIsPreview)).toBool()) {
      continue;
    }
    QString wsId = ws.value(QStringLiteral("id")).toString();
    if (!wsId.isEmpty() && wsId != p_keepWorkspaceId) {
      targetIds.append(wsId);
    }
  }

  for (const QString &wsId : targetIds) {
    // Abort the batch if the user cancels an unsaved-changes prompt.
    if (!removeWorkspace(wsId, false)) {
      return;
    }
  }
}

void ViewAreaController::switchWorkspace(const QString &p_currentWorkspaceId,
                                         const QString &p_targetWorkspaceId) {
  if (p_currentWorkspaceId.isEmpty() || p_targetWorkspaceId.isEmpty() ||
      isPreviewWorkspace(p_currentWorkspaceId) || isPreviewWorkspace(p_targetWorkspaceId)) {
    return;
  }
  if (p_currentWorkspaceId == p_targetWorkspaceId) {
    return;
  }
  if (m_view && !m_transitioningWindows.isEmpty()) {
    const auto windows = m_view->getViewWindowIdsForWorkspace(p_currentWorkspaceId);
    for (const ID id : windows) {
      if (m_transitioningWindows.contains(id)) {
        return;
      }
    }
  }

  // A workspace can only be shown in one ViewSplit at a time.
  // Reject switching to a workspace that is already visible in another split.
  if (m_view) {
    QStringList visibleIds = m_view->getVisibleWorkspaceIds();
    if (visibleIds.contains(p_targetWorkspaceId)) {
      qWarning() << "ViewAreaController::switchWorkspace: target workspace" << p_targetWorkspaceId
                 << "is already visible in another split";
      return;
    }
  }

  auto *currentWrapper = m_workspaces.value(p_currentWorkspaceId, nullptr);
  auto *targetWrapper = m_workspaces.value(p_targetWorkspaceId, nullptr);
  if (!currentWrapper || !targetWrapper) {
    qWarning() << "ViewAreaController::switchWorkspace: missing wrapper for" << p_currentWorkspaceId
               << "or" << p_targetWorkspaceId;
    return;
  }

  // Guard: current workspace must be visible (displayed in a split).
  // This can happen if a stale signal triggers a switch after the workspace
  // was already hidden by a previous switch.
  if (!currentWrapper->isVisible()) {
    qWarning() << "ViewAreaController::switchWorkspace: current workspace" << p_currentWorkspaceId
               << "is not visible, ignoring";
    return;
  }

  qDebug() << "ViewAreaController::switchWorkspace:" << p_currentWorkspaceId << "->"
           << p_targetWorkspaceId << "currentWrapper visible:" << currentWrapper->isVisible()
           << "windowCount:" << currentWrapper->viewWindowCount()
           << "targetWrapper visible:" << targetWrapper->isVisible()
           << "windowCount:" << targetWrapper->viewWindowCount();

  // Suppress vxcore propagation during reparenting to prevent
  // currentChanged signals from corrupting workspace state.
  // NOTE: Suppression starts AFTER takeViewWindowsFromSplit so that
  // buffer order sync (inside take) can propagate to vxcore.
  // This ensures hidden workspaces have correct tab order in vxcore
  // for session persistence.
  bool wasPropagating = m_shouldPropagateToCore;

  // Step 1: HIDE current workspace — take windows from split into wrapper.
  // Done BEFORE suppressing propagation so buffer order is synced to vxcore.
  if (m_view) {
    int currentIndex = 0;
    QVector<QObject *> windows =
        m_view->takeViewWindowsFromSplit(p_currentWorkspaceId, &currentIndex);
    currentWrapper->receiveViewWindows(windows, currentIndex);
    currentWrapper->setVisible(false);
  }

  m_shouldPropagateToCore = false;

  // Step 2: Update split identity (old workspace ID -> new workspace ID).
  if (m_view) {
    m_view->updateSplitWorkspaceId(p_currentWorkspaceId, p_targetWorkspaceId);
  }

  // Step 3: SHOW target workspace.
  targetWrapper->setVisible(true);
  if (targetWrapper->viewWindowCount() > 0) {
    // Target has cached windows — reparent them into the split.
    int targetIndex = targetWrapper->currentIndex();
    QVector<QObject *> windows = targetWrapper->takeAllViewWindows();
    if (m_view) {
      m_view->placeViewWindowsInSplit(p_targetWorkspaceId, windows, targetIndex);
    }
  } else {
    // Fresh workspace (no cached windows) — fall back to openRestoredBuffer pattern.
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    auto *bufferSvc = m_services.get<BufferService>();
    if (wsSvc && bufferSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_targetWorkspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      QString currentBufferId = wsConfig.value(QStringLiteral("currentBufferId")).toString();

      qDebug() << "ViewAreaController::switchWorkspace: fresh workspace" << p_targetWorkspaceId
               << "buffers:" << bufferIds << "currentBufferId:" << currentBufferId;

      m_currentWorkspaceId = p_targetWorkspaceId;
      for (int i = 0; i < bufferIds.size(); ++i) {
        QString bufferId = bufferIds[i].toString();
        if (!bufferId.isEmpty()) {
          if (!openRestoredBuffer(bufferSvc, p_targetWorkspaceId, bufferId, false) &&
              bufferId == currentBufferId) {
            // No window exists for it; activating it would only warn.
            currentBufferId.clear();
          }
        }
      }

      if (!currentBufferId.isEmpty() && m_view) {
        m_view->setCurrentBuffer(p_targetWorkspaceId, currentBufferId, true);
      }
    }
  }

  m_shouldPropagateToCore = wasPropagating;
  setCurrentViewSplit(p_targetWorkspaceId, true);
}

void ViewAreaController::setBufferOrder(const QString &p_workspaceId,
                                        const QStringList &p_bufferIds) {
  if (p_workspaceId.isEmpty()) {
    return;
  }
  qDebug() << "ViewAreaController::setBufferOrder: workspace:" << p_workspaceId
           << "bufferIds:" << p_bufferIds;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    wsSvc->setBufferOrder(p_workspaceId, p_bufferIds);
  }
}

QJsonObject ViewAreaController::saveLayout(const QJsonObject &p_widgetTree) const {
  QJsonObject layout;
  layout[QStringLiteral("splitterTree")] = p_widgetTree;
  // Note: currentWorkspaceId is NOT saved here. It is persisted by vxcore
  // (via vxcore_shutdown) and restored by restoreSession().
  return layout;
}

void ViewAreaController::loadLayout(const QJsonObject &p_layout) {
  if (p_layout.isEmpty()) {
    return;
  }
  if (m_view) {
    m_view->loadLayout(p_layout);
  }
  emit viewSplitsCountChanged();
}

void ViewAreaController::restoreSession(const QStringList &p_layoutWorkspaceIds) {
  m_shouldPropagateToCore = false;

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  auto *bufferSvc = m_services.get<BufferService>();
  if (!wsSvc || !bufferSvc) {
    qWarning() << "ViewAreaController::restoreSession: services unavailable"
               << "wsSvc:" << (wsSvc != nullptr) << "bufferSvc:" << (bufferSvc != nullptr);
    return;
  }

  if (p_layoutWorkspaceIds.isEmpty()) {
    qCDebug(lcWorkspace) << "ViewAreaController::restoreSession: no workspaces in layout";
    return;
  }

  qCDebug(lcWorkspace) << "ViewAreaController::restoreSession: layout has"
                       << p_layoutWorkspaceIds.size() << "workspaces";

  // Determine the current workspace. If the saved current workspace is not in the
  // layout, fall back to the first workspace in the layout.
  QString restoredCurrentWsId = wsSvc->getCurrentWorkspace();
  if (!p_layoutWorkspaceIds.contains(restoredCurrentWsId)) {
    restoredCurrentWsId = p_layoutWorkspaceIds.first();
    qCDebug(lcWorkspace)
        << "ViewAreaController::restoreSession: saved current workspace not in layout,"
        << "falling back to:" << restoredCurrentWsId;
  }

  qCDebug(lcWorkspace) << "ViewAreaController::restoreSession: restoring workspaces from layout:"
                       << p_layoutWorkspaceIds << "currentWorkspace:" << restoredCurrentWsId;

  QStringList restoredWorkspaceIds;

  for (const auto &wsId : p_layoutWorkspaceIds) {
    // Get the full workspace config (includes bufferIds, currentBufferId).
    QJsonObject wsConfig = wsSvc->getWorkspace(wsId);
    if (wsConfig.isEmpty()) {
      // Workspace from layout no longer exists in vxcore. Create a replacement
      // so the split has a valid workspace backing.
      QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
      qWarning() << "  Workspace" << wsId << "not found in vxcore,"
                 << "created replacement:" << newWsId;
      if (!newWsId.isEmpty() && m_view) {
        auto *wrapper = new WorkspaceWrapper(newWsId, this);
        wrapper->setVisible(true);
        m_workspaces.insert(newWsId, wrapper);
        m_view->switchWorkspace(wsId, newWsId);
        restoredWorkspaceIds.append(newWsId);
        // Update restoredCurrentWsId if the stale one was the current.
        if (restoredCurrentWsId == wsId) {
          restoredCurrentWsId = newWsId;
        }
      }
      continue;
    }

    restoredWorkspaceIds.append(wsId);

    // Create WorkspaceWrapper for this restored workspace (visible in a split).
    if (!m_workspaces.contains(wsId)) {
      auto *wrapper = new WorkspaceWrapper(wsId, this);
      wrapper->setVisible(true);
      m_workspaces.insert(wsId, wrapper);
    }

    QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
    QString currentBufferId = wsConfig.value(QStringLiteral("currentBufferId")).toString();

    // Per-buffer metadata is now stored in "bufferMetadata" (native vxcore field).
    QJsonObject bufferMetadata = wsConfig.value(QStringLiteral("bufferMetadata")).toObject();

    qCDebug(lcWorkspace) << "  Workspace" << wsId
                         << "name:" << wsConfig.value(QStringLiteral("name")).toString()
                         << "buffers:" << bufferIds << "currentBuffer:" << currentBufferId
                         << "bufferMetadata keys:" << bufferMetadata.keys();

    if (bufferIds.isEmpty()) {
      qCDebug(lcWorkspace) << "    (no buffers, skipping)";
      continue;
    }

    // Set this workspace as current so openBuffer routes to the right split.
    m_currentWorkspaceId = wsId;

    // Open all buffers in their saved order.
    for (int j = 0; j < bufferIds.size(); ++j) {
      QString bufferId = bufferIds[j].toString();
      if (bufferId.isEmpty()) {
        continue;
      }
      // Resolve saved view mode and cursor position from per-buffer metadata.
      ViewWindowMode mode = ViewWindowMode::Read;
      int lineNumber = -1;
      QJsonObject bufMeta = bufferMetadata.value(bufferId).toObject();
      QString modeStr = bufMeta.value(QStringLiteral("mode")).toString();
      if (modeStr == QStringLiteral("Edit")) {
        mode = ViewWindowMode::Edit;
      }
      lineNumber = bufMeta.value(QStringLiteral("cursorPosition")).toInt(-1);
      qCDebug(lcWorkspace) << "    Opening buffer" << bufferId
                           << (bufferId == currentBufferId ? "(current)" : "(non-current)")
                           << "mode:" << modeStr << "cursor:" << lineNumber;
      if (!openRestoredBuffer(bufferSvc, wsId, bufferId, false, mode, lineNumber) &&
          bufferId == currentBufferId) {
        // No window exists for it; activating it would only warn.
        currentBufferId.clear();
      }
    }

    // After all buffers are opened in order, set the current buffer's tab as active.
    if (!currentBufferId.isEmpty() && m_view) {
      bool shouldFocus = (wsId == restoredCurrentWsId);
      qCDebug(lcWorkspace) << "    Setting current buffer:" << currentBufferId
                           << "focus:" << shouldFocus;
      m_view->setCurrentBuffer(wsId, currentBufferId, shouldFocus);
    }
  }

  // Create WorkspaceWrappers for hidden workspaces (in vxcore but not in the layout).
  // These will be switched to later by the user.
  QJsonArray allWorkspaces = wsSvc->listWorkspaces();
  for (int i = 0; i < allWorkspaces.size(); ++i) {
    QJsonObject wsObj = allWorkspaces[i].toObject();
    QString wsId = wsObj.value(QStringLiteral("id")).toString();
    if (!m_workspaces.contains(wsId)) {
      auto *wrapper = new WorkspaceWrapper(wsId, this);
      // Not visible — no split is displaying it.
      m_workspaces.insert(wsId, wrapper);
    }
  }

  // Resolve current workspace: must be one that was actually restored.
  // If the saved current workspace was not restored, fall back to the first restored one.
  if (!restoredWorkspaceIds.contains(restoredCurrentWsId)) {
    if (!restoredWorkspaceIds.isEmpty()) {
      restoredCurrentWsId = restoredWorkspaceIds.first();
      qDebug() << "  Current workspace not restored, falling back to:" << restoredCurrentWsId;
    } else {
      restoredCurrentWsId.clear();
      qDebug() << "  No workspaces were restored";
    }
  }

  // Restore the current workspace from vxcore.
  // Clear m_currentWorkspaceId first so setCurrentViewSplit doesn't early-return
  // (it was already set during the loop). This ensures the view receives
  // setCurrentViewSplit() to mark the split as active.
  m_currentWorkspaceId.clear();
  if (!restoredCurrentWsId.isEmpty()) {
    qCDebug(lcWorkspace) << "  Setting current workspace:" << restoredCurrentWsId;
    setCurrentViewSplit(restoredCurrentWsId, true);
  }

  qCDebug(lcWorkspace) << "ViewAreaController::restoreSession: done";
  // Note: m_shouldPropagateToCore stays false. The caller is responsible for
  // re-enabling it after pending signals have been processed (e.g., via
  // QTimer::singleShot(0, ...)) to avoid deferred currentChanged signals
  // from addTab corrupting vxcore state.
}

bool ViewAreaController::openRestoredBuffer(BufferService *p_bufferSvc,
                                            const QString &p_workspaceId, const QString &p_bufferId,
                                            bool p_focus, ViewWindowMode p_mode, int p_lineNumber) {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  const bool readOnly = wsSvc && wsSvc->getBufferMetadata(p_workspaceId, p_bufferId)
                                     .value(QLatin1String(vxcore::kJsonKeyReadOnly))
                                     .toBool();
  Buffer2 buf = p_bufferSvc->getBufferHandle(p_bufferId, readOnly);
  if (!buf.isValid()) {
    qWarning() << "    Failed to get buffer handle:" << p_bufferId;
    return false;
  }

  if (buf.isEncrypted() && buf.editorType().isEmpty()) {
    FileOpenSettings settings;
    settings.m_alwaysNewWindow = true;
    settings.m_focus = p_focus;
    settings.m_mode = p_mode;
    settings.m_lineNumber = p_lineNumber;
    if (m_view)
      m_view->openBuffer(buf, QString(), p_workspaceId, settings);
    return m_view != nullptr;
  }

  // Resolve file type.
  QString fileType;
  auto *ftSvc = m_services.get<FileTypeCoreService>();
  if (ftSvc) {
    FileType ft = ftSvc->getFileType(buf.nodeId().relativePath);
    fileType = ft.m_typeName;
  }
  if (fileType.isEmpty()) {
    fileType = QStringLiteral("Text");
  }

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
  // PDF has no built-in viewer below Qt 6.9 (see the ViewWindowFactory gate and
  // src/data/extra/web/pdf.js/AGENTS.md). Restoring the tab would make
  // ViewArea2::openBuffer emit viewWindowCreationFailed and raise an error
  // notification at every startup; launching an external application unprompted
  // at session restore is not acceptable either. So the tab is simply not
  // recreated.
  //
  // Deliberately NOT closing or unregistering the buffer: it stays a member of
  // the workspace, so the persisted session is untouched, no backup is

  if (fileType.compare(QStringLiteral("PDF"), Qt::CaseInsensitive) == 0) {
    qInfo() << "ViewAreaController::openRestoredBuffer: PDF viewing needs Qt 6.9+ -"
            << "skipping restore of" << buf.nodeId().relativePath;
    return false;
  }
#endif

  qCDebug(lcWorkspace) << "    Opening restored buffer: buffer:" << p_bufferId
                       << "path:" << buf.nodeId().relativePath << "type:" << fileType
                       << "workspace:" << p_workspaceId << "focus:" << p_focus
                       << "mode:" << static_cast<int>(p_mode) << "lineNumber:" << p_lineNumber;

  FileOpenSettings settings;
  settings.m_alwaysNewWindow = true;
  settings.m_focus = p_focus;
  settings.m_mode = p_mode;
  settings.m_lineNumber = p_lineNumber;

  if (m_view) {
    m_view->openBuffer(buf, fileType, p_workspaceId, settings);
  }
  return true;
}

void ViewAreaController::checkCurrentViewWindowChange(const QString &p_workspaceId) {
  Q_UNUSED(p_workspaceId)
  emit currentViewWindowChanged();
}

void ViewAreaController::notifyCurrentViewWindowChanged() {
  qCDebug(lcWorkspace) << "ViewAreaController::notifyCurrentViewWindowChanged: currentWindowId:"
                       << m_currentWindowId << "currentWorkspaceId:" << m_currentWorkspaceId;
  emit currentViewWindowChanged();
}

void ViewAreaController::requestQuickNote() {
  // If no quick note schemes are configured, deep-link into the settings page
  // so the user can create one; otherwise emit the request for the explorer.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr && configMgr->getSessionConfig().getQuickNoteSchemes().isEmpty()) {
    openVxUrl(QUrl(QStringLiteral("vx://settings/quickaccess#quicknote")));
    return;
  }

  emit quickNoteRequested();
}

void ViewAreaController::openVxUrl(const QUrl &p_url) {
  if (p_url.scheme() != QStringLiteral("vx")) {
    qWarning() << "ViewAreaController::openVxUrl: unsupported scheme:" << p_url.scheme();
    return;
  }

  const QString authority = p_url.authority();
  QStringList pathSegments = p_url.path().split(QStringLiteral("/"), Qt::SkipEmptyParts);
  QString fragment = p_url.fragment();

  if (authority == QStringLiteral("settings")) {
    auto *content = new SettingsWidget(m_services, nullptr);
    openWidgetContent(content, pathSegments, fragment);
  } else if (authority == QStringLiteral("home")) {
    auto *content = new DashboardContent(m_services, nullptr);
    openWidgetContent(content, pathSegments, fragment);
  } else {
    qWarning() << "ViewAreaController::openVxUrl: unknown authority:" << authority;
  }
}

void ViewAreaController::subscribeToHooks() {
  auto *hookMgr = m_services.get<HookManager>();
  if (!hookMgr) {
    qWarning() << "ViewAreaController::subscribeToHooks: HookManager not available";
    return;
  }
  hookMgr->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [this](HookContext &p_ctx, const FileOpenEvent &p_event) {
        Q_UNUSED(p_ctx)
        onFileAfterOpen(p_event);
      },
      10);
  hookMgr->addAction<NodeRenameEvent>(
      HookNames::NodeAfterRename,
      [this](HookContext &p_ctx, const NodeRenameEvent &p_event) {
        Q_UNUSED(p_ctx)
        onNodeAfterRename(p_event);
      },
      10);
  hookMgr->addAction<NodeOperationEvent>(
      HookNames::NodeAfterDelete,
      [this](HookContext &p_ctx, const NodeOperationEvent &p_event) {
        Q_UNUSED(p_ctx)
        onNodeAfterDelete(p_event);
      },
      10);
  hookMgr->addAction<NodeMoveEvent>(
      HookNames::NodeAfterMove,
      [this](HookContext &p_ctx, const NodeMoveEvent &p_event) {
        Q_UNUSED(p_ctx)
        onNodeAfterMove(p_event);
      },
      10);
  hookMgr->addAction(
      HookNames::ConfigEditorChanged,
      [this](HookContext &p_ctx, const QVariantMap &) {
        Q_UNUSED(p_ctx)
        onEditorConfigChanged();
      },
      10);
  hookMgr->addAction<NotebookCloseEvent>(
      HookNames::NotebookBeforeClose,
      [this](HookContext &p_ctx, const NotebookCloseEvent &p_event) {
        onNotebookBeforeClose(p_ctx, p_event);
      },
      10);
  hookMgr->addAction<NotebookCloseEvent>(
      HookNames::NotebookAfterClose,
      [this](HookContext &p_ctx, const NotebookCloseEvent &p_event) {
        Q_UNUSED(p_ctx)
        onNotebookAfterClose(p_event);
      },
      10);
}

void ViewAreaController::onFileAfterOpen(const FileOpenEvent &p_event) {
  if (m_noteConversionReopen) {
    return;
  }
  const QString bufferId = p_event.bufferId;
  if (bufferId.isEmpty()) {
    qWarning() << "ViewAreaController::onFileAfterOpen: empty bufferId";
    return;
  }
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "ViewAreaController::onFileAfterOpen: BufferService unavailable";
    return;
  }
  Buffer2 buf = bufferSvc->getBufferHandle(bufferId);
  if (!buf.isValid()) {
    qWarning() << "ViewAreaController::onFileAfterOpen: invalid buffer" << bufferId;
    return;
  }
  FileOpenSettings settings;
  settings.m_mode = static_cast<ViewWindowMode>(p_event.mode);
  settings.m_forceMode = p_event.forceMode;
  settings.m_focus = p_event.focus;
  settings.m_newFile = p_event.newFile;
  settings.m_readOnly = p_event.readOnly;
  settings.m_lineNumber = p_event.lineNumber;
  settings.m_cursorOffset = p_event.cursorOffset;
  settings.m_anchor = p_event.anchor;
  settings.m_alwaysNewWindow = p_event.alwaysNewWindow;
  settings.m_detachedView = p_event.detachedView;
  settings.m_targetWorkspaceId = p_event.targetWorkspaceId;
  settings.m_previewSourceWindowId = p_event.previewSourceWindowId;
  settings.m_previewSourceGeneration = p_event.previewSourceGeneration;
  // Reconstruct SearchHighlightContext from event's search fields.
  if (!p_event.searchPatterns.isEmpty()) {
    settings.m_searchHighlight.m_patterns = p_event.searchPatterns;
    settings.m_searchHighlight.m_options = static_cast<FindOptions>(p_event.searchOptions);
    settings.m_searchHighlight.m_currentMatchLine = p_event.searchCurrentMatchLine;
    settings.m_searchHighlight.m_isValid = true;
  }
  openBuffer(buf, settings);
}

void ViewAreaController::onNodeAfterRename(const NodeRenameEvent &p_event) {
  const QString notebookId = p_event.notebookId;
  const QString relativePath = p_event.relativePath;
  const QString newName = p_event.newName;
  const bool isFolder = p_event.isFolder;

  if (notebookId.isEmpty() || relativePath.isEmpty() || newName.isEmpty()) {
    return;
  }

  // Derive new path: replace the last component of the old path with newName.
  QString newPath = relativePath;
  int lastSlash = newPath.lastIndexOf(QLatin1Char('/'));
  if (lastSlash >= 0) {
    newPath = newPath.left(lastSlash + 1) + newName;
  } else {
    newPath = newName;
  }

  // vxcore has already updated buffer paths internally during the rename.
  // We only need to update the cached NodeIdentifier on ViewWindow2 instances.

  if (!isFolder) {
    // File rename: single buffer affected.
    NodeIdentifier oldNodeId;
    oldNodeId.notebookId = notebookId;
    oldNodeId.relativePath = relativePath;

    NodeIdentifier newNodeId;
    newNodeId.notebookId = notebookId;
    newNodeId.relativePath = newPath;

    // Update visible windows via the view interface.
    if (m_view) {
      m_view->onNodeRenamed(oldNodeId, newNodeId);
    }

    // Update hidden workspace windows.
    for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
      auto *wrapper = it.value();
      if (wrapper->isVisible()) {
        continue;
      }
      const auto &windows = wrapper->viewWindows();
      for (auto *obj : windows) {
        auto *win = qobject_cast<ViewWindow2 *>(obj);
        if (win && win->getNodeId() == oldNodeId) {
          win->onNodeRenamed(newNodeId);
        }
      }
    }
  } else {
    // Folder rename: update all windows whose paths are under the old folder.
    QString oldPrefix = relativePath + QLatin1Char('/');
    QString newPrefix = newPath + QLatin1Char('/');

    // Collect old->new pairs for all affected windows.
    QVector<QPair<NodeIdentifier, NodeIdentifier>> renames;

    auto collectRenames = [&](const NodeIdentifier &nodeId) {
      if (nodeId.notebookId != notebookId) {
        return;
      }
      if (nodeId.relativePath.startsWith(oldPrefix)) {
        NodeIdentifier oldId = nodeId;
        NodeIdentifier newId;
        newId.notebookId = notebookId;
        newId.relativePath = newPrefix + nodeId.relativePath.mid(oldPrefix.size());
        renames.append({oldId, newId});
      }
    };

    // Scan visible windows.
    if (m_view) {
      // We'll iterate hidden workspaces' windows and visible workspace windows
      // by first collecting all renames, then applying them.
      for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
        auto *wrapper = it.value();
        const auto &windows = wrapper->viewWindows();
        for (auto *obj : windows) {
          auto *win = qobject_cast<ViewWindow2 *>(obj);
          if (win) {
            collectRenames(win->getNodeId());
          }
        }
      }
    }

    // Apply renames.
    for (const auto &pair : renames) {
      if (m_view) {
        m_view->onNodeRenamed(pair.first, pair.second);
      }
      // Update hidden workspace windows.
      for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
        auto *wrapper = it.value();
        if (wrapper->isVisible()) {
          continue;
        }
        const auto &windows = wrapper->viewWindows();
        for (auto *obj : windows) {
          auto *win = qobject_cast<ViewWindow2 *>(obj);
          if (win && win->getNodeId() == pair.first) {
            win->onNodeRenamed(pair.second);
          }
        }
      }
    }
  }
}

void ViewAreaController::onNodeAfterDelete(const NodeOperationEvent &p_event) {
  if (p_event.notebookId.isEmpty() || p_event.relativePath.isEmpty()) {
    return;
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = p_event.notebookId;
  nodeId.relativePath = p_event.relativePath;

  // Phase 1: visible windows. Close via the standard close path (deleteLater
  // under the hood; force=true skips save prompts since the file no longer
  // exists on disk).
  if (m_view) {
    const QVector<ID> visibleIds = m_view->findWindowIdsByNode(nodeId, p_event.isFolder);
    for (ID winId : visibleIds) {
      closeViewWindow(winId, /*p_force=*/true);
    }
  }

  // Phase 2: hidden workspace windows. Mirror onNotebookBeforeClose's
  // take-filter-give-back idiom.
  const QString folderPrefix =
      p_event.isFolder ? (p_event.relativePath + QLatin1Char('/')) : QString();
  for (auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it) {
    auto *wrapper = it.value();
    if (wrapper->isVisible())
      continue;
    QVector<QObject *> all = wrapper->takeAllViewWindows();
    QVector<QObject *> remaining;
    for (auto *obj : all) {
      auto *win = qobject_cast<ViewWindow2 *>(obj);
      bool match = false;
      if (win) {
        const auto &nid = win->getNodeId();
        if (nid.notebookId == p_event.notebookId) {
          if (p_event.isFolder) {
            match = nid.relativePath.startsWith(folderPrefix);
          } else {
            match = (nid.relativePath == p_event.relativePath);
          }
        }
      }
      if (match) {
        delete win;
      } else {
        remaining.append(obj);
      }
    }
    wrapper->receiveViewWindows(remaining, 0);
  }

  emit windowsChanged();
}

void ViewAreaController::onNodeAfterMove(const NodeMoveEvent &p_event) {
  if (p_event.notebookId.isEmpty() || p_event.oldRelativePath.isEmpty() ||
      p_event.newRelativePath.isEmpty()) {
    return;
  }

  const QString notebookId = p_event.notebookId;
  const QString relativePath = p_event.oldRelativePath;
  const QString newPath = p_event.newRelativePath;
  const bool isFolder = p_event.isFolder;

  // vxcore has already updated buffer paths internally during the move.
  // We only need to update the cached NodeIdentifier on ViewWindow2 instances.

  if (!isFolder) {
    // File move: single buffer affected.
    NodeIdentifier oldNodeId;
    oldNodeId.notebookId = notebookId;
    oldNodeId.relativePath = relativePath;

    NodeIdentifier newNodeId;
    newNodeId.notebookId = notebookId;
    newNodeId.relativePath = newPath;

    // Update visible windows via the view interface.
    if (m_view) {
      m_view->onNodeRenamed(oldNodeId, newNodeId);
    }

    // Update hidden workspace windows.
    for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
      auto *wrapper = it.value();
      if (wrapper->isVisible()) {
        continue;
      }
      const auto &windows = wrapper->viewWindows();
      for (auto *obj : windows) {
        auto *win = qobject_cast<ViewWindow2 *>(obj);
        if (win && win->getNodeId() == oldNodeId) {
          win->onNodeRenamed(newNodeId);
        }
      }
    }
  } else {
    // Folder move: update all windows whose paths are under the old folder.
    QString oldPrefix = relativePath + QLatin1Char('/');
    QString newPrefix = newPath + QLatin1Char('/');

    QVector<QPair<NodeIdentifier, NodeIdentifier>> renames;

    auto collectRenames = [&](const NodeIdentifier &nodeId) {
      if (nodeId.notebookId != notebookId) {
        return;
      }
      if (nodeId.relativePath.startsWith(oldPrefix)) {
        NodeIdentifier oldId = nodeId;
        NodeIdentifier newId;
        newId.notebookId = notebookId;
        newId.relativePath = newPrefix + nodeId.relativePath.mid(oldPrefix.size());
        renames.append({oldId, newId});
      }
    };

    if (m_view) {
      for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
        auto *wrapper = it.value();
        const auto &windows = wrapper->viewWindows();
        for (auto *obj : windows) {
          auto *win = qobject_cast<ViewWindow2 *>(obj);
          if (win) {
            collectRenames(win->getNodeId());
          }
        }
      }
    }

    for (const auto &pair : renames) {
      if (m_view) {
        m_view->onNodeRenamed(pair.first, pair.second);
      }
      for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
        auto *wrapper = it.value();
        if (wrapper->isVisible()) {
          continue;
        }
        const auto &windows = wrapper->viewWindows();
        for (auto *obj : windows) {
          auto *win = qobject_cast<ViewWindow2 *>(obj);
          if (win && win->getNodeId() == pair.first) {
            win->onNodeRenamed(pair.second);
          }
        }
      }
    }
  }
}

void ViewAreaController::onEditorConfigChanged() {
  // Sync auto-save policy from config.
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto *bufferService = m_services.get<BufferService>();
  if (configMgr && bufferService) {
    bufferService->syncAutoSavePolicy(
        static_cast<int>(configMgr->getEditorConfig().getAutoSavePolicy()));
  }

  // Notify all view windows to reload their editor config.
  if (m_view) {
    m_view->notifyEditorConfigChanged();
  }
}

QString ViewAreaController::generateWorkspaceName() const {
  QSet<int> usedNumbers;
  static const QRegularExpression re(QStringLiteral("^Workspace (\\d+)$"));

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    QJsonArray workspaces = wsSvc->listWorkspaces();
    for (int i = 0; i < workspaces.size(); ++i) {
      QJsonObject wsObj = workspaces[i].toObject();
      if (wsObj.value(QLatin1String(vxcore::kJsonKeyIsPreview)).toBool()) {
        continue;
      }
      QString name = wsObj.value(QStringLiteral("name")).toString();
      QRegularExpressionMatch match = re.match(name);
      if (match.hasMatch()) {
        usedNumbers.insert(match.captured(1).toInt());
      }
    }
  }

  int n = 1;
  while (usedNumbers.contains(n)) {
    ++n;
  }
  return QStringLiteral("Workspace %1").arg(n);
}

void ViewAreaController::onNotebookBeforeClose(HookContext &p_ctx,
                                               const NotebookCloseEvent &p_event) {
  m_pendingNotebookCloseBufferIds.clear();
  for (const auto &conversion : m_noteConversions) {
    if (conversion->m_nodeId.notebookId == p_event.notebookId) {
      p_ctx.cancel();
      return;
    }
  }
  // A source notebook can own a dirty preview of another notebook, including
  // while its source tab is stored in a hidden ordinary workspace.
  if (m_view) {
    const auto previews = m_view->previewWorkspacesForSourceNotebook(p_event.notebookId);
    for (const auto &workspaceId : previews) {
      if (!closePreviewWorkspace(workspaceId)) {
        p_ctx.cancel();
        return;
      }
    }
  }

  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    return;
  }

  bool hasDirty = false;
  QJsonArray buffers = bufferSvc->listBuffers();
  for (int i = 0; i < buffers.size(); ++i) {
    QJsonObject bufObj = buffers[i].toObject();
    QString notebookId = bufObj.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
    if (notebookId.isEmpty() || notebookId != p_event.notebookId) {
      continue;
    }
    QString bufferId = bufObj.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    Buffer2 handle = bufferSvc->getBufferHandle(bufferId);
    if (handle.isValid() && handle.isModified()) {
      hasDirty = true;
      break;
    }
    m_pendingNotebookCloseBufferIds.append(bufferId);
  }

  if (hasDirty) {
    m_pendingNotebookCloseBufferIds.clear();
    p_ctx.cancel();
  }
}

void ViewAreaController::onNotebookAfterClose(const NotebookCloseEvent &p_event) {
  // Phase 1: Include preview targets even when their source is hidden.
  if (m_view) {
    QStringList targetWsIds = m_view->getVisibleWorkspaceIds();
    // Detached windows are hosted outside the main splitter tree; include their
    // workspaces so their tabs are closed too (closeViewWindow tears down an
    // emptied detached window).
    for (const auto &detWsId : m_detachedWorkspaceIds) {
      if (!targetWsIds.contains(detWsId)) {
        targetWsIds.append(detWsId);
      }
    }
    for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
      if (it.value()->isPreview() && !targetWsIds.contains(it.key())) {
        targetWsIds.append(it.key());
      }
    }
    for (const auto &bufferId : m_pendingNotebookCloseBufferIds) {
      for (const auto &wsId : targetWsIds) {
        ID winId = m_view->findWindowIdByBufferId(wsId, bufferId);
        if (winId != InvalidViewWindowId) {
          closeViewWindow(winId, true);
        }
      }
    }
  }

  // Phase 2: Close tabs in hidden workspaces.
  for (auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it) {
    auto *wrapper = it.value();
    if (wrapper->isPreview() || wrapper->isVisible()) {
      continue;
    }
    QVector<QObject *> allWindows = wrapper->takeAllViewWindows();
    QVector<QObject *> remaining;
    for (auto *obj : allWindows) {
      auto *win = qobject_cast<ViewWindow2 *>(obj);
      if (win && win->getNodeId().notebookId == p_event.notebookId) {
        delete win;
      } else {
        remaining.append(obj);
      }
    }
    wrapper->receiveViewWindows(remaining, remaining.isEmpty() ? 0 : 0);
  }

  m_pendingNotebookCloseBufferIds.clear();
  emit windowsChanged();
}

void ViewAreaController::onAppStateChanged(Qt::ApplicationState p_state) {
  if (p_state == Qt::ApplicationActive) {
    checkAllBuffersForExternalChanges();
    m_fileCheckTimer->start();
  } else {
    m_fileCheckTimer->stop();
  }
}

void ViewAreaController::onFileCheckTimerTick() { checkActiveBufferForExternalChanges(); }

void ViewAreaController::checkAllBuffersForExternalChanges() {
  if (m_fileCheckInProgress) {
    return;
  }
  m_fileCheckInProgress = true;
  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    bufferSvc->checkAllExternalChanges();
  }
  m_fileCheckInProgress = false;
}

void ViewAreaController::checkActiveBufferForExternalChanges() {
  if (m_fileCheckInProgress) {
    return;
  }
  m_fileCheckInProgress = true;

  const QString workspaceId = getCurrentWorkspaceId();
  if (!workspaceId.isEmpty() && m_view) {
    QString activeBufferId = m_view->getCurrentBufferIdForWorkspace(workspaceId);
    if (!activeBufferId.isEmpty()) {
      auto *bufferSvc = m_services.get<BufferService>();
      if (bufferSvc) {
        bufferSvc->checkSingleExternalChange(activeBufferId);
      }
    }
  }

  m_fileCheckInProgress = false;
}
