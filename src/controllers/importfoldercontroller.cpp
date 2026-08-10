#include "importfoldercontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/folderbundleimporter.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <utils/pathutils.h>

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

// Bounded wait for the notebook I/O gate. importBundle runs on the GUI thread,
// where NotebookIoGate::ScopedLock is forbidden (an in-flight sync stage would
// freeze the window), so the commit uses ScopedTryLock and reports "busy"
// instead of blocking indefinitely. Matches BufferService::saveForSnapshot.
constexpr int kIoGateTimeoutMs = 5000;

} // namespace

ImportFolderController::ImportFolderController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

ImportFolderValidationResult
ImportFolderController::validateSourceFolder(const QString &p_notebookId,
                                             const QString &p_parentPath,
                                             const QString &p_sourceFolderPath) const {
  ImportFolderValidationResult result;

  QString folderPath = p_sourceFolderPath.trimmed();
  if (folderPath.isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a folder to import.");
    return result;
  }

  // Check if folder exists.
  if (!QFileInfo::exists(folderPath)) {
    result.valid = false;
    result.message = tr("Folder does not exist.");
    return result;
  }

  // Check if it's a valid path.
  if (!PathUtils::isLegalPath(folderPath)) {
    result.valid = false;
    result.message = tr("Please specify a valid folder path.");
    return result;
  }

  // Check if it's a directory.
  QFileInfo fi(folderPath);
  if (!fi.isDir()) {
    result.valid = false;
    result.message = tr("Please specify a folder, not a file.");
    return result;
  }

  // Check for recursive import (avoid importing into self).
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    QJsonObject notebookConfig = notebookService->getNotebookConfig(p_notebookId);
    QString rootPath = notebookConfig.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
    QString parentAbsPath = PathUtils::concatenateFilePath(rootPath, p_parentPath);

    if (PathUtils::pathContains(folderPath, parentAbsPath)) {
      result.valid = false;
      result.message = tr("Cannot import folder into itself or its subdirectory.");
      return result;
    }
  }

  return result;
}

ImportFolderValidationResult
ImportFolderController::validateAll(const ImportFolderInput &p_input) const {
  ImportFolderValidationResult result;

  if (p_input.notebookId.isEmpty()) {
    result.valid = false;
    result.message = tr("No notebook specified.");
    return result;
  }

  result =
      validateSourceFolder(p_input.notebookId, p_input.parentFolderPath, p_input.sourceFolderPath);
  return result;
}

ImportFolderResult ImportFolderController::importFolder(const ImportFolderInput &p_input) {
  ImportFolderResult result;

  // Validate first.
  ImportFolderValidationResult validation = validateAll(p_input);
  if (!validation.valid) {
    result.success = false;
    result.errorMessage = validation.message;
    return result;
  }

  // Get NotebookService.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    return result;
  }

  // Convert QStringList suffixes to semicolon-separated string for vxcore API.
  QString suffixAllowlist = p_input.suffixes.join(QStringLiteral(";"));

  // Use vxcore_folder_import API to import the folder recursively.
  // The API handles folder creation, file copying, and indexing in one call.
  QString folderId = notebookService->importFolder(p_input.notebookId, p_input.parentFolderPath,
                                                   p_input.sourceFolderPath, suffixAllowlist);

  if (folderId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to import folder.");
    return result;
  }
  // Get the folder path from its ID using the vxcore API.
  QString folderPath = notebookService->getNodePathById(p_input.notebookId, folderId);

  result.success = true;
  result.nodeId.notebookId = p_input.notebookId;
  result.nodeId.relativePath = folderPath;
  return result;
}

// ---------------------------------------------------------------------------
// Share bundle import
// ---------------------------------------------------------------------------

QString ImportFolderController::phaseLabel(FolderBundleImporter::Phase p_phase) {
  switch (p_phase) {
  case FolderBundleImporter::Phase::Validating:
    return tr("Checking the shared folder…");
  case FolderBundleImporter::Phase::Copying:
    return tr("Copying files…");
  case FolderBundleImporter::Phase::Verifying:
    return tr("Verifying the copy…");
  case FolderBundleImporter::Phase::Attaching:
    return tr("Adding the folder to the notebook…");
  }
  return tr("Importing…");
}

ImportBundleValidationResult
ImportFolderController::validateBundle(const ImportBundleInput &p_input) const {
  ImportBundleValidationResult result;

  const QString bundlePath = p_input.bundlePath.trimmed();
  if (p_input.notebookId.isEmpty()) {
    result.valid = false;
    result.message = tr("No notebook specified.");
    return result;
  }
  if (bundlePath.isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a shared folder to import.");
    return result;
  }
  if (!PathUtils::isLegalPath(bundlePath)) {
    result.valid = false;
    result.message = tr("Please specify a valid folder path.");
    return result;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    result.valid = false;
    result.message = tr("NotebookService not available.");
    return result;
  }

  // The destination must be a real, indexed, writable, BUNDLED folder. vxcore
  // owns that judgement, including the raw-notebook and read-only rejections.
  const FolderImportPaths paths =
      notebookService->getFolderImportPaths(p_input.notebookId, p_input.parentFolderPath);
  if (!paths.isValid()) {
    result.valid = false;
    switch (paths.m_error) {
    case VXCORE_ERR_UNSUPPORTED:
      result.message = tr("Only bundled notebooks can import a shared folder.");
      break;
    case VXCORE_ERR_READ_ONLY:
      result.message = tr("This notebook is read-only.");
      break;
    default:
      result.message = paths.m_errorMessage.isEmpty()
                           ? tr("The destination folder could not be resolved.")
                           : paths.m_errorMessage;
      break;
    }
    return result;
  }

  // Importing a bundle that lives INSIDE the destination notebook would copy a
  // tree onto itself while the notebook is being mutated.
  if (PathUtils::pathContains(paths.m_notebookRoot, bundlePath) ||
      PathUtils::pathContains(bundlePath, paths.m_notebookRoot)) {
    result.valid = false;
    result.message = tr("Cannot import a shared folder that is inside this notebook.");
    return result;
  }

  const FolderBundleImporter::Inspection inspection = FolderBundleImporter::inspect(bundlePath);
  if (!inspection.m_valid) {
    result.valid = false;
    result.message = inspection.m_message;
    return result;
  }

  result.message = inspection.m_message;
  result.folderName = inspection.m_folderName;
  result.fileCount = inspection.m_fileCount;
  result.subfolderCount = inspection.m_subfolderCount;
  return result;
}

ImportFolderResult ImportFolderController::importBundle(const ImportBundleInput &p_input,
                                                        const Callbacks &p_callbacks) {
  ImportFolderResult result;
  auto makeFailure = [&result](const QString &p_message) {
    result.success = false;
    result.errorMessage = p_message;
    return result;
  };

  // The callbacks pump the event loop, so a second invocation (a shortcut, a
  // queued menu action) is genuinely reachable. Refuse rather than run two
  // imports over the same staging namespace.
  if (m_busy) {
    return makeFailure(tr("Another folder is already being imported."));
  }
  m_busy = true;
  struct BusyGuard {
    bool *flag;
    ~BusyGuard() { *flag = false; }
  } guard{&m_busy};

  const ImportBundleValidationResult validation = validateBundle(p_input);
  if (!validation.valid) {
    return makeFailure(validation.message);
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    return makeFailure(tr("NotebookService not available."));
  }

  const FolderImportPaths paths =
      notebookService->getFolderImportPaths(p_input.notebookId, p_input.parentFolderPath);
  if (!paths.isValid()) {
    return makeFailure(paths.m_errorMessage.isEmpty()
                           ? tr("The destination folder could not be resolved.")
                           : paths.m_errorMessage);
  }

  FolderBundleImporter::Request request;
  request.m_notebookRoot = paths.m_notebookRoot;
  request.m_destContentRoot = paths.m_contentRoot;
  request.m_destMetadataRoot = paths.m_metadataRoot;
  request.m_destRelativePath =
      p_input.parentFolderPath.isEmpty() ? QStringLiteral(".") : p_input.parentFolderPath;
  request.m_bundlePath = p_input.bundlePath.trimmed();
  request.m_failureInjection = m_failureInjection;

  auto *ioGate = m_services.get<NotebookIoGate>();
  const QString notebookId = p_input.notebookId;

  FolderBundleImporter::Callbacks callbacks;
  if (p_callbacks.m_labelChanged) {
    callbacks.m_phaseChanged = [&p_callbacks](FolderBundleImporter::Phase p_phase) {
      p_callbacks.m_labelChanged(phaseLabel(p_phase));
    };
  }
  callbacks.m_progress = p_callbacks.m_progress;
  callbacks.m_isCancelled = p_callbacks.m_isCancelled;

  // The AUTHORITATIVE id oracle. It walks vx.json on disk rather than querying
  // SQLite, because a bundled notebook indexes lazily and a store miss proves
  // nothing. Any non-success code fails the import CLOSED.
  callbacks.m_collectNodeIds = [notebookService, notebookId](QStringList *p_outIds,
                                                             QString *p_outError) {
    VxCoreError error = VXCORE_OK;
    *p_outIds = notebookService->collectNodeIds(notebookId, &error);
    if (error != VXCORE_OK) {
      *p_outError = tr("Could not check the notebook for conflicting notes, so nothing was "
                       "imported.");
      return false;
    }
    return true;
  };

  // The commit. This is the ONLY window that mutates the notebook, so it is
  // also the only window that needs the I/O gate — the staged copy above writes
  // exclusively into our own private staging directory.
  callbacks.m_commit = [notebookService, notebookId,
                        ioGate](const FolderBundleImporter::CommitRequest &p_commit,
                                QString *p_outError) {
    if (ioGate) {
      NotebookIoGate::ScopedTryLock lock(*ioGate, notebookId, kIoGateTimeoutMs);
      if (!lock.isLocked()) {
        *p_outError = tr("The notebook is busy saving or syncing. Try again in a moment.");
        return false;
      }
      VxCoreError error = VXCORE_OK;
      const QString folderId = notebookService->attachImportedFolder(
          notebookId, p_commit.m_destRelativePath, p_commit.m_folderName, p_commit.m_stagingDir,
          &error);
      if (error != VXCORE_OK || folderId.isEmpty()) {
        *p_outError = error == VXCORE_ERR_ALREADY_EXISTS
                          ? tr("This folder is already in this notebook, so nothing was imported.")
                          : tr("The folder could not be added to the notebook.");
        return false;
      }
      *p_commit.m_outFolderId = folderId;
      return true;
    }

    VxCoreError error = VXCORE_OK;
    const QString folderId =
        notebookService->attachImportedFolder(notebookId, p_commit.m_destRelativePath,
                                              p_commit.m_folderName, p_commit.m_stagingDir, &error);
    if (error != VXCORE_OK || folderId.isEmpty()) {
      *p_outError = error == VXCORE_ERR_ALREADY_EXISTS
                        ? tr("This folder is already in this notebook, so nothing was imported.")
                        : tr("The folder could not be added to the notebook.");
      return false;
    }
    *p_commit.m_outFolderId = folderId;
    return true;
  };

  const FolderBundleImporter::Result importResult = FolderBundleImporter::run(request, callbacks);

  switch (importResult.m_status) {
  case FolderBundleImporter::Status::Cancelled:
    return makeFailure(tr("The import was cancelled and nothing was changed."));
  case FolderBundleImporter::Status::Failed:
    return makeFailure(importResult.m_errorMessage.isEmpty()
                           ? tr("The shared folder could not be imported.")
                           : importResult.m_errorMessage);
  case FolderBundleImporter::Status::Succeeded:
    break;
  }

  // Resolve the identity through the store so the view selects exactly the node
  // vxcore attached, rather than a path this controller composed.
  QString relativePath =
      notebookService->getNodePathById(p_input.notebookId, importResult.m_folderId);
  if (relativePath.isEmpty()) {
    relativePath = importResult.m_relativePath;
  }

  result.success = true;
  result.nodeId.notebookId = p_input.notebookId;
  result.nodeId.relativePath = relativePath;
  return result;
}
