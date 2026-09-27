#ifndef NEWNOTEBOOKDIALOG2_H
#define NEWNOTEBOOKDIALOG2_H

#include "scrolldialog.h"
#include <core/services/syncsettings.h>

class QComboBox;
class QLabel;
class QPushButton;

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

  // Returns the userData string of the currently selected sync method
  // ("none", "git" or "webdav"). Returns "none" for Raw notebooks
  // or unavailable.
  QString getSelectedSyncMethod() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void onTypeComboChanged();

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

  // Sync method selection (visible only for Bundled notebooks).
  QLabel *m_syncMethodLabel = nullptr;
  QComboBox *m_syncMethodCombo = nullptr;

  // User-entered settings live only for this create/bootstrap operation.
  QPushButton *m_configureSyncButton = nullptr;
  QWidget *m_syncMethodContainer = nullptr; // wraps combo + Configure... button
  bool m_syncConfigured = false;
  SyncSettings m_pendingSettings;

  // Result.
  QString m_newNotebookId;
};

} // namespace vnotex

#endif // NEWNOTEBOOKDIALOG2_H
