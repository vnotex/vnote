#ifndef FOLDERSHAREPACKAGER_H
#define FOLDERSHAREPACKAGER_H

#include <QString>

#include <functional>

namespace vnotex {

// FolderSharePackager
//
// SYNCHRONOUS creation of a movable "share bundle" from a bundled notebook's
// subfolder. Given the three storage roots vxcore resolved, it validates both
// source trees, copies them into a hidden temporary sibling of the chosen
// destination, verifies the copy, and publishes it with a single atomic rename:
//
//   <chosen-parent>/Alpha-bundle/
//   |-- Alpha/                       complete physical source folder tree
//   `-- vx_notebook/
//       `-- contents/
//           `-- Alpha/               matching metadata directories + vx.json
//
// A nested source such as "Projects/Alpha" is FLATTENED to a top-level "Alpha";
// the ancestors are not included. Nothing is regenerated: ids, timestamps,
// metadata objects, tags, attachment lists and child order are copied
// byte-for-byte. No manifest and no vx_notebook/config.json is written.
//
// Design notes
// ------------
//   - Deliberately has NO threading, NO Qt Widgets and NO service dependencies:
//     it is a pure function over paths, which is what makes it testable without
//     a GUI and free of the lifetime hazards an async version carries.
//   - The CALLER owns responsiveness. It supplies a progress callback and a
//     cancellation predicate; the packager calls them between entries and
//     between file chunks, so a modal QProgressDialog can pump the event loop
//     and observe Cancel.
//   - Cancellation and every failure remove the temporary tree and publish
//     NOTHING. The successful rename is the commit point.
class FolderSharePackager {
public:
  FolderSharePackager() = delete;

  struct Request {
    // Absolute notebook root (used to validate the ancestor chains).
    QString m_notebookRoot;
    // Absolute physical directory of the selected folder.
    QString m_contentRoot;
    // Absolute directory CONTAINING the selected folder's vx.json.
    QString m_metadataRoot;
    // Absolute, already-canonicalized parent directory chosen by the user.
    QString m_destinationParent;
    // Basename of the selected folder; also the bundle's name stem.
    QString m_folderName;

    // Test seam: inject a failure at "copy", "verify" or "publish".
    QString m_failureInjection;
  };

  enum class Status {
    Succeeded,
    Cancelled,
    Failed,
  };

  struct Result {
    Status m_status = Status::Failed;
    // Absolute path of the published bundle (only when Succeeded).
    QString m_bundlePath;
    // User-facing reason (only when Failed).
    QString m_errorMessage;

    bool succeeded() const { return m_status == Status::Succeeded; }
  };

  // Coarse phase, so the caller can label its progress dialog.
  enum class Phase {
    Validating, // indeterminate: scanning + checking both source trees
    Copying,    // determinate
    Verifying,  // determinate
    Publishing, // indeterminate: the atomic rename
  };

  struct Callbacks {
    // Called when the phase changes. May be null.
    //
    // NOTE: this is invoked BEFORE the final no-callback validation section, so
    // the Publishing phase is announced while it is still safe for the callback
    // to pump the event loop.
    std::function<void(Phase)> m_phaseChanged;
    // Determinate byte progress. p_total is stable for the whole run and is 0
    // while nothing is countable yet. May be null.
    std::function<void(qint64 /*done*/, qint64 /*total*/)> m_progress;
    // Polled between entries and file chunks. Returning true aborts the run and
    // removes the temporary tree. May be null (never cancelled).
    std::function<bool()> m_isCancelled;

    // Last gate before the commit. Invoked ONCE, inside the final section where
    // nothing may pump the event loop, immediately before the atomic rename.
    // Returning false aborts the run as a failure with the message written to
    // the out-parameter. May be null.
    //
    // The packager only knows about the FILESYSTEM. This is how the caller
    // asserts facts it alone owns — for VNote, that no open note under the
    // folder gained an in-memory edit while the progress callbacks were
    // pumping the event loop. Such an edit never reaches disk under
    // AutoSavePolicy::None, so no amount of source re-hashing can see it.
    //
    // MUST NOT pump the event loop.
    std::function<bool(QString * /*outError*/)> m_finalPrecondition;
  };

  // Runs the whole job on the CALLING thread and returns when it is done.
  static Result run(const Request &p_request, const Callbacks &p_callbacks);

  // Exposed for the caller's pre-flight UI: the bundle metadata directory name
  // that the selected folder's basename must not collide with.
  static QString reservedPackageDirName();

  // True when @p_directory lives on a case-sensitive filesystem. Probes the
  // actual directory rather than assuming a platform default (a case-sensitive
  // volume can be mounted on Windows and vice versa). Falls back to the
  // platform default when the probe fails.
  static bool destinationIsCaseSensitive(const QString &p_directory);
};

} // namespace vnotex

#endif // FOLDERSHAREPACKAGER_H
