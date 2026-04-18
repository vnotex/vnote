#include "openvnote3notebookdialog2.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QLabel>

#include <controllers/openvnote3notebookcontroller.h>
#include <core/servicelocator.h>

#include "../locationinputwithbrowsebutton.h"

using namespace vnotex;

OpenVNote3NotebookDialog2::OpenVNote3NotebookDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  m_controller = new OpenVNote3NotebookController(m_services, this);

  setupUI();
}

OpenVNote3NotebookDialog2::~OpenVNote3NotebookDialog2() = default;

void OpenVNote3NotebookDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  m_sourceInput = new LocationInputWithBrowseButton(mainWidget);
  m_sourceInput->setBrowseType(LocationInputWithBrowseButton::Folder, tr("Select Source Folder"));
  m_sourceInput->setPlaceholderText(tr("Select the VNote3 notebook root folder"));
  layout->addRow(tr("Source Folder:"), m_sourceInput);

  m_destinationInput = new LocationInputWithBrowseButton(mainWidget);
  m_destinationInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                    tr("Select Destination Folder"));
  m_destinationInput->setPlaceholderText(tr("Select the destination root folder"));
  layout->addRow(tr("Destination Folder:"), m_destinationInput);

  m_nameTitleLabel = new QLabel(tr("Notebook Name:"), mainWidget);
  m_nameLabel = new QLabel(mainWidget);
  m_nameTitleLabel->hide();
  m_nameLabel->hide();
  layout->addRow(m_nameTitleLabel, m_nameLabel);

  m_descriptionTitleLabel = new QLabel(tr("Description:"), mainWidget);
  m_descriptionLabel = new QLabel(mainWidget);
  m_descriptionLabel->setWordWrap(true);
  m_descriptionTitleLabel->hide();
  m_descriptionLabel->hide();
  layout->addRow(m_descriptionTitleLabel, m_descriptionLabel);

  m_confirmCheckBox = new QCheckBox(
      tr("I understand this will copy the notebook to the destination folder and not all data will be migrated."), mainWidget);
  layout->addRow(QString(), m_confirmCheckBox);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setButtonEnabled(QDialogButtonBox::Ok, false);

  setWindowTitle(tr("Open VNote3 Notebook"));
  setMinimumWidth(600);

  // Track manual edits to destination.
  connect(m_destinationInput, &LocationInputWithBrowseButton::textChanged, this, [this]() {
    if (!m_suppressDestinationTracking) {
      m_destinationManuallyEdited = true;
    }
  });

  // Reactive validation on all inputs.
  connect(m_sourceInput, &LocationInputWithBrowseButton::textChanged, this,
          &OpenVNote3NotebookDialog2::validateInputs);
  connect(m_destinationInput, &LocationInputWithBrowseButton::textChanged, this,
          &OpenVNote3NotebookDialog2::validateInputs);
  connect(m_confirmCheckBox, &QCheckBox::toggled, this, &OpenVNote3NotebookDialog2::validateInputs);
}

void OpenVNote3NotebookDialog2::validateInputs() {
  auto sourcePath = m_sourceInput->text().trimmed();

  // Empty source: reset everything.
  if (sourcePath.isEmpty()) {
    m_nameTitleLabel->hide();
    m_nameLabel->hide();
    m_descriptionTitleLabel->hide();
    m_descriptionLabel->hide();
    setInformationText(QString(), InformationLevel::Info);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  // Inspect source.
  auto inspection = m_controller->inspectSourceRootFolder(sourcePath);
  if (!inspection.valid) {
    m_nameTitleLabel->hide();
    m_nameLabel->hide();
    m_descriptionTitleLabel->hide();
    m_descriptionLabel->hide();
    setInformationText(inspection.errorMessage, InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  // Show metadata.
  m_nameTitleLabel->show();
  m_nameLabel->setText(inspection.notebookName);
  m_nameLabel->show();

  if (inspection.notebookDescription.isEmpty()) {
    m_descriptionTitleLabel->hide();
    m_descriptionLabel->hide();
  } else {
    m_descriptionTitleLabel->show();
    m_descriptionLabel->setText(inspection.notebookDescription);
    m_descriptionLabel->show();
  }

  // Content height changed — resize dialog to fit new fields.
  resizeToHideScrollBarLater(true, false);

  // Auto-fill destination if user hasn't manually edited it (tracks source changes).
  if (!m_destinationManuallyEdited) {
    m_suppressDestinationTracking = true;
    m_destinationInput->setText(QDir::cleanPath(sourcePath + "-converted"));
    m_suppressDestinationTracking = false;
  }

  // Validate destination.
  auto destinationPath = m_destinationInput->text().trimmed();
  if (destinationPath.isEmpty()) {
    setInformationText(tr("Enter a destination folder."), InformationLevel::Info);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  auto destResult = m_controller->validateDestinationRootFolder(sourcePath, destinationPath);
  if (!destResult.valid) {
    setInformationText(destResult.message, InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  // Checkbox gate.
  if (!m_confirmCheckBox->isChecked()) {
    setInformationText(QString(), InformationLevel::Info);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  // All valid.
  setInformationText(QString(), InformationLevel::Info);
  setButtonEnabled(QDialogButtonBox::Ok, true);
}

void OpenVNote3NotebookDialog2::acceptedButtonClicked() {
  OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = m_sourceInput->text().trimmed();
  input.destinationRootFolderPath = m_destinationInput->text().trimmed();
  input.confirmedConversion = m_confirmCheckBox->isChecked();

  QApplication::setOverrideCursor(Qt::WaitCursor);
  auto result = m_controller->convertAndOpen(input);
  QApplication::restoreOverrideCursor();

  if (result.success) {
    m_openedNotebookId = result.notebookId;
    accept();
  } else {
    setInformationText(result.errorMessage, InformationLevel::Error);
  }
}

QString OpenVNote3NotebookDialog2::getOpenedNotebookId() const { return m_openedNotebookId; }
