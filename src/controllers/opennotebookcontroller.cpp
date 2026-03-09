#include "opennotebookcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <utils/pathutils.h>

using namespace vnotex;

OpenNotebookController::OpenNotebookController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

OpenNotebookValidationResult OpenNotebookController::validateRootFolder(const QString &p_path) const {
  OpenNotebookValidationResult result;
  QString rootFolderPath = p_path.trimmed();

  // Check if path is provided.
  if (rootFolderPath.isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a folder path.");
    return result;
  }

  // Check if path is legal.
  if (!PathUtils::isLegalPath(rootFolderPath)) {
    result.valid = false;
    result.message = tr("Please specify a valid folder path.");
    return result;
  }

  // Check if path exists and is a directory.
  QFileInfo finfo(rootFolderPath);
  if (!finfo.exists()) {
    result.valid = false;
    result.message = tr("The specified folder does not exist.");
    return result;
  }

  if (!finfo.isDir()) {
    result.valid = false;
    result.message = tr("The specified path is not a folder.");
    return result;
  }

  // Check for duplicate notebook with same root folder via NotebookService.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    QJsonArray notebooks = notebookService->listNotebooks();
    for (const auto &nb : notebooks) {
      QJsonObject nbObj = nb.toObject();
      QString existingPath = nbObj.value("root_path").toString();
      if (QDir(existingPath) == QDir(rootFolderPath)) {
        QString existingName = nbObj.value("name").toString();
        result.valid = false;
        result.message = tr("This notebook (%1) is already open.").arg(existingName);
        return result;
      }
    }
  }

  return result;
}

OpenNotebookResult OpenNotebookController::openNotebook(const OpenNotebookInput &p_input) {
  OpenNotebookResult result;

  // Validate first.
  OpenNotebookValidationResult validation = validateRootFolder(p_input.rootFolderPath);
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

  // Open notebook via service.
  // The vxcore layer will validate if it's a valid VNote notebook.
  QString notebookId = notebookService->openNotebook(p_input.rootFolderPath.trimmed());

  if (notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage =
        tr("Failed to open notebook from (%1). "
           "The folder may not be a valid VNote notebook.")
            .arg(p_input.rootFolderPath);
    return result;
  }

  // Get notebook name from config for display.
  QJsonObject config = notebookService->getNotebookConfig(notebookId);
  result.notebookName = config.value("name").toString();

  result.success = true;
  result.notebookId = notebookId;
  return result;
}
