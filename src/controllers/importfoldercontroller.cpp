#include "importfoldercontroller.h"

#include <QFileInfo>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <utils/pathutils.h>

using namespace vnotex;

ImportFolderController::ImportFolderController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

ImportFolderValidationResult ImportFolderController::validateSourceFolder(
    const QString &p_notebookId, const QString &p_parentPath,
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
    QString rootPath = notebookConfig.value("rootFolder").toString();
    QString parentAbsPath = PathUtils::concatenateFilePath(rootPath, p_parentPath);

    if (PathUtils::pathContains(folderPath, parentAbsPath)) {
      result.valid = false;
      result.message = tr("Cannot import folder into itself or its subdirectory.");
      return result;
    }
  }

  return result;
}

ImportFolderValidationResult ImportFolderController::validateAll(
    const ImportFolderInput &p_input) const {
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
  QString folderId = notebookService->importFolder(
      p_input.notebookId, p_input.parentFolderPath, p_input.sourceFolderPath, suffixAllowlist);

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
