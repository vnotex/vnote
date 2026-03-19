#ifndef NOTEBOOKCORESERVICE_H
#define NOTEBOOKCORESERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// Qt wrapper for notebook types matching VxCoreNotebookType.
enum class NotebookType { Bundled = VXCORE_NOTEBOOK_BUNDLED, Raw = VXCORE_NOTEBOOK_RAW };

class HookManager;

// Service layer for notebook operations. Wraps VxCore C API and provides Qt signals.
// NotebookCoreService IS the new notebook layer - replaces legacy NotebookMgr.
class NotebookCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit NotebookCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~NotebookCoreService();

  // Set HookManager for firing node operation hooks.
  // Called from main() after both services are constructed.
  void setHookManager(HookManager *p_hookMgr);

  // Notebook operations (7 methods).
  QString createNotebook(const QString &p_path, const QString &p_configJson, NotebookType p_type);
  QString openNotebook(const QString &p_path);
  bool closeNotebook(const QString &p_notebookId);
  QJsonArray listNotebooks() const;
  QJsonObject getNotebookConfig(const QString &p_notebookId) const;
  bool updateNotebookConfig(const QString &p_notebookId, const QString &p_configJson);
  bool rebuildNotebookCache(const QString &p_notebookId);

  // Resolve an absolute path to its containing notebook.
  // Returns JSON with "notebookId" and "relativePath" keys.
  // Returns empty object if path is not within any open notebook.
  QJsonObject resolvePathToNotebook(const QString &p_absolutePath) const;

  // Build an absolute path from a notebook ID and a relative path.
  // Returns empty string if the notebook is not open.
  QString buildAbsolutePath(const QString &p_notebookId, const QString &p_relativePath) const;

  // Get relative path of a node (file or folder) by its ID.
  // Returns empty string if node is not found.
  QString getNodePathById(const QString &p_notebookId, const QString &p_nodeId) const;

  // Remove a node (file or folder) from the notebook index.
  // Files remain on disk - only metadata is removed.
  bool unindexNode(const QString &p_notebookId, const QString &p_nodePath);

  // Recycle bin operations (bundled notebooks only).
  QString getRecycleBinPath(const QString &p_notebookId) const;
  bool emptyRecycleBin(const QString &p_notebookId);
  // Folder operations (10 methods).
  QString createFolder(const QString &p_notebookId, const QString &p_parentPath,
                       const QString &p_folderName);
  QString createFolderPath(const QString &p_notebookId, const QString &p_folderPath);
  // Delete a folder. Fires NodeBeforeDelete hook (cancellable) before deletion.
  // Returns false if cancelled by hook or if deletion fails.
  bool deleteFolder(const QString &p_notebookId, const QString &p_folderPath);
  QJsonObject getFolderConfig(const QString &p_notebookId, const QString &p_folderPath) const;
  bool updateFolderMetadata(const QString &p_notebookId, const QString &p_folderPath,
                            const QString &p_metadataJson);
  QJsonObject getFolderMetadata(const QString &p_notebookId, const QString &p_folderPath) const;
  // Rename a folder. Fires NodeBeforeRename hook (cancellable) before rename,
  // and NodeAfterRename hook after success. Returns false if cancelled or failed.
  bool renameFolder(const QString &p_notebookId, const QString &p_folderPath,
                    const QString &p_newName);

  // Move a folder. Fires NodeBeforeMove hook (cancellable) before move.
  // Returns false if cancelled or failed.
  bool moveFolder(const QString &p_notebookId, const QString &p_srcPath,
                  const QString &p_destParentPath);
  QString copyFolder(const QString &p_notebookId, const QString &p_srcPath,
                     const QString &p_destParentPath, const QString &p_newName);

  // List direct children of a folder (files and subfolders).
  // Returns JSON with "files" and "folders" arrays.
  QJsonObject listFolderChildren(const QString &p_notebookId, const QString &p_folderPath) const;

  // List external (unindexed) nodes in a folder.
  // External nodes exist on filesystem but are not tracked in metadata.
  // Returns JSON with "files" and "folders" arrays (each entry has "name" and "type" only).
  QJsonObject listFolderExternal(const QString &p_notebookId, const QString &p_folderPath) const;

  // Index an external node (file or folder) into the notebook metadata.
  // The node must exist on filesystem but not be tracked in metadata.
  // Returns true on success, false if node doesn't exist or is already indexed.
  bool indexNode(const QString &p_notebookId, const QString &p_nodePath);

  // Get an available (non-conflicting) name for a new node in the given folder.
  // Returns p_desiredName if available, or a modified version (e.g., 'name_1') if not.
  // Returns empty string on error.
  QString getAvailableName(const QString &p_notebookId, const QString &p_folderPath,
                           const QString &p_desiredName) const;

  // File operations (8 methods).
  QString createFile(const QString &p_notebookId, const QString &p_folderPath,
                     const QString &p_fileName);
  // Delete a file. Fires NodeBeforeDelete hook (cancellable) before deletion.
  // Returns false if cancelled by hook or if deletion fails.
  bool deleteFile(const QString &p_notebookId, const QString &p_filePath);
  QJsonObject getFileInfo(const QString &p_notebookId, const QString &p_filePath) const;
  QJsonObject getFileMetadata(const QString &p_notebookId, const QString &p_filePath) const;
  bool updateFileMetadata(const QString &p_notebookId, const QString &p_filePath,
                          const QString &p_metadataJson);
  // Rename a file. Fires NodeBeforeRename hook (cancellable) before rename,
  // and NodeAfterRename hook after success. Returns false if cancelled or failed.
  bool renameFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_newName);

  // Move a file. Fires NodeBeforeMove hook (cancellable) before move.
  // Returns false if cancelled or failed.
  bool moveFile(const QString &p_notebookId, const QString &p_srcFilePath,
                const QString &p_destFolderPath);
  QString copyFile(const QString &p_notebookId, const QString &p_srcFilePath,
                   const QString &p_destFolderPath, const QString &p_newName);
  // Import external file into notebook folder (copies file and adds to index).
  // Returns file ID on success, empty string on failure.
  QString importFile(const QString &p_notebookId, const QString &p_folderPath,
                     const QString &p_externalFilePath);

  // Import external folder into notebook folder (copies folder recursively and adds to index).
  // p_suffixAllowlist: semicolon-separated list of file extensions (e.g., "md;txt"), or empty to import all.
  // Returns folder ID on success, empty string on failure.
  QString importFolder(const QString &p_notebookId, const QString &p_destFolderPath,
                       const QString &p_externalFolderPath, const QString &p_suffixAllowlist = QString());

  // Peek file content (first N characters for preview).
  // p_maxChars: maximum number of characters to return (default 256).
  // Returns empty string if file doesn't exist or on error.
  QString peekFile(const QString &p_notebookId, const QString &p_filePath, int p_maxChars = 256) const;

  // Tag operations (8 methods).
  bool updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QString &p_tagsJson);
  bool tagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  bool untagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  bool createTag(const QString &p_notebookId, const QString &p_tagName);
  bool createTagPath(const QString &p_notebookId, const QString &p_tagPath);
  bool deleteTag(const QString &p_notebookId, const QString &p_tagName);
  QJsonArray listTags(const QString &p_notebookId) const;
  bool moveTag(const QString &p_notebookId, const QString &p_tagName, const QString &p_parentTag);

private:
  // Check context validity before operations.
  bool checkContext() const;

  // Convert C string from vxcore and free it.
  static QString cstrToQString(char *p_str);

  // Convert QString to C string for vxcore (immediate use only).
  static const char *qstringToCStr(const QString &p_str);

  // Parse JSON string to QJsonObject from C string (takes ownership, frees p_str).
  static QJsonObject parseJsonObjectFromCStr(char *p_str);

  // Parse JSON string to QJsonArray from C string (takes ownership, frees p_str).
  static QJsonArray parseJsonArrayFromCStr(char *p_str);

  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
};

} // namespace vnotex

#endif // NOTEBOOKCORESERVICE_H
