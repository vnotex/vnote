#include "buffer2.h"

#include <QtGlobal>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

Buffer2::Buffer2() : m_bufferService(nullptr), m_hookMgr(nullptr) {}

Buffer2::Buffer2(BufferService *p_bufferService, HookManager *p_hookMgr, const QString &p_bufferId,
                 const NodeIdentifier &p_nodeId)
    : m_bufferService(p_bufferService), m_hookMgr(p_hookMgr), m_bufferId(p_bufferId),
      m_nodeId(p_nodeId) {
  Q_ASSERT(m_bufferService);
  Q_ASSERT(m_hookMgr);
}

bool Buffer2::isValid() const { return m_bufferService && !m_bufferId.isEmpty(); }

QString Buffer2::id() const { return m_bufferId; }

std::shared_ptr<ProtectedBufferLease> Buffer2::acquireProtectedLease() const {
  return isEncrypted() && isValid() ? m_bufferService->acquireProtectedLease(*this) : nullptr;
}

const NodeIdentifier &Buffer2::nodeId() const { return m_nodeId; }

void Buffer2::setNodeId(const NodeIdentifier &p_nodeId) {
  if (isEncrypted()) {
    m_bufferService->updateProtectedNodeId(*this, p_nodeId);
  }
  m_nodeId = p_nodeId;
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

bool Buffer2::save(VxCoreError *p_error) {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  if (!isValid()) {
    return false;
  }

  BufferEvent event;
  event.bufferId = m_bufferId;
  if (m_hookMgr->doAction(HookNames::FileBeforeSave, event)) {
    if (p_error) {
      *p_error = VXCORE_ERR_CANCELLED;
    }
    return false; // Cancelled by plugin.
  }

  bool ok = m_bufferService->saveBuffer(*this, p_error);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterSave, event);
  }

  return ok;
}

bool Buffer2::reload(VxCoreError *p_error) {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  if (!isValid()) {
    return false;
  }
  return m_bufferService->reloadBuffer(*this, p_error);
}

QJsonObject Buffer2::getContent(VxCoreError *p_error) const {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  if (!isValid()) {
    return QJsonObject();
  }
  return m_bufferService->getContent(*this, p_error);
}

bool Buffer2::setContent(const QString &p_contentJson) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->setContent(*this, p_contentJson);
}

QByteArray Buffer2::getContentRaw(VxCoreError *p_error) const {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  if (!isValid()) {
    return QByteArray();
  }
  return m_bufferService->getContentRaw(*this, p_error);
}

QByteArrayViewCompat Buffer2::peekContentRaw(VxCoreError *p_error) const {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  if (!isValid()) {
    return QByteArrayViewCompat{};
  }
  return m_bufferService->peekContentRaw(*this, p_error);
}

bool Buffer2::setContentRaw(const QByteArray &p_data) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->setContentRaw(*this, p_data);
}

// ============ Encoding ============

QString Buffer2::encoding() const {
  if (!isValid()) {
    return QStringLiteral("UTF-8");
  }
  return m_bufferService->bufferEncoding(m_bufferId);
}

bool Buffer2::setEncoding(const QString &p_codecName) {
  if (!isValid()) {
    return false;
  }
  m_bufferService->setBufferEncoding(m_bufferId, p_codecName);
  return true;
}

QString Buffer2::decode(const QByteArrayViewCompat &p_raw) const {
  if (!isValid()) {
    return QString::fromUtf8(p_raw);
  }
  return m_bufferService->decodeContent(m_bufferId, p_raw);
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

bool Buffer2::isReadOnly() const noexcept {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->isBufferReadOnly(m_bufferId);
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

QByteArray Buffer2::readResource(const QString &p_resourceUrl, VxCoreError *p_error) const {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  return isValid() ? m_bufferService->readResource(*this, p_resourceUrl, p_error) : QByteArray();
}

QJsonArray Buffer2::resources(VxCoreError *p_error) const {
  if (p_error) {
    *p_error = VXCORE_ERR_INVALID_STATE;
  }
  return isValid() ? m_bufferService->resources(*this, p_error) : QJsonArray();
}

VxCoreError Buffer2::exportResource(const QString &p_resourceUrl,
                                    const QString &p_destination) const {
  return isValid() ? m_bufferService->exportResource(*this, p_resourceUrl, p_destination)
                   : VXCORE_ERR_INVALID_STATE;
}

// ============ Asset Operations ============

QString Buffer2::insertAssetRaw(const QString &p_assetName, const QByteArray &p_data) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->insertAssetRaw(*this, p_assetName, p_data);
}

QString Buffer2::insertAsset(const QString &p_sourcePath) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->insertAsset(*this, p_sourcePath);
}

bool Buffer2::deleteAsset(const QString &p_relativePath) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->deleteAsset(*this, p_relativePath);
}

QString Buffer2::getAssetsFolder() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->getAssetsFolder(m_bufferId);
}

// ============ Attachment Operations ============

bool Buffer2::isAttachmentSupported() const {
  return isValid() && m_bufferService->isNotebookBundled(m_nodeId.notebookId);
}

bool Buffer2::isTagSupported() const {
  return isValid() && m_bufferService->isNotebookBundled(m_nodeId.notebookId);
}

QString Buffer2::insertAttachment(const QString &p_sourcePath) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->insertAttachment(*this, p_sourcePath);
}

bool Buffer2::deleteAttachment(const QString &p_filename) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->deleteAttachment(*this, p_filename);
}

QString Buffer2::renameAttachment(const QString &p_oldFilename, const QString &p_newFilename) {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->renameAttachment(*this, p_oldFilename, p_newFilename);
}

QJsonArray Buffer2::listAttachments() const {
  if (!isValid()) {
    return QJsonArray();
  }
  return m_bufferService->listAttachments(*this);
}

QJsonArray Buffer2::listUnindexedAttachments() const {
  if (!isValid()) {
    return QJsonArray();
  }
  return m_bufferService->listUnindexedAttachments(m_bufferId);
}

bool Buffer2::registerAttachment(const QString &p_filename) {
  if (!isValid()) {
    return false;
  }
  return m_bufferService->registerAttachment(m_bufferId, p_filename);
}

QString Buffer2::getAttachmentsFolder() const {
  if (!isValid()) {
    return QString();
  }
  return m_bufferService->getAttachmentsFolder(m_bufferId);
}

bool Buffer2::hasAttachments() const {
  if (!isValid()) {
    return false;
  }

  return !listAttachments().isEmpty();
}
