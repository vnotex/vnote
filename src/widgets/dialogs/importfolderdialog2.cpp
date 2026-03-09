#include "importfolderdialog2.h"

#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <controllers/importfoldercontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookservice.h>
#include <utils/pathutils.h>

#include "folderfilesfilterwidget.h"

using namespace vnotex;
using vnotex::core::NotebookService;

ImportFolderDialog2::ImportFolderDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                                         QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_parentId(p_parentId) {
  // Create controller.
  m_controller = new ImportFolderController(m_services, this);

  setupUI();

  m_filterWidget->getFolderPathEdit()->setFocus();
}

ImportFolderDialog2::~ImportFolderDialog2() = default;

void ImportFolderDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QVBoxLayout(mainWidget);

  // Show parent folder path for context.
  auto *notebookService = m_services.get<NotebookService>();
  QString parentPath;
  if (notebookService) {
    QJsonObject notebookConfig = notebookService->getNotebookConfig(m_parentId.notebookId);
    QString rootPath = notebookConfig.value("rootFolder").toString();
    parentPath = PathUtils::concatenateFilePath(rootPath, m_parentId.relativePath);
  }

  auto *label = new QLabel(tr("Import folder into (%1).").arg(parentPath), mainWidget);
  label->setWordWrap(true);
  layout->addWidget(label);

  // Folder and file filter widget.
  m_filterWidget = new FolderFilesFilterWidget(mainWidget);
  layout->addWidget(m_filterWidget);
  connect(m_filterWidget, &FolderFilesFilterWidget::filesChanged, this,
          &ImportFolderDialog2::validateInputs);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setButtonEnabled(QDialogButtonBox::Ok, false);

  setWindowTitle(tr("Import Folder"));
}

void ImportFolderDialog2::validateInputs() {
  bool valid = true;
  QString msg;

  if (!m_filterWidget->isReady()) {
    // Still scanning - disable OK button but don't show error.
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  auto folderPath = m_filterWidget->getFolderPath();

  // Validate using controller.
  auto result = m_controller->validateSourceFolder(m_parentId.notebookId, m_parentId.relativePath,
                                                   folderPath);
  if (!result.valid) {
    msg = result.message;
    valid = false;
  }

  setInformationText(msg,
                     valid ? ScrollDialog::InformationLevel::Info : ScrollDialog::InformationLevel::Error);
  setButtonEnabled(QDialogButtonBox::Ok, valid);
}

void ImportFolderDialog2::acceptedButtonClicked() {
  if (isCompleted()) {
    accept();
    return;
  }

  // Collect input.
  ImportFolderInput input;
  input.notebookId = m_parentId.notebookId;
  input.parentFolderPath = m_parentId.relativePath;
  input.sourceFolderPath = m_filterWidget->getFolderPath();
  input.suffixes = m_filterWidget->getSuffixes();

  // Delegate to controller.
  ImportFolderResult result = m_controller->importFolder(input);

  if (result.success) {
    m_newNodeId = result.nodeId;

    // Show warning if any, but still complete.
    if (!result.warningMessage.isEmpty()) {
      setInformationText(result.warningMessage, ScrollDialog::InformationLevel::Warning);
      completeButStay();
      return;
    }

    accept();
  } else {
    setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
  }
}

NodeIdentifier ImportFolderDialog2::getNewNodeId() const { return m_newNodeId; }
