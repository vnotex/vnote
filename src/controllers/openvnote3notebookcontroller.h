#ifndef OPENVNOTE3NOTEBOOKCONTROLLER_H
#define OPENVNOTE3NOTEBOOKCONTROLLER_H

#include <QObject>
#include <QString>

#include <controllers/opennotebookcontroller.h>
#include <core/services/vnote3migrationservice.h>

namespace vnotex {

class ServiceLocator;

// Input data structure for converting and opening a VNote3 notebook.
struct OpenVNote3NotebookInput {
  QString sourceRootFolderPath;
  QString destinationRootFolderPath;
  bool confirmedConversion = false;
};

// Result structure for VNote3 notebook conversion and open operation.
struct OpenVNote3NotebookResult {
  bool success = false;
  QString notebookId;
  QString notebookName;
  QString errorMessage;
};

// Controller for converting and opening VNote3 notebooks.
// Handles validation, source inspection, and delegates conversion to VNote3MigrationService.
// Does NOT contain any UI logic.
class OpenVNote3NotebookController : public QObject {
  Q_OBJECT

public:
  explicit OpenVNote3NotebookController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Delegates to VNote3MigrationService::inspectSourceNotebook()
  VNote3SourceInspectionResult inspectSourceRootFolder(const QString &p_path) const;

  // Validates destination path for conversion target.
  // p_sourcePath needed to check overlap.
  OpenNotebookValidationResult
  validateDestinationRootFolder(const QString &p_sourcePath,
                                const QString &p_destinationPath) const;

  OpenVNote3NotebookResult convertAndOpen(const OpenVNote3NotebookInput &p_input);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // OPENVNOTE3NOTEBOOKCONTROLLER_H
