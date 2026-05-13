#ifndef NEWNOTEBOOKDIALOG2_H
#define NEWNOTEBOOKDIALOG2_H

#include "scrolldialog.h"

#include <core/services/notebookcoreservice.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
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

private:
  void setupUI();

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

  // Advanced section.
  QToolButton *m_advancedToggle = nullptr;
  QWidget *m_advancedSection = nullptr;
  QLineEdit *m_assetsFolderEdit = nullptr;

  // Result.
  QString m_newNotebookId;
  QString m_selectedSyncMethod;
};

} // namespace vnotex

#endif // NEWNOTEBOOKDIALOG2_H
