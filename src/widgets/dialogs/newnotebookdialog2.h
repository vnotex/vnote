#ifndef NEWNOTEBOOKDIALOG2_H
#define NEWNOTEBOOKDIALOG2_H

#include "scrolldialog.h"
#include <core/services/syncsettings.h>

namespace vnotex {

class NotebookInfoWidget;
class NewNotebookController;
class ServiceLocator;

// NewNotebookDialog2 - View for creating notebooks.
// Pure UI component - delegates business logic to NewNotebookController.
// Uses ServiceLocator for dependency injection.
class NewNotebookDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  explicit NewNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~NewNotebookDialog2() override;

  // Get the ID of the newly created notebook (valid after accept()).
  QString getNewNotebookId() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  // Collect settings before creating the notebook; bootstrap owns persistence.
  void onConfigureSyncClicked();

private:
  void setupUI();

  // Sync creation requires valid settings collected through Configure.
  void updateOkButtonState();

  ServiceLocator &m_services;

  // Controller handles validation and creation logic.
  NewNotebookController *m_controller = nullptr;

  // UI widgets.
  NotebookInfoWidget *m_infoWidget = nullptr;

  // User-entered settings live only for this create/bootstrap operation.
  bool m_syncConfigured = false;
  SyncSettings m_pendingSettings;

  // Result.
  QString m_newNotebookId;
};

} // namespace vnotex

#endif // NEWNOTEBOOKDIALOG2_H
