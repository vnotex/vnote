#ifndef NEWNOTEBOOKDIALOG2_H
#define NEWNOTEBOOKDIALOG2_H

#include "scrolldialog.h"

#include <core/services/notebookcoreservice.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace vnotex {

class LineEditWithSnippet;
class LocationInputWithBrowseButton;
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
  // ("none" or "git"). Returns "none" if the combo is hidden (e.g. Raw type)
  // or unavailable.
  QString getSelectedSyncMethod() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void handleRootFolderPathChanged();
  void onTypeComboChanged(int p_index);

  // Opens NotebookSyncInfoDialog2 in pre-create mode to collect remote URL +
  // PAT before the notebook exists. Stashes the result in m_pendingRemoteUrl /
  // m_pendingPat for T4 to consume in acceptedButtonClicked().
  void onConfigureSyncClicked();

private:
  void setupUI();

  // Recompute the OK button enabled state. When Git sync is selected but the
  // user hasn't run Configure... yet, OK is disabled with an explanatory
  // tooltip.
  void updateOkButtonState();

  // Collect input from UI into NewNotebookInput struct.
  // Defined in cpp to avoid exposing controller types in header.
  void collectInput();

  ServiceLocator &m_services;

  // Controller handles validation and creation logic.
  NewNotebookController *m_controller = nullptr;

  // UI widgets.
  LineEditWithSnippet *m_nameEdit = nullptr;
  QPlainTextEdit *m_descriptionEdit = nullptr;
  LocationInputWithBrowseButton *m_rootFolderInput = nullptr;
  QComboBox *m_typeCombo = nullptr;

  // Sync method selection (visible only for Bundled notebooks).
  QLabel *m_syncMethodLabel = nullptr;
  QComboBox *m_syncMethodCombo = nullptr;

  // Pre-create sync config (T3 of notebook-sync-config-pre-create plan).
  // The Configure... button is shown next to the sync method combo when Git
  // is selected; clicking it opens NotebookSyncInfoDialog2 in pre-create mode.
  QPushButton *m_configureSyncButton = nullptr;
  QWidget *m_syncMethodContainer = nullptr; // wraps combo + Configure... button
  bool m_syncConfigured = false;
  QString m_pendingRemoteUrl;
  QString m_pendingPat;

  // Advanced section.
  QToolButton *m_advancedToggle = nullptr;
  QWidget *m_advancedSection = nullptr;
  QLineEdit *m_assetsFolderEdit = nullptr;

  // Result.
  QString m_newNotebookId;
};

} // namespace vnotex

#endif // NEWNOTEBOOKDIALOG2_H
