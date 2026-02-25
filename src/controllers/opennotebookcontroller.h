#ifndef OPENNOTEBOOKCONTROLLER_H
#define OPENNOTEBOOKCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;

// Input data structure for opening an existing notebook.
struct OpenNotebookInput {
  QString rootFolderPath;
};

// Result structure for notebook open operation.
struct OpenNotebookResult {
  bool success = false;
  QString notebookId;
  QString notebookName;
  QString errorMessage;
};

// Validation result structure for open notebook.
struct OpenNotebookValidationResult {
  bool valid = true;
  QString message;
};

// Controller for opening existing notebooks.
// Handles business logic: validation, duplicate checking, notebook opening via NotebookService.
// View (OpenNotebookDialog2) collects input and displays results.
class OpenNotebookController : public QObject {
  Q_OBJECT

public:
  explicit OpenNotebookController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate root folder path for opening.
  // Checks: path exists, is a valid VNote notebook, not already open.
  OpenNotebookValidationResult validateRootFolder(const QString &p_path) const;

  // Open an existing notebook with the given input.
  // Returns result with success status and notebook ID or error message.
  OpenNotebookResult openNotebook(const OpenNotebookInput &p_input);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // OPENNOTEBOOKCONTROLLER_H
