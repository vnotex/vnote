#ifndef IMPORTFOLDERCONTROLLER_H
#define IMPORTFOLDERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include <core/nodeinfo.h>
#include <core/services/folderbundleimporter.h>

namespace vnotex {

class ServiceLocator;
namespace core {
class NotebookService;
}

// Input data structure for importing a folder.
struct ImportFolderInput {
  QString notebookId;
  QString parentFolderPath; // Relative path within notebook
  QString sourceFolderPath; // Absolute path to source folder
  QStringList suffixes;     // File suffixes to import
};

// Input data structure for importing a VNote share bundle.
struct ImportBundleInput {
  QString notebookId;
  QString parentFolderPath; // Relative path within notebook ("." for the root)
  QString bundlePath;       // Absolute path to the "*-bundle" directory
};

// Result structure for folder import.
struct ImportFolderResult {
  bool success = false;
  NodeIdentifier nodeId; // Identifier of the created folder node
  QString errorMessage;
  QString warningMessage; // Non-fatal warnings during import
};

// Validation result structure.
struct ImportFolderValidationResult {
  bool valid = true;
  QString message;
};

// Validation result for a share bundle, carrying the preview the dialog shows.
struct ImportBundleValidationResult {
  bool valid = true;
  QString message;
  QString folderName;
  int fileCount = 0;
  int subfolderCount = 0;
};

// Controller for folder import operations.
// Handles validation and delegates to vxcore_folder_import API.
// View (ImportFolderDialog2) collects input and displays results.
class ImportFolderController : public QObject {
  Q_OBJECT

public:
  explicit ImportFolderController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate source folder path.
  // Checks: exists, legal path, not recursive.
  ImportFolderValidationResult validateSourceFolder(const QString &p_notebookId,
                                                    const QString &p_parentPath,
                                                    const QString &p_sourceFolderPath) const;

  // Validate all inputs.
  ImportFolderValidationResult validateAll(const ImportFolderInput &p_input) const;

  // Import folder with the given input.
  // Returns result with success status and node identifier or error message.
  ImportFolderResult importFolder(const ImportFolderInput &p_input);

  // --- Share bundle import -------------------------------------------------

  // Everything the view needs to drive its progress dialog. All members are
  // optional.
  struct Callbacks {
    // Human-readable label for the current phase.
    std::function<void(const QString &)> m_labelChanged;
    // Determinate byte progress; p_total is 0 while nothing is countable.
    std::function<void(qint64 /*done*/, qint64 /*total*/)> m_progress;
    // Polled frequently. Returning true aborts the run and imports nothing.
    std::function<bool()> m_isCancelled;
  };

  // Pre-flight for the bundle mode: the destination must be a writable bundled
  // notebook, the bundle must not live inside that notebook, and the bundle
  // itself must be a well-formed share bundle. Also produces the preview the
  // dialog shows (folder name, note count, subfolder count).
  ImportBundleValidationResult validateBundle(const ImportBundleInput &p_input) const;

  // Imports a share bundle, restoring its metadata verbatim.
  //
  // SYNCHRONOUS by design, like FolderShareController::shareFolder: it returns
  // only when the import has been committed (or failed / was cancelled). The
  // caller drives a modal progress dialog and supplies progress / cancel
  // callbacks, which is what keeps the feature free of cross-thread hazards.
  //
  // Re-entrant calls are refused: the callbacks pump the event loop, so a
  // second invocation is genuinely reachable.
  ImportFolderResult importBundle(const ImportBundleInput &p_input, const Callbacks &p_callbacks);

  // True while an importBundle() call is on the stack.
  bool isBusy() const { return m_busy; }

  // Human-readable label for an importer phase. Exposed so the view can reuse
  // the same strings for its initial dialog text.
  static QString phaseLabel(FolderBundleImporter::Phase p_phase);

  // Test seam: injects an importer failure at "copy", "verify", "publish" or
  // "attach".
  void testSetFailureInjection(const QString &p_stage) { m_failureInjection = p_stage; }

private:
  ServiceLocator &m_services;
  bool m_busy = false;
  QString m_failureInjection;
};

} // namespace vnotex

#endif // IMPORTFOLDERCONTROLLER_H
