#include "newfoldercontroller.h"

#include <QJsonArray>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <utils/pathutils.h>

using namespace vnotex;

NewFolderController::NewFolderController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

FolderValidationResult NewFolderController::validateName(const QString &p_notebookId,
                                                         const QString &p_parentPath,
                                                         const QString &p_name) const {
  FolderValidationResult result;

  QString name = p_name.trimmed();
  if (name.isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a name for the folder.");
    return result;
  }

  // Check if it's a legal filename.
  if (!PathUtils::isLegalFileName(name)) {
    result.valid = false;
    result.message = tr("Please specify a valid name for the folder.");
    return result;
  }

  // Check for conflicts with existing folders in the parent folder.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    QJsonObject children = notebookService->listFolderChildren(p_notebookId, p_parentPath);
    QJsonArray folders = children.value("folders").toArray();
    for (const auto &folder : folders) {
      QJsonObject folderObj = folder.toObject();
      QString existingName = folderObj.value("name").toString();
      if (existingName.compare(name, Qt::CaseInsensitive) == 0) {
        result.valid = false;
        result.message = tr("Name conflicts with existing folder.");
        return result;
      }
    }
    // Also check files to avoid conflict with existing notes.
    QJsonArray files = children.value("files").toArray();
    for (const auto &file : files) {
      QJsonObject fileObj = file.toObject();
      QString existingName = fileObj.value("name").toString();
      if (existingName.compare(name, Qt::CaseInsensitive) == 0) {
        result.valid = false;
        result.message = tr("Name conflicts with existing note.");
        return result;
      }
    }
  }

  return result;
}

FolderValidationResult NewFolderController::validateAll(const NewFolderInput &p_input) const {
  FolderValidationResult result;

  if (p_input.notebookId.isEmpty()) {
    result.valid = false;
    result.message = tr("No notebook specified.");
    return result;
  }

  result = validateName(p_input.notebookId, p_input.parentFolderPath, p_input.name);
  return result;
}

NewFolderResult NewFolderController::createFolder(const NewFolderInput &p_input) {
  NewFolderResult result;

  // Validate first.
  FolderValidationResult validation = validateAll(p_input);
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

  // Create folder via service (returns folder ID, not path).
  QString folderId =
      notebookService->createFolder(p_input.notebookId, p_input.parentFolderPath, p_input.name);

  if (folderId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to create folder (%1).").arg(p_input.name);
    return result;
  }

  // Get the relative path from the folder ID.
  QString folderPath = notebookService->getNodePathById(p_input.notebookId, folderId);
  if (folderPath.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to get path for created folder.");
    return result;
  }

  result.success = true;
  result.nodeId.notebookId = p_input.notebookId;
  result.nodeId.relativePath = folderPath;
  return result;
}
