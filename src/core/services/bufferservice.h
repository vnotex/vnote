#ifndef BUFFERSERVICE_H
#define BUFFERSERVICE_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <functional>

#include <core/fileopensettings.h>
#include <core/services/buffer2.h>
#include <core/services/buffercoreservice.h>

class QTimer;

namespace vnotex {

class HookManager;

// Auto-save policy for buffer content.
// Matches EditorConfig::AutoSavePolicy values.
enum class AutoSavePolicy {
  None,      // Sync to buffer only, no disk write.
  AutoSave,  // Sync + save to disk.
  BackupFile // Sync + write backup (.vswp).
};

// Callback type for fetching latest content from an active writer (ViewWindow2).
// Returns the latest editor content as a UTF-8 encoded string.
using ContentFetchCallback = std::function<QString()>;

// Hook-aware wrapper around BufferCoreService.
// Manages buffer lifecycle (open, close) with vnote.file.* hooks.
// Per-buffer operations are accessed via the Buffer handle returned by openBuffer().
// Also manages auto-save timer, dirty buffer tracking, and active writer registration.
class BufferService : private BufferCoreService {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle, HookManager, and initial auto-save policy.
  explicit BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                         AutoSavePolicy p_autoSavePolicy = AutoSavePolicy::AutoSave,
                         QObject *p_parent = nullptr);
  ~BufferService() override;

  // Expose QObject base for signal connections from outside.
  // Needed because BufferService privately inherits QObject (via BufferCoreService),
  // so external code cannot use connect() with BufferService* directly.
  QObject *asQObject();

  // Set the auto-save policy directly.
  void setAutoSavePolicy(AutoSavePolicy p_policy);

  // Map an EditorConfig auto-save policy value and apply it.
  // @p_configPolicy: 0=None, 1=AutoSave, 2=BackupFile (matches EditorConfig::AutoSavePolicy).
  void syncAutoSavePolicy(int p_configPolicy);

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

  // ============ Auto-Save & Dirty Tracking ============

  // Mark a buffer as having pending editor changes.
  // Starts the shared auto-save timer if not running.
  void markDirty(const QString &p_bufferId);

  // Immediately sync a dirty buffer's content from its active writer.
  // Used on focus-loss for instant consistency.
  void syncNow(const QString &p_bufferId);

  // Register a content fetch callback for a buffer's active writer.
  // @p_callback: Called to get the latest content from the editor.
  // @p_writerKey: Opaque identifier for the writer (e.g., pointer cast to quintptr).
  //               Used by unregisterActiveWriter to identify which writer to remove.
  void registerActiveWriter(const QString &p_bufferId, quintptr p_writerKey,
                            ContentFetchCallback p_callback);

  // Unregister the active writer for a buffer.
  // Only unregisters if @p_writerKey matches the current active writer.
  void unregisterActiveWriter(const QString &p_bufferId, quintptr p_writerKey);

signals:
  // Emitted after buffer content is synced from editor to vxcore buffer.
  void bufferContentSynced(const QString &p_bufferId);

  // Emitted after auto-save completes (save to disk or backup written).
  void bufferAutoSaved(const QString &p_bufferId);

  // Emitted when auto-save fails for a buffer.
  void bufferAutoSaveFailed(const QString &p_bufferId);

  // Emitted after 3 consecutive auto-save failures; auto-save suspended for this buffer.
  void bufferAutoSaveAborted(const QString &p_bufferId);

private:
  // Allow Buffer2 to access BufferCoreService base for delegated operations.
  friend class Buffer2;

  // Get a pointer to the underlying BufferCoreService.
  BufferCoreService *coreService();
  const BufferCoreService *coreService() const;

  // Timer tick handler — syncs all dirty buffers and executes auto-save policy.
  void onAutoSaveTimerTick();

  // Sync content from active writer to vxcore buffer and execute auto-save policy.
  void executeSyncForBuffer(const QString &p_bufferId);

  struct ActiveWriter {
    quintptr key = 0;
    ContentFetchCallback callback;
  };

  static const int c_autoSaveIntervalMs = 3000;
  static const int c_maxSaveFailures = 3;

  HookManager *m_hookMgr = nullptr;
  AutoSavePolicy m_autoSavePolicy = AutoSavePolicy::AutoSave;

  // Managed by QObject.
  QTimer *m_autoSaveTimer = nullptr;

  // Set of buffer IDs with pending editor changes.
  QSet<QString> m_dirtyBuffers;

  // Map from buffer ID to the active writer's content fetch callback.
  QHash<QString, ActiveWriter> m_activeWriters;

  // Consecutive save failure counts per buffer (for bounded retry).
  QHash<QString, int> m_saveFailureCounts;
};

} // namespace vnotex

#endif // BUFFERSERVICE_H
