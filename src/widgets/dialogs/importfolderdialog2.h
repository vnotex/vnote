#ifndef IMPORTFOLDERDIALOG2_H
#define IMPORTFOLDERDIALOG2_H

#include "scrolldialog.h"

#include <core/nodeinfo.h>

namespace vnotex {

class FolderFilesFilterWidget;
class ImportFolderController;
class ServiceLocator;

// ImportFolderDialog2 - View for importing external folders using new DI architecture.
// Pure UI component - delegates business logic to ImportFolderController.
// Uses ServiceLocator for dependency injection, NodeIdentifier for parent reference.
class ImportFolderDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  // Import a folder under the parent folder identified by p_parentId.
  ImportFolderDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                      QWidget *p_parent = nullptr);
  ~ImportFolderDialog2() override;

  // Get the identifier of the newly created folder node (valid after accept()).
  NodeIdentifier getNewNodeId() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void validateInputs();

private:
  void setupUI();

  ServiceLocator &m_services;
  NodeIdentifier m_parentId;

  // Controller handles validation and import logic.
  ImportFolderController *m_controller = nullptr;

  // UI widgets.
  FolderFilesFilterWidget *m_filterWidget = nullptr;

  // Result.
  NodeIdentifier m_newNodeId;
};

} // namespace vnotex

#endif // IMPORTFOLDERDIALOG2_H
