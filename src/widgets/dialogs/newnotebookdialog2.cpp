#include "newnotebookdialog2.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <utils/pathutils.h>

#include "../locationinputwithbrowsebutton.h"
#include "../widgetsfactory.h"

using namespace vnotex;

NewNotebookDialog2::NewNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  // Create controller.
  m_controller = new NewNotebookController(m_services, this);

  setupUI();

  m_nameEdit->setFocus();
}

NewNotebookDialog2::~NewNotebookDialog2() = default;

void NewNotebookDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  // Name input.
  m_nameEdit = WidgetsFactory::createLineEdit(mainWidget);
  m_nameEdit->setPlaceholderText(tr("Notebook name"));
  layout->addRow(tr("Name:"), m_nameEdit);

  // Description input.
  m_descriptionEdit = new QPlainTextEdit(mainWidget);
  m_descriptionEdit->setPlaceholderText(tr("Optional description for the notebook"));
  m_descriptionEdit->setMaximumHeight(100);
  layout->addRow(tr("Description:"), m_descriptionEdit);

  // Root folder input with browse button.
  m_rootFolderInput = new LocationInputWithBrowseButton(mainWidget);
  m_rootFolderInput->setPlaceholderText(tr("Select a folder as notebook root"));
  m_rootFolderInput->setToolTip(
      tr("Root folder of the notebook.\n"
         "A new notebook requires an empty folder or a non-existent path (will be created)."));
  connect(m_rootFolderInput, &LocationInputWithBrowseButton::clicked, this,
          &NewNotebookDialog2::browseRootFolder);
  connect(m_rootFolderInput, &LocationInputWithBrowseButton::textChanged, this,
          &NewNotebookDialog2::handleRootFolderPathChanged);
  layout->addRow(tr("Root Folder:"), m_rootFolderInput);

  // Notebook type combo.
  m_typeCombo = WidgetsFactory::createComboBox(mainWidget);
  m_typeCombo->addItem(tr("Bundled Notebook"), static_cast<int>(NotebookType::Bundled));
  m_typeCombo->addItem(tr("Raw Notebook"), static_cast<int>(NotebookType::Raw));
  m_typeCombo->setToolTip(
      tr("Bundled: Notebook with metadata stored in config files.\n"
         "Raw: Plain folder structure with minimal VNote metadata."));
  layout->addRow(tr("Type:"), m_typeCombo);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Notebook"));
}

void NewNotebookDialog2::handleRootFolderPathChanged() {
  // Auto-fill name from folder name if name is empty.
  if (m_nameEdit->text().isEmpty()) {
    QString rootPath = m_rootFolderInput->text().trimmed();
    if (!rootPath.isEmpty()) {
      m_nameEdit->setText(PathUtils::dirName(rootPath));
    }
  }
}

void NewNotebookDialog2::browseRootFolder() {
  QString startPath = m_rootFolderInput->text().trimmed();
  if (startPath.isEmpty()) {
    // Use last used path from session config, fallback to home.
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    startPath = sessionConfig.getNewNotebookDefaultRootFolderPath();
    if (startPath.isEmpty()) {
      startPath = QDir::homePath();
    }
  }

  QString dir = QFileDialog::getExistingDirectory(
      this, tr("Select Notebook Root Folder"), startPath,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (!dir.isEmpty()) {
    m_rootFolderInput->setText(dir);
  }
}

void NewNotebookDialog2::acceptedButtonClicked() {
  // Collect input from UI.
  NewNotebookInput input;
  input.name = m_nameEdit->text();
  input.description = m_descriptionEdit->toPlainText();
  input.rootFolderPath = m_rootFolderInput->text();
  input.type = static_cast<NotebookType>(m_typeCombo->currentData().toInt());

  // Delegate to controller.
  NewNotebookResult result = m_controller->createNotebook(input);

  if (result.success) {
    // Save the parent directory as the default for next time.
    QFileInfo fi(input.rootFolderPath);
    QString parentDir = fi.absolutePath();
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    sessionConfig.setNewNotebookDefaultRootFolderPath(parentDir);

    m_newNotebookId = result.notebookId;
    accept();
  } else {
    setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
  }
}

QString NewNotebookDialog2::getNewNotebookId() const { return m_newNotebookId; }