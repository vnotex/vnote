#include "newnotebookdialog2.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <core/sessionconfig.h>
#include <utils/pathutils.h>

#include "../lineeditwithsnippet.h"
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
  auto *snippetService = m_services.get<SnippetCoreService>();
  m_nameEdit = WidgetsFactory::createLineEditWithSnippet(snippetService, mainWidget);
  layout->addRow(tr("Name:"), m_nameEdit);

  // Description input.
  m_descriptionEdit = new QPlainTextEdit(mainWidget);
  m_descriptionEdit->setPlaceholderText(tr("Optional description for the notebook"));
  m_descriptionEdit->setMaximumHeight(100);
  layout->addRow(tr("Description:"), m_descriptionEdit);

  // Root folder input with browse button.
  // Get default path from session config.
  QString defaultRootPath;
  {
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    defaultRootPath = sessionConfig.getNewNotebookDefaultRootFolderPath();
  }
  m_rootFolderInput = new LocationInputWithBrowseButton(mainWidget, defaultRootPath);
  m_rootFolderInput->setBrowseType(
      LocationInputWithBrowseButton::Folder,
      tr("Select Notebook Root Folder"));
  m_rootFolderInput->setPlaceholderText(tr("Select a folder as notebook root"));
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

  // Update root folder tooltip based on selected notebook type.
  auto updateRootFolderTooltip = [this]() {
    NotebookType type = static_cast<NotebookType>(m_typeCombo->currentData().toInt());
    if (type == NotebookType::Raw) {
      m_rootFolderInput->setToolTip(
          tr("Root folder of the notebook.\n"
             "For raw notebooks, you can select an existing folder with files.\n"
             "The folder's contents will be indexed as notebook nodes."));
    } else {
      m_rootFolderInput->setToolTip(
          tr("Root folder of the notebook.\n"
             "A new notebook requires an empty folder or a non-existent path (will be created)."));
    }
  };

  connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, updateRootFolderTooltip);

  // Initialize tooltip for default type.
  updateRootFolderTooltip();

  // Advanced section toggle.
  m_advancedToggle = new QToolButton(mainWidget);
  m_advancedToggle->setText(tr("Advanced"));
  m_advancedToggle->setCheckable(true);
  m_advancedToggle->setArrowType(Qt::RightArrow);
  m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  layout->addRow(m_advancedToggle);

  // Advanced section content.
  m_advancedSection = new QWidget(mainWidget);
  auto *advancedLayout = new QFormLayout(m_advancedSection);

  m_assetsFolderEdit = new QLineEdit(m_advancedSection);
  m_assetsFolderEdit->setText(QStringLiteral("vx_assets"));
  m_assetsFolderEdit->setToolTip(
      tr("Name or path for the assets folder.\n"
         "Can be a folder name (vx_assets), relative path, or absolute path.\n"
         "Relative paths resolve against each note file's parent directory."));
  advancedLayout->addRow(tr("Assets Folder:"), m_assetsFolderEdit);

  m_advancedSection->setVisible(false);
  layout->addRow(m_advancedSection);

  connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool p_checked) {
    m_advancedSection->setVisible(p_checked);
    m_advancedToggle->setArrowType(p_checked ? Qt::DownArrow : Qt::RightArrow);
  });

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

void NewNotebookDialog2::acceptedButtonClicked() {
  // Collect input from UI.
  NewNotebookInput input;
  input.name = m_nameEdit->evaluatedText();
  input.description = m_descriptionEdit->toPlainText();
  input.rootFolderPath = m_rootFolderInput->text();
  input.type = static_cast<NotebookType>(m_typeCombo->currentData().toInt());
  input.assetsFolder = m_assetsFolderEdit->text();

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
