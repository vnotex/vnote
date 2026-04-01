#include "newnotedialog2.h"

#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>

#include <controllers/newnotecontroller.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/configmgr2.h>
#include <core/widgetconfig.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>

#include "../widgetsfactory.h"
#include "../lineeditwithsnippet.h"
#include "notetemplateselector.h"

using namespace vnotex;

QString NewNoteDialog2::s_lastTemplate;

NewNoteDialog2::NewNoteDialog2(ServiceLocator &p_services, const NodeIdentifier &p_parentId,
                               QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_parentId(p_parentId) {
  // Create controller.
  m_controller = new NewNoteController(m_services, this);

  setupUI();

  initDefaultValues();

  m_nameEdit->setFocus();
}

NewNoteDialog2::~NewNoteDialog2() = default;

void NewNoteDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  // File type combo.
  m_fileTypeCombo = WidgetsFactory::createComboBox(mainWidget);
  auto *fileTypeService = m_services.get<FileTypeCoreService>();
  const auto fileTypes = fileTypeService->getAllFileTypes();
  for (const auto &ft : fileTypes) {
    if (ft.m_isNewable) {
      m_fileTypeCombo->addItem(ft.m_displayName, ft.m_typeName);
    }
  }
  connect(m_fileTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            if (!m_fileTypeComboMuted) {
              updateNameForFileType();
            }
          });
  layout->addRow(tr("Type:"), m_fileTypeCombo);

  // Name input.
  auto *snippetService = m_services.get<SnippetCoreService>();
  m_nameEdit = WidgetsFactory::createLineEditWithSnippet(snippetService, mainWidget);
  connect(m_nameEdit, &QLineEdit::textEdited, this,
          [this]() { updateFileTypeForName(); });
  layout->addRow(tr("Name:"), m_nameEdit);

  // Template selector.
  m_templateSelector = new NoteTemplateSelector(m_services, mainWidget);
  layout->addRow(tr("Template:"), m_templateSelector);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Note"));
}

void NewNoteDialog2::initDefaultValues() {
  // Restore last file type from config (stored as type name string).
  QString defaultTypeName =
      m_services.get<ConfigMgr2>()->getWidgetConfig().getNewNoteDefaultFileTypeName();
  int index = m_fileTypeCombo->findData(defaultTypeName);
  if (index >= 0) {
    m_fileTypeComboMuted = true;
    m_fileTypeCombo->setCurrentIndex(index);
    m_fileTypeComboMuted = false;
  }

  // Generate default name based on file type.
  updateNameForFileType();

  // Restore last template.
  if (!s_lastTemplate.isEmpty()) {
    if (!m_templateSelector->setCurrentTemplate(s_lastTemplate)) {
      s_lastTemplate.clear();
    }
  }
}

void NewNoteDialog2::updateNameForFileType() {
  auto *fileTypeService = m_services.get<FileTypeCoreService>();
  const auto fileType = fileTypeService->getFileTypeByName(m_fileTypeCombo->currentData().toString());

  // Get current name and extract base name (without extension).
  QString currentName = m_nameEdit->text().trimmed();
  QString baseName;
  if (!currentName.isEmpty()) {
    int dotPos = currentName.lastIndexOf('.');
    baseName = (dotPos > 0) ? currentName.left(dotPos) : currentName;
  } else {
    baseName = tr("note");
  }

  // Build new name with new suffix.
  QString newName(baseName);
  if (!fileType.preferredSuffix().isEmpty()) {
    newName += "." + fileType.preferredSuffix();
  }
  m_nameEdit->setText(newName);
  WidgetUtils::selectBaseName(m_nameEdit);
}

void NewNoteDialog2::updateFileTypeForName() {
  auto *fileTypeService = m_services.get<FileTypeCoreService>();
  auto inputName = m_nameEdit->text();
  QString typeName;
  int dotIdx = inputName.lastIndexOf(QLatin1Char('.'));
  if (dotIdx != -1) {
    auto suffix = inputName.mid(dotIdx + 1);
    const auto fileType = fileTypeService->getFileTypeBySuffix(suffix);
    typeName = fileType.m_typeName;
  } else {
    typeName = QStringLiteral("Others");
  }

  int idx = m_fileTypeCombo->findData(typeName);
  if (idx != -1) {
    m_fileTypeComboMuted = true;
    m_fileTypeCombo->setCurrentIndex(idx);
    m_fileTypeComboMuted = false;
  }
}

bool NewNoteDialog2::validateInputs() {
  NoteValidationResult result =
      m_controller->validateName(m_parentId.notebookId, m_parentId.relativePath,
                                 m_nameEdit->evaluatedText().trimmed());

  if (!result.valid) {
    setInformationText(result.message, ScrollDialog::InformationLevel::Error);
    return false;
  }

  setInformationText(QString(), ScrollDialog::InformationLevel::Info);
  return true;
}

QString NewNoteDialog2::getFileTypeName() const {
  return m_fileTypeCombo->currentData().toString();
}

QString NewNoteDialog2::getPreferredSuffix() const {
  QString typeName = m_fileTypeCombo->currentData().toString();
  auto *fileTypeService = m_services.get<FileTypeCoreService>();
  const auto fileType = fileTypeService->getFileTypeByName(typeName);
  return fileType.preferredSuffix();
}

void NewNoteDialog2::acceptedButtonClicked() {
  // Save last template.
  s_lastTemplate = m_templateSelector->getCurrentTemplate();

  // Save default file type (as type name string).
  QString fileTypeName = m_fileTypeCombo->currentData().toString();
  m_services.get<ConfigMgr2>()->getWidgetConfig().setNewNoteDefaultFileTypeName(fileTypeName);

  if (!validateInputs()) {
    return;
  }

  // Collect input.
  NewNoteInput input;
  input.notebookId = m_parentId.notebookId;
  input.parentFolderPath = m_parentId.relativePath;
  input.name = m_nameEdit->evaluatedText().trimmed();
  input.templateContent = m_templateSelector->getTemplateContent();
  input.fileTypeName = fileTypeName;

  // Delegate to controller.
  NewNoteResult result = m_controller->createNote(input);

  if (result.success) {
    m_newNodeId = result.nodeId;
    accept();
  } else {
    setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
  }
}

NodeIdentifier NewNoteDialog2::getNewNodeId() const { return m_newNodeId; }
