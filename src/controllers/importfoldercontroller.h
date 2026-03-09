#ifndef IMPORTFOLDERCONTROLLER_H
#define IMPORTFOLDERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <core/nodeinfo.h>

namespace vnotex {

class ServiceLocator;
namespace core { class NotebookService; }

// Input data structure for importing a folder.
struct ImportFolderInput {
  QString notebookId;
  QString parentFolderPath; // Relative path within notebook
  QString sourceFolderPath; // Absolute path to source folder
  QStringList suffixes;     // File suffixes to import
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

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // IMPORTFOLDERCONTROLLER_H
