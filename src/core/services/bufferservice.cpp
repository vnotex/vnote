#include "bufferservice.h"

#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             QObject *p_parent)
    : BufferCoreService(p_context, p_parent), m_hookMgr(p_hookMgr) {
}

BufferService::~BufferService() {
}

// ============ Buffer Lifecycle (with hooks) ============

Buffer2 BufferService::openBuffer(const NodeIdentifier &p_nodeId) {
  if (m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_nodeId.notebookId;
    args[QStringLiteral("filePath")] = p_nodeId.relativePath;
    if (m_hookMgr->doAction(HookNames::FileBeforeOpen, args)) {
      return Buffer2(); // Cancelled by plugin.
    }
  }

  QString bufferId = BufferCoreService::openBuffer(p_nodeId.notebookId, p_nodeId.relativePath);

  if (bufferId.isEmpty()) {
    return Buffer2(); // Failed to open.
  }

  if (m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_nodeId.notebookId;
    args[QStringLiteral("filePath")] = p_nodeId.relativePath;
    args[QStringLiteral("bufferId")] = bufferId;
    m_hookMgr->doAction(HookNames::FileAfterOpen, args);
  }

  return Buffer2(coreService(), m_hookMgr, bufferId, p_nodeId);
}

bool BufferService::closeBuffer(const QString &p_bufferId) {
  if (m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("bufferId")] = p_bufferId;
    if (m_hookMgr->doAction(HookNames::FileBeforeClose, args)) {
      return false; // Cancelled by plugin.
    }
  }

  bool ok = BufferCoreService::closeBuffer(p_bufferId);

  if (ok && m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("bufferId")] = p_bufferId;
    m_hookMgr->doAction(HookNames::FileAfterClose, args);
  }

  return ok;
}

// ============ Pass-through methods ============

QJsonObject BufferService::getBuffer(const QString &p_bufferId) const {
  return BufferCoreService::getBuffer(p_bufferId);
}

QJsonArray BufferService::listBuffers() const {
  return BufferCoreService::listBuffers();
}

bool BufferService::autoSaveTick() {
  return BufferCoreService::autoSaveTick();
}

bool BufferService::setAutoSaveInterval(qint64 p_intervalMs) {
  return BufferCoreService::setAutoSaveInterval(p_intervalMs);
}

// ============ Core service access ============

BufferCoreService *BufferService::coreService() {
  return static_cast<BufferCoreService *>(this);
}

const BufferCoreService *BufferService::coreService() const {
  return static_cast<const BufferCoreService *>(this);
}
