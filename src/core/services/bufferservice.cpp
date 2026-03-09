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

QString BufferService::openBuffer(const QString &p_notebookId, const QString &p_filePath) {
  if (m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("filePath")] = p_filePath;
    if (m_hookMgr->doAction(HookNames::FileBeforeOpen, args)) {
      return QString(); // Cancelled by plugin.
    }
  }

  QString bufferId = BufferCoreService::openBuffer(p_notebookId, p_filePath);

  if (!bufferId.isEmpty() && m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("notebookId")] = p_notebookId;
    args[QStringLiteral("filePath")] = p_filePath;
    args[QStringLiteral("bufferId")] = bufferId;
    m_hookMgr->doAction(HookNames::FileAfterOpen, args);
  }

  return bufferId;
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

bool BufferService::saveBuffer(const QString &p_bufferId) {
  if (m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("bufferId")] = p_bufferId;
    if (m_hookMgr->doAction(HookNames::FileBeforeSave, args)) {
      return false; // Cancelled by plugin.
    }
  }

  bool ok = BufferCoreService::saveBuffer(p_bufferId);

  if (ok && m_hookMgr) {
    QVariantMap args;
    args[QStringLiteral("bufferId")] = p_bufferId;
    m_hookMgr->doAction(HookNames::FileAfterSave, args);
  }

  return ok;
}

// ============ Pass-through methods ============

bool BufferService::reloadBuffer(const QString &p_bufferId) {
  return BufferCoreService::reloadBuffer(p_bufferId);
}

QJsonObject BufferService::getBuffer(const QString &p_bufferId) const {
  return BufferCoreService::getBuffer(p_bufferId);
}

QJsonArray BufferService::listBuffers() const {
  return BufferCoreService::listBuffers();
}

QJsonObject BufferService::getContent(const QString &p_bufferId) const {
  return BufferCoreService::getContent(p_bufferId);
}

bool BufferService::setContent(const QString &p_bufferId, const QString &p_contentJson) {
  return BufferCoreService::setContent(p_bufferId, p_contentJson);
}

QByteArray BufferService::getContentRaw(const QString &p_bufferId) const {
  return BufferCoreService::getContentRaw(p_bufferId);
}

bool BufferService::setContentRaw(const QString &p_bufferId, const QByteArray &p_data) {
  return BufferCoreService::setContentRaw(p_bufferId, p_data);
}

BufferState BufferService::getState(const QString &p_bufferId) const {
  return BufferCoreService::getState(p_bufferId);
}

bool BufferService::isModified(const QString &p_bufferId) const {
  return BufferCoreService::isModified(p_bufferId);
}

bool BufferService::autoSaveTick() {
  return BufferCoreService::autoSaveTick();
}

bool BufferService::setAutoSaveInterval(qint64 p_intervalMs) {
  return BufferCoreService::setAutoSaveInterval(p_intervalMs);
}

QString BufferService::insertAssetRaw(const QString &p_bufferId, const QString &p_assetName,
                                      const QByteArray &p_data) {
  return BufferCoreService::insertAssetRaw(p_bufferId, p_assetName, p_data);
}

QString BufferService::insertAsset(const QString &p_bufferId, const QString &p_sourcePath) {
  return BufferCoreService::insertAsset(p_bufferId, p_sourcePath);
}

bool BufferService::deleteAsset(const QString &p_bufferId, const QString &p_relativePath) {
  return BufferCoreService::deleteAsset(p_bufferId, p_relativePath);
}

QString BufferService::getAssetsFolder(const QString &p_bufferId) const {
  return BufferCoreService::getAssetsFolder(p_bufferId);
}

QString BufferService::insertAttachment(const QString &p_bufferId, const QString &p_sourcePath) {
  return BufferCoreService::insertAttachment(p_bufferId, p_sourcePath);
}

bool BufferService::deleteAttachment(const QString &p_bufferId, const QString &p_filename) {
  return BufferCoreService::deleteAttachment(p_bufferId, p_filename);
}

QString BufferService::renameAttachment(const QString &p_bufferId, const QString &p_oldFilename,
                                        const QString &p_newFilename) {
  return BufferCoreService::renameAttachment(p_bufferId, p_oldFilename, p_newFilename);
}

QJsonArray BufferService::listAttachments(const QString &p_bufferId) const {
  return BufferCoreService::listAttachments(p_bufferId);
}

QString BufferService::getAttachmentsFolder(const QString &p_bufferId) const {
  return BufferCoreService::getAttachmentsFolder(p_bufferId);
}
