#ifndef CORE_VXCORE_H
#define CORE_VXCORE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "noncopyable.h"

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_events.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// Qt wrapper for notebook types matching VxCoreNotebookType.
enum class NotebookType { Bundled = VXCORE_NOTEBOOK_BUNDLED, Raw = VXCORE_NOTEBOOK_RAW };

// Qt wrapper for data locations matching VxCoreDataLocation.
enum class DataLocation { App = VXCORE_DATA_APP, Local = VXCORE_DATA_LOCAL };

// Singleton wrapper for vxcore C API.
// Provides Qt-friendly interface with automatic memory management and signal/slot support.
class VxCore : public QObject, private Noncopyable {
  Q_OBJECT

public:
  static VxCore &getInst();

  // Initialize the vxcore context. Call once at application startup.
  // @p_configJson: Optional JSON configuration string (nullptr for defaults).
  void init(const QString &p_configJson = QString());

  // Check if context is initialized.
  bool isInitialized() const;

  // Get vxcore version information.
  static QString getVersionString();
  static QJsonObject getVersion();

  QString getExecutionFilePath() const;
  QString getExecutionFolderPath() const;

  // Configuration access.
  QString getDataPath(DataLocation location) const;
  QString getConfigPath() const;
  QString getSessionConfigPath() const;
  QJsonObject getConfig() const;
  QJsonObject getSessionConfig() const;
  QJsonObject getConfigByName(DataLocation p_location, const QString &p_baseName) const;
  QJsonObject getConfigByNameWithDefaults(DataLocation p_location, const QString &p_baseName, const QJsonObject &p_defaults) const;
  void updateConfigByName(DataLocation p_location, const QString &p_baseName,
                          const QString &p_json);

  // Notebook operations.
  QString createNotebook(const QString &p_path, const QString &p_configJson, NotebookType p_type);
  QString openNotebook(const QString &p_path);
  void closeNotebook(const QString &p_notebookId);
  QJsonArray listNotebooks() const;
  QJsonObject getNotebookConfig(const QString &p_notebookId) const;
  void updateNotebookConfig(const QString &p_notebookId, const QString &p_configJson);
  void rebuildNotebookCache(const QString &p_notebookId);

  // Folder operations.
  QString createFolder(const QString &p_notebookId, const QString &p_parentPath,
                       const QString &p_folderName);
  QString createFolderPath(const QString &p_notebookId, const QString &p_folderPath);
  void deleteFolder(const QString &p_notebookId, const QString &p_folderPath);
  QJsonObject getFolderConfig(const QString &p_notebookId, const QString &p_folderPath) const;
  void updateFolderMetadata(const QString &p_notebookId, const QString &p_folderPath,
                            const QString &p_metadataJson);
  QJsonObject getFolderMetadata(const QString &p_notebookId, const QString &p_folderPath) const;
  void renameFolder(const QString &p_notebookId, const QString &p_folderPath,
                    const QString &p_newName);
  void moveFolder(const QString &p_notebookId, const QString &p_srcPath,
                  const QString &p_destParentPath);
  QString copyFolder(const QString &p_notebookId, const QString &p_srcPath,
                     const QString &p_destParentPath, const QString &p_newName);

  // File operations.
  QString createFile(const QString &p_notebookId, const QString &p_folderPath,
                     const QString &p_fileName);
  void deleteFile(const QString &p_notebookId, const QString &p_filePath);
  QJsonObject getFileInfo(const QString &p_notebookId, const QString &p_filePath) const;
  QJsonObject getFileMetadata(const QString &p_notebookId, const QString &p_filePath) const;
  void updateFileMetadata(const QString &p_notebookId, const QString &p_filePath,
                          const QString &p_metadataJson);
  void renameFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_newName);
  void moveFile(const QString &p_notebookId, const QString &p_srcFilePath,
                const QString &p_destFolderPath);
  QString copyFile(const QString &p_notebookId, const QString &p_srcFilePath,
                   const QString &p_destFolderPath, const QString &p_newName);

  // Tag operations on files.
  void updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QString &p_tagsJson);
  void tagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  void untagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);

  // Tag management.
  void createTag(const QString &p_notebookId, const QString &p_tagName);
  void createTagPath(const QString &p_notebookId, const QString &p_tagPath);
  void deleteTag(const QString &p_notebookId, const QString &p_tagName);
  QJsonArray listTags(const QString &p_notebookId) const;
  void moveTag(const QString &p_notebookId, const QString &p_tagName, const QString &p_parentTag);

  // Search operations.
  QJsonArray searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                         const QString &p_inputFilesJson = QString()) const;
  QJsonArray searchContent(const QString &p_notebookId, const QString &p_queryJson,
                           const QString &p_inputFilesJson = QString()) const;
  QJsonArray searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                          const QString &p_inputFilesJson = QString()) const;

  // Get human-readable error message for a VxCoreError code.
  static QString getErrorMessage(VxCoreError p_error);

signals:
  // Emitted when a vxcore operation fails.
  void errorOccurred(VxCoreError p_error, const QString &p_message);

  // Event signals for vxcore events.
  void noteCreated(const QString &p_payloadJson, quint64 p_timestampMs);
  void noteUpdated(const QString &p_payloadJson, quint64 p_timestampMs);
  void noteDeleted(const QString &p_payloadJson, quint64 p_timestampMs);
  void noteMoved(const QString &p_payloadJson, quint64 p_timestampMs);
  void tagAdded(const QString &p_payloadJson, quint64 p_timestampMs);
  void tagRemoved(const QString &p_payloadJson, quint64 p_timestampMs);
  void attachmentAdded(const QString &p_payloadJson, quint64 p_timestampMs);
  void attachmentRemoved(const QString &p_payloadJson, quint64 p_timestampMs);
  void notebookOpened(const QString &p_payloadJson, quint64 p_timestampMs);
  void notebookClosed(const QString &p_payloadJson, quint64 p_timestampMs);
  void indexUpdated(const QString &p_payloadJson, quint64 p_timestampMs);

private:
  explicit VxCore(QObject *p_parent = nullptr);
  ~VxCore();

  // Check if context is valid, emit error if not.
  bool checkContext() const;

  // Handle vxcore error: emit signal and optionally throw.
  void handleError(VxCoreError p_error, const QString &p_operation) const;

  // Convert QString to C string (returns nullptr for empty strings).
  static const char *qstringToCStr(const QString &p_str);

  // Convert C string to QString and free the C string.
  static QString cstrToQString(char *p_str);

  // Parse JSON string to QJsonObject.
  static QJsonObject parseJsonObject(const QString &p_json);

  // Parse JSON string to QJsonArray.
  static QJsonArray parseJsonArray(const QString &p_json);

  // Static event callback for vxcore events.
  static void eventCallback(const VxCoreEvent *p_event, void *p_userData);

  // Subscribe to all vxcore events.
  void subscribeEvents();

  // Unsubscribe from all vxcore events.
  void unsubscribeEvents();

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // CORE_VXCORE_H
