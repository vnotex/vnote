#include "newnotebookcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <utils/pathutils.h>

using namespace vnotex;

NewNotebookController::NewNotebookController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

ValidationResult NewNotebookController::validateName(const QString &p_name) const {
  ValidationResult result;

  if (p_name.trimmed().isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a name for the notebook.");
  }

  return result;
}

ValidationResult NewNotebookController::validateRootFolder(const QString &p_path,
                                                           NotebookType p_type) const {
  ValidationResult result;
  QString rootFolderPath = p_path.trimmed();

  // Check if path is legal.
  if (!PathUtils::isLegalPath(rootFolderPath)) {
    result.valid = false;
    result.message = tr("Please specify a valid root folder for the notebook.");
    return result;
  }

  // Check if path exists and is valid.
  QFileInfo finfo(rootFolderPath);
  if (finfo.exists()) {
    if (!finfo.isDir()) {
      result.valid = false;
      result.message = tr("Root folder should be a directory.");
      return result;
    }
    // Only require empty directory for bundled notebooks.
    if (p_type == NotebookType::Bundled && !QDir(rootFolderPath).isEmpty()) {
      result.valid = false;
      result.message = tr("Root folder of the notebook must be empty. "
                          "If you want to import existing data, please try other operations.");
      return result;
    }
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
        result.message =
            tr("There already exists a notebook (%1) with the same root folder.").arg(existingName);
        return result;
      }
    }
  }

  return result;
}

ValidationResult NewNotebookController::validateAll(const NewNotebookInput &p_input) const {
  ValidationResult result;

  // Validate name.
  result = validateName(p_input.name);
  if (!result.valid) {
    return result;
  }

  // Validate root folder.
  result = validateRootFolder(p_input.rootFolderPath, p_input.type);
  if (!result.valid) {
    return result;
  }

  return result;
}

NewNotebookResult NewNotebookController::createNotebook(const NewNotebookInput &p_input) {
  NewNotebookResult result;

  // Validate first.
  ValidationResult validation = validateAll(p_input);
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

  // Build config JSON for vxcore.
  QString configJson = buildConfigJson(p_input);

  // Create notebook via service.
  QString notebookId =
      notebookService->createNotebook(p_input.rootFolderPath.trimmed(), configJson, p_input.type);

  if (notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to create notebook in (%1).").arg(p_input.rootFolderPath);
    return result;
  }

  result.success = true;
  result.notebookId = notebookId;
  return result;
}

QString NewNotebookController::buildConfigJson(const NewNotebookInput &p_input) {
  QJsonObject configObj;
  configObj[QStringLiteral("name")] = p_input.name.trimmed();
  if (!p_input.description.trimmed().isEmpty()) {
    configObj[QStringLiteral("description")] = p_input.description.trimmed();
  }
  QString assetsFolderTrimmed = p_input.assetsFolder.trimmed();
  if (!assetsFolderTrimmed.isEmpty() && assetsFolderTrimmed != QStringLiteral("vx_assets")) {
    configObj[QStringLiteral("assetsFolder")] = assetsFolderTrimmed;
  }
  return QString::fromUtf8(QJsonDocument(configObj).toJson(QJsonDocument::Compact));
}
