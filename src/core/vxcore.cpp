#include "vxcore.h"

#include <QDebug>
#include <QJsonDocument>

#include <core/configmgr.h>

using namespace vnotex;

VxCore::VxCore(QObject *p_parent) : QObject(p_parent) {
  vxcore_set_app_info(ConfigMgr::c_orgName.toUtf8().constData(),
                      ConfigMgr::c_appName.toUtf8().constData());
}

VxCore::~VxCore() {
  if (m_context) {
    unsubscribeEvents();
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

VxCore &VxCore::getInst() {
  static VxCore inst;
  return inst;
}

void VxCore::init(const QString &p_configJson) {
  if (m_context) {
    qWarning() << "VxCore already initialized";
    return;
  }

  const char *configCStr = qstringToCStr(p_configJson);
  VxCoreError err = vxcore_context_create(configCStr, &m_context);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("init"));
    return;
  }

  subscribeEvents();
  qDebug() << "VxCore initialized successfully";
}

bool VxCore::isInitialized() const { return m_context != nullptr; }

QString VxCore::getVersionString() { return QString::fromUtf8(vxcore_get_version_string()); }

QJsonObject VxCore::getVersion() {
  VxCoreVersion ver = vxcore_get_version();
  QJsonObject obj;
  obj[QStringLiteral("major")] = static_cast<int>(ver.major);
  obj[QStringLiteral("minor")] = static_cast<int>(ver.minor);
  obj[QStringLiteral("patch")] = static_cast<int>(ver.patch);
  return obj;
}

QString VxCore::getExecutionFilePath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  vxcore_get_execution_file_path(&path);
  return cstrToQString(path);
}

QString VxCore::getExecutionFolderPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  vxcore_get_execution_folder_path(&path);
  return cstrToQString(path);
}

QString VxCore::getDataPath(DataLocation location) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_context_get_data_path(
      m_context, static_cast<VxCoreDataLocation>(location), &path);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getDataPath"));
    return QString();
  }
  return cstrToQString(path);
}

QString VxCore::getConfigPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_context_get_config_path(m_context, &path);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getConfigPath"));
    return QString();
  }
  return cstrToQString(path);
}

QString VxCore::getSessionConfigPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_context_get_session_config_path(m_context, &path);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getSessionConfigPath"));
    return QString();
  }
  return cstrToQString(path);
}

QJsonObject VxCore::getConfig() const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_config(m_context, &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getConfig"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

QJsonObject VxCore::getSessionConfig() const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_session_config(m_context, &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getSessionConfig"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

QJsonObject VxCore::getConfigByName(DataLocation p_location, const QString &p_baseName) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_config_by_name(
      m_context, static_cast<VxCoreDataLocation>(p_location), p_baseName.toUtf8().constData(),
      &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getConfigByName"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

QJsonObject VxCore::getConfigByNameWithDefaults(DataLocation p_location,
                                                const QString &p_baseName,
                                                const QJsonObject &p_defaults) const {
  if (!checkContext()) {
    return p_defaults;
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_config_by_name_with_defaults(
    m_context, static_cast<VxCoreDataLocation>(p_location),
    p_baseName.toUtf8().constData(),
    QJsonDocument(p_defaults).toJson(QJsonDocument::Compact).constData(),
    &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getConfigByNameWithDefaults"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

void VxCore::updateConfigByName(DataLocation p_location, const QString &p_baseName,
                                const QString &p_json) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_context_update_config_by_name(
      m_context, static_cast<VxCoreDataLocation>(p_location), p_baseName.toUtf8().constData(),
      p_json.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("updateConfigByName"));
  }
}

QString VxCore::createNotebook(const QString &p_path, const QString &p_configJson,
                               NotebookType p_type) {
  if (!checkContext()) {
    return QString();
  }

  char *notebookId = nullptr;
  VxCoreError err =
      vxcore_notebook_create(m_context, p_path.toUtf8().constData(), p_configJson.toUtf8().constData(),
                             static_cast<VxCoreNotebookType>(p_type), &notebookId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("createNotebook"));
    return QString();
  }
  return cstrToQString(notebookId);
}

QString VxCore::openNotebook(const QString &p_path) {
  if (!checkContext()) {
    return QString();
  }

  char *notebookId = nullptr;
  VxCoreError err = vxcore_notebook_open(m_context, p_path.toUtf8().constData(), &notebookId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("openNotebook"));
    return QString();
  }
  return cstrToQString(notebookId);
}

void VxCore::closeNotebook(const QString &p_notebookId) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_notebook_close(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("closeNotebook"));
  }
}

QJsonArray VxCore::listNotebooks() const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_notebook_list(m_context, &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("listNotebooks"));
    return QJsonArray();
  }
  return parseJsonArray(cstrToQString(json));
}

QJsonObject VxCore::getNotebookConfig(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err =
      vxcore_notebook_get_config(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getNotebookConfig"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

void VxCore::updateNotebookConfig(const QString &p_notebookId, const QString &p_configJson) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_notebook_update_config(
      m_context, p_notebookId.toUtf8().constData(), p_configJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("updateNotebookConfig"));
  }
}

void VxCore::rebuildNotebookCache(const QString &p_notebookId) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_notebook_rebuild_cache(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("rebuildNotebookCache"));
  }
}

QString VxCore::createFolder(const QString &p_notebookId, const QString &p_parentPath,
                             const QString &p_folderName) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err =
      vxcore_folder_create(m_context, p_notebookId.toUtf8().constData(),
                           p_parentPath.toUtf8().constData(), p_folderName.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("createFolder"));
    return QString();
  }
  return cstrToQString(folderId);
}

QString VxCore::createFolderPath(const QString &p_notebookId, const QString &p_folderPath) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_create_path(m_context, p_notebookId.toUtf8().constData(),
                                              p_folderPath.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("createFolderPath"));
    return QString();
  }
  return cstrToQString(folderId);
}

void VxCore::deleteFolder(const QString &p_notebookId, const QString &p_folderPath) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_folder_delete(m_context, p_notebookId.toUtf8().constData(),
                                         p_folderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("deleteFolder"));
  }
}

QJsonObject VxCore::getFolderConfig(const QString &p_notebookId, const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_folder_get_config(m_context, p_notebookId.toUtf8().constData(),
                                             p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getFolderConfig"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

void VxCore::updateFolderMetadata(const QString &p_notebookId, const QString &p_folderPath,
                                  const QString &p_metadataJson) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err =
      vxcore_folder_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                    p_folderPath.toUtf8().constData(), p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("updateFolderMetadata"));
  }
}

QJsonObject VxCore::getFolderMetadata(const QString &p_notebookId,
                                      const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_folder_get_metadata(m_context, p_notebookId.toUtf8().constData(),
                                               p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getFolderMetadata"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

void VxCore::renameFolder(const QString &p_notebookId, const QString &p_folderPath,
                          const QString &p_newName) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_folder_rename(m_context, p_notebookId.toUtf8().constData(),
                                         p_folderPath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("renameFolder"));
  }
}

void VxCore::moveFolder(const QString &p_notebookId, const QString &p_srcPath,
                        const QString &p_destParentPath) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_folder_move(m_context, p_notebookId.toUtf8().constData(),
                                       p_srcPath.toUtf8().constData(), p_destParentPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("moveFolder"));
  }
}

QString VxCore::copyFolder(const QString &p_notebookId, const QString &p_srcPath,
                           const QString &p_destParentPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err =
      vxcore_folder_copy(m_context, p_notebookId.toUtf8().constData(), p_srcPath.toUtf8().constData(),
                         p_destParentPath.toUtf8().constData(), p_newName.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("copyFolder"));
    return QString();
  }
  return cstrToQString(folderId);
}

QString VxCore::createFile(const QString &p_notebookId, const QString &p_folderPath,
                           const QString &p_fileName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err =
      vxcore_file_create(m_context, p_notebookId.toUtf8().constData(), p_folderPath.toUtf8().constData(),
                         p_fileName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("createFile"));
    return QString();
  }
  return cstrToQString(fileId);
}

void VxCore::deleteFile(const QString &p_notebookId, const QString &p_filePath) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_file_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_filePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("deleteFile"));
  }
}

QJsonObject VxCore::getFileInfo(const QString &p_notebookId, const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_file_get_info(m_context, p_notebookId.toUtf8().constData(),
                                         p_filePath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getFileInfo"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

QJsonObject VxCore::getFileMetadata(const QString &p_notebookId, const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_file_get_metadata(m_context, p_notebookId.toUtf8().constData(),
                                             p_filePath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("getFileMetadata"));
    return QJsonObject();
  }
  return parseJsonObject(cstrToQString(json));
}

void VxCore::updateFileMetadata(const QString &p_notebookId, const QString &p_filePath,
                                const QString &p_metadataJson) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err =
      vxcore_file_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                  p_filePath.toUtf8().constData(), p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("updateFileMetadata"));
  }
}

void VxCore::renameFile(const QString &p_notebookId, const QString &p_filePath,
                        const QString &p_newName) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_file_rename(m_context, p_notebookId.toUtf8().constData(),
                                       p_filePath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("renameFile"));
  }
}

void VxCore::moveFile(const QString &p_notebookId, const QString &p_srcFilePath,
                      const QString &p_destFolderPath) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_file_move(m_context, p_notebookId.toUtf8().constData(),
                                     p_srcFilePath.toUtf8().constData(), p_destFolderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("moveFile"));
  }
}

QString VxCore::copyFile(const QString &p_notebookId, const QString &p_srcFilePath,
                         const QString &p_destFolderPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err =
      vxcore_file_copy(m_context, p_notebookId.toUtf8().constData(), p_srcFilePath.toUtf8().constData(),
                       p_destFolderPath.toUtf8().constData(), p_newName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("copyFile"));
    return QString();
  }
  return cstrToQString(fileId);
}

void VxCore::updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                            const QString &p_tagsJson) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err =
      vxcore_file_update_tags(m_context, p_notebookId.toUtf8().constData(),
                              p_filePath.toUtf8().constData(), p_tagsJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("updateFileTags"));
  }
}

void VxCore::tagFile(const QString &p_notebookId, const QString &p_filePath,
                     const QString &p_tagName) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_file_tag(m_context, p_notebookId.toUtf8().constData(),
                                    p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("tagFile"));
  }
}

void VxCore::untagFile(const QString &p_notebookId, const QString &p_filePath,
                       const QString &p_tagName) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_file_untag(m_context, p_notebookId.toUtf8().constData(),
                                      p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("untagFile"));
  }
}

void VxCore::createTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_tag_create(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("createTag"));
  }
}

void VxCore::createTagPath(const QString &p_notebookId, const QString &p_tagPath) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_tag_create_path(m_context, p_notebookId.toUtf8().constData(),
                                           p_tagPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("createTagPath"));
  }
}

void VxCore::deleteTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_tag_delete(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("deleteTag"));
  }
}

QJsonArray VxCore::listTags(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_tag_list(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("listTags"));
    return QJsonArray();
  }
  return parseJsonArray(cstrToQString(json));
}

void VxCore::moveTag(const QString &p_notebookId, const QString &p_tagName,
                     const QString &p_parentTag) {
  if (!checkContext()) {
    return;
  }

  VxCoreError err = vxcore_tag_move(m_context, p_notebookId.toUtf8().constData(),
                                    p_tagName.toUtf8().constData(), p_parentTag.toUtf8().constData());
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("moveTag"));
  }
}

QJsonArray VxCore::searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                               const QString &p_inputFilesJson) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err =
      vxcore_search_files(m_context, p_notebookId.toUtf8().constData(), p_queryJson.toUtf8().constData(),
                          qstringToCStr(p_inputFilesJson), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("searchFiles"));
    return QJsonArray();
  }
  return parseJsonArray(cstrToQString(json));
}

QJsonArray VxCore::searchContent(const QString &p_notebookId, const QString &p_queryJson,
                                 const QString &p_inputFilesJson) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_search_content(m_context, p_notebookId.toUtf8().constData(),
                                          p_queryJson.toUtf8().constData(),
                                          qstringToCStr(p_inputFilesJson), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("searchContent"));
    return QJsonArray();
  }
  return parseJsonArray(cstrToQString(json));
}

QJsonArray VxCore::searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                                const QString &p_inputFilesJson) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_search_by_tags(m_context, p_notebookId.toUtf8().constData(),
                                          p_queryJson.toUtf8().constData(),
                                          qstringToCStr(p_inputFilesJson), &json);
  if (err != VXCORE_OK) {
    handleError(err, QStringLiteral("searchByTags"));
    return QJsonArray();
  }
  return parseJsonArray(cstrToQString(json));
}

QString VxCore::getErrorMessage(VxCoreError p_error) {
  return QString::fromUtf8(vxcore_error_message(p_error));
}

bool VxCore::checkContext() const {
  if (!m_context) {
    qWarning() << "VxCore not initialized";
    emit const_cast<VxCore *>(this)->errorOccurred(VXCORE_ERR_NOT_INITIALIZED,
                                                   QStringLiteral("VxCore not initialized"));
    return false;
  }
  return true;
}

void VxCore::handleError(VxCoreError p_error, const QString &p_operation) const {
  QString msg = QStringLiteral("%1 failed: %2").arg(p_operation, getErrorMessage(p_error));
  qWarning() << msg;
  emit const_cast<VxCore *>(this)->errorOccurred(p_error, msg);
}

const char *VxCore::qstringToCStr(const QString &p_str) {
  if (p_str.isEmpty()) {
    return nullptr;
  }
  // Note: This returns a pointer to temporary data. Only safe for immediate use
  // in vxcore calls that copy the string internally.
  return p_str.toUtf8().constData();
}

QString VxCore::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

QJsonObject VxCore::parseJsonObject(const QString &p_json) {
  if (p_json.isEmpty()) {
    return QJsonObject();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(p_json.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON object:" << error.errorString();
    return QJsonObject();
  }
  return doc.object();
}

QJsonArray VxCore::parseJsonArray(const QString &p_json) {
  if (p_json.isEmpty()) {
    return QJsonArray();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(p_json.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON array:" << error.errorString();
    return QJsonArray();
  }
  return doc.array();
}

void VxCore::eventCallback(const VxCoreEvent *p_event, void *p_userData) {
  if (!p_event || !p_userData) {
    return;
  }

  VxCore *self = static_cast<VxCore *>(p_userData);
  QString payload = p_event->payload_json ? QString::fromUtf8(p_event->payload_json) : QString();
  quint64 timestamp = p_event->timestamp_ms;

  switch (p_event->type) {
  case VXCORE_EVENT_NOTE_CREATED:
    emit self->noteCreated(payload, timestamp);
    break;
  case VXCORE_EVENT_NOTE_UPDATED:
    emit self->noteUpdated(payload, timestamp);
    break;
  case VXCORE_EVENT_NOTE_DELETED:
    emit self->noteDeleted(payload, timestamp);
    break;
  case VXCORE_EVENT_NOTE_MOVED:
    emit self->noteMoved(payload, timestamp);
    break;
  case VXCORE_EVENT_TAG_ADDED:
    emit self->tagAdded(payload, timestamp);
    break;
  case VXCORE_EVENT_TAG_REMOVED:
    emit self->tagRemoved(payload, timestamp);
    break;
  case VXCORE_EVENT_ATTACHMENT_ADDED:
    emit self->attachmentAdded(payload, timestamp);
    break;
  case VXCORE_EVENT_ATTACHMENT_REMOVED:
    emit self->attachmentRemoved(payload, timestamp);
    break;
  case VXCORE_EVENT_NOTEBOOK_OPENED:
    emit self->notebookOpened(payload, timestamp);
    break;
  case VXCORE_EVENT_NOTEBOOK_CLOSED:
    emit self->notebookClosed(payload, timestamp);
    break;
  case VXCORE_EVENT_INDEX_UPDATED:
    emit self->indexUpdated(payload, timestamp);
    break;
  }
}

void VxCore::subscribeEvents() {
  if (!m_context) {
    return;
  }

  // Subscribe to all event types.
  const VxCoreEventType eventTypes[] = {
      VXCORE_EVENT_NOTE_CREATED,      VXCORE_EVENT_NOTE_UPDATED,
      VXCORE_EVENT_NOTE_DELETED,      VXCORE_EVENT_NOTE_MOVED,
      VXCORE_EVENT_TAG_ADDED,         VXCORE_EVENT_TAG_REMOVED,
      VXCORE_EVENT_ATTACHMENT_ADDED,  VXCORE_EVENT_ATTACHMENT_REMOVED,
      VXCORE_EVENT_NOTEBOOK_OPENED,   VXCORE_EVENT_NOTEBOOK_CLOSED,
      VXCORE_EVENT_INDEX_UPDATED,
  };

  for (VxCoreEventType eventType : eventTypes) {
    vxcore_event_subscribe(m_context, eventType, eventCallback, this);
  }
}

void VxCore::unsubscribeEvents() {
  if (!m_context) {
    return;
  }

  const VxCoreEventType eventTypes[] = {
      VXCORE_EVENT_NOTE_CREATED,      VXCORE_EVENT_NOTE_UPDATED,
      VXCORE_EVENT_NOTE_DELETED,      VXCORE_EVENT_NOTE_MOVED,
      VXCORE_EVENT_TAG_ADDED,         VXCORE_EVENT_TAG_REMOVED,
      VXCORE_EVENT_ATTACHMENT_ADDED,  VXCORE_EVENT_ATTACHMENT_REMOVED,
      VXCORE_EVENT_NOTEBOOK_OPENED,   VXCORE_EVENT_NOTEBOOK_CLOSED,
      VXCORE_EVENT_INDEX_UPDATED,
  };

  for (VxCoreEventType eventType : eventTypes) {
    vxcore_event_unsubscribe(m_context, eventType, eventCallback);
  }
}
