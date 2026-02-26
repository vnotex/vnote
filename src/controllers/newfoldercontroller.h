#ifndef NEWFOLDERCONTROLLER_H
#define NEWFOLDERCONTROLLER_H

#include <QObject>
#include <QString>

#include <core/nodeinfo.h>

namespace vnotex {

class ServiceLocator;

// Input data structure for creating a new folder.
struct NewFolderInput {
  QString notebookId;
  QString parentFolderPath; // Relative path within notebook
  QString name;             // Folder name
};

// Result structure for folder creation.
struct NewFolderResult {
  bool success = false;
  NodeIdentifier nodeId; // Identifier of the created folder
  QString errorMessage;
};

// Validation result structure.
struct FolderValidationResult {
  bool valid = true;
  QString message;
};

// Controller for new folder creation operations.
// Handles business logic: validation, duplicate checking, folder creation.
// View (NewFolderDialog2) collects input and displays results.
class NewFolderController : public QObject {
  Q_OBJECT

public:
  explicit NewFolderController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate folder name.
  // Checks: non-empty, legal filename, no conflicts.
  FolderValidationResult validateName(const QString &p_notebookId, const QString &p_parentPath,
                                      const QString &p_name) const;

  // Validate all inputs.
  FolderValidationResult validateAll(const NewFolderInput &p_input) const;

  // Create a new folder with the given input.
  // Returns result with success status and node identifier or error message.
  NewFolderResult createFolder(const NewFolderInput &p_input);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // NEWFOLDERCONTROLLER_H
