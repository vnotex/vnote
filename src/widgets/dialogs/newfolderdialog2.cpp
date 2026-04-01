#include "newfolderdialog2.h"

#include <QFormLayout>
#include <QVBoxLayout>

#include <controllers/newfoldercontroller.h>
#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>

#include "../lineeditwithsnippet.h"
#include "../widgetsfactory.h"

using namespace vnotex;

NewFolderDialog2::NewFolderDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                                   QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_parentId(p_parentId) {
  // Create controller.
  m_controller = new NewFolderController(m_services, this);

  setupUI();

  m_nameEdit->setFocus();
}

NewFolderDialog2::~NewFolderDialog2() = default;

void NewFolderDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  // Name input.
  auto *snippetService = m_services.get<SnippetCoreService>();
  m_nameEdit = WidgetsFactory::createLineEditWithSnippet(snippetService, mainWidget);
  layout->addRow(tr("Name:"), m_nameEdit);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Folder"));
}

bool NewFolderDialog2::validateInputs() {
  FolderValidationResult result =
      m_controller->validateName(m_parentId.notebookId, m_parentId.relativePath,
                                 m_nameEdit->evaluatedText().trimmed());

  if (!result.valid) {
    setInformationText(result.message, ScrollDialog::InformationLevel::Error);
    return false;
  }

  setInformationText(QString(), ScrollDialog::InformationLevel::Info);
  return true;
}

void NewFolderDialog2::acceptedButtonClicked() {
  if (!validateInputs()) {
    return;
  }

  // Collect input.
  NewFolderInput input;
  input.notebookId = m_parentId.notebookId;
  input.parentFolderPath = m_parentId.relativePath;
  input.name = m_nameEdit->evaluatedText().trimmed();

  // Delegate to controller.
  NewFolderResult result = m_controller->createFolder(input);

  if (result.success) {
    m_newNodeId = result.nodeId;
    accept();
  } else {
    setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
  }
}

NodeIdentifier NewFolderDialog2::getNewNodeId() const { return m_newNodeId; }
