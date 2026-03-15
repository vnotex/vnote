#include "buffer2.h"

#include <QtGlobal>

#include <core/hooknames.h>
#include <core/services/buffercoreservice.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

Buffer2::Buffer2()
    : m_bufferCoreService(nullptr), m_hookMgr(nullptr) {
}

Buffer2::Buffer2(BufferCoreService *p_bufferCoreService, HookManager *p_hookMgr,
                 const QString &p_bufferId, const NodeIdentifier &p_nodeId)
    : m_bufferCoreService(p_bufferCoreService),
      m_hookMgr(p_hookMgr),
      m_bufferId(p_bufferId),
      m_nodeId(p_nodeId) {
  Q_ASSERT(m_bufferCoreService);
  Q_ASSERT(m_hookMgr);
}

bool Buffer2::isValid() const {
  return m_bufferCoreService && !m_bufferId.isEmpty();
}

QString Buffer2::id() const {
  return m_bufferId;
}

const NodeIdentifier &Buffer2::nodeId() const {
  return m_nodeId;
}

// ============ Buffer Content ============

bool Buffer2::save() {
  if (!isValid()) {
    return false;
  }

  QVariantMap args;
  args[QStringLiteral("bufferId")] = m_bufferId;
  if (m_hookMgr->doAction(HookNames::FileBeforeSave, args)) {
    return false; // Cancelled by plugin.
  }

  bool ok = m_bufferCoreService->saveBuffer(m_bufferId);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterSave, args);
  }

  return ok;
}

bool Buffer2::reload() {
  if (!isValid()) {
    return false;
  }
  return m_bufferCoreService->reloadBuffer(m_bufferId);
}

QJsonObject Buffer2::getContent() const {
  if (!isValid()) {
    return QJsonObject();
  }
  return m_bufferCoreService->getContent(m_bufferId);
}

bool Buffer2::setContent(const QString &p_contentJson) {
  if (!isValid()) {
    return false;
  }
  return m_bufferCoreService->setContent(m_bufferId, p_contentJson);
}

QByteArray Buffer2::getContentRaw() const {
  if (!isValid()) {
    return QByteArray();
  }
  return m_bufferCoreService->getContentRaw(m_bufferId);
}

QByteArrayView Buffer2::peekContentRaw() const {
  if (!isValid()) {
    return QByteArrayView{};
  }
  return m_bufferCoreService->peekContentRaw(m_bufferId);
}

bool Buffer2::setContentRaw(const QByteArray &p_data) {
  if (!isValid()) {
    return false;
  }
  return m_bufferCoreService->setContentRaw(m_bufferId, p_data);
}

// ============ Buffer State ============

BufferState Buffer2::getState() const {
  if (!isValid()) {
    return BufferState::Normal;
  }
  return m_bufferCoreService->getState(m_bufferId);
}

bool Buffer2::isModified() const {
  if (!isValid()) {
    return false;
  }
  return m_bufferCoreService->isModified(m_bufferId);
}

// ============ Buffer Info ============

QJsonObject Buffer2::getBuffer() const {
  if (!isValid()) {
    return QJsonObject();
  }
  return m_bufferCoreService->getBuffer(m_bufferId);
}

// ============ Asset Operations ============

QString Buffer2::insertAssetRaw(const QString &p_assetName, const QByteArray &p_data) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferCoreService->insertAssetRaw(m_bufferId, p_assetName, p_data);
}

QString Buffer2::insertAsset(const QString &p_sourcePath) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferCoreService->insertAsset(m_bufferId, p_sourcePath);
}

bool Buffer2::deleteAsset(const QString &p_relativePath) {
  if (!isValid()) {
    return false;
  }
  return m_bufferCoreService->deleteAsset(m_bufferId, p_relativePath);
}

QString Buffer2::getAssetsFolder() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferCoreService->getAssetsFolder(m_bufferId);
}

// ============ Attachment Operations ============

QString Buffer2::insertAttachment(const QString &p_sourcePath) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferCoreService->insertAttachment(m_bufferId, p_sourcePath);
}

bool Buffer2::deleteAttachment(const QString &p_filename) {
  if (!isValid()) {
    return false;
  }
  return m_bufferCoreService->deleteAttachment(m_bufferId, p_filename);
}

QString Buffer2::renameAttachment(const QString &p_oldFilename, const QString &p_newFilename) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferCoreService->renameAttachment(m_bufferId, p_oldFilename, p_newFilename);
}

QJsonArray Buffer2::listAttachments() const {
  if (!isValid()) {
    return QJsonArray();
  }
  return m_bufferCoreService->listAttachments(m_bufferId);
}

QString Buffer2::getAttachmentsFolder() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferCoreService->getAttachmentsFolder(m_bufferId);
}
