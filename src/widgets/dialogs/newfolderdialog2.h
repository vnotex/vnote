#ifndef NEWFOLDERDIALOG2_H
#define NEWFOLDERDIALOG2_H

#include "scrolldialog.h"

#include <core/nodeinfo.h>

namespace vnotex {

class LineEditWithSnippet;
class NewFolderController;
class ServiceLocator;

// NewFolderDialog2 - View for creating folders using new DI architecture.
// Pure UI component - delegates business logic to NewFolderController.
// Uses ServiceLocator for dependency injection, NodeIdentifier for parent reference.
class NewFolderDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  // Create a folder under the parent folder identified by p_parentId.
  NewFolderDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                   QWidget *p_parent = nullptr);
  ~NewFolderDialog2() override;

  // Get the identifier of the newly created folder (valid after accept()).
  NodeIdentifier getNewNodeId() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  // Validate inputs and show error message if invalid.
  bool validateInputs();

  ServiceLocator &m_services;
  NodeIdentifier m_parentId;

  // Controller handles validation and creation logic.
  NewFolderController *m_controller = nullptr;

  // UI widgets.
  LineEditWithSnippet *m_nameEdit = nullptr;

  // Result.
  NodeIdentifier m_newNodeId;
};

} // namespace vnotex

#endif // NEWFOLDERDIALOG2_H
