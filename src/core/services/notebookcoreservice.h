#ifndef NOTEBOOKCORESERVICE_H
#define NOTEBOOKCORESERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <string>

#include <core/noncopyable.h>
#include <core/services/isyncnotebookservice.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// Qt wrapper for notebook types matching VxCoreNotebookType.
enum class NotebookType { Bundled = VXCORE_NOTEBOOK_BUNDLED, Raw = VXCORE_NOTEBOOK_RAW };

class HookManager;
class NotebookIoGate;

// Service layer for notebook operations. Wraps VxCore C API and provides Qt signals.
// NotebookCoreService IS the new notebook layer - replaces legacy NotebookMgr.
class NotebookCoreService : public QObject, public ISyncNotebookService, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit NotebookCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~NotebookCoreService();

  // Set HookManager for firing node operation hooks.
  // Called from main() after both services are constructed.
  void setHookManager(HookManager *p_hookMgr);

  // Set NotebookIoGate (the per-notebook working-tree serializer shared with
  // BufferSaveQueue / SyncOps). Retained for dependency wiring; NOT consulted by
  // reorderFolderChildren, which — like its sibling folder mutations
  // (createFolder/createFile/rename/delete) — now runs synchronously on the UI
  // thread (see reorderFolderChildren in the .cpp for the rationale). Called
  // from main() with the SAME instance injected into the other actors.
  void setNotebookIoGate(NotebookIoGate *p_ioGate);

  // Notebook operations (7 methods).
  QString createNotebook(const QString &p_path, const QString &p_configJson, NotebookType p_type);
  QString openNotebook(const QString &p_path);
  // Extended open accepting JSON options string. Currently honors:
  //   { "readOnly": bool }  // default false
  // Existing openNotebook(path) is preserved as a thin shim calling this
  // method with "{}". Fires NotebookAfterOpen on success (same as openNotebook).
  QString openNotebookEx(const QString &p_path, const QString &p_optionsJson);
  // Clone a remote VNote notebook into p_targetDir (which must exist and be
  // empty -- caller's responsibility). Wraps vxcore_sync_clone (T19). On
  // success returns the new notebook id and fires NotebookAfterClone hook
  // with NotebookCloneEvent {notebookId, targetDir, isReadOnly}.
  // Returns empty string on failure (hook does NOT fire).
  // p_credentialsJson may be empty -- the C ABI then installs a
  // NoOpCredentialProvider (anonymous clone). PAT contents are NEVER logged.
  // Per design (snapshot-only MVP), this method does NOT register sync; the
  // caller must invoke enableSync separately if they want a RW notebook.
  //
  // p_cancellationToken: optional cancellation handle (created by caller via
  //   vxcore_sync_create_cancellation, freed by caller via
  //   vxcore_sync_free_cancellation). When non-null, forwarded to
  //   vxcore_sync_clone_cancellable so the call can be aborted from another
  //   thread via vxcore_sync_cancel(token). When null, behavior matches the
  //   pre-cancellation contract bit-for-bit.
  //
  // p_outErr: optional out-pointer for the raw VxCoreError code. The empty
  //   QString return value cannot distinguish VXCORE_ERR_CANCELLED from any
  //   other failure; callers that need this distinction (e.g.,
  //   OpenNotebookController showing a user-friendly "cancelled" message vs
  //   a generic error) should pass a non-null p_outErr and inspect it on
  //   empty return.
  QString cloneNotebookFromUrl(const QString &p_targetDir, const QString &p_configJson,
                               const QString &p_credentialsJson,
                               VxCoreSyncCancellation *p_cancellationToken = nullptr,
                               VxCoreError *p_outErr = nullptr);
  bool closeNotebook(const QString &p_notebookId);
  QJsonArray listNotebooks() const;
  QJsonObject getNotebookConfig(const QString &p_notebookId) const;
  bool updateNotebookConfig(const QString &p_notebookId, const QString &p_configJson);
  bool rebuildNotebookCache(const QString &p_notebookId);

  // History operations.
  QJsonArray getHistoryResolved(const QString &p_notebookId) const;

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

  // Read-only state query. Wraps vxcore_notebook_is_read_only so widgets do
  // not need a back-door into the private VxCore context. Returns false on
  // any error (notebook not found, null context, etc.) — defensive default
  // so UI badges fail closed rather than misreporting an editable notebook
  // as read-only.
  bool isNotebookReadOnly(const QString &p_notebookId) const;

  // Sync operations (8 methods). Thin wrappers around vxcore C sync APIs.
  // All methods return VxCoreError directly so callers can react to specific
  // codes (VXCORE_ERR_NOT_FOUND, VXCORE_ERR_UNSUPPORTED, etc.). Returns
  // VXCORE_ERR_NOT_INITIALIZED if the underlying vxcore context is null.
  // Credential JSON contents (e.g. "pat") are NEVER logged by these wrappers.
  // enableSync: unified vxcore_sync_enable wrapper. p_credentialsJson is optional;
  // when empty/null the C ABI installs a NoOpCredentialProvider (legacy
  // creds-less path). When non-empty, credentials are parsed and forwarded
  // BEFORE the backend's Initialize() runs — required for backends whose
  // initialization itself needs auth (e.g., authenticated git clone).
  VxCoreError enableSync(const QString &p_notebookId, const QString &p_configJson,
                         const QString &p_credentialsJson = QString());
  VxCoreError disableSync(const QString &p_notebookId);
  // Release the per-notebook sync runtime (frees the libgit2 repo handle and
  // unmaps any mmapped .pack files on Windows) WITHOUT touching on-disk sync
  // config or the keychain PAT. Wraps vxcore_sync_unregister_notebook.
  // Idempotent: returns VXCORE_OK on a notebook that was never registered.
  // Used by SyncService's NotebookAfterClose handler so closing a notebook
  // releases the file handles immediately instead of leaking them until app
  // exit. NEVER logs PAT or remote URL values.
  VxCoreError unregisterSyncRuntime(const QString &p_notebookId);
  VxCoreError triggerSync(const QString &p_notebookId);
  // Wave 12.2 / F5.9: cancellable triggerSync. @p_cancellationHandle is the
  // raw VxCoreSyncCancellation* (forward-declared below) owned by
  // the caller (typically SyncService). Null degrades to legacy behaviour.
  VxCoreError triggerSyncCancellable(const QString &p_notebookId,
                                     struct VxCoreSyncCancellation_ *p_cancellationHandle);
  // ISyncNotebookService overrides. Wrap vxcore_sync_stage_only /
  // vxcore_sync_network_phase so SyncOps::triggerSync can hold the per-
  // notebook IO gate around staging only and release it before network IO.
  VxCoreError syncStageOnly(const QString &p_notebookId,
                            VxCoreSyncCancellation *p_cancellationToken,
                            bool *p_didCommit) override;
  VxCoreError syncNetworkPhase(const QString &p_notebookId,
                               VxCoreSyncCancellation *p_cancellationToken) override;
  VxCoreError getSyncStatus(const QString &p_notebookId, QString &p_outStatusJson);
  VxCoreError getSyncConflicts(const QString &p_notebookId, QString &p_outConflictsJson);
  VxCoreError resolveSyncConflict(const QString &p_notebookId, const QString &p_filePath,
                                  const QString &p_resolution);
  VxCoreError setSyncCredentials(const QString &p_notebookId, const QString &p_credentialsJson);
  bool isSyncReady(const QString &p_notebookId) const;

  // Lightweight runtime check whether a notebook is registered with vxcore's
  // SyncManager (i.e., present in states_ map). Wraps vxcore_sync_is_registered.
  // Unlike getSyncStatus, this NEVER acquires the per-backend op_mutex_,
  // making it safe to call from the UI thread at high frequency without
  // racing the worker-thread sync work that would otherwise be starved into
  // VXCORE_ERR_SYNC_IN_PROGRESS. Returns false on any error (null context,
  // notebook unknown, etc.).
  bool isSyncRegistered(const QString &p_notebookId) const;

  // Per-device last successful sync timestamp (milliseconds since Unix epoch).
  // Returns 0 if the notebook has never been successfully synced on this
  // device or if the underlying vxcore read fails. Backed by metadata.db
  // (NOT NotebookConfig JSON) to avoid a self-sync loop.
  qint64 getLastSyncUtc(const QString &p_notebookId) const;
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

  // Atomically rewrite the order of a folder's children (subfolders / files)
  // in its vx.json. The vxcore write runs SYNCHRONOUSLY on the calling (UI)
  // thread — like every sibling folder mutation — because vxcore's FolderManager
  // cache is not thread-safe and is read on the UI thread (model fetchMore);
  // the read-modify-write must therefore be one atomic UI-thread step. The
  // result is delivered ASYNCHRONOUSLY via the reorderCompleted signal
  // (Qt::QueuedConnection), so callers may wait() for it after this returns.
  //
  // Hook contract:
  //   - vnote.node.before_reorder fires SYNCHRONOUSLY on the calling thread.
  //     A hook that calls ctx.cancel() short-circuits with success=false
  //     and errorMessage="Cancelled by hook"; the vxcore call is NOT made.
  //   - vnote.node.after_reorder fires on this service's owning thread AFTER
  //     a successful write, just before reorderCompleted(true).
  //
  // No-op contract:
  //   If both presented sub-arrays match (or are empty), the method emits
  //   reorderCompleted(true) WITHOUT firing hooks or dispatching to vxcore.
  //
  // Args:
  //   p_orderedFolders: empty = "do not reorder folders" (sub-array omitted
  //     from JSON); non-empty = exact permutation of current folder names.
  //   p_orderedFiles:   same semantics for files.
  //
  // Virtual to allow test subclasses (e.g. RecordingNotebookCoreService used
  // by tests/controllers/test_notebook_node_controller_reorder.cpp) to
  // intercept calls without touching vxcore or the filesystem. No runtime
  // overhead for production callers (single-inheritance vtable dispatch).
  virtual void reorderFolderChildren(const QString &p_notebookId, const QString &p_folderRelPath,
                                     const QStringList &p_orderedFolders,
                                     const QStringList &p_orderedFiles);

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
  // p_suffixAllowlist: semicolon-separated list of file extensions (e.g., "md;txt"), or empty to
  // import all. Returns folder ID on success, empty string on failure.
  QString importFolder(const QString &p_notebookId, const QString &p_destFolderPath,
                       const QString &p_externalFolderPath,
                       const QString &p_suffixAllowlist = QString());

  // Tag operations.
  // Create a tag in the notebook. Returns true on success or if tag already exists (idempotent).
  bool createTag(const QString &p_notebookId, const QString &p_tagName);

  // Update the tags of a file. p_tags is the full list of tags to set.
  bool updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QStringList &p_tags);

  // Update timestamps (created/modified) for a node (file or folder).
  // Timestamps are millisecond epoch values. Pass <= 0 to preserve existing value.
  bool updateNodeTimestamps(const QString &p_notebookId, const QString &p_nodePath,
                            qint64 p_createdUtc, qint64 p_modifiedUtc);

  // Update the list of attachments for a file.
  bool updateFileAttachments(const QString &p_notebookId, const QString &p_filePath,
                             const QStringList &p_attachments);

  // Peek file content (first N characters for preview).
  // p_maxChars: maximum number of characters to return (default 256).
  // Returns empty string if file doesn't exist or on error.
  QString peekFile(const QString &p_notebookId, const QString &p_filePath,
                   int p_maxChars = 256) const;

  // Get absolute path to the file's attachments folder.
  // Returns empty string on error.
  QString getAttachmentsFolder(const QString &p_notebookId, const QString &p_filePath) const;

  // List attachment filenames for a file.
  // Returns empty array on error.
  QJsonArray listAttachments(const QString &p_notebookId, const QString &p_filePath) const;

signals:
  // T6 (notebook-explorer-drag-reorder): emitted from this service's owning
  // thread when reorderFolderChildren() finishes (success OR failure path).
  // success=true with empty errorMessage signals OK; success=false carries a
  // translated, user-facing message.
  void reorderCompleted(const QString &notebookId, const QString &folderRelPath, bool success,
                        const QString &errorMessage);

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

  // T6 helper: build the {"folders":[...],"files":[...]} JSON consumed by
  // vxcore_folder_set_children_order. Either sub-array is omitted if its
  // corresponding QStringList is empty (matches vxcore's "do not touch"
  // contract for missing keys).
  static std::string buildOrderedJson(const QStringList &p_orderedFolders,
                                      const QStringList &p_orderedFiles);

  // T6 helper: translate a VxCoreError code from
  // vxcore_folder_set_children_order into a user-facing message.
  static QString reorderErrorToString(int p_rc);

  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
};

} // namespace vnotex

#endif // NOTEBOOKCORESERVICE_H
