#ifndef MANAGENOTEBOOKSCONTROLLER_H
#define MANAGENOTEBOOKSCONTROLLER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;

// Complete editable-settings snapshot for updating notebook config (not a patch).
struct NotebookUpdateInput {
  QString notebookId;
  QString name;
  QString description;
  QString assetsFolder;
  QString recycleBinFolder;
  QString lineEnding; // Empty means inherit the global editor setting.
};

// Result structure for notebook operations.
struct NotebookOperationResult {
  bool success = false;
  QString errorMessage;
};

// Validation result structure for manage notebooks operations.
struct ManageNotebooksValidationResult {
  bool valid = true;
  QString message;
};

// Notebook info structure for display.
struct NotebookInfo {
  QString id;
  QString name;
  QString description;
  QString rootFolder;
  QString assetsFolder;
  QString recycleBinFolder;
  QString type;
  QString lineEnding; // Empty means inherit the global editor setting.
  bool readOnly = false;
};

// Controller for notebook management operations.
// Handles business logic: validation, config updates, closing notebooks.
// View (ManageNotebooksDialog2) handles UI and user interaction.
class ManageNotebooksController : public QObject {
  Q_OBJECT

public:
  explicit ManageNotebooksController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // List all notebooks.
  // Returns array of notebook objects with id and name.
  QJsonArray listNotebooks() const;

  // Get notebook info for display.
  // Includes notebook access state alongside its editable settings.
  NotebookInfo getNotebookInfo(const QString &p_notebookId) const;

  // Validate notebook name.
  ManageNotebooksValidationResult validateName(const QString &p_name) const;

  // Update editable notebook configuration.
  NotebookOperationResult updateNotebook(const NotebookUpdateInput &p_input);

  // Close a notebook (remove from VNote without deleting files).
  NotebookOperationResult closeNotebook(const QString &p_notebookId);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // MANAGENOTEBOOKSCONTROLLER_H
