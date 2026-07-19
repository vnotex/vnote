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
  QString fileTypeName;     // File type name (e.g., "Markdown", "Text")
};

// Result structure for note creation.
struct NewNoteResult {
  bool success = false;
  NodeIdentifier nodeId; // Identifier of the created note
  QString errorMessage;
  int cursorOffset = -1; // QTextDocument caret position from a template "@@" mark, or -1.
};

// Input data structure for the quick-note-scheme flow.
struct QuickNoteInput {
  QString notebookId;
  QString parentFolderPath; // Relative path within notebook (may be empty = root)
  QString noteNameScheme;   // Filename scheme, may contain %name% symbols (e.g. "%date%.md")
  QString templateContent;  // Optional raw template body
};

// Result of evaluating template content (expanded text + caret position).
struct EvaluatedTemplate {
  QString content;
  int cursorOffset = -1;
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

  // Create a quick note from a scheme (filename expansion + folder creation +
  // template body expansion + write). Encapsulates the MVC-correct business logic
  // formerly inlined in NotebookExplorer2.
  NewNoteResult createQuickNote(const QuickNoteInput &p_input);

  // Evaluate template content with snippets. Returns expanded content plus the
  // caret position derived from a top-level "@@" cursor mark (-1 if absent).
  EvaluatedTemplate evaluateTemplateContent(const QString &p_content, const QString &p_name);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // NEWNOTECONTROLLER_H
