#ifndef NEWNOTEBOOKCONTROLLER_H
#define NEWNOTEBOOKCONTROLLER_H

#include <QObject>
#include <QString>

#include <core/services/notebookcoreservice.h>

namespace vnotex {

class ServiceLocator;

// Input data structure for creating a new notebook.
struct NewNotebookInput {
  QString name;
  QString description;
  QString rootFolderPath;
  QString assetsFolder = QStringLiteral("vx_assets");
  NotebookType type = NotebookType::Bundled;
  // Sync method selected at creation time. "none" (default) creates a notebook
  // without sync configuration. "git" injects flat sync markers (per ADR-8)
  // into the vxcore notebook config so that bootstrap (T14) can later enable
  // sync against the empty root (per ADR-7: create-then-enable).
  QString syncMethod = QStringLiteral("none");
};

// Result structure for notebook creation.
struct NewNotebookResult {
  bool success = false;
  QString notebookId;
  QString errorMessage;
};

// Validation result structure.
struct ValidationResult {
  bool valid = true;
  QString message;
};

// Controller for new notebook creation operations.
// Handles business logic: validation, duplicate checking, notebook creation.
// View (NewNotebookDialog2) collects input and displays results.
class NewNotebookController : public QObject {
  Q_OBJECT

public:
  explicit NewNotebookController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate notebook name.
  ValidationResult validateName(const QString &p_name) const;

  // Validate root folder path.
  // Checks: legal path, empty or non-existent, no duplicate notebook.
  ValidationResult validateRootFolder(const QString &p_path,
                                      NotebookType p_type = NotebookType::Bundled) const;

  // Validate all inputs.
  ValidationResult validateAll(const NewNotebookInput &p_input) const;

  // Build config JSON string from input for vxcore.
  static QString buildConfigJson(const NewNotebookInput &p_input);

  // Create a new notebook with the given input.
  // Returns result with success status and notebook ID or error message.
  NewNotebookResult createNotebook(const NewNotebookInput &p_input);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // NEWNOTEBOOKCONTROLLER_H
