#ifndef FOLDERSHARECONTROLLER_H
#define FOLDERSHARECONTROLLER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include <core/nodeidentifier.h>
#include <core/services/foldersharepackager.h>

namespace vnotex {

class ServiceLocator;

// Orchestrates "Share Folder": path resolution, the save barrier for open
// notes, and the packaging run.
//
// SYNCHRONOUS by design. shareFolder() returns only when the bundle has been
// published (or the run failed / was cancelled). There is no worker thread, no
// I/O gate, and no cancellation token plumbing — the caller drives a modal
// progress dialog and supplies progress/cancel callbacks, which is what keeps
// the whole feature free of cross-thread buffer-lifetime hazards.
//
// It shows NO dialogs (per src/controllers/AGENTS.md): the destination
// QFileDialog and the QProgressDialog belong to NotebookExplorer2.
class FolderShareController : public QObject {
  Q_OBJECT

public:
  explicit FolderShareController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~FolderShareController() override;

  // Everything the view needs to drive its progress dialog. All members are
  // optional.
  struct Callbacks {
    // Human-readable label for the current phase.
    std::function<void(const QString &)> m_labelChanged;
    // Determinate byte progress; p_total is 0 while nothing is countable.
    std::function<void(qint64 /*done*/, qint64 /*total*/)> m_progress;
    // Polled frequently. Returning true aborts the run and publishes nothing.
    std::function<bool()> m_isCancelled;
  };

  // Runs the whole share on the CALLING (GUI) thread.
  //
  // Steps: resolve the storage roots through vxcore, refuse a selection whose
  // basename collides with the reserved package directory, force every open
  // modified note under the folder to disk, then package.
  //
  // Re-entrant calls are refused (Failed with a busy message): the callbacks
  // pump the event loop, so a second invocation is genuinely reachable.
  FolderSharePackager::Result shareFolder(const NodeIdentifier &p_nodeId,
                                          const QString &p_destinationParent,
                                          const Callbacks &p_callbacks);

  // True while a shareFolder() call is on the stack.
  bool isBusy() const { return m_busy; }

  // Human-readable label for a packager phase. Exposed so the view can reuse
  // the same strings for its initial dialog text.
  static QString phaseLabel(FolderSharePackager::Phase p_phase);

  // Test seam: injects a packager failure at "copy", "verify" or "publish".
  void testSetFailureInjection(const QString &p_stage) { m_failureInjection = p_stage; }

private:
  // Save every open, modified note under the folder to REAL disk, so the copy
  // cannot capture stale bytes. Returns false (with p_outError set) when a note
  // could not be made durable — a bundle with stale content would be worse than
  // no bundle at all. Records the resulting per-buffer revisions so
  // openNotesAreStillDurable() can detect a later edit.
  bool saveOpenNotesUnder(const QString &p_notebookId, const QString &p_folderPath,
                          const Callbacks &p_callbacks, QString *p_outError);

  // Last-moment assertion handed to the packager, run immediately before the
  // atomic rename with no event pumping around it.
  //
  // The packager can only re-hash the FILESYSTEM. An in-memory edit to an open
  // note never touches disk under AutoSavePolicy::None, so only this check can
  // see one. Requires: the same candidate set as the barrier produced, every
  // one still open, unmodified, not queued for saving, and at the exact
  // revision the barrier made durable.
  bool openNotesAreStillDurable(const QString &p_notebookId, const QString &p_folderPath,
                                QString *p_outError) const;

  // Open, non-virtual buffers whose notebook-relative path lies under the
  // folder. GUI thread only.
  QStringList collectOpenNotesUnder(const QString &p_notebookId, const QString &p_folderPath) const;

  ServiceLocator &m_services;
  bool m_busy = false;
  QString m_failureInjection;

  // bufferId -> revision made durable by the save barrier for the CURRENT run.
  QHash<QString, quint64> m_barrierRevisions;
};

} // namespace vnotex

#endif // FOLDERSHARECONTROLLER_H
