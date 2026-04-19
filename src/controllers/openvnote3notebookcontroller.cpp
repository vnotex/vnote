#include "openvnote3notebookcontroller.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/vnote3migrationservice.h>

using namespace vnotex;

OpenVNote3NotebookController::OpenVNote3NotebookController(ServiceLocator &p_services,
                                                           QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

VNote3SourceInspectionResult
OpenVNote3NotebookController::inspectSourceRootFolder(const QString &p_path) const {
  auto *migrationService = m_services.get<VNote3MigrationService>();
  if (!migrationService) {
    VNote3SourceInspectionResult result;
    result.valid = false;
    result.errorMessage = tr("VNote3MigrationService not available.");
    return result;
  }
  return migrationService->inspectSourceNotebook(p_path);
}

OpenNotebookValidationResult OpenVNote3NotebookController::validateDestinationRootFolder(
    const QString &p_sourcePath, const QString &p_destinationPath) const {
  OpenNotebookValidationResult result;

  if (p_destinationPath.trimmed().isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a destination folder path.");
    return result;
  }

  const QString src = QDir::cleanPath(p_sourcePath);
  const QString dst = QDir::cleanPath(p_destinationPath);

  // Same path check.
  if (src == dst) {
    result.valid = false;
    result.message = tr("Destination folder must be different from the source folder.");
    return result;
  }

  // Destination inside source check.
  if (dst.startsWith(src + QLatin1Char('/'))) {
    result.valid = false;
    result.message = tr("Destination folder must not be inside the source folder.");
    return result;
  }

  // Source inside destination check.
  if (src.startsWith(dst + QLatin1Char('/'))) {
    result.valid = false;
    result.message = tr("Source folder must not be inside the destination folder.");
    return result;
  }

  // Non-empty destination check.
  QDir destDir(p_destinationPath);
  if (destDir.exists() && !destDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
    result.valid = false;
    result.message = tr("Destination folder must be empty or non-existent.");
    return result;
  }

  // Duplicate-open check via NotebookCoreService.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    QJsonArray notebooks = notebookService->listNotebooks();
    for (const auto &nb : notebooks) {
      QJsonObject nbObj = nb.toObject();
      QString existingPath = QDir::cleanPath(nbObj.value(QStringLiteral("root_path")).toString());
      if (existingPath == dst) {
        QString existingName = nbObj.value(QStringLiteral("name")).toString();
        result.valid = false;
        result.message = tr("This path is already opened as notebook (%1).").arg(existingName);
        return result;
      }
    }
  }

  return result;
}

OpenVNote3NotebookResult
OpenVNote3NotebookController::convertAndOpen(const OpenVNote3NotebookInput &p_input) {
  OpenVNote3NotebookResult result;

  // Step 1: Require user confirmation.
  if (!p_input.confirmedConversion) {
    result.success = false;
    result.errorMessage = tr("Conversion not confirmed by user");
    return result;
  }

  // Step 2: Inspect source folder.
  VNote3SourceInspectionResult inspection = inspectSourceRootFolder(p_input.sourceRootFolderPath);
  if (!inspection.valid) {
    result.success = false;
    result.errorMessage = inspection.errorMessage;
    return result;
  }

  // Step 3: Validate destination folder.
  OpenNotebookValidationResult destValidation = validateDestinationRootFolder(
      p_input.sourceRootFolderPath, p_input.destinationRootFolderPath);
  if (!destValidation.valid) {
    result.success = false;
    result.errorMessage = destValidation.message;
    return result;
  }

  // Step 4: Run conversion.
  auto *migrationService = m_services.get<VNote3MigrationService>();
  if (!migrationService) {
    result.success = false;
    result.errorMessage = tr("VNote3MigrationService not available.");
    return result;
  }

  connect(migrationService, &VNote3MigrationService::progressUpdated,
          this, &OpenVNote3NotebookController::progressUpdated);

  VNote3ConversionResult convResult = migrationService->convertNotebook(
      p_input.sourceRootFolderPath, p_input.destinationRootFolderPath);

  disconnect(migrationService, &VNote3MigrationService::progressUpdated,
             this, &OpenVNote3NotebookController::progressUpdated);
  if (!convResult.success) {
    result.success = false;
    result.errorMessage = convResult.errorMessage;
    return result;
  }

  // Step 5: Open the converted notebook via OpenNotebookController.
  OpenNotebookController openController(m_services);
  OpenNotebookInput openInput;
  openInput.rootFolderPath = p_input.destinationRootFolderPath;
  OpenNotebookResult openResult = openController.openNotebook(openInput);

  result.success = openResult.success;
  result.notebookId = openResult.notebookId;
  result.notebookName = openResult.notebookName;
  result.errorMessage = openResult.errorMessage;
  return result;
}
