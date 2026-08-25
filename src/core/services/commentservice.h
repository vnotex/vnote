#ifndef COMMENTSERVICE_H
#define COMMENTSERVICE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QWaitCondition>

#include <core/nodeidentifier.h>

#include "commenttypes.h"

namespace vnotex {

class HookManager;
class NotebookCoreService;
class NotebookIoGate;

// CommentService
//
// Owns the per-file `comments.json` sidecar store. Generic and
// file-type-agnostic: nothing here knows what a PDF is.
//
// === Where the store lives ===
//
//   bundled notebook  ->  <getAttachmentsFolder()>/comments.json
//                         (getAttachmentsFolder ALREADY returns
//                          <assets-root>/<file-uuid> and does NOT create it)
//   raw notebook      ->  <filename>.comments.json, beside the file
//   external file     ->  <filename>.comments.json, beside the file
//
// One rule: the store travels with the file.
//
// === Everything that touches a store goes through ONE per-file FIFO ===
//
// Writes are ASYNCHRONOUS. This is not a style choice:
// `NotebookIoGate::ScopedLock` is worker-thread-only and blocks on
// construction, so a synchronous save from the dock or the bridge would either
// freeze the GUI or have to degrade to `ScopedTryLock` and drop edits.
//
// Crucially, the sibling-lifecycle operations (rename/move/delete of a raw
// file's sidecar) are queued on the SAME per-file FIFO rather than executed
// inline on the GUI thread. Doing them inline raced the queue in both
// directions: a move that ran before a pending worker found no sidecar and did
// nothing, after which the worker recreated it under the OLD name; and a delete
// could be undone by an in-flight save. Ordering them behind the pending save
// removes the race by construction instead of by timing.
//
//   1. the UI snapshots a validated CommentSet; scheduleSave() returns;
//   2. a pool worker takes NotebookIoGate::ScopedLock(notebookId) for bundled
//      stores (a sibling store touches no working tree that sync stages, and an
//      external file has no notebook at all, so neither takes the gate);
//   3. the worker creates the parent directory, then commits with QSaveFile;
//   4. completion is queued back to this object's owning thread;
//   5. a newer pending snapshot REPLACES an older pending one, but only while
//      it is still the LAST job for that file - a queued move/delete is a
//      barrier that coalescing may not jump;
//   6. shutdown() drains; the destructor drains UNBOUNDED (see below).
//
// Reads are synchronous. `comments.json` is small and QSaveFile commits by
// rename, so a concurrent write is never observed half-applied.
class CommentService : public QObject {
  Q_OBJECT

public:
  // Where a file's store lives, and how it must be written.
  struct Location {
    enum class Kind {
      Invalid,     // could not be resolved (unknown notebook, empty path, ...)
      Attachments, // bundled notebook: inside the file's UUID assets folder
      Sibling      // raw notebook or external file: <filename>.comments.json
    };

    bool isValid() const { return m_kind != Kind::Invalid; }

    // True when the write must be serialized against sync staging.
    bool needsIoGate() const { return m_kind == Kind::Attachments; }

    Kind m_kind = Kind::Invalid;

    // Absolute path of comments.json.
    QString m_storePath;

    // Empty for an external file.
    QString m_notebookId;
  };

  // "no store yet" and "the store is there but unreadable" MUST be
  // distinguishable. Treating a parse failure as an empty set would let the
  // very next edit overwrite a recoverable file with a fresh empty document.
  struct LoadResult {
    enum class Status {
      Missing, // no store on disk - the normal case for an uncommented file
      Loaded,  // parsed successfully
      Error    // present but unreadable/malformed - DO NOT overwrite
    };

    bool isUsable() const { return m_status != Status::Error; }

    Status m_status = Status::Missing;

    CommentSet m_comments;

    QString m_error;
  };

  CommentService(NotebookCoreService *p_notebookService, NotebookIoGate *p_ioGate,
                 HookManager *p_hookMgr, QObject *p_parent = nullptr);

  ~CommentService() override;

  // Resolves the sidecar location for @p_nodeId. Pure lookup: creates nothing.
  Location resolveLocation(const NodeIdentifier &p_nodeId) const;

  LoadResult load(const NodeIdentifier &p_nodeId) const;

  // Snapshots @p_comments and schedules an atomic write. Returns immediately.
  // Rejects (and emits saveRejectedReadOnly) when the notebook is read-only:
  // vxcore refuses asset writes on a read-only notebook before touching disk,
  // and a direct QSaveFile would bypass that guard.
  //
  // @p_generation is echoed back in saveFinished so the caller can tell which
  // snapshot a completion refers to and keep a FAILED one dirty for retry.
  void scheduleSave(const NodeIdentifier &p_nodeId, const CommentSet &p_comments,
                    quint64 p_generation = 0);

  // Stop accepting new jobs and wait up to @p_timeoutMs for workers to drain.
  bool shutdown(int p_timeoutMs = 5000);

  // True when any job for @p_nodeId is pending or in flight.
  bool isBusy(const NodeIdentifier &p_nodeId) const;

  static QString storeFileName() { return QStringLiteral("comments.json"); }

  static QString siblingSuffix() { return QStringLiteral(".comments.json"); }

signals:
  // Emitted on this object's owning thread. @p_generation echoes scheduleSave().
  void saveFinished(const vnotex::NodeIdentifier &p_nodeId, quint64 p_generation, bool p_ok,
                    const QString &p_error);

  // Emitted on the CALLING thread from scheduleSave(); nothing was written.
  void saveRejectedReadOnly(const vnotex::NodeIdentifier &p_nodeId);

  // Emitted after a successful bundled/raw-notebook write. `comments.json` is
  // written directly rather than through vxcore, so NO `file.saved` event is
  // produced and a synced notebook would otherwise never learn the working tree
  // changed. SyncService's existing debounce/coalescing owns the actual
  // scheduling - this is a fact, not a request to sync now.
  void storeDirty(const QString &p_notebookId);

private:
  struct Job {
    enum class Kind { Save, Move, Remove };

    Kind m_kind = Kind::Save;

    NodeIdentifier m_nodeId;

    Location m_location;

    // Save only.
    QByteArray m_payload;
    quint64 m_generation = 0;

    // Move only: the destination sidecar.
    QString m_destPath;
  };

  static QString jobKey(const NodeIdentifier &p_nodeId);

  static QString jobKey(const QString &p_notebookId, const QString &p_relativePath);

  // Appends @p_job to its file's FIFO and dispatches a worker if idle.
  void enqueue(const QString &p_key, const Job &p_job);

  void runWorker(const QString &p_key);

  // Worker body: mkpath + QSaveFile commit. Returns an error string, empty on
  // success. Must NOT run on the GUI thread.
  static QString writeStore(const Location &p_location, const QByteArray &p_payload);

  static QString moveStore(const QString &p_from, const QString &p_to);

  void installLifecycleHooks();

  // Sibling stores do not follow their file automatically. Bundled stores do
  // (the UUID folder is stable), so these only act on Kind::Sibling.
  void onNodeRenamed(const QString &p_notebookId, const QString &p_oldRelativePath,
                     const QString &p_newName);
  void onNodeMoved(const QString &p_notebookId, const QString &p_oldRelativePath,
                   const QString &p_newRelativePath);
  void onNodeDeleted(const QString &p_notebookId, const QString &p_relativePath);

  // True when the notebook exists and is NOT bundled.
  bool isSiblingNotebook(const QString &p_notebookId) const;

  NotebookCoreService *m_notebookService = nullptr;

  NotebookIoGate *m_ioGate = nullptr;

  HookManager *m_hookMgr = nullptr;

  // Guards m_queues, m_running, m_inFlightCount, m_stopping.
  mutable QMutex m_mutex;
  QWaitCondition m_drained;

  // Per-file FIFO. Save jobs coalesce with the tail; Move/Remove never do.
  QHash<QString, QQueue<Job>> m_queues;
  QHash<QString, bool> m_running;

  int m_inFlightCount = 0;
  bool m_stopping = false;

  QVector<int> m_hookIds;
};

} // namespace vnotex

#endif // COMMENTSERVICE_H
