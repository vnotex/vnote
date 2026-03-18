#ifndef BUFFER2_H
#define BUFFER2_H

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <core/nodeidentifier.h>

namespace vnotex {

class BufferCoreService;
class HookManager;
enum class BufferState;

// Lightweight copyable handle to an open buffer (like QModelIndex).
// Stores pointers to services and a buffer ID. Does NOT own the buffer —
// BufferService manages buffer lifecycle (open/close).
//
// Obtain a Buffer2 from BufferService::openBuffer().
// All per-buffer operations (save, content read/write, assets, attachments)
// are methods on this handle, delegating to BufferCoreService or firing
// hooks via HookManager as appropriate.
class Buffer2 {
public:
  // Default constructor creates an invalid (null) buffer handle.
  Buffer2();

  // Construct a valid buffer handle. Called by BufferService::openBuffer().
  Buffer2(BufferCoreService *p_bufferCoreService, HookManager *p_hookMgr,
          const QString &p_bufferId, const NodeIdentifier &p_nodeId);

  // Check whether this handle refers to a valid open buffer.
  bool isValid() const;

  // Get the buffer ID.
  QString id() const;

  // Get the node identifier (notebook + relative path) for this buffer.
  const NodeIdentifier &nodeId() const;

  // ============ Buffer Content ============

  // Save buffer content to disk.
  // Fires FileBeforeSave (cancellable) and FileAfterSave hooks.
  bool save();

  // Reload buffer content from disk.
  bool reload();

  // Get buffer content as JSON (hex-encoded).
  QJsonObject getContent() const;

  // Set buffer content from JSON (hex-encoded).
  bool setContent(const QString &p_contentJson);

  // Get buffer content as raw bytes.
  QByteArray getContentRaw() const;

  // Return a non-owning view over the buffer's raw content held by vxcore.
  // The view is only valid until the next buffer-mutating operation
  // (setContent, setContentRaw, save, reload, close).
  QByteArrayView peekContentRaw() const;

  // Set buffer content from raw bytes.
  bool setContentRaw(const QByteArray &p_data);

  // ============ Buffer State ============

  // Get buffer state.
  BufferState getState() const;

  // Check if buffer has unsaved modifications.
  bool isModified() const;

  // Get buffer content revision number.
  int getRevision() const;

  // ============ Buffer Info ============

  // Get buffer configuration as JSON.
  QJsonObject getBuffer() const;

  // ============ Asset Operations (Filesystem Only) ============

  // Insert binary data as an asset file.
  // Returns relative path for embedding, or empty string on failure.
  QString insertAssetRaw(const QString &p_assetName, const QByteArray &p_data);

  // Copy a file to assets folder.
  // Returns relative path for embedding, or empty string on failure.
  QString insertAsset(const QString &p_sourcePath);

  // Delete an asset file.
  bool deleteAsset(const QString &p_relativePath);

  // Get absolute path to the buffer's assets folder.
  QString getAssetsFolder() const;

  // ============ Attachment Operations (Filesystem + Metadata) ============

  // Copy a file to attachments folder and add to attachment list.
  // Returns the filename (not full path), or empty string on failure.
  QString insertAttachment(const QString &p_sourcePath);

  // Delete an attachment file and remove from attachment list.
  bool deleteAttachment(const QString &p_filename);

  // Rename an attachment file and update attachment list.
  // Returns the actual new filename (may differ on collision), or empty string on failure.
  QString renameAttachment(const QString &p_oldFilename, const QString &p_newFilename);

  // List all attachments as JSON array of filenames.
  QJsonArray listAttachments() const;

  // Get absolute path to the buffer's attachments folder.
  QString getAttachmentsFolder() const;

private:
  friend class BufferService;
  friend class ViewWindow2;

  // Update the node identifier after a rename operation.
  void setNodeId(const NodeIdentifier &p_nodeId) { m_nodeId = p_nodeId; }

  BufferCoreService *m_bufferCoreService = nullptr;
  HookManager *m_hookMgr = nullptr;
  QString m_bufferId;
  NodeIdentifier m_nodeId;
};

} // namespace vnotex

#endif // BUFFER2_H
