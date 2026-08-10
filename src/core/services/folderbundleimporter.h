#ifndef FOLDERBUNDLEIMPORTER_H
#define FOLDERBUNDLEIMPORTER_H

#include <QString>
#include <QStringList>

#include <functional>

namespace vnotex {

// FolderBundleImporter
//
// SYNCHRONOUS import of a share bundle produced by FolderSharePackager into a
// bundled notebook, restoring its metadata VERBATIM: ids, timestamps, metadata
// objects, per-file tag strings, attachment lists and child order are all
// preserved byte-for-byte.
//
//   <bundle>/Alpha-bundle/
//   |-- Alpha/                       physical folder tree
//   `-- vx_notebook/contents/Alpha/  matching metadata dirs + vx.json
//
// It is NOT vxcore_folder_import: that call REGENERATES ids and timestamps, so
// a folder round-tripped through it loses its identity, its tag associations
// and its history. Preserving ids is the whole point of this class, and it is
// also why an id collision with the destination notebook is a HARD FAILURE
// rather than something to remap.
//
// Design notes
// ------------
//   - Mirrors FolderSharePackager deliberately: same non-following enumeration,
//     same chunked SHA-256 copy-and-verify, same staged-then-atomic publish.
//     Deviating would mean two different definitions of "faithful copy".
//   - Deliberately has NO threading, NO Qt Widgets and NO service dependencies.
//     The CALLER owns responsiveness (progress + cancellation callbacks) and
//     supplies the two facts the importer cannot obtain by itself: the set of
//     ids already present in the notebook, and the commit itself.
//   - The importer OWNS ROLLBACK. When the commit callback fails it removes
//     everything it staged, so the notebook is left byte-identical.
class FolderBundleImporter {
public:
  FolderBundleImporter() = delete;

  struct Request {
    // Absolute notebook root.
    QString m_notebookRoot;
    // Absolute physical directory that will CONTAIN the imported folder.
    QString m_destContentRoot;
    // Absolute metadata directory that will CONTAIN the imported folder's
    // metadata directory.
    QString m_destMetadataRoot;
    // Destination path relative to the notebook root ("." for the root).
    QString m_destRelativePath;
    // Absolute path of the "*-bundle" directory chosen by the user.
    QString m_bundlePath;

    // Test seam: inject a failure at "copy", "verify", "publish" or "attach".
    QString m_failureInjection;
  };

  enum class Status {
    Succeeded,
    Cancelled,
    Failed,
  };

  struct Result {
    Status m_status = Status::Failed;
    // Name the folder was published under; may be uniquified ("Alpha (2)").
    QString m_folderName;
    // Notebook-relative path of the imported folder (only when Succeeded).
    QString m_relativePath;
    // Id of the imported root folder, as reported by the commit (Succeeded).
    QString m_folderId;
    // User-facing reason (only when Failed).
    QString m_errorMessage;

    bool succeeded() const { return m_status == Status::Succeeded; }
  };

  // Coarse phase, so the caller can label its progress dialog.
  enum class Phase {
    Validating, // indeterminate: scanning + checking the bundle
    Copying,    // determinate
    Verifying,  // determinate
    Attaching,  // indeterminate: the journaled commit
  };

  // What the commit callback needs in order to attach the staged tree.
  struct CommitRequest {
    // Absolute staging directory holding "content/" and "metadata/".
    QString m_stagingDir;
    // Destination path relative to the notebook root ("." for the root).
    QString m_destRelativePath;
    // Final (possibly uniquified) folder name.
    QString m_folderName;
    // Receives the attached folder's id.
    QString *m_outFolderId = nullptr;
  };

  struct Callbacks {
    // Called when the phase changes. May be null.
    //
    // NOTE: invoked BEFORE the final no-callback section, so the Attaching
    // phase is announced while it is still safe to pump the event loop.
    std::function<void(Phase)> m_phaseChanged;
    // Determinate byte progress. May be null.
    std::function<void(qint64 /*done*/, qint64 /*total*/)> m_progress;
    // Polled between entries and file chunks. Returning true aborts the run and
    // removes the staged tree. May be null (never cancelled).
    std::function<bool()> m_isCancelled;

    // The AUTHORITATIVE set of ids already present in the destination notebook.
    // Called once during preflight and AGAIN inside the final section, because
    // the progress callbacks pump the event loop and the notebook can change
    // under them. Returning false aborts the run: the importer FAILS CLOSED,
    // never treating "could not determine" as "no collision".
    //
    // MUST NOT pump the event loop when called from the final section.
    std::function<bool(QStringList * /*outIds*/, QString * /*outError*/)> m_collectNodeIds;

    // Performs the actual attach. Invoked ONCE, inside the final section, after
    // every check has passed. Returning false makes the run a failure and the
    // importer rolls its staging back.
    //
    // MUST NOT pump the event loop.
    std::function<bool(const CommitRequest &, QString * /*outError*/)> m_commit;
  };

  // Runs the whole job on the CALLING thread and returns when it is done.
  static Result run(const Request &p_request, const Callbacks &p_callbacks);

  // Cheap, read-only description of a candidate bundle, for the dialog preview.
  // Performs layout + metadata validation but copies nothing.
  struct Inspection {
    bool m_valid = false;
    QString m_folderName;
    int m_fileCount = 0;
    int m_subfolderCount = 0;
    // Reason when invalid; a short summary when valid.
    QString m_message;
  };

  static Inspection inspect(const QString &p_bundlePath);

  // The metadata directory name a bundle carries beside its content folder.
  static QString reservedPackageDirName();
};

} // namespace vnotex

#endif // FOLDERBUNDLEIMPORTER_H
