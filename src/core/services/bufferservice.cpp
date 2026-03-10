#include "bufferservice.h"

#include <QDebug>
#include <QtGlobal>

#include <core/fileopensettings.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             QObject *p_parent)
    : BufferCoreService(p_context, p_parent), m_hookMgr(p_hookMgr) {
  Q_ASSERT(m_hookMgr);
}

BufferService::~BufferService() {
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
