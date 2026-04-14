#include "bufferservice.h"

#include <QDebug>
#include <QTimer>
#include <QtGlobal>

#include <core/fileopensettings.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             AutoSavePolicy p_autoSavePolicy, QObject *p_parent)
    : BufferCoreService(p_context, p_parent), m_hookMgr(p_hookMgr),
      m_autoSavePolicy(p_autoSavePolicy) {
  Q_ASSERT(m_hookMgr);

  m_autoSaveTimer = new QTimer(this);
  m_autoSaveTimer->setInterval(c_autoSaveIntervalMs);
  QObject::connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() { onAutoSaveTimerTick(); });
}

BufferService::~BufferService() {}

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
                                  const FileOpenSettings &p_settings) {
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

  QString bufferId = BufferCoreService::openBuffer(p_nodeId.notebookId, p_nodeId.relativePath);

  if (bufferId.isEmpty()) {
    qWarning() << "BufferService::openBuffer failed for" << p_nodeId.relativePath;
    return Buffer2(); // Failed to open.
  }

  event.bufferId = bufferId;
  m_hookMgr->doAction(HookNames::FileAfterOpen, event);

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
  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }

  return BufferCoreService::closeBuffer(p_bufferId);
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
  nodeId.notebookId = bufJson.value(QStringLiteral("notebookId")).toString();
  nodeId.relativePath = bufJson.value(QStringLiteral("filePath")).toString();

  return Buffer2(this, m_hookMgr, p_bufferId, nodeId);
}

// ============ Pass-through methods ============

QJsonObject BufferService::getBuffer(const QString &p_bufferId) const {
  return BufferCoreService::getBuffer(p_bufferId);
}

bool BufferService::isVirtualBuffer(const QString &p_bufferId) const {
  return m_virtualBufferIds.contains(p_bufferId);
}

QJsonArray BufferService::listBuffers() const { return BufferCoreService::listBuffers(); }

// ============ BufferCoreService wrappers ============

bool BufferService::saveBuffer(const QString &p_bufferId) {
  bool ok = BufferCoreService::saveBuffer(p_bufferId);
  emit bufferModifiedChanged(p_bufferId);
  return ok;
}

bool BufferService::reloadBuffer(const QString &p_bufferId) {
  bool ok = BufferCoreService::reloadBuffer(p_bufferId);
  emit bufferModifiedChanged(p_bufferId);
  return ok;
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

  // Guard: read-only buffers should not be marked dirty.
  if (BufferCoreService::isModified(p_bufferId)) {
    // isModified doesn't tell us read-only, but if we can't even get the buffer, skip.
  }

  // Check read-only via buffer JSON (lightweight check).
  // Buffer2::isReadOnly() is not available here, but vxcore buffers don't have a read-only flag
  // in the C API. The read-only check is done at the ViewWindow2 level before calling markDirty.

  m_dirtyBuffers.insert(p_bufferId);
  if (!m_autoSaveTimer->isActive()) {
    m_autoSaveTimer->start();
  }
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
}

void BufferService::executeSyncForBuffer(const QString &p_bufferId) {
  // Virtual buffers have no file content to sync.
  if (m_virtualBufferIds.contains(p_bufferId)) {
    return;
  }

  auto it = m_activeWriters.find(p_bufferId);
  if (it == m_activeWriters.end() || !it->callback) {
    // No active writer — content already synced from previous focus-loss.
    return;
  }

  // Fetch content from editor via callback and write to vxcore buffer.
  QString content = it->callback();
  QByteArray contentBytes = content.toUtf8();
  bool setOk = BufferCoreService::setContentRaw(p_bufferId, contentBytes);
  if (!setOk) {
    qWarning() << "BufferService: failed to set content for buffer" << p_bufferId;
    return;
  }

  emit bufferContentSynced(p_bufferId);
  emit bufferModifiedChanged(p_bufferId);

  // Execute auto-save policy.
  switch (m_autoSavePolicy) {
  case AutoSavePolicy::None:
    // Content synced to vxcore buffer only — no disk write.
    break;

  case AutoSavePolicy::AutoSave: {
    bool saveOk = BufferCoreService::saveBuffer(p_bufferId);
    if (saveOk) {
      m_saveFailureCounts.remove(p_bufferId);
      emit bufferAutoSaved(p_bufferId);
      emit bufferModifiedChanged(p_bufferId);
    } else {
      int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
      m_saveFailureCounts[p_bufferId] = failCount;
      qWarning() << "BufferService: AutoSave failed for buffer" << p_bufferId << "(attempt"
                 << failCount << ")";
      emit bufferAutoSaveFailed(p_bufferId);
      if (failCount >= c_maxSaveFailures) {
        qWarning() << "BufferService: AutoSave aborted for buffer" << p_bufferId << "after"
                   << c_maxSaveFailures << "failures";
        emit bufferAutoSaveAborted(p_bufferId);
      } else {
        // Keep in dirty set for retry on next tick.
        m_dirtyBuffers.insert(p_bufferId);
      }
    }
    break;
  }

  case AutoSavePolicy::BackupFile: {
    // Use vxcore's built-in backup mechanism via BufferCoreService.
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
}
