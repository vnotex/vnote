#ifndef NEWNOTEDIALOG2_H
#define NEWNOTEDIALOG2_H

#include "scrolldialog.h"

#include <core/nodeinfo.h>

class QComboBox;
class QLineEdit;

namespace vnotex {

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
  // Create a note under the parent folder identified by p_parentId.
  NewNoteDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                 QWidget *p_parent = nullptr);
  ~NewNoteDialog2() override;

  // Get the identifier of the newly created note (valid after accept()).
  NodeIdentifier getNewNodeId() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void initDefaultValues();

  // Update filename when file type changes.
  void updateNameForFileType();

  // Validate inputs and show error message if invalid.
  bool validateInputs();

  // Get selected file type name and preferred suffix.
  QString getFileTypeName() const;
  QString getPreferredSuffix() const;

  ServiceLocator &m_services;
  NodeIdentifier m_parentId;

  // Controller handles validation and creation logic.
  NewNoteController *m_controller = nullptr;

  // UI widgets.
  QLineEdit *m_nameEdit = nullptr;
  QComboBox *m_fileTypeCombo = nullptr;
  NoteTemplateSelector *m_templateSelector = nullptr;

  // Result.
  NodeIdentifier m_newNodeId;

  // Remember last template selection.
  static QString s_lastTemplate;
};

} // namespace vnotex

#endif // NEWNOTEDIALOG2_H
