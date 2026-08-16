#ifndef NEWNOTEDIALOG2_H
#define NEWNOTEDIALOG2_H

#include "scrolldialog.h"

#include <QHash>

#include <core/nodeinfo.h>

class QComboBox;
class QPlainTextEdit;

namespace vnotex {

class LineEditWithSnippet;
class NewNoteController;
class NodeInfoWidget2;
class NoteTemplateSelector;
class ServiceLocator;

// NewNoteDialog2 - View for creating notes using new DI architecture.
// Pure UI component - delegates business logic to NewNoteController.
// Uses ServiceLocator for dependency injection, NodeIdentifier for parent reference.
class NewNoteDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  // Where the new note's body comes from. Control visibility is derived from
  // the mode, so contradictory combinations cannot be expressed.
  enum class BodyMode {
    // Default: show the Template selector; its content is expanded through the
    // snippet engine.
    Template,
    // Capture mode: show an editable Content field whose text is written
    // verbatim, with no snippet/template interpretation.
    LiteralContent
  };

  // Immutable dialog configuration. Defaults reproduce the ordinary New Note
  // behavior exactly.
  struct Options {
    BodyMode m_bodyMode = BodyMode::Template;

    // Only meaningful for BodyMode::LiteralContent.
    QString m_initialContent;
  };

  // Create a note under the parent folder identified by p_parentId, using the
  // default (template) options.
  NewNoteDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                 QWidget *p_parent = nullptr);

  // Configured overload. There are deliberately no post-construction setters:
  // the mode fixes which controls exist.
  NewNoteDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                 const Options &p_options, QWidget *p_parent = nullptr);

  ~NewNoteDialog2() override;

  // Get the identifier of the newly created note (valid after accept()).
  NodeIdentifier getNewNodeId() const;

  // Get the caret offset from a template "@@" mark (valid after accept()), or -1.
  // In literal-content mode this is the end of the captured content.
  int getNewCursorOffset() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void initDefaultValues();

  // Update filename when file type changes.
  void updateNameForFileType();

  // Update file type combobox when name suffix changes.
  void updateFileTypeForName();

  // Validate inputs and show error message if invalid.
  bool validateInputs();

  // Select the template for the currently chosen file type: the session cache
  // (last template accepted for that type in this run) when present, otherwise
  // the configured default for the type, otherwise "None".
  void applyTemplateForFileType();

  // Get selected file type name and preferred suffix.
  QString getFileTypeName() const;
  QString getPreferredSuffix() const;

  ServiceLocator &m_services;
  NodeIdentifier m_parentId;

  const Options m_options;

  // Controller handles validation and creation logic.
  NewNoteController *m_controller = nullptr;

  // UI widgets.
  LineEditWithSnippet *m_nameEdit = nullptr;
  QComboBox *m_fileTypeCombo = nullptr;
  // Only one of these exists, per m_options.m_bodyMode.
  NoteTemplateSelector *m_templateSelector = nullptr;
  QPlainTextEdit *m_contentEdit = nullptr;

  // Guard flag to prevent feedback loops between name↔type sync.
  bool m_fileTypeComboMuted = false;

  // Once the user types in the Name field, the dialog must never rewrite their
  // text: only the auto-generated default gets uniquified with a _N suffix.
  bool m_nameEditedByUser = false;

  // Set while the dialog itself drives the template selector, so a programmatic
  // selection is not mistaken for a user choice.
  bool m_templateSelectorMuted = false;

  // Once the user picks a template by hand, a later file-type change must not
  // overwrite that choice with the type's default.
  bool m_templateChosenByUser = false;

  // Result.
  NodeIdentifier m_newNodeId;
  int m_newCursorOffset = -1;

  // Session cache: the last template accepted per file type name, for this
  // process only. A present key (even with an empty value, i.e. "None") wins
  // over the configured default; an absent key falls back to it.
  static QHash<QString, QString> s_lastTemplateByFileType;
};

} // namespace vnotex

#endif // NEWNOTEDIALOG2_H
