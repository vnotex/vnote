#include "bufferservice.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTextCodec>
#include <QTimer>
#include <QtGlobal>

#include <core/fileopensettings.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/buffersavequeue.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookiogate.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(perfSave, "vnote.perf.save")

// Resolve a codec by name, falling back to UTF-8 when the name is empty or
// unknown. Never returns null (QTextCodec::codecForName("UTF-8") always exists).
QTextCodec *resolveCodec(const QString &p_codecName) {
  if (!p_codecName.isEmpty()) {
    if (QTextCodec *codec = QTextCodec::codecForName(p_codecName.toUtf8())) {
      return codec;
    }
  }
  return QTextCodec::codecForName("UTF-8");
}
} // namespace

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             NotebookIoGate *p_ioGate, AutoSavePolicy p_autoSavePolicy,
                             QObject *p_parent)
    : BufferCoreService(p_context, p_parent), m_hookMgr(p_hookMgr), m_ioGate(p_ioGate),
      m_autoSavePolicy(p_autoSavePolicy) {
  Q_ASSERT(m_hookMgr);
  Q_ASSERT(m_ioGate);

  m_autoSaveTimer = new QTimer(this);
  m_autoSaveTimer->setInterval(c_autoSaveIntervalMs);
  QObject::connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() { onAutoSaveTimerTick(); });

  // T7: BufferSaveQueue dispatches auto-save off the UI thread, serializing
  // per (notebookId, bufferId) and acquiring NotebookIoGate per notebook.
  m_saveQueue = new BufferSaveQueue(*this, *m_ioGate, this);
  QObject::connect(
      m_saveQueue, &BufferSaveQueue::saveFinished, asQObject(),
      [this](const QString &p_bufferId, quint64 p_revision, bool p_ok, const QString &p_errorMsg) {
        onSaveFinished(p_bufferId, p_revision, p_ok, p_errorMsg);
      },
      Qt::QueuedConnection);
  // T28: forward the read-only rejection signal to consumers via this
  // BufferService instance so UI listeners (ViewWindow2 modal warning) can
  // subscribe without reaching into the private m_saveQueue. The queue emits
  // directly on the UI thread inside enqueue() (per its threading contract),
  // so a DirectConnection is correct here — keeps the modal trigger
  // synchronous with the rejected enqueue call. NOTE: BufferService inherits
  // QObject privately (via BufferCoreService), so we route through a lambda
  // bound to asQObject() and forward by emitting via the PMF — the lambda
  // captures `this` to access the protected emit hook.
  QObject::connect(
      m_saveQueue, &BufferSaveQueue::saveRejectedReadOnly, asQObject(),
      [this](const QString &p_bufferId) { emit saveRejectedReadOnly(p_bufferId); },
      Qt::DirectConnection);
}

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             AutoSavePolicy p_autoSavePolicy, QObject *p_parent)
    : BufferService(p_context, p_hookMgr, new NotebookIoGate(), p_autoSavePolicy, p_parent) {
  // Re-tag the gate we just created as owned so the dtor cleans it up.
  m_ownedIoGate = m_ioGate;
}

BufferService::~BufferService() {
  // Safety net — idempotent. The aboutToQuit handler should have called this already.
  if (m_saveQueue) {
    m_saveQueue->shutdown(5000);
  }
  delete m_ownedIoGate;
  m_ownedIoGate = nullptr;
}

bool BufferService::shutdown(int p_timeoutMs) {
  return m_saveQueue ? m_saveQueue->shutdown(p_timeoutMs) : true;
}

QObject *BufferService::asQObject() { return this; }

void BufferService::setAutoSavePolicy(AutoSavePolicy p_policy) { m_autoSavePolicy = p_policy; }

void BufferService::syncAutoSavePolicy(int p_configPolicy) {
  // Map EditorConfig::AutoSavePolicy (0=None, 1=AutoSave, 2=BackupFile)
  // to BufferService::AutoSavePolicy.
  AutoSavePolicy policy = AutoSavePolicy::AutoSave;
  if (p_configPolicy == 0) {
    policy = AutoSavePolicy::None;
  } else if (p_configPolicy == 2) {
    policy = AutoSavePolicy::BackupFile;
  }
  setAutoSavePolicy(policy);
}

// ============ Buffer Lifecycle (with hooks) ============

Buffer2 BufferService::openBuffer(const NodeIdentifier &p_nodeId,
                                  const FileOpenSettings &p_settings, VxCoreError *p_outErr) {
  if (p_outErr) {
    *p_outErr = VXCORE_OK;
  }
  qDebug() << "BufferService::openBuffer notebook:" << p_nodeId.notebookId
           << "path:" << p_nodeId.relativePath << "mode:" << static_cast<int>(p_settings.m_mode)
           << "readOnly:" << p_settings.m_readOnly << "lineNumber:" << p_settings.m_lineNumber;

  FileOpenEvent event;
  event.notebookId = p_nodeId.notebookId;
  event.filePath = p_nodeId.relativePath;
  event.mode = static_cast<int>(p_settings.m_mode);
  event.forceMode = p_settings.m_forceMode;
  event.focus = p_settings.m_focus;
  event.newFile = p_settings.m_newFile;
  event.readOnly = p_settings.m_readOnly;
  event.lineNumber = p_settings.m_lineNumber;
  event.cursorOffset = p_settings.m_cursorOffset;
  event.anchor = p_settings.m_anchor;
  event.alwaysNewWindow = p_settings.m_alwaysNewWindow;
  if (p_settings.m_searchHighlight.m_isValid) {
    event.searchPatterns = p_settings.m_searchHighlight.m_patterns;
    event.searchOptions = static_cast<int>(p_settings.m_searchHighlight.m_options);
    event.searchCurrentMatchLine = p_settings.m_searchHighlight.m_currentMatchLine;
  }
  if (m_hookMgr->doAction(HookNames::FileBeforeOpen, event)) {
    qDebug() << "BufferService::openBuffer cancelled by hook";
    return Buffer2(); // Cancelled by plugin.
  }

  QString bufferId =
      BufferCoreService::openBuffer(p_nodeId.notebookId, p_nodeId.relativePath, p_outErr);

  if (bufferId.isEmpty()) {
    qWarning() << "BufferService::openBuffer failed for" << p_nodeId.relativePath;
    return Buffer2(); // Failed to open.
  }

  event.bufferId = bufferId;
  m_hookMgr->doAction(HookNames::FileAfterOpen, event);

  // Honor the per-open read-only override: the notebook may be writable, but a
  // buffer explicitly opened read-only (e.g. "View Logs") must stay read-only.
  if (p_settings.m_readOnly) {
    m_forcedReadOnlyBuffers.insert(bufferId);
  }

  qDebug() << "BufferService::openBuffer succeeded bufferId:" << bufferId;
  return Buffer2(this, m_hookMgr, bufferId, p_nodeId);
}

Buffer2 BufferService::openBufferByNodeId(const QString &p_nodeId,
                                          const FileOpenSettings &p_settings) {
  // Step 1: Resolve UUID to notebookId + relativePath (NO buffer opened here).
  QString notebookId;
  QString relativePath;
  if (!BufferCoreService::resolveNodeId(p_nodeId, notebookId, relativePath)) {
    qDebug() << "BufferService::openBufferByNodeId: UUID not found:" << p_nodeId;
    return Buffer2();
  }

  // Step 2: Delegate to existing openBuffer() which fires FileBeforeOpen BEFORE
  // vxcore_buffer_open — preserving the hook-before-open contract.
  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = relativePath;
  return openBuffer(nodeId, p_settings);
}

Buffer2 BufferService::openVirtualBuffer(const QString &p_address) {
  qDebug() << "BufferService::openVirtualBuffer address:" << p_address;

  // Call vxcore to open (or dedup) the virtual buffer.
  QString bufferId = BufferCoreService::openVirtualBuffer(p_address);

  if (bufferId.isEmpty()) {
    qWarning() << "BufferService::openVirtualBuffer failed for" << p_address;
    return Buffer2();
  }

  // Track as virtual for auto-save skip.
  m_virtualBufferIds.insert(bufferId);

  // Construct NodeIdentifier with empty notebookId and address as path.
  NodeIdentifier nodeId;
  nodeId.notebookId = QString();
  nodeId.relativePath = p_address;

  // NO hooks fired — virtual buffers are not files.

  qDebug() << "BufferService::openVirtualBuffer succeeded bufferId:" << bufferId;
  return Buffer2(this, m_hookMgr, bufferId, nodeId);
}

bool BufferService::closeBuffer(const QString &p_bufferId) {
  // Clean up auto-save state for this buffer.
  m_dirtyBuffers.remove(p_bufferId);
  m_activeWriters.remove(p_bufferId);
  m_saveFailureCounts.remove(p_bufferId);
  m_virtualBufferIds.remove(p_bufferId);
  m_forcedReadOnlyBuffers.remove(p_bufferId);
  m_revisions.remove(p_bufferId);
  m_bufferEncodings.remove(p_bufferId);
  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }

  return BufferCoreService::closeBuffer(p_bufferId);
}

// ============ Per-Buffer Encoding ============

QString BufferService::bufferEncoding(const QString &p_bufferId) const {
  return m_bufferEncodings.value(p_bufferId, QStringLiteral("UTF-8"));
}

void BufferService::setBufferEncoding(const QString &p_bufferId, const QString &p_codecName) {
  if (p_codecName.isEmpty()) {
    m_bufferEncodings.remove(p_bufferId);
  } else {
    m_bufferEncodings.insert(p_bufferId, p_codecName);
  }
}

QByteArray BufferService::encodeContent(const QString &p_bufferId, const QString &p_text) const {
  // Fast path: no override → plain UTF-8, identical to the pre-encoding code
  // path (no codec-registry lookup, no virtual dispatch).
  auto it = m_bufferEncodings.constFind(p_bufferId);
  if (it == m_bufferEncodings.constEnd()) {
    return p_text.toUtf8();
  }
  return resolveCodec(it.value())->fromUnicode(p_text);
}

QString BufferService::decodeContent(const QString &p_bufferId,
                                     const QByteArrayViewCompat &p_raw) const {
  auto it = m_bufferEncodings.constFind(p_bufferId);
  if (it == m_bufferEncodings.constEnd()) {
    return QString::fromUtf8(p_raw);
  }
  return resolveCodec(it.value())->toUnicode(p_raw.data(), static_cast<int>(p_raw.size()));
}

// ============ Buffer Handle ============

Buffer2 BufferService::getBufferHandle(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return Buffer2();
  }

  // Query vxcore for the buffer info to construct NodeIdentifier.
  QJsonObject bufJson = BufferCoreService::getBuffer(p_bufferId);
  if (bufJson.isEmpty()) {
    qWarning() << "BufferService::getBufferHandle: buffer not found for" << p_bufferId;
    return Buffer2();
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = bufJson.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  nodeId.relativePath = bufJson.value(QStringLiteral("filePath")).toString();

  return Buffer2(this, m_hookMgr, p_bufferId, nodeId);
}

// ============ Pass-through methods ============

bool BufferService::isNotebookBundled(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return false;
  }
  char *json = nullptr;
  VxCoreError err = vxcore_notebook_get_config(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    return false;
  }
  QJsonObject config = parseJsonObjectFromCStr(json);
  return config.value(QLatin1String(vxcore::kJsonKeyType))
             .toString()
             .compare(QStringLiteral("bundled"), Qt::CaseInsensitive) == 0;
}

QJsonObject BufferService::getBuffer(const QString &p_bufferId) const {
  return BufferCoreService::getBuffer(p_bufferId);
}

bool BufferService::isVirtualBuffer(const QString &p_bufferId) const {
  return m_virtualBufferIds.contains(p_bufferId);
}

QJsonArray BufferService::listBuffers() const { return BufferCoreService::listBuffers(); }

bool BufferService::isBufferReadOnly(const QString &p_bufferId) const {
  // Per-open override (FileOpenSettings::m_readOnly) OR the notebook's read-only flag.
  if (m_forcedReadOnlyBuffers.contains(p_bufferId)) {
    return true;
  }
  return BufferCoreService::isBufferReadOnly(p_bufferId);
}

// ============ BufferCoreService wrappers ============

bool BufferService::saveBuffer(const QString &p_bufferId) {
  // Read-only buffers must never reach vxcore_buffer_save (which returns
  // "Notebook is read-only" and surfaces a generic save failure). Mirrors the
  // markDirty / BufferSaveQueue::enqueue guards. UI consumes saveRejectedReadOnly
  // via ViewWindow2::showReadOnlyWarning.
  if (isBufferReadOnly(p_bufferId)) {
    qWarning() << "BufferService::saveBuffer rejected: buffer is read-only" << p_bufferId;
    emit saveRejectedReadOnly(p_bufferId);
    return false;
  }
  bool ok = BufferCoreService::saveBuffer(p_bufferId);
  emit bufferModifiedChanged(p_bufferId);
  return ok;
}

bool BufferService::reloadBuffer(const QString &p_bufferId) {
  // Fire FileBeforeReload hook (cancellable).
  BufferEvent event;
  event.bufferId = p_bufferId;
  if (m_hookMgr->doAction(HookNames::FileBeforeReload, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = BufferCoreService::reloadBuffer(p_bufferId);

  if (ok) {
    // Fire FileAfterReload hook (informational).
    m_hookMgr->doAction(HookNames::FileAfterReload, event);
  }

  emit bufferModifiedChanged(p_bufferId);
  return ok;
}

QStringList BufferService::checkAllExternalChanges() {
  QStringList changedBufferIds;
  QJsonArray buffers = BufferCoreService::listBuffers();
  for (const auto &bufferVal : buffers) {
    QJsonObject bufObj = bufferVal.toObject();
    QString bufferId = bufObj.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (bufferId.isEmpty()) {
      continue;
    }

    // Skip virtual buffers.
    bool isVirtual = bufObj.value(QStringLiteral("isVirtual")).toBool();
    if (isVirtual) {
      continue;
    }

    // Skip while VNote is itself writing this buffer's file on a worker thread
    // (per-buffer gate applied to the full sweep so background tabs do not
    // false-positive against their own in-flight save). See
    // checkSingleExternalChange for the rationale.
    if (m_saveQueue) {
      const QString notebookId = bufObj.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
      if (m_saveQueue->isBusy(notebookId, bufferId)) {
        continue;
      }
    }

    // Check for external changes.
    if (!BufferCoreService::checkExternalChanges(bufferId)) {
      continue;
    }

    // Query the updated state.
    BufferState state = BufferCoreService::getState(bufferId);
    if (state == BufferState::FileChanged || state == BufferState::FileMissing) {
      // Fire FileExternalChange hook.
      FileExternalChangeEvent event;
      event.bufferId = bufferId;
      event.filePath = bufObj.value(QStringLiteral("filePath")).toString();
      event.state = static_cast<int>(state);
      m_hookMgr->doAction(HookNames::FileExternalChange, event);

      // Emit signal for UI layer.
      emit bufferExternallyChanged(bufferId, state);
      changedBufferIds.append(bufferId);
    }
  }

  return changedBufferIds;
}

bool BufferService::checkSingleExternalChange(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return false;
  }

  // Skip virtual buffers.
  if (isVirtualBuffer(p_bufferId)) {
    return false;
  }

  // Skip while VNote is itself writing this buffer's file on a worker thread.
  // A check that lands mid-self-write would read the new on-disk mtime before
  // BufferSaveQueue's worker re-stamps Buffer::last_modified_time_, producing a
  // false-positive "modified outside VNote". Once the save drains, the stamp
  // matches disk and a later check stays NORMAL.
  if (m_saveQueue) {
    const QString notebookId =
        getBuffer(p_bufferId).value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
    if (m_saveQueue->isBusy(notebookId, p_bufferId)) {
      return false;
    }
  }

  // Check for external changes via vxcore.
  if (!BufferCoreService::checkExternalChanges(p_bufferId)) {
    return false;
  }

  // Query the updated state.
  BufferState state = BufferCoreService::getState(p_bufferId);
  if (state == BufferState::FileChanged || state == BufferState::FileMissing) {
    // Fire FileExternalChange hook.
    FileExternalChangeEvent event;
    event.bufferId = p_bufferId;
    QJsonObject bufObj = BufferCoreService::getBuffer(p_bufferId);
    event.filePath = bufObj.value(QStringLiteral("filePath")).toString();
    event.state = static_cast<int>(state);
    m_hookMgr->doAction(HookNames::FileExternalChange, event);

    // Emit signal for UI layer.
    emit bufferExternallyChanged(p_bufferId, state);
    return true;
  }

  return false;
}

QString BufferService::insertAttachment(const QString &p_bufferId, const QString &p_sourcePath) {
  AttachmentAddEvent event;
  event.bufferId = p_bufferId;
  event.sourcePath = p_sourcePath;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeAdd, event)) {
    return QString(); // Cancelled by plugin.
  }

  QString filename = BufferCoreService::insertAttachment(p_bufferId, p_sourcePath);

  if (!filename.isEmpty()) {
    event.filename = filename;
    m_hookMgr->doAction(HookNames::AttachmentAfterAdd, event);
    emit attachmentChanged(p_bufferId);
  }

  return filename;
}

bool BufferService::deleteAttachment(const QString &p_bufferId, const QString &p_filename) {
  AttachmentDeleteEvent event;
  event.bufferId = p_bufferId;
  event.filename = p_filename;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeDelete, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = BufferCoreService::deleteAttachment(p_bufferId, p_filename);

  if (ok) {
    m_hookMgr->doAction(HookNames::AttachmentAfterDelete, event);
    emit attachmentChanged(p_bufferId);
  }

  return ok;
}

QString BufferService::renameAttachment(const QString &p_bufferId, const QString &p_oldFilename,
                                        const QString &p_newFilename) {
  AttachmentRenameEvent event;
  event.bufferId = p_bufferId;
  event.oldFilename = p_oldFilename;
  event.newFilename = p_newFilename;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeRename, event)) {
    return QString(); // Cancelled by plugin.
  }

  QString actualName =
      BufferCoreService::renameAttachment(p_bufferId, p_oldFilename, p_newFilename);

  if (!actualName.isEmpty()) {
    event.newFilename = actualName;
    m_hookMgr->doAction(HookNames::AttachmentAfterRename, event);
    emit attachmentChanged(p_bufferId);
  }

  return actualName;
}

// ============ Auto-Save & Dirty Tracking ============

void BufferService::markDirty(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return;
  }

  // Guard: read-only buffers must never enter the dirty set (T16).
  // Notebook RO state is immutable for the notebook's lifetime under the
  // current design (no live RO transition), so checking once at the entry
  // point is sufficient. Emits dirtyRejectedReadOnly so the UI orchestration
  // layer (T28 modal warning) can warn the user. Does NOT clear an existing
  // dirty flag — under the no-live-transition rule a buffer cannot legally
  // be dirty before becoming read-only, but if it ever were, we refuse to
  // silently drop the unsaved edit.
  if (isBufferReadOnly(p_bufferId)) {
    qWarning() << "BufferService::markDirty rejected: buffer is read-only" << p_bufferId;
    emit dirtyRejectedReadOnly(p_bufferId);
    return;
  }

  m_dirtyBuffers.insert(p_bufferId);
  // Atomic with the auto-save trigger: bump the revision so the snapshot
  // captured by the next save tick is uniquely identifiable.
  ++m_revisions[p_bufferId].m_revision;
  if (!m_autoSaveTimer->isActive()) {
    m_autoSaveTimer->start();
  }
}

// ============ Snapshot Revision Tracking (T6) ============

quint64 BufferService::currentRevision(const QString &p_bufferId) const {
  auto it = m_revisions.constFind(p_bufferId);
  return it == m_revisions.constEnd() ? 0 : it->m_revision;
}

quint64 BufferService::lastSavedRevision(const QString &p_bufferId) const {
  auto it = m_revisions.constFind(p_bufferId);
  return it == m_revisions.constEnd() ? 0 : it->m_lastSavedRevision;
}

void BufferService::markRevisionSaved(const QString &p_bufferId, quint64 p_revision) {
  if (p_bufferId.isEmpty()) {
    return;
  }
  auto it = m_revisions.find(p_bufferId);
  if (it == m_revisions.end()) {
    return;
  }
  // Stale completion: a newer save already advanced lastSavedRevision past this.
  if (p_revision <= it->m_lastSavedRevision) {
    return;
  }
  it->m_lastSavedRevision = p_revision;
  // Only clear dirty if the latest persisted revision matches the latest edit.
  // Otherwise newer edits exist (rev > p_revision) and the buffer stays dirty.
  if (it->m_lastSavedRevision == it->m_revision) {
    m_dirtyBuffers.remove(p_bufferId);
    if (m_dirtyBuffers.isEmpty()) {
      m_autoSaveTimer->stop();
    }
  }
}

bool BufferService::isDirty(const QString &p_bufferId) const {
  return m_dirtyBuffers.contains(p_bufferId);
}

void BufferService::syncNow(const QString &p_bufferId) {
  if (!m_dirtyBuffers.contains(p_bufferId)) {
    return;
  }

  // Virtual buffers have no file content to sync.
  if (m_virtualBufferIds.contains(p_bufferId)) {
    m_dirtyBuffers.remove(p_bufferId);
    if (m_dirtyBuffers.isEmpty()) {
      m_autoSaveTimer->stop();
    }
    return;
  }

  executeSyncForBuffer(p_bufferId);
  m_dirtyBuffers.remove(p_bufferId);

  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }
}

void BufferService::registerActiveWriter(const QString &p_bufferId, quintptr p_writerKey,
                                         ContentFetchCallback p_callback) {
  if (p_bufferId.isEmpty() || !p_callback) {
    return;
  }
  m_activeWriters[p_bufferId] = ActiveWriter{p_writerKey, std::move(p_callback)};
}

void BufferService::unregisterActiveWriter(const QString &p_bufferId, quintptr p_writerKey) {
  if (p_bufferId.isEmpty()) {
    return;
  }
  // Only unregister if this writer is the current active writer.
  auto it = m_activeWriters.find(p_bufferId);
  if (it != m_activeWriters.end() && it->key == p_writerKey) {
    m_activeWriters.erase(it);
  }
}

void BufferService::onAutoSaveTimerTick() {
  QElapsedTimer timer;
  timer.start();

  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
    return;
  }

  // Copy the set to iterate safely (executeSyncForBuffer may modify it indirectly).
  const auto dirtyBuffersCopy = m_dirtyBuffers;
  m_dirtyBuffers.clear();

  for (const auto &bufferId : dirtyBuffersCopy) {
    executeSyncForBuffer(bufferId);
  }

  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }

  qint64 elapsed = timer.elapsed();
  qCDebug(perfSave) << "[perf.save] tick_ms=" << elapsed;
}

void BufferService::executeSyncForBuffer(const QString &p_bufferId) {
  QElapsedTimer timer;
  timer.start();

  // Virtual buffers have no file content to sync.
  if (m_virtualBufferIds.contains(p_bufferId)) {
    return;
  }

  auto it = m_activeWriters.find(p_bufferId);
  if (it == m_activeWriters.end() || !it->callback) {
    // No active writer — content already synced from previous focus-loss.
    return;
  }

  // Skip auto-save disk write for externally changed/missing files.
  // (Inline check before snapshot — cheap.)
  BufferState state = BufferCoreService::getState(p_bufferId);
  if (state == BufferState::FileChanged || state == BufferState::FileMissing) {
    return;
  }

  // Fetch latest editor content as a QString snapshot — UI thread only.
  QString content = it->callback();

  // Capture the revision tied to this snapshot (T6). markRevisionSaved on
  // completion advances lastSavedRevision and clears dirty iff it catches up
  // to current.
  const quint64 capturedRev = currentRevision(p_bufferId);

  switch (m_autoSavePolicy) {
  case AutoSavePolicy::None: {
    // Sync to in-memory vxcore buffer only — no disk write, fast.
    if (BufferCoreService::setContentRaw(p_bufferId, encodeContent(p_bufferId, content))) {
      emit bufferContentSynced(p_bufferId);
      emit bufferModifiedChanged(p_bufferId);
    } else {
      qWarning() << "BufferService: failed to set content for buffer" << p_bufferId;
    }
    break;
  }

  case AutoSavePolicy::AutoSave: {
    // T7: dispatch off UI thread via BufferSaveQueue.
    // Resolve notebookId for gate keying.
    QJsonObject bufJson = BufferCoreService::getBuffer(p_bufferId);
    QString notebookId = bufJson.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();

    // Emit content-synced eagerly: the snapshot is committed to the queue and
    // the editor view can drop any "syncing" hint. The actual disk write
    // completes asynchronously via onSaveFinished.
    emit bufferContentSynced(p_bufferId);
    // Pass the raw override (empty when none) so the worker's UTF-8 fast path
    // is used for buffers without an encoding override.
    m_saveQueue->enqueue(notebookId, p_bufferId, content, capturedRev,
                         m_bufferEncodings.value(p_bufferId));
    break;
  }

  case AutoSavePolicy::BackupFile: {
    // Backup file does NOT contend with libgit2 (writes to .vswp, not the working tree)
    // and so remains inline. Still cheap (in-memory + sibling-file write).
    if (!BufferCoreService::setContentRaw(p_bufferId, encodeContent(p_bufferId, content))) {
      qWarning() << "BufferService: failed to set content for buffer" << p_bufferId;
      break;
    }
    emit bufferContentSynced(p_bufferId);
    emit bufferModifiedChanged(p_bufferId);

    bool backupOk = BufferCoreService::writeBackup(p_bufferId);
    if (backupOk) {
      qDebug() << "BufferService: backup written for buffer" << p_bufferId << "at"
               << BufferCoreService::getBackupPath(p_bufferId);
      m_saveFailureCounts.remove(p_bufferId);
      emit bufferAutoSaved(p_bufferId);
      emit bufferModifiedChanged(p_bufferId);
    } else {
      int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
      m_saveFailureCounts[p_bufferId] = failCount;
      qWarning() << "BufferService: BackupFile write failed for buffer" << p_bufferId << "(attempt"
                 << failCount << ")";
      emit bufferAutoSaveFailed(p_bufferId);
      if (failCount >= c_maxSaveFailures) {
        qWarning() << "BufferService: BackupFile aborted for buffer" << p_bufferId << "after"
                   << c_maxSaveFailures << "failures";
        emit bufferAutoSaveAborted(p_bufferId);
      } else {
        m_dirtyBuffers.insert(p_bufferId);
      }
    }
    break;
  }
  }

  qint64 elapsed = timer.elapsed();
  qCDebug(perfSave) << "[perf.save] execute_ms=" << elapsed;
}

void BufferService::onSaveFinished(const QString &p_bufferId, quint64 p_revision, bool p_ok,
                                   const QString &p_errorMsg) {
  if (p_ok) {
    // T6: advances lastSavedRevision and clears dirty iff lastSaved == current.
    markRevisionSaved(p_bufferId, p_revision);
    m_saveFailureCounts.remove(p_bufferId);
    emit bufferAutoSaved(p_bufferId);
    emit bufferModifiedChanged(p_bufferId);
  } else {
    int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
    m_saveFailureCounts[p_bufferId] = failCount;
    qWarning() << "BufferService: AutoSave failed for buffer" << p_bufferId << "(attempt"
               << failCount << "):" << p_errorMsg;
    emit saveError(p_bufferId, p_errorMsg);
    emit bufferAutoSaveFailed(p_bufferId);
    if (failCount >= c_maxSaveFailures) {
      qWarning() << "BufferService: AutoSave aborted for buffer" << p_bufferId << "after"
                 << c_maxSaveFailures << "failures";
      emit bufferAutoSaveAborted(p_bufferId);
    } else {
      // Re-mark dirty so the next timer tick retries.
      m_dirtyBuffers.insert(p_bufferId);
      if (!m_autoSaveTimer->isActive()) {
        m_autoSaveTimer->start();
      }
    }
  }
}
