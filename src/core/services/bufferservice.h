#ifndef BUFFERSERVICE_H
#define BUFFERSERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <core/fileopensettings.h>
#include <core/services/buffer2.h>
#include <core/services/buffercoreservice.h>

namespace vnotex {

class HookManager;

// Hook-aware wrapper around BufferCoreService.
// Manages buffer lifecycle (open, close) with vnote.file.* hooks.
// Per-buffer operations are accessed via the Buffer handle returned by openBuffer().
class BufferService : private BufferCoreService {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle and HookManager for hook integration.
  explicit BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                         QObject *p_parent = nullptr);
  ~BufferService() override;

  // ============ Buffer Lifecycle (with hooks) ============

  // Open a file as a buffer.
  // Fires FileBeforeOpen (cancellable) and FileAfterOpen.
  // @p_settings controls how the file should be opened (mode, readOnly, lineNumber, etc.).
  // Returns a Buffer2 handle for per-buffer operations, or an invalid Buffer2 on failure/cancel.
  Buffer2 openBuffer(const NodeIdentifier &p_nodeId,
                     const FileOpenSettings &p_settings = FileOpenSettings());

  // Close a buffer by ID.
  // Fires FileBeforeClose (cancellable) and FileAfterClose.
  bool closeBuffer(const QString &p_bufferId);

  // ============ Buffer Handle ============

  // Get a Buffer2 handle for an already-open buffer by ID.
  // Useful when you have a buffer ID (e.g., from a hook) and need a handle.
  // Constructs NodeIdentifier internally from vxcore buffer info.
  // Returns an invalid Buffer2 if the buffer ID is not valid.
  Buffer2 getBufferHandle(const QString &p_bufferId);

  // ============ Buffer Queries (pass-through) ============

  // Get buffer configuration as JSON.
  QJsonObject getBuffer(const QString &p_bufferId) const;

  // List all open buffers as JSON array.
  QJsonArray listBuffers() const;

private:
  // Allow Buffer2 to access BufferCoreService base for delegated operations.
  friend class Buffer2;

  // Get a pointer to the underlying BufferCoreService.
  BufferCoreService *coreService();
  const BufferCoreService *coreService() const;

  HookManager *m_hookMgr = nullptr;
};

} // namespace vnotex

#endif // BUFFERSERVICE_H
