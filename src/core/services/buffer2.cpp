#include "buffer2.h"

#include <QtGlobal>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

Buffer2::Buffer2()
    : m_bufferService(nullptr), m_hookMgr(nullptr) {
}

Buffer2::Buffer2(BufferService *p_bufferService, HookManager *p_hookMgr,
                 const QString &p_bufferId, const NodeIdentifier &p_nodeId)
    : m_bufferService(p_bufferService),
      m_hookMgr(p_hookMgr),
      m_bufferId(p_bufferId),
      m_nodeId(p_nodeId) {
  Q_ASSERT(m_bufferService);
  Q_ASSERT(m_hookMgr);
}

bool Buffer2::isValid() const {
  return m_bufferService && !m_bufferId.isEmpty();
}

QString Buffer2::id() const {
  return m_bufferId;
}

const NodeIdentifier &Buffer2::nodeId() const {
  return m_nodeId;
}

// ============ Path Resolution ============

QString Buffer2::resolvedPath() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->getResolvedPath(m_nodeId.notebookId, m_nodeId.relativePath);
}

QString Buffer2::getResourceBasePath() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->getResourceBasePath(m_bufferId);
}

// ============ Buffer Content ============

bool Buffer2::save() {
  if (!isValid()) {
    return false;
  }

  BufferEvent event;
  event.bufferId = m_bufferId;
  if (m_hookMgr->doAction(HookNames::FileBeforeSave, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = m_bufferService->saveBuffer(m_bufferId);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterSave, event);
  }

  return ok;
}

bool Buffer2::reload() {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->reloadBuffer(m_bufferId);
}

QJsonObject Buffer2::getContent() const {
  if (!isValid()) {
    return QJsonObject();
  }
  return m_bufferService->getContent(m_bufferId);
}

bool Buffer2::setContent(const QString &p_contentJson) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->setContent(m_bufferId, p_contentJson);
}

QByteArray Buffer2::getContentRaw() const {
  if (!isValid()) {
    return QByteArray();
  }
  return m_bufferService->getContentRaw(m_bufferId);
}

QByteArrayView Buffer2::peekContentRaw() const {
  if (!isValid()) {
    return QByteArrayView{};
  }
  return m_bufferService->peekContentRaw(m_bufferId);
}

bool Buffer2::setContentRaw(const QByteArray &p_data) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->setContentRaw(m_bufferId, p_data);
}

// ============ Buffer State ============

BufferState Buffer2::getState() const {
  if (!isValid()) {
    return BufferState::Normal;
  }
  return m_bufferService->getState(m_bufferId);
}

bool Buffer2::isModified() const {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->isModified(m_bufferId);
}

int Buffer2::getRevision() const {
  if (!isValid()) {
    return 0;
  }
  return m_bufferService->getRevision(m_bufferId);
}

// ============ Buffer Info ============

QJsonObject Buffer2::getBuffer() const {
  if (!isValid()) {
    return QJsonObject();
  }
  return m_bufferService->getBuffer(m_bufferId);
}

// ============ Asset Operations ============

QString Buffer2::insertAssetRaw(const QString &p_assetName, const QByteArray &p_data) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->insertAssetRaw(m_bufferId, p_assetName, p_data);
}

QString Buffer2::insertAsset(const QString &p_sourcePath) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->insertAsset(m_bufferId, p_sourcePath);
}

bool Buffer2::deleteAsset(const QString &p_relativePath) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->deleteAsset(m_bufferId, p_relativePath);
}

QString Buffer2::getAssetsFolder() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->getAssetsFolder(m_bufferId);
}

// ============ Attachment Operations ============

bool Buffer2::isAttachmentSupported() const { return isValid(); }

QString Buffer2::insertAttachment(const QString &p_sourcePath) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->insertAttachment(m_bufferId, p_sourcePath);
}

bool Buffer2::deleteAttachment(const QString &p_filename) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->deleteAttachment(m_bufferId, p_filename);
}

QString Buffer2::renameAttachment(const QString &p_oldFilename, const QString &p_newFilename) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->renameAttachment(m_bufferId, p_oldFilename, p_newFilename);
}

QJsonArray Buffer2::listAttachments() const {
  if (!isValid()) {
    return QJsonArray();
  }
  return m_bufferService->listAttachments(m_bufferId);
}

QString Buffer2::getAttachmentsFolder() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->getAttachmentsFolder(m_bufferId);
}
