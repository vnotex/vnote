#include "bufferservice.h"

#include <QDebug>
#include <QTimer>
#include <QtGlobal>

#include <core/fileopensettings.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             QObject *p_parent)
    : BufferCoreService(p_context, p_parent), m_hookMgr(p_hookMgr) {
  Q_ASSERT(m_hookMgr);

  m_autoSaveTimer = new QTimer(this);
  m_autoSaveTimer->setInterval(c_autoSaveIntervalMs);
  QObject::connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
    onAutoSaveTimerTick();
  });
}

BufferService::~BufferService() {
}

QObject *BufferService::asQObject() {
  return this;
}

void BufferService::setAutoSavePolicy(AutoSavePolicy p_policy) {
  m_autoSavePolicy = p_policy;
}

// ============ Buffer Lifecycle (with hooks) ============

Buffer2 BufferService::openBuffer(const NodeIdentifier &p_nodeId,
                                  const FileOpenSettings &p_settings) {
  qDebug() << "BufferService::openBuffer notebook:" << p_nodeId.notebookId
           << "path:" << p_nodeId.relativePath
           << "mode:" << static_cast<int>(p_settings.m_mode)
           << "readOnly:" << p_settings.m_readOnly
           << "lineNumber:" << p_settings.m_lineNumber;

  QVariantMap args = p_settings.toVariantMap();
  args[QStringLiteral("notebookId")] = p_nodeId.notebookId;
  args[QStringLiteral("filePath")] = p_nodeId.relativePath;
  if (m_hookMgr->doAction(HookNames::FileBeforeOpen, args)) {
    qDebug() << "BufferService::openBuffer cancelled by hook";
    return Buffer2(); // Cancelled by plugin.
  }

  QString bufferId = BufferCoreService::openBuffer(p_nodeId.notebookId, p_nodeId.relativePath);

  if (bufferId.isEmpty()) {
    qWarning() << "BufferService::openBuffer failed for" << p_nodeId.relativePath;
    return Buffer2(); // Failed to open.
  }

  args[QStringLiteral("bufferId")] = bufferId;
  m_hookMgr->doAction(HookNames::FileAfterOpen, args);

  qDebug() << "BufferService::openBuffer succeeded bufferId:" << bufferId;
  return Buffer2(coreService(), m_hookMgr, bufferId, p_nodeId);
}

bool BufferService::closeBuffer(const QString &p_bufferId) {
  QVariantMap args;
  args[QStringLiteral("bufferId")] = p_bufferId;
  if (m_hookMgr->doAction(HookNames::FileBeforeClose, args)) {
    return false; // Cancelled by plugin.
  }

  // Clean up auto-save state for this buffer.
  m_dirtyBuffers.remove(p_bufferId);
  m_activeWriters.remove(p_bufferId);
  m_saveFailureCounts.remove(p_bufferId);
  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }

  bool ok = BufferCoreService::closeBuffer(p_bufferId);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterClose, args);
  }

  return ok;
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

  return Buffer2(coreService(), m_hookMgr, p_bufferId, nodeId);
}

// ============ Pass-through methods ============

QJsonObject BufferService::getBuffer(const QString &p_bufferId) const {
  return BufferCoreService::getBuffer(p_bufferId);
}

QJsonArray BufferService::listBuffers() const {
  return BufferCoreService::listBuffers();
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
    } else {
      int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
      m_saveFailureCounts[p_bufferId] = failCount;
      qWarning() << "BufferService: AutoSave failed for buffer" << p_bufferId
                 << "(attempt" << failCount << ")";
      emit bufferAutoSaveFailed(p_bufferId);
      if (failCount >= c_maxSaveFailures) {
        qWarning() << "BufferService: AutoSave aborted for buffer" << p_bufferId
                   << "after" << c_maxSaveFailures << "failures";
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
      qDebug() << "BufferService: backup written for buffer" << p_bufferId
               << "at" << BufferCoreService::getBackupPath(p_bufferId);
      m_saveFailureCounts.remove(p_bufferId);
      emit bufferAutoSaved(p_bufferId);
    } else {
      int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
      m_saveFailureCounts[p_bufferId] = failCount;
      qWarning() << "BufferService: BackupFile write failed for buffer" << p_bufferId
                 << "(attempt" << failCount << ")";
      emit bufferAutoSaveFailed(p_bufferId);
      if (failCount >= c_maxSaveFailures) {
        qWarning() << "BufferService: BackupFile aborted for buffer" << p_bufferId
                   << "after" << c_maxSaveFailures << "failures";
        emit bufferAutoSaveAborted(p_bufferId);
      } else {
        m_dirtyBuffers.insert(p_bufferId);
      }
    }
    break;
  }
  }
}

// ============ Core service access ============

BufferCoreService *BufferService::coreService() {
  return static_cast<BufferCoreService *>(this);
}

const BufferCoreService *BufferService::coreService() const {
  return static_cast<const BufferCoreService *>(this);
}
