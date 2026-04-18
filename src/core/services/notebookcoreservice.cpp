#include "notebookcoreservice.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QVariantMap>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

NotebookCoreService::NotebookCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

void NotebookCoreService::setHookManager(HookManager *p_hookMgr) { m_hookMgr = p_hookMgr; }

NotebookCoreService::~NotebookCoreService() {}

// Notebook operations.
QString NotebookCoreService::createNotebook(const QString &p_path, const QString &p_configJson,
                                            NotebookType p_type) {
  if (!checkContext()) {
    return QString();
  }

  char *notebookId = nullptr;
  VxCoreError err = vxcore_notebook_create(m_context, p_path.toUtf8().constData(),
                                           p_configJson.toUtf8().constData(),
                                           static_cast<VxCoreNotebookType>(p_type), &notebookId);
  if (err != VXCORE_OK) {
    qWarning() << "createNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(notebookId);
}

QString NotebookCoreService::openNotebook(const QString &p_path) {
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

bool NotebookCoreService::closeNotebook(const QString &p_notebookId) {
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

QJsonArray NotebookCoreService::listNotebooks() const {
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

QJsonObject NotebookCoreService::getNotebookConfig(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_notebook_get_config(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getNotebookConfig failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::updateNotebookConfig(const QString &p_notebookId,
                                               const QString &p_configJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_update_config(m_context, p_notebookId.toUtf8().constData(),
                                                  p_configJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateNotebookConfig failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookCoreService::rebuildNotebookCache(const QString &p_notebookId) {
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

QJsonObject NotebookCoreService::resolvePathToNotebook(const QString &p_absolutePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *notebookId = nullptr;
  char *relativePath = nullptr;
  VxCoreError err = vxcore_path_resolve(m_context, p_absolutePath.toUtf8().constData(), &notebookId,
                                        &relativePath);
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

QString NotebookCoreService::buildAbsolutePath(const QString &p_notebookId,
                                               const QString &p_relativePath) const {
  if (!checkContext()) {
    return QString();
  }

  char *absPath = nullptr;
  VxCoreError err = vxcore_path_build_absolute(m_context, p_notebookId.toUtf8().constData(),
                                               p_relativePath.toUtf8().constData(), &absPath);
  if (err != VXCORE_OK) {
    qWarning() << "buildAbsolutePath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }

  QString result = QString::fromUtf8(absPath);
  vxcore_string_free(absPath);
  return result;
}

QString NotebookCoreService::getNodePathById(const QString &p_notebookId,
                                             const QString &p_nodeId) const {
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

bool NotebookCoreService::unindexNode(const QString &p_notebookId, const QString &p_nodePath) {
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

QString NotebookCoreService::getRecycleBinPath(const QString &p_notebookId) const {
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

bool NotebookCoreService::emptyRecycleBin(const QString &p_notebookId) {
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
QString NotebookCoreService::createFolder(const QString &p_notebookId, const QString &p_parentPath,
                                          const QString &p_folderName) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_create(m_context, p_notebookId.toUtf8().constData(),
                                         p_parentPath.toUtf8().constData(),
                                         p_folderName.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "createFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

QString NotebookCoreService::createFolderPath(const QString &p_notebookId,
                                              const QString &p_folderPath) {
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

bool NotebookCoreService::deleteFolder(const QString &p_notebookId, const QString &p_folderPath) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeDelete hook (cancellable).
  if (m_hookMgr) {
    NodeOperationEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_folderPath;
    event.isFolder = true;
    event.name = p_folderPath.mid(p_folderPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.operation = QStringLiteral("delete");
    if (m_hookMgr->doAction(HookNames::NodeBeforeDelete, event)) {
      return false;
    }
  }

  VxCoreError err = vxcore_node_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookCoreService::getFolderConfig(const QString &p_notebookId,
                                                 const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_config(m_context, p_notebookId.toUtf8().constData(),
                                           p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFolderConfig failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "folderPath:" << p_folderPath;
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::updateFolderMetadata(const QString &p_notebookId,
                                               const QString &p_folderPath,
                                               const QString &p_metadataJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                                p_folderPath.toUtf8().constData(),
                                                p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFolderMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookCoreService::getFolderMetadata(const QString &p_notebookId,
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

bool NotebookCoreService::renameFolder(const QString &p_notebookId, const QString &p_folderPath,
                                       const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeRename hook (cancellable).
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_folderPath;
    event.isFolder = true;
    event.oldName = p_folderPath.mid(p_folderPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    if (m_hookMgr->doAction(HookNames::NodeBeforeRename, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_rename(m_context, p_notebookId.toUtf8().constData(),
                         p_folderPath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterRename hook so other modules can react.
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_folderPath;
    event.isFolder = true;
    event.oldName = p_folderPath.mid(p_folderPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    m_hookMgr->doAction(HookNames::NodeAfterRename, event);
  }

  return true;
}

bool NotebookCoreService::moveFolder(const QString &p_notebookId, const QString &p_srcPath,
                                     const QString &p_destParentPath) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeMove hook (cancellable).
  if (m_hookMgr) {
    NodeOperationEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_srcPath;
    event.isFolder = true;
    event.name = p_srcPath.mid(p_srcPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.operation = QStringLiteral("move");
    if (m_hookMgr->doAction(HookNames::NodeBeforeMove, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_move(m_context, p_notebookId.toUtf8().constData(), p_srcPath.toUtf8().constData(),
                       p_destParentPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString NotebookCoreService::copyFolder(const QString &p_notebookId, const QString &p_srcPath,
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

QJsonObject NotebookCoreService::listFolderChildren(const QString &p_notebookId,
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

QJsonObject NotebookCoreService::listFolderExternal(const QString &p_notebookId,
                                                    const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_folder_list_external(
      m_context, p_notebookId.toUtf8().constData(),
      p_folderPath.isEmpty() ? "." : p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "listFolderExternal failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::indexNode(const QString &p_notebookId, const QString &p_nodePath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_index(m_context, p_notebookId.toUtf8().constData(),
                                      p_nodePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "indexNode failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    return false;
  }
  return true;
}

QString NotebookCoreService::getAvailableName(const QString &p_notebookId,
                                              const QString &p_folderPath,
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
QString NotebookCoreService::createFile(const QString &p_notebookId, const QString &p_folderPath,
                                        const QString &p_fileName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err = vxcore_file_create(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData(),
                                       p_fileName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "createFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}

bool NotebookCoreService::deleteFile(const QString &p_notebookId, const QString &p_filePath) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeDelete hook (cancellable).
  if (m_hookMgr) {
    NodeOperationEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_filePath;
    event.isFolder = false;
    event.name = p_filePath.mid(p_filePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.operation = QStringLiteral("delete");
    if (m_hookMgr->doAction(HookNames::NodeBeforeDelete, event)) {
      return false;
    }
  }

  VxCoreError err = vxcore_node_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_filePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookCoreService::getFileInfo(const QString &p_notebookId,
                                             const QString &p_filePath) const {
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

QJsonObject NotebookCoreService::getFileMetadata(const QString &p_notebookId,
                                                 const QString &p_filePath) const {
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

bool NotebookCoreService::updateFileMetadata(const QString &p_notebookId, const QString &p_filePath,
                                             const QString &p_metadataJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                                p_filePath.toUtf8().constData(),
                                                p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookCoreService::renameFile(const QString &p_notebookId, const QString &p_filePath,
                                     const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeRename hook (cancellable).
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_filePath;
    event.isFolder = false;
    event.oldName = p_filePath.mid(p_filePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    if (m_hookMgr->doAction(HookNames::NodeBeforeRename, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_rename(m_context, p_notebookId.toUtf8().constData(),
                         p_filePath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterRename hook so other modules can react.
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_filePath;
    event.isFolder = false;
    event.oldName = p_filePath.mid(p_filePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    m_hookMgr->doAction(HookNames::NodeAfterRename, event);
  }

  return true;
}

bool NotebookCoreService::moveFile(const QString &p_notebookId, const QString &p_srcFilePath,
                                   const QString &p_destFolderPath) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeMove hook (cancellable).
  if (m_hookMgr) {
    NodeOperationEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_srcFilePath;
    event.isFolder = false;
    event.name = p_srcFilePath.mid(p_srcFilePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.operation = QStringLiteral("move");
    if (m_hookMgr->doAction(HookNames::NodeBeforeMove, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_move(m_context, p_notebookId.toUtf8().constData(),
                       p_srcFilePath.toUtf8().constData(), p_destFolderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QString NotebookCoreService::copyFile(const QString &p_notebookId, const QString &p_srcFilePath,
                                      const QString &p_destFolderPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err = vxcore_node_copy(
      m_context, p_notebookId.toUtf8().constData(), p_srcFilePath.toUtf8().constData(),
      p_destFolderPath.isEmpty() ? "." : p_destFolderPath.toUtf8().constData(),
      p_newName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "copyFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}

QString NotebookCoreService::importFile(const QString &p_notebookId, const QString &p_folderPath,
                                        const QString &p_externalFilePath) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err = vxcore_file_import(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData(),
                                       p_externalFilePath.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "importFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}
QString NotebookCoreService::importFolder(const QString &p_notebookId,
                                          const QString &p_destFolderPath,
                                          const QString &p_externalFolderPath,
                                          const QString &p_suffixAllowlist) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_import(
      m_context, p_notebookId.toUtf8().constData(), p_destFolderPath.toUtf8().constData(),
      p_externalFolderPath.toUtf8().constData(),
      p_suffixAllowlist.isEmpty() ? nullptr : p_suffixAllowlist.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "importFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

bool NotebookCoreService::createTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_tag_create(m_context, p_notebookId.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK && err != VXCORE_ERR_ALREADY_EXISTS) {
    qWarning() << "createTag failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "tagName:" << p_tagName;
    return false;
  }
  return true;
}

bool NotebookCoreService::updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                                         const QStringList &p_tags) {
  if (!checkContext()) {
    return false;
  }

  QJsonArray tagsArray;
  for (const auto &tag : p_tags) {
    tagsArray.append(tag);
  }
  QString tagsJson = QString::fromUtf8(QJsonDocument(tagsArray).toJson(QJsonDocument::Compact));

  VxCoreError err = vxcore_file_update_tags(m_context, p_notebookId.toUtf8().constData(),
                                            p_filePath.toUtf8().constData(),
                                            tagsJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileTags failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "filePath:" << p_filePath;
    return false;
  }
  return true;
}

bool NotebookCoreService::updateNodeTimestamps(const QString &p_notebookId,
                                               const QString &p_nodePath, qint64 p_createdUtc,
                                               qint64 p_modifiedUtc) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_update_timestamps(m_context, p_notebookId.toUtf8().constData(),
                                                  p_nodePath.toUtf8().constData(), p_createdUtc,
                                                  p_modifiedUtc);
  if (err != VXCORE_OK) {
    qWarning() << "updateNodeTimestamps failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    return false;
  }
  return true;
}

bool NotebookCoreService::updateFileAttachments(const QString &p_notebookId,
                                                const QString &p_filePath,
                                                const QStringList &p_attachments) {
  if (!checkContext()) {
    return false;
  }

  QJsonArray attachArray;
  for (const auto &att : p_attachments) {
    attachArray.append(att);
  }
  QString attachJson =
      QString::fromUtf8(QJsonDocument(attachArray).toJson(QJsonDocument::Compact));

  VxCoreError err = vxcore_file_update_attachments(m_context, p_notebookId.toUtf8().constData(),
                                                   p_filePath.toUtf8().constData(),
                                                   attachJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileAttachments failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "filePath:" << p_filePath;
    return false;
  }
  return true;
}

QString NotebookCoreService::peekFile(const QString &p_notebookId, const QString &p_filePath,
                                       int p_maxChars) const {
  if (!checkContext()) {
    return QString();
  }

  char *content = nullptr;
  VxCoreError err =
      vxcore_file_peek(m_context, p_notebookId.toUtf8().constData(),
                       p_filePath.toUtf8().constData(), static_cast<size_t>(p_maxChars), &content);
  if (err != VXCORE_OK) {
    // Not found is expected for some files, don't warn
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "peekFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QString();
  }
  return cstrToQString(content);
}

QString NotebookCoreService::getAttachmentsFolder(const QString &p_notebookId,
                                                  const QString &p_filePath) const {
  if (!checkContext()) {
    return QString();
  }

  char *outPath = nullptr;
  VxCoreError err = vxcore_node_get_attachments_folder(
      m_context, p_notebookId.toUtf8().constData(), p_filePath.toUtf8().constData(), &outPath);
  if (err != VXCORE_OK) {
    qWarning() << "getAttachmentsFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(outPath);
}

QJsonArray NotebookCoreService::listAttachments(const QString &p_notebookId,
                                                const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *outJson = nullptr;
  VxCoreError err = vxcore_node_list_attachments(
      m_context, p_notebookId.toUtf8().constData(), p_filePath.toUtf8().constData(), &outJson);
  if (err != VXCORE_OK) {
    qWarning() << "listAttachments failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(outJson);
}

// Private methods.
bool NotebookCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "NotebookCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}

// Private helper methods.
QString NotebookCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

const char *NotebookCoreService::qstringToCStr(const QString &p_str) {
  if (p_str.isEmpty()) {
    return nullptr;
  }
  // Note: Returns pointer to temporary data. Only safe for immediate use.
  return p_str.toUtf8().constData();
}

QJsonObject NotebookCoreService::parseJsonObjectFromCStr(char *p_str) {
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

QJsonArray NotebookCoreService::parseJsonArrayFromCStr(char *p_str) {
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
