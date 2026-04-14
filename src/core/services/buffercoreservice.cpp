#include "buffercoreservice.h"

#include <QJsonDocument>
#include <QJsonParseError>

using namespace vnotex;

BufferCoreService::BufferCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

BufferCoreService::~BufferCoreService() {}

// Buffer lifecycle.
QString BufferCoreService::openBuffer(const QString &p_notebookId, const QString &p_filePath) {
  if (!checkContext()) {
    return QString();
  }

  char *bufferId = nullptr;
  VxCoreError err = vxcore_buffer_open(
      m_context, p_notebookId.isEmpty() ? nullptr : p_notebookId.toUtf8().constData(),
      p_filePath.toUtf8().constData(), &bufferId);
  if (err != VXCORE_OK) {
    qWarning() << "openBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(bufferId);
}

QString BufferCoreService::openBufferByNodeId(const QString &p_nodeId) {
  if (!checkContext()) {
    return QString();
  }

  char *bufferId = nullptr;
  VxCoreError err =
      vxcore_buffer_open_by_node_id(m_context, p_nodeId.toUtf8().constData(), &bufferId);
  if (err != VXCORE_OK) {
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "openBufferByNodeId failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QString();
  }
  return cstrToQString(bufferId);
}

QString BufferCoreService::openVirtualBuffer(const QString &p_address) {
  if (!checkContext()) {
    return QString();
  }

  char *bufferId = nullptr;
  VxCoreError err =
      vxcore_buffer_open_virtual(m_context, p_address.toUtf8().constData(), &bufferId);
  if (err != VXCORE_OK) {
    qWarning() << "openVirtualBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(bufferId);
}

bool BufferCoreService::closeBuffer(const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_close(m_context, p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "closeBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject BufferCoreService::getBuffer(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_buffer_get(m_context, p_bufferId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonArray BufferCoreService::listBuffers() const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_buffer_list(m_context, &json);
  if (err != VXCORE_OK) {
    qWarning() << "listBuffers failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

// Buffer content.
bool BufferCoreService::saveBuffer(const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_save(m_context, p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "saveBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool BufferCoreService::reloadBuffer(const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_reload(m_context, p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "reloadBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject BufferCoreService::getContent(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_buffer_get_content(m_context, p_bufferId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getContent failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool BufferCoreService::setContent(const QString &p_bufferId, const QString &p_contentJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_set_content(m_context, p_bufferId.toUtf8().constData(),
                                              p_contentJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "setContent failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QByteArray BufferCoreService::getContentRaw(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QByteArray();
  }

  const void *data = nullptr;
  size_t size = 0;
  VxCoreError err =
      vxcore_buffer_get_content_raw(m_context, p_bufferId.toUtf8().constData(), &data, &size);
  if (err != VXCORE_OK) {
    qWarning() << "getContentRaw failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QByteArray();
  }
  // Copy data into QByteArray since the internal pointer is only valid until buffer is modified.
  return QByteArray(static_cast<const char *>(data), static_cast<int>(size));
}

QByteArrayView BufferCoreService::peekContentRaw(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QByteArrayView{};
  }

  const void *data = nullptr;
  size_t size = 0;
  VxCoreError err =
      vxcore_buffer_get_content_raw(m_context, p_bufferId.toUtf8().constData(), &data, &size);
  if (err != VXCORE_OK) {
    qWarning() << "peekContentRaw failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QByteArrayView{};
  }
  // Return a non-owning view. Valid only until the next buffer-mutating operation.
  return QByteArrayView(static_cast<const char *>(data), static_cast<qsizetype>(size));
}

bool BufferCoreService::setContentRaw(const QString &p_bufferId, const QByteArray &p_data) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_buffer_set_content_raw(m_context, p_bufferId.toUtf8().constData(), p_data.constData(),
                                    static_cast<size_t>(p_data.size()));
  if (err != VXCORE_OK) {
    qWarning() << "setContentRaw failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

// Buffer state.
BufferState BufferCoreService::getState(const QString &p_bufferId) const {
  if (!checkContext()) {
    return BufferState::Normal;
  }

  VxCoreBufferState state = VXCORE_BUFFER_NORMAL;
  VxCoreError err = vxcore_buffer_get_state(m_context, p_bufferId.toUtf8().constData(), &state);
  if (err != VXCORE_OK) {
    qWarning() << "getState failed:" << QString::fromUtf8(vxcore_error_message(err));
    return BufferState::Normal;
  }
  return static_cast<BufferState>(state);
}

bool BufferCoreService::isModified(const QString &p_bufferId) const {
  if (!checkContext()) {
    return false;
  }

  int modified = 0;
  VxCoreError err =
      vxcore_buffer_is_modified(m_context, p_bufferId.toUtf8().constData(), &modified);
  if (err != VXCORE_OK) {
    qWarning() << "isModified failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return modified != 0;
}

int BufferCoreService::getRevision(const QString &p_bufferId) const {
  if (!checkContext()) {
    return 0;
  }

  int revision = 0;
  VxCoreError err =
      vxcore_buffer_get_revision(m_context, p_bufferId.toUtf8().constData(), &revision);
  if (err != VXCORE_OK) {
    qWarning() << "getRevision failed:" << QString::fromUtf8(vxcore_error_message(err));
    return 0;
  }
  return revision;
}

bool BufferCoreService::writeBackup(const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_write_backup(m_context, p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "writeBackup failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString BufferCoreService::getBackupPath(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err =
      vxcore_buffer_get_backup_path(m_context, p_bufferId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    qWarning() << "getBackupPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(path);
}

// Path resolution.
QString BufferCoreService::getResolvedPath(const QString &p_notebookId,
                                           const QString &p_filePath) const {
  // External file: p_filePath is already absolute.
  if (p_notebookId.isEmpty()) {
    return p_filePath;
  }

  if (!checkContext()) {
    return QString();
  }

  char *absPath = nullptr;
  VxCoreError err = vxcore_path_build_absolute(m_context, p_notebookId.toUtf8().constData(),
                                               p_filePath.toUtf8().constData(), &absPath);
  if (err != VXCORE_OK) {
    qWarning() << "getResolvedPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(absPath);
}

bool BufferCoreService::resolveNodeId(const QString &p_nodeId, QString &p_outNotebookId,
                                      QString &p_outRelativePath) const {
  if (!checkContext()) {
    return false;
  }

  char *outNotebookId = nullptr;
  char *outRelativePath = nullptr;
  VxCoreError err = vxcore_node_resolve_by_id(m_context, p_nodeId.toUtf8().constData(),
                                              &outNotebookId, &outRelativePath);
  if (err == VXCORE_OK) {
    p_outNotebookId = cstrToQString(outNotebookId);
    p_outRelativePath = cstrToQString(outRelativePath);
    return true;
  }

  if (err != VXCORE_ERR_NOT_FOUND) {
    qWarning() << "resolveNodeId failed:" << QString::fromUtf8(vxcore_error_message(err));
  }
  return false;
}

QString BufferCoreService::getResourceBasePath(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err =
      vxcore_buffer_get_resource_base_path(m_context, p_bufferId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    qWarning() << "getResourceBasePath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(path);
}

// Asset operations.
QString BufferCoreService::insertAssetRaw(const QString &p_bufferId, const QString &p_assetName,
                                          const QByteArray &p_data) {
  if (!checkContext()) {
    return QString();
  }

  char *relativePath = nullptr;
  VxCoreError err = vxcore_buffer_insert_asset_raw(
      m_context, p_bufferId.toUtf8().constData(), p_assetName.toUtf8().constData(),
      p_data.constData(), static_cast<size_t>(p_data.size()), &relativePath);
  if (err != VXCORE_OK) {
    qWarning() << "insertAssetRaw failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(relativePath);
}

QString BufferCoreService::insertAsset(const QString &p_bufferId, const QString &p_sourcePath) {
  if (!checkContext()) {
    return QString();
  }

  char *relativePath = nullptr;
  VxCoreError err = vxcore_buffer_insert_asset(m_context, p_bufferId.toUtf8().constData(),
                                               p_sourcePath.toUtf8().constData(), &relativePath);
  if (err != VXCORE_OK) {
    qWarning() << "insertAsset failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(relativePath);
}

bool BufferCoreService::deleteAsset(const QString &p_bufferId, const QString &p_relativePath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_delete_asset(m_context, p_bufferId.toUtf8().constData(),
                                               p_relativePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteAsset failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString BufferCoreService::getAssetsFolder(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err =
      vxcore_buffer_get_assets_folder(m_context, p_bufferId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    qWarning() << "getAssetsFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(path);
}

// Attachment operations.
QString BufferCoreService::insertAttachment(const QString &p_bufferId,
                                            const QString &p_sourcePath) {
  if (!checkContext()) {
    return QString();
  }

  char *filename = nullptr;
  VxCoreError err = vxcore_buffer_insert_attachment(m_context, p_bufferId.toUtf8().constData(),
                                                    p_sourcePath.toUtf8().constData(), &filename);
  if (err != VXCORE_OK) {
    qWarning() << "insertAttachment failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(filename);
}

bool BufferCoreService::deleteAttachment(const QString &p_bufferId, const QString &p_filename) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_buffer_delete_attachment(m_context, p_bufferId.toUtf8().constData(),
                                                    p_filename.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteAttachment failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString BufferCoreService::renameAttachment(const QString &p_bufferId, const QString &p_oldFilename,
                                            const QString &p_newFilename) {
  if (!checkContext()) {
    return QString();
  }

  char *newFilename = nullptr;
  VxCoreError err = vxcore_buffer_rename_attachment(
      m_context, p_bufferId.toUtf8().constData(), p_oldFilename.toUtf8().constData(),
      p_newFilename.toUtf8().constData(), &newFilename);
  if (err != VXCORE_OK) {
    qWarning() << "renameAttachment failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(newFilename);
}

QJsonArray BufferCoreService::listAttachments(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err =
      vxcore_buffer_list_attachments(m_context, p_bufferId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "listAttachments failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

QString BufferCoreService::getAttachmentsFolder(const QString &p_bufferId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err =
      vxcore_buffer_get_attachments_folder(m_context, p_bufferId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    qWarning() << "getAttachmentsFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(path);
}

// Private methods.
bool BufferCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "BufferCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}

QString BufferCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

QJsonObject BufferCoreService::parseJsonObjectFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonObject();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON object:" << error.errorString();
    return QJsonObject();
  }
  return doc.object();
}

QJsonArray BufferCoreService::parseJsonArrayFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonArray();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON array:" << error.errorString();
    return QJsonArray();
  }
  return doc.array();
}
