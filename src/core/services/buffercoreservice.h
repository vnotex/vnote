#ifndef BUFFERCORESERVICE_H
#define BUFFERCORESERVICE_H

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// Qt wrapper for VxCoreBufferState.
enum class BufferState {
  Normal = VXCORE_BUFFER_NORMAL,
  FileMissing = VXCORE_BUFFER_FILE_MISSING,
  FileChanged = VXCORE_BUFFER_FILE_CHANGED,
  SaveFailed = VXCORE_BUFFER_SAVE_FAILED
};

// Service layer for buffer operations. Wraps VxCore buffer C API and provides Qt-friendly interface.
// BufferCoreService manages open file buffers - opening, closing, reading/writing content, and assets.
class BufferCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit BufferCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~BufferCoreService();

  // ============ Buffer Lifecycle ============

  // Open a file as a buffer.
  // p_notebookId: ID of the notebook (empty for external files).
  // p_filePath: Relative path (notebook) or absolute path (external).
  // Returns buffer ID, or empty string on failure.
  QString openBuffer(const QString &p_notebookId, const QString &p_filePath);

  // Close a buffer by ID.
  bool closeBuffer(const QString &p_bufferId);

  // Get buffer configuration as JSON.
  QJsonObject getBuffer(const QString &p_bufferId) const;

  // List all open buffers as JSON array.
  QJsonArray listBuffers() const;

  // ============ Buffer Content ============

  // Save buffer content to disk.
  bool saveBuffer(const QString &p_bufferId);

  // Reload buffer content from disk.
  bool reloadBuffer(const QString &p_bufferId);

  // Get buffer content as JSON (hex-encoded).
  QJsonObject getContent(const QString &p_bufferId) const;

  // Set buffer content from JSON (hex-encoded).
  bool setContent(const QString &p_bufferId, const QString &p_contentJson);

  // Get buffer content as raw bytes.
  QByteArray getContentRaw(const QString &p_bufferId) const;

  // Return a non-owning view over the buffer's raw content held by vxcore.
  // The view is only valid until the next buffer-mutating operation
  // (setContent, setContentRaw, save, reload, close, autoSaveTick).
  QByteArrayView peekContentRaw(const QString &p_bufferId) const;

  // Set buffer content from raw bytes.
  bool setContentRaw(const QString &p_bufferId, const QByteArray &p_data);

  // ============ Buffer State ============

  // Get buffer state.
  BufferState getState(const QString &p_bufferId) const;

  // Check if buffer has unsaved modifications.
  bool isModified(const QString &p_bufferId) const;

  // ============ Auto-Save ============

  // Trigger auto-save for modified buffers.
  bool autoSaveTick();

  // Set auto-save interval in milliseconds.
  bool setAutoSaveInterval(qint64 p_intervalMs);

  // ============ Asset Operations (Filesystem Only) ============

  // Insert binary data as an asset file.
  // Returns relative path for embedding, or empty string on failure.
  QString insertAssetRaw(const QString &p_bufferId, const QString &p_assetName,
                         const QByteArray &p_data);

  // Copy a file to assets folder.
  // Returns relative path for embedding, or empty string on failure.
  QString insertAsset(const QString &p_bufferId, const QString &p_sourcePath);

  // Delete an asset file.
  bool deleteAsset(const QString &p_bufferId, const QString &p_relativePath);

  // Get absolute path to the buffer's assets folder.
  QString getAssetsFolder(const QString &p_bufferId) const;

  // ============ Attachment Operations (Filesystem + Metadata) ============

  // Copy a file to attachments folder and add to attachment list.
  // Returns the filename (not full path), or empty string on failure.
  QString insertAttachment(const QString &p_bufferId, const QString &p_sourcePath);

  // Delete an attachment file and remove from attachment list.
  bool deleteAttachment(const QString &p_bufferId, const QString &p_filename);

  // Rename an attachment file and update attachment list.
  // Returns the actual new filename (may differ on collision), or empty string on failure.
  QString renameAttachment(const QString &p_bufferId, const QString &p_oldFilename,
                           const QString &p_newFilename);

  // List all attachments as JSON array of filenames.
  QJsonArray listAttachments(const QString &p_bufferId) const;

  // Get absolute path to the buffer's attachments folder.
  QString getAttachmentsFolder(const QString &p_bufferId) const;

private:
  // Check context validity before operations.
  bool checkContext() const;

  // Convert C string from vxcore and free it.
  static QString cstrToQString(char *p_str);

  // Parse JSON string to QJsonObject from C string (takes ownership, frees p_str).
  static QJsonObject parseJsonObjectFromCStr(char *p_str);

  // Parse JSON string to QJsonArray from C string (takes ownership, frees p_str).
  static QJsonArray parseJsonArrayFromCStr(char *p_str);

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // BUFFERCORESERVICE_H
