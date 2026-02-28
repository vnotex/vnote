#include "notebookservice.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <vxcore/vxcore_events.h>

using namespace vnotex;

NotebookService::NotebookService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {
  // Subscribe to vxcore events - wire C callbacks to Qt signals.
  if (m_context) {
    vxcore_event_subscribe(m_context, VXCORE_EVENT_NOTE_CREATED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_NOTE_UPDATED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_NOTE_DELETED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_NOTE_MOVED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_TAG_ADDED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_TAG_REMOVED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_NOTEBOOK_OPENED, eventCallback, this);
    vxcore_event_subscribe(m_context, VXCORE_EVENT_NOTEBOOK_CLOSED, eventCallback, this);
  }
}

NotebookService::~NotebookService() {
  // Unsubscribe from all events if context still valid.
  if (m_context) {
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_NOTE_CREATED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_NOTE_UPDATED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_NOTE_DELETED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_NOTE_MOVED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_TAG_ADDED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_TAG_REMOVED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_NOTEBOOK_OPENED, eventCallback);
    vxcore_event_unsubscribe(m_context, VXCORE_EVENT_NOTEBOOK_CLOSED, eventCallback);
  }
}

// Notebook operations.
QString NotebookService::createNotebook(const QString &p_path, const QString &p_configJson,
                                        NotebookType p_type) {
  if (!checkContext()) {
    return QString();
  }

  char *notebookId = nullptr;
  VxCoreError err =
      vxcore_notebook_create(m_context, p_path.toUtf8().constData(), p_configJson.toUtf8().constData(),
                             static_cast<VxCoreNotebookType>(p_type), &notebookId);
  if (err != VXCORE_OK) {
    qWarning() << "createNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(notebookId);
}

QString NotebookService::openNotebook(const QString &p_path) {
  if (!checkContext()) {
    return QString();
  }

  char *notebookId = nullptr;
  VxCoreError err = vxcore_notebook_open(m_context, p_path.toUtf8().constData(), &notebookId);
  if (err != VXCORE_OK) {
    qWarning() << "openNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(notebookId);
}

bool NotebookService::closeNotebook(const QString &p_notebookId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_close(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "closeNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonArray NotebookService::listNotebooks() const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_notebook_list(m_context, &json);
  if (err != VXCORE_OK) {
    qWarning() << "listNotebooks failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

QJsonObject NotebookService::getNotebookConfig(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err =
      vxcore_notebook_get_config(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getNotebookConfig failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookService::updateNotebookConfig(const QString &p_notebookId, const QString &p_configJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_update_config(
      m_context, p_notebookId.toUtf8().constData(), p_configJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateNotebookConfig failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::rebuildNotebookCache(const QString &p_notebookId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_rebuild_cache(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "rebuildNotebookCache failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookService::resolvePathToNotebook(const QString &p_absolutePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *notebookId = nullptr;
  char *relativePath = nullptr;
  VxCoreError err = vxcore_path_resolve(m_context, p_absolutePath.toUtf8().constData(),
                                        &notebookId, &relativePath);
  if (err != VXCORE_OK) {
    // Not finding the path is not necessarily an error - just return empty.
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "resolvePathToNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QJsonObject();
  }

  QJsonObject result;
  result["notebookId"] = QString::fromUtf8(notebookId);
  result["relativePath"] = QString::fromUtf8(relativePath);

  vxcore_string_free(notebookId);
  vxcore_string_free(relativePath);

  return result;
}

QString NotebookService::getNodePathById(const QString &p_notebookId, const QString &p_nodeId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_node_get_path_by_id(m_context, p_notebookId.toUtf8().constData(),
                                               p_nodeId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    qWarning() << "getNodePathById failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodeId:" << p_nodeId;
    return QString();
  }
  return cstrToQString(path);
}

QJsonObject NotebookService::getNodeConfig(const QString &p_notebookId,
                                           const QString &p_nodePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_config(m_context, p_notebookId.toUtf8().constData(),
                                           p_nodePath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    // Not found is common for clipboard operations - don't warn
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "getNodeConfig failed:" << QString::fromUtf8(vxcore_error_message(err))
                 << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    }
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookService::unindexNode(const QString &p_notebookId, const QString &p_nodePath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_unindex(m_context, p_notebookId.toUtf8().constData(),
                                        p_nodePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "unindexNode failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    return false;
  }
  return true;
}

QString NotebookService::getRecycleBinPath(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err =
      vxcore_notebook_get_recycle_bin_path(m_context, p_notebookId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    // VXCORE_ERR_UNSUPPORTED is expected for raw notebooks.
    if (err != VXCORE_ERR_UNSUPPORTED) {
      qWarning() << "getRecycleBinPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QString();
  }
  return cstrToQString(path);
}

bool NotebookService::emptyRecycleBin(const QString &p_notebookId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_empty_recycle_bin(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    // VXCORE_ERR_UNSUPPORTED is expected for raw notebooks.
    if (err != VXCORE_ERR_UNSUPPORTED) {
      qWarning() << "emptyRecycleBin failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return false;
  }
  return true;
}

// Folder operations.
QString NotebookService::createFolder(const QString &p_notebookId, const QString &p_parentPath,
                                      const QString &p_folderName) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err =
      vxcore_folder_create(m_context, p_notebookId.toUtf8().constData(),
                           p_parentPath.toUtf8().constData(), p_folderName.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "createFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

QString NotebookService::createFolderPath(const QString &p_notebookId, const QString &p_folderPath) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_create_path(m_context, p_notebookId.toUtf8().constData(),
                                              p_folderPath.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "createFolderPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

bool NotebookService::deleteFolder(const QString &p_notebookId, const QString &p_folderPath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookService::getFolderConfig(const QString &p_notebookId, const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_config(m_context, p_notebookId.toUtf8().constData(),
                                           p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFolderConfig failed:" << QString::fromUtf8(vxcore_error_message(err)) << "notebookId:" << p_notebookId << "folderPath:" << p_folderPath;
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookService::updateFolderMetadata(const QString &p_notebookId, const QString &p_folderPath,
                                           const QString &p_metadataJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_node_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                  p_folderPath.toUtf8().constData(), p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFolderMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookService::getFolderMetadata(const QString &p_notebookId,
                                               const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_metadata(m_context, p_notebookId.toUtf8().constData(),
                                             p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFolderMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookService::renameFolder(const QString &p_notebookId, const QString &p_folderPath,
                                   const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_rename(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::moveFolder(const QString &p_notebookId, const QString &p_srcPath,
                                 const QString &p_destParentPath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_move(m_context, p_notebookId.toUtf8().constData(),
                                     p_srcPath.toUtf8().constData(), p_destParentPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString NotebookService::copyFolder(const QString &p_notebookId, const QString &p_srcPath,
                                    const QString &p_destParentPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err =
      vxcore_node_copy(m_context, p_notebookId.toUtf8().constData(), p_srcPath.toUtf8().constData(),
                       p_destParentPath.isEmpty() ? "." : p_destParentPath.toUtf8().constData(),
                       p_newName.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "copyFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

QJsonObject NotebookService::listFolderChildren(const QString &p_notebookId,
                                                const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_folder_list_children(
      m_context, p_notebookId.toUtf8().constData(),
      p_folderPath.isEmpty() ? "." : p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "listFolderChildren failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QString NotebookService::getAvailableName(const QString &p_notebookId, const QString &p_folderPath,
                                          const QString &p_desiredName) const {
  if (!checkContext()) {
    return QString();
  }

  char *availableName = nullptr;
  VxCoreError err = vxcore_folder_get_available_name(
      m_context, p_notebookId.toUtf8().constData(),
      p_folderPath.isEmpty() ? "." : p_folderPath.toUtf8().constData(),
      p_desiredName.toUtf8().constData(), &availableName);
  if (err != VXCORE_OK) {
    qWarning() << "getAvailableName failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(availableName);
}

// File operations.
QString NotebookService::createFile(const QString &p_notebookId, const QString &p_folderPath,
                                    const QString &p_fileName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err =
      vxcore_file_create(m_context, p_notebookId.toUtf8().constData(), p_folderPath.toUtf8().constData(),
                         p_fileName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "createFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}

bool NotebookService::deleteFile(const QString &p_notebookId, const QString &p_filePath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_filePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookService::getFileInfo(const QString &p_notebookId, const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_config(m_context, p_notebookId.toUtf8().constData(),
                                           p_filePath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFileInfo failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonObject NotebookService::getFileMetadata(const QString &p_notebookId, const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_metadata(m_context, p_notebookId.toUtf8().constData(),
                                             p_filePath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFileMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookService::updateFileMetadata(const QString &p_notebookId, const QString &p_filePath,
                                         const QString &p_metadataJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_node_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                  p_filePath.toUtf8().constData(), p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::renameFile(const QString &p_notebookId, const QString &p_filePath,
                                 const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_rename(m_context, p_notebookId.toUtf8().constData(),
                                       p_filePath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::moveFile(const QString &p_notebookId, const QString &p_srcFilePath,
                               const QString &p_destFolderPath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_move(m_context, p_notebookId.toUtf8().constData(),
                                     p_srcFilePath.toUtf8().constData(), p_destFolderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString NotebookService::copyFile(const QString &p_notebookId, const QString &p_srcFilePath,
                                  const QString &p_destFolderPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err =
      vxcore_node_copy(m_context, p_notebookId.toUtf8().constData(), p_srcFilePath.toUtf8().constData(),
                       p_destFolderPath.isEmpty() ? "." : p_destFolderPath.toUtf8().constData(),
                       p_newName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "copyFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}

QString NotebookService::importFile(const QString &p_notebookId, const QString &p_folderPath,
                                    const QString &p_externalFilePath) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err =
      vxcore_file_import(m_context, p_notebookId.toUtf8().constData(),
                         p_folderPath.toUtf8().constData(),
                         p_externalFilePath.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "importFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}
QString NotebookService::importFolder(const QString &p_notebookId, const QString &p_destFolderPath,
                                      const QString &p_externalFolderPath,
                                      const QString &p_suffixAllowlist) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err =
      vxcore_folder_import(m_context, p_notebookId.toUtf8().constData(),
                           p_destFolderPath.toUtf8().constData(),
                           p_externalFolderPath.toUtf8().constData(),
                           p_suffixAllowlist.isEmpty() ? nullptr : p_suffixAllowlist.toUtf8().constData(),
                           &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "importFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}


// Tag operations.
bool NotebookService::updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                                     const QString &p_tagsJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_file_update_tags(m_context, p_notebookId.toUtf8().constData(),
                              p_filePath.toUtf8().constData(), p_tagsJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileTags failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::tagFile(const QString &p_notebookId, const QString &p_filePath,
                              const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_file_tag(m_context, p_notebookId.toUtf8().constData(),
                                    p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "tagFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::untagFile(const QString &p_notebookId, const QString &p_filePath,
                                const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_file_untag(m_context, p_notebookId.toUtf8().constData(),
                                      p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "untagFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::createTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_create(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "createTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::createTagPath(const QString &p_notebookId, const QString &p_tagPath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_create_path(m_context, p_notebookId.toUtf8().constData(),
                                           p_tagPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "createTagPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookService::deleteTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_delete(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonArray NotebookService::listTags(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_tag_list(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "listTags failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

bool NotebookService::moveTag(const QString &p_notebookId, const QString &p_tagName,
                              const QString &p_parentTag) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_move(m_context, p_notebookId.toUtf8().constData(),
                                    p_tagName.toUtf8().constData(), p_parentTag.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

// Private methods.
bool NotebookService::checkContext() const {
  if (!m_context) {
    qWarning() << "NotebookService: VxCore context not initialized";
    return false;
  }
  return true;
}

void NotebookService::eventCallback(const VxCoreEvent *p_event, void *p_userData) {
  // Static C callback - route to instance method.
  auto *service = static_cast<NotebookService *>(p_userData);
  if (service && p_event) {
    service->handleEvent(p_event->type, QString::fromUtf8(p_event->payload_json),
                         p_event->timestamp_ms);
  }
}

void NotebookService::handleEvent(VxCoreEventType p_eventType, const QString &p_payloadJson,
                                  quint64 p_timestampMs) {
  // Re-emit vxcore events as Qt signals.
  switch (p_eventType) {
  case VXCORE_EVENT_NOTE_CREATED:
    emit noteCreated(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_NOTE_UPDATED:
    emit noteUpdated(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_NOTE_DELETED:
    emit noteDeleted(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_NOTE_MOVED:
    emit noteMoved(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_TAG_ADDED:
    emit tagAdded(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_TAG_REMOVED:
    emit tagRemoved(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_NOTEBOOK_OPENED:
    emit notebookOpened(p_payloadJson, p_timestampMs);
    break;
  case VXCORE_EVENT_NOTEBOOK_CLOSED:
    emit notebookClosed(p_payloadJson, p_timestampMs);
    break;
  default:
    break;
  }
}

QString NotebookService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

const char *NotebookService::qstringToCStr(const QString &p_str) {
  if (p_str.isEmpty()) {
    return nullptr;
  }
  // Note: Returns pointer to temporary data. Only safe for immediate use.
  return p_str.toUtf8().constData();
}

QJsonObject NotebookService::parseJsonObjectFromCStr(char *p_str) {
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

QJsonArray NotebookService::parseJsonArrayFromCStr(char *p_str) {
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
