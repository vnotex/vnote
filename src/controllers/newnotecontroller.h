#ifndef NEWNOTECONTROLLER_H
#define NEWNOTECONTROLLER_H

#include <QObject>
#include <QString>

#include <core/nodeinfo.h>

namespace vnotex {

class ServiceLocator;

// Input data structure for creating a new note.
struct NewNoteInput {
  QString notebookId;
  QString parentFolderPath; // Relative path within notebook
  QString name;             // File name with extension
  QString templateContent;  // Optional template content
  int fileType = 0;         // FileTypeHelper type index
};

// Result structure for note creation.
struct NewNoteResult {
  bool success = false;
  NodeIdentifier nodeId; // Identifier of the created note
  QString errorMessage;
};

// Validation result structure.
struct NoteValidationResult {
  bool valid = true;
  QString message;
};

// Controller for new note creation operations.
// Handles business logic: validation, duplicate checking, note creation.
// View (NewNoteDialog2) collects input and displays results.
class NewNoteController : public QObject {
  Q_OBJECT

public:
  explicit NewNoteController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate note name.
  // Checks: non-empty, legal filename, no conflicts.
  NoteValidationResult validateName(const QString &p_notebookId, const QString &p_parentPath,
                                    const QString &p_name) const;

  // Validate all inputs.
  NoteValidationResult validateAll(const NewNoteInput &p_input) const;

  // Create a new note with the given input.
  // Returns result with success status and node identifier or error message.
  NewNoteResult createNote(const NewNoteInput &p_input);

  // Evaluate template content with snippets.
  static QString evaluateTemplateContent(const QString &p_content, const QString &p_name);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // NEWNOTECONTROLLER_H
