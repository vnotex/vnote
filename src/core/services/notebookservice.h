#ifndef NOTEBOOKSERVICE_H
#define NOTEBOOKSERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_events.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// Qt wrapper for notebook types matching VxCoreNotebookType.
enum class NotebookType { Bundled = VXCORE_NOTEBOOK_BUNDLED, Raw = VXCORE_NOTEBOOK_RAW };

// Service layer for notebook operations. Wraps VxCore C API and provides Qt signals.
// NotebookService IS the new notebook layer - replaces legacy NotebookMgr.
class NotebookService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit NotebookService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~NotebookService();

  // Notebook operations (7 methods).
  QString createNotebook(const QString &p_path, const QString &p_configJson, NotebookType p_type);
  QString openNotebook(const QString &p_path);
  void closeNotebook(const QString &p_notebookId);
  QJsonArray listNotebooks() const;
  QJsonObject getNotebookConfig(const QString &p_notebookId) const;
  void updateNotebookConfig(const QString &p_notebookId, const QString &p_configJson);
  void rebuildNotebookCache(const QString &p_notebookId);

  // Folder operations (10 methods).
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

  // File operations (8 methods).
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

  // Tag operations (8 methods).
  void updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QString &p_tagsJson);
  void tagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  void untagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);
  void createTag(const QString &p_notebookId, const QString &p_tagName);
  void createTagPath(const QString &p_notebookId, const QString &p_tagPath);
  void deleteTag(const QString &p_notebookId, const QString &p_tagName);
  QJsonArray listTags(const QString &p_notebookId) const;
  void moveTag(const QString &p_notebookId, const QString &p_tagName, const QString &p_parentTag);

signals:
  // Domain-specific signals for notebook events (8 signals).
  // Per task spec: noteCreated, noteUpdated, noteDeleted, noteMoved, tagAdded, tagRemoved,
  // notebookOpened, notebookClosed.
  // Attachment signals and indexUpdated are deferred as per MUST NOT DO.
  void noteCreated(const QString &p_payloadJson, quint64 p_timestampMs);
  void noteUpdated(const QString &p_payloadJson, quint64 p_timestampMs);
  void noteDeleted(const QString &p_payloadJson, quint64 p_timestampMs);
  void noteMoved(const QString &p_payloadJson, quint64 p_timestampMs);
  void tagAdded(const QString &p_payloadJson, quint64 p_timestampMs);
  void tagRemoved(const QString &p_payloadJson, quint64 p_timestampMs);
  void notebookOpened(const QString &p_payloadJson, quint64 p_timestampMs);
  void notebookClosed(const QString &p_payloadJson, quint64 p_timestampMs);

private:
  // Check context validity before operations.
  bool checkContext() const;

  // Static callback for vxcore events - routes to instance method.
  static void eventCallback(const VxCoreEvent *p_event, void *p_userData);

  // Instance method to handle events and emit Qt signals.
  void handleEvent(VxCoreEventType p_eventType, const QString &p_payloadJson,
                   quint64 p_timestampMs);

  // Convert C string from vxcore and free it.
  static QString cstrToQString(char *p_str);

  // Convert QString to C string for vxcore (immediate use only).
  static const char *qstringToCStr(const QString &p_str);

  // Parse JSON string to QJsonObject.
  static QJsonObject parseJsonObject(const QString &p_json);

  // Parse JSON string to QJsonArray.
  static QJsonArray parseJsonArray(const QString &p_json);

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // NOTEBOOKSERVICE_H
