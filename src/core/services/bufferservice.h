#ifndef BUFFERSERVICE_H
#define BUFFERSERVICE_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

#include <core/fileopensettings.h>
#include <core/services/buffer2.h>
#include <core/services/buffercoreservice.h>

class QTimer;

namespace vnotex {

class HookManager;
class BufferSaveQueue;
class NotebookIoGate;

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
  // Constructor receives VxCore context handle, HookManager, NotebookIoGate, and initial
  // auto-save policy. NotebookIoGate is shared with SyncOps; required by the BufferSaveQueue
  // worker pool to serialize working-tree access per notebook.
  explicit BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                         NotebookIoGate *p_ioGate,
                         AutoSavePolicy p_autoSavePolicy = AutoSavePolicy::AutoSave,
                         QObject *p_parent = nullptr);

  // Convenience overload (test / legacy callers): owns an internal NotebookIoGate.
  // Production code MUST pass the shared NotebookIoGate explicitly.
  explicit BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                         AutoSavePolicy p_autoSavePolicy, QObject *p_parent = nullptr);
  ~BufferService() override;

  // Drain in-flight async saves. Returns true if drained within @p_timeoutMs.
  // Safe to call multiple times (idempotent). The dtor calls this as a safety net.
  bool shutdown(int p_timeoutMs = 5000);

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

  // Open a file as a buffer by its node ID (UUID).
  // Resolves the UUID to notebookId + relativePath first (without opening a buffer),
  // then delegates to openBuffer() which fires FileBeforeOpen/AfterOpen hooks.
  // @p_settings controls how the file should be opened.
  // Returns a Buffer2 handle, or invalid Buffer2 if UUID not found or cancelled.
  Buffer2 openBufferByNodeId(const QString &p_nodeId,
                             const FileOpenSettings &p_settings = FileOpenSettings());

  // Open a virtual buffer (no filesystem backing).
  // Does NOT fire FileBeforeOpen/FileAfterOpen hooks (virtual buffers are not files).
  // @p_address: Virtual address (e.g., "vx://settings").
  // Returns a Buffer2 handle, or invalid Buffer2 on failure.
  Buffer2 openVirtualBuffer(const QString &p_address);

  // Close a buffer by ID.
  // Cleans up auto-save state and closes the buffer in vxcore.
  bool closeBuffer(const QString &p_bufferId);

  // ============ Buffer Handle ============

  // Get a Buffer2 handle for an already-open buffer by ID.
  // Useful when you have a buffer ID (e.g., from a hook) and need a handle.
  // Constructs NodeIdentifier internally from vxcore buffer info.
  // Returns an invalid Buffer2 if the buffer ID is not valid.
  Buffer2 getBufferHandle(const QString &p_bufferId);

  // ============ Buffer Queries (pass-through) ============

  // Check whether the notebook for this buffer is a bundled notebook.
  // Returns false for raw notebooks or if the notebook ID is empty/invalid.
  bool isNotebookBundled(const QString &p_notebookId) const;

  // Get buffer configuration as JSON.
  QJsonObject getBuffer(const QString &p_bufferId) const;

  // Check whether a buffer is virtual (non-file-backed).
  bool isVirtualBuffer(const QString &p_bufferId) const;

  // List all open buffers as JSON array.
  QJsonArray listBuffers() const;

  // ============ BufferCoreService wrappers (for Buffer2) ============
  // Wrap selected methods from privately-inherited BufferCoreService.
  // Buffer2 delegates to these; external code should use Buffer2 methods instead.
  // saveBuffer and reloadBuffer emit bufferModifiedChanged after the operation.
  bool saveBuffer(const QString &p_bufferId);
  bool reloadBuffer(const QString &p_bufferId);
  using BufferCoreService::checkExternalChanges;
  using BufferCoreService::deleteAsset;
  using BufferCoreService::getAssetsFolder;
  using BufferCoreService::getContent;
  using BufferCoreService::getContentRaw;
  using BufferCoreService::getResolvedPath;
  using BufferCoreService::getResourceBasePath;
  using BufferCoreService::getRevision;
  using BufferCoreService::getState;
  using BufferCoreService::insertAsset;
  using BufferCoreService::insertAssetRaw;
  using BufferCoreService::isBufferReadOnly;
  using BufferCoreService::isModified;
  using BufferCoreService::peekContentRaw;
  using BufferCoreService::setContent;
  using BufferCoreService::setContentRaw;
  QString insertAttachment(const QString &p_bufferId, const QString &p_sourcePath);
  bool deleteAttachment(const QString &p_bufferId, const QString &p_filename);
  QString renameAttachment(const QString &p_bufferId, const QString &p_oldFilename,
                           const QString &p_newFilename);
  using BufferCoreService::getAttachmentsFolder;
  using BufferCoreService::listAttachments;

  // ============ Auto-Save & Dirty Tracking ============

  // Mark a buffer as having pending editor changes.
  // Starts the shared auto-save timer if not running.
  // Also increments the buffer's monotonic revision counter (snapshot versioning).
  void markDirty(const QString &p_bufferId);

  // ============ Snapshot Revision Tracking (T6) ============
  // Snapshot-versioned save support: a monotonic revision counter is bumped
  // on every editor content-change signal (via markDirty). The save-completion
  // handler reports back the revision it actually persisted via
  // markRevisionSaved(). The buffer is only cleared from the dirty set when
  // lastSavedRevision catches up to currentRevision.
  //
  // Assumption: a quint64 counter cannot overflow in practice (~10^19 edits).

  // Current monotonic revision for a buffer. 0 if buffer unknown or unedited.
  quint64 currentRevision(const QString &p_bufferId) const;

  // Last successfully-saved revision for a buffer. 0 if never saved/unknown.
  quint64 lastSavedRevision(const QString &p_bufferId) const;

  // Report that a save completed for @p_revision. Only advances
  // m_lastSavedRevision if p_revision > current m_lastSavedRevision (stale
  // completions are ignored). If lastSavedRevision == currentRevision after
  // the update, the buffer's dirty flag is cleared. To be called by the
  // BufferSaveQueue save-completion handler (wired in T7).
  void markRevisionSaved(const QString &p_bufferId, quint64 p_revision);

  // Whether the buffer has pending unsaved content (membership in dirty set).
  // Preserves the legacy "has unsaved content" semantic.
  bool isDirty(const QString &p_bufferId) const;

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

  // ============ External Change Detection ============

  // Check all open non-virtual buffers for external file changes.
  // For each buffer whose state transitions to FileChanged or FileMissing,
  // fires the FileExternalChange hook and emits bufferExternallyChanged signal.
  // Returns a list of buffer IDs that have external changes detected.
  QStringList checkAllExternalChanges();

  // Check a single buffer for external file changes.
  // If the buffer's state transitions to FileChanged or FileMissing,
  // fires the FileExternalChange hook and emits bufferExternallyChanged signal.
  // Returns true if a change was detected, false otherwise.
  bool checkSingleExternalChange(const QString &p_bufferId);

signals:
  // Emitted after buffer content is synced from editor to vxcore buffer.
  void bufferContentSynced(const QString &p_bufferId);

  // Emitted when the buffer's modified state may have changed.
  // Fired after save, reload, or auto-save — any operation that clears/sets the
  // vxcore modified flag. Listeners should re-query isModified() for current state.
  void bufferModifiedChanged(const QString &p_bufferId);

  // Emitted after auto-save completes (save to disk or backup written).
  void bufferAutoSaved(const QString &p_bufferId);

  // Emitted when auto-save fails for a buffer.
  void bufferAutoSaveFailed(const QString &p_bufferId);

  // Emitted after 3 consecutive auto-save failures; auto-save suspended for this buffer.
  void bufferAutoSaveAborted(const QString &p_bufferId);

  // Emitted after attachment list/content changes for a buffer.
  void attachmentChanged(const QString &p_bufferId);

  // Emitted when an async save reports an error from the BufferSaveQueue worker.
  void saveError(const QString &p_bufferId, const QString &p_errorMsg);

  // Emitted when an open buffer's file is detected as changed or missing on disk.
  // p_bufferId: the affected buffer.
  // p_state: the detected state (BufferState::FileChanged or BufferState::FileMissing).
  void bufferExternallyChanged(const QString &p_bufferId, BufferState p_state);

private:
  // Timer tick handler — syncs all dirty buffers and executes auto-save policy.
  void onAutoSaveTimerTick();

  // Sync content from active writer to vxcore buffer and execute auto-save policy.
  void executeSyncForBuffer(const QString &p_bufferId);

  // Async save-completion handler wired to BufferSaveQueue::saveFinished.
  void onSaveFinished(const QString &p_bufferId, quint64 p_revision, bool p_ok,
                      const QString &p_errorMsg);

  struct ActiveWriter {
    quintptr key = 0;
    ContentFetchCallback callback;
  };

  static const int c_autoSaveIntervalMs = 3000;
  static const int c_maxSaveFailures = 3;

  HookManager *m_hookMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  // Owned gate used only by the convenience ctor (test/legacy). Nullptr when
  // a shared gate is supplied externally.
  NotebookIoGate *m_ownedIoGate = nullptr;
  // Heap-allocated, parented to `this` — Qt destroys it. Do NOT delete manually.
  // Dtor calls m_saveQueue->shutdown(5000) as a safety net.
  BufferSaveQueue *m_saveQueue = nullptr;
  AutoSavePolicy m_autoSavePolicy = AutoSavePolicy::AutoSave;

  // Managed by QObject.
  QTimer *m_autoSaveTimer = nullptr;

  // Set of buffer IDs with pending editor changes.
  QSet<QString> m_dirtyBuffers;

  // Set of buffer IDs that are virtual (non-file-backed).
  // Used to skip auto-save/sync for virtual buffers.
  QSet<QString> m_virtualBufferIds;

  // Map from buffer ID to the active writer's content fetch callback.
  QHash<QString, ActiveWriter> m_activeWriters;

  // Consecutive save failure counts per buffer (for bounded retry).
  QHash<QString, int> m_saveFailureCounts;

  // Per-buffer snapshot revision record (T6). Lazily created on first
  // markDirty/openBuffer; cleared on closeBuffer.
  struct BufferRecord {
    quint64 m_revision = 0;
    quint64 m_lastSavedRevision = 0;
  };
  QHash<QString, BufferRecord> m_revisions;
};

} // namespace vnotex

#endif // BUFFERSERVICE_H
