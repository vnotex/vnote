#ifndef BUFFERCORESERVICE_H
#define BUFFERCORESERVICE_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <core/noncopyable.h>
#include <core/services/ibuffercoreservice.h>
#include <utils/qtcompat.h>

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

// Service layer for buffer operations. Wraps VxCore buffer C API and provides Qt-friendly
// interface. BufferCoreService manages open file buffers - opening, closing, reading/writing
// content, and assets.
class BufferCoreService : public QObject, public IBufferCoreService, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit BufferCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~BufferCoreService();

  // ============ Buffer Lifecycle ============

  // Open a file as a buffer.
  // p_notebookId: ID of the notebook (empty for external files).
  // p_filePath: Relative path (notebook) or absolute path (external).
  // p_outErr: optional out-pointer for the raw VxCoreError from vxcore_buffer_open.
  //   Set to VXCORE_OK on success. The empty-string return cannot distinguish
  //   VXCORE_ERR_NODE_NOT_EXISTS (bundled file whose content is gone on disk)
  //   from any other failure; callers that need to detect a missing-on-disk
  //   node should pass a non-null p_outErr and inspect it on empty return.
  // Returns buffer ID, or empty string on failure.
  QString openBuffer(const QString &p_notebookId, const QString &p_filePath,
                     VxCoreError *p_outErr = nullptr);

  // Open a file as a buffer by its node ID (UUID).
  // Combines UUID resolution and buffer opening in a single atomic vxcore call.
  // Returns buffer ID, or empty string on failure.
  QString openBufferByNodeId(const QString &p_nodeId);

  // Open a virtual buffer (no filesystem backing).
  // @p_address: Virtual address (e.g., "vx://settings").
  // Returns buffer ID, or empty string on failure.
  QString openVirtualBuffer(const QString &p_address);

  // Close a buffer by ID.
  bool closeBuffer(const QString &p_bufferId);

  // Get buffer configuration as JSON.
  QJsonObject getBuffer(const QString &p_bufferId) const;

  // List all open buffers as JSON array.
  QJsonArray listBuffers() const;

  // ============ Buffer Content ============

  // Save buffer content to disk.
  //
  // THREADING: This call may block on filesystem / libgit2 contention. UI-thread
  // callers MUST prefer BufferService + BufferSaveQueue::enqueue, which dispatches
  // the save to a worker and serializes per-notebook via NotebookIoGate. Direct
  // calls to this method are permitted ONLY from worker threads (e.g. the
  // BufferSaveQueue worker itself) or from test code.
  bool saveBuffer(const QString &p_bufferId) override;

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
  // (setContent, setContentRaw, save, reload, close).
  QByteArrayViewCompat peekContentRaw(const QString &p_bufferId) const;

  // Set buffer content from raw bytes.
  bool setContentRaw(const QString &p_bufferId, const QByteArray &p_data) override;

  // ============ Buffer State ============

  // Get buffer state.
  BufferState getState(const QString &p_bufferId) const;

  // Check if buffer has unsaved modifications.
  bool isModified(const QString &p_bufferId) const;

  // Check if the buffer's owning notebook is read-only.
  // Resolves the buffer ID to its owning notebook, then queries the notebook's
  // read-only flag via vxcore_notebook_is_read_only.
  // Returns false on any error (buffer not found, notebook not found, vxcore error).
  bool isBufferReadOnly(const QString &p_bufferId) const override;

  // Check if the buffer's file has been modified or deleted externally.
  // Updates the buffer's internal state (query with getState() afterwards).
  // Returns true on success, false on failure.
  bool checkExternalChanges(const QString &p_bufferId);

  // Get buffer content revision number.
  // Lightweight — avoids full JSON parsing from getBuffer().
  int getRevision(const QString &p_bufferId) const;

  // ============ Buffer Backup ============

  // Write buffer's in-memory content to a backup file (.vswp).
  bool writeBackup(const QString &p_bufferId);

  // Get the backup file path for a buffer.
  // Returns empty string on failure.
  QString getBackupPath(const QString &p_bufferId) const;

  // ============ Path Resolution ============

  // Resolve the absolute path for a notebook file or external file.
  // If p_notebookId is empty, p_filePath is already absolute — returned as-is.
  // Otherwise, calls vxcore_path_build_absolute to combine notebook root + relative path.
  // Returns empty string on failure.
  QString getResolvedPath(const QString &p_notebookId, const QString &p_filePath) const;

  // Resolve a node ID (UUID) to its notebook ID and relative path.
  // This only resolves the identity — it does NOT open a buffer.
  // Returns true if resolution succeeded, false if UUID not found or error.
  bool resolveNodeId(const QString &p_nodeId, QString &p_outNotebookId,
                     QString &p_outRelativePath) const;

  // Get the resource base path for resolving relative URLs in buffer content.
  // Returns empty string on failure.
  QString getResourceBasePath(const QString &p_bufferId) const;

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

protected:
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
