#ifndef RECYCLEBINCONTROLLER_H
#define RECYCLEBINCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;

// Result structure for recycle bin operations.
struct RecycleBinResult {
  bool success = false;
  QString errorMessage;
  QString path;  // For operations that return a path.
};

// Controller for recycle bin operations on notebooks.
// Handles business logic: validation, confirmation, service calls.
// View (NotebookExplorer2) triggers actions and displays results.
class RecycleBinController : public QObject {
  Q_OBJECT

public:
  explicit RecycleBinController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Check if recycle bin is supported for the given notebook.
  // Returns empty string if not supported, otherwise returns the path.
  QString getRecycleBinPath(const QString &p_notebookId) const;

  // Get the notebook name for display in dialogs.
  QString getNotebookName(const QString &p_notebookId) const;

  // Prepare the recycle bin folder for opening.
  // Creates the folder if it doesn't exist.
  // Returns result with the path to open, or error message.
  RecycleBinResult prepareRecycleBinPath(const QString &p_notebookId);

  // Empty the recycle bin after confirmation.
  // Returns result with success status or error message.
  RecycleBinResult emptyRecycleBin(const QString &p_notebookId);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // RECYCLEBINCONTROLLER_H
