#ifndef IMPORTFOLDERDIALOG2_H
#define IMPORTFOLDERDIALOG2_H

#include "scrolldialog.h"

#include <core/nodeinfo.h>

class QButtonGroup;
class QLabel;
class QRadioButton;
class QStackedWidget;

namespace vnotex {

class FolderFilesFilterWidget;
class ImportFolderController;
class LocationInputWithBrowseButton;
class ServiceLocator;

// ImportFolderDialog2 - View for importing a folder into a notebook.
//
// Two modes, selected by radios over a QStackedWidget (modeled on
// OpenNotebookDialog2):
//
//   External folder       copy an arbitrary directory in, filtered by suffix.
//                         Ids and timestamps are generated fresh.
//   Shared folder from    restore a "*-bundle" directory produced by Share
//   VNote                 Folder, preserving ids, timestamps, tags and
//                         attachments VERBATIM.
//
// Pure UI component - delegates business logic to ImportFolderController.
class ImportFolderDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  // Mode values double as QStackedWidget page indices.
  enum Mode { ExternalMode = 0, BundleMode = 1 };

  // Import a folder under the parent folder identified by p_parentId.
  ImportFolderDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                      QWidget *p_parent = nullptr);
  ~ImportFolderDialog2() override;

  // Get the identifier of the newly created folder node (valid after accept()).
  NodeIdentifier getNewNodeId() const;

  // Currently selected mode.
  Mode currentMode() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void validateInputs();
  void onModeChanged();
  void onBundlePathChanged();

private:
  void setupUI();
  void setupExternalPage(QWidget *p_page);
  void setupBundlePage(QWidget *p_page);

  // Disables the bundle mode with an explanatory tooltip when the destination
  // notebook cannot accept one (raw or read-only).
  void applyBundleModeAvailability();

  void validateExternalInputs();
  void validateBundleInputs();

  void importExternalFolder();
  void importBundle();

  ServiceLocator &m_services;
  NodeIdentifier m_parentId;

  // Controller handles validation and import logic.
  ImportFolderController *m_controller = nullptr;

  // UI widgets.
  QRadioButton *m_externalModeRadio = nullptr;
  QRadioButton *m_bundleModeRadio = nullptr;
  QButtonGroup *m_modeGroup = nullptr;
  QStackedWidget *m_modeStack = nullptr;
  FolderFilesFilterWidget *m_filterWidget = nullptr;
  LocationInputWithBrowseButton *m_bundleInput = nullptr;
  QLabel *m_bundlePreviewLabel = nullptr;

  // Result.
  NodeIdentifier m_newNodeId;
};

} // namespace vnotex

#endif // IMPORTFOLDERDIALOG2_H
