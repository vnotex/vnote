#include "newquickaccessitemdialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NewQuickAccessItemDialog::NewQuickAccessItemDialog(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  setupUI();
}

void NewQuickAccessItemDialog::setupUI() {
  auto widget = new QWidget(this);
  setCentralWidget(widget);

  auto mainLayout = WidgetsFactory::createFormLayout(widget);

  {
    m_pathInput = new LocationInputWithBrowseButton(widget);
    m_pathInput->setPlaceholderText(tr("File path"));
    m_pathInput->setBrowseType(LocationInputWithBrowseButton::File, tr("Select Quick Access File"));
    mainLayout->addRow(tr("File:"), m_pathInput);
  }

  {
    m_openModeComboBox = WidgetsFactory::createComboBox(widget);
    m_openModeComboBox->addItem(tr("Default"), static_cast<int>(QuickAccessOpenMode::Default));
    m_openModeComboBox->addItem(tr("Read"), static_cast<int>(QuickAccessOpenMode::Read));
    m_openModeComboBox->addItem(tr("Edit"), static_cast<int>(QuickAccessOpenMode::Edit));
    mainLayout->addRow(tr("Open Mode:"), m_openModeComboBox);
  }

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Quick Access"));
}

void NewQuickAccessItemDialog::acceptedButtonClicked() {
  if (validateInputs()) {
    accept();
  }
}

bool NewQuickAccessItemDialog::validateInputs() {
  auto path = m_pathInput->text().trimmed();
  if (path.isEmpty()) {
    setInformationText(tr("Please specify a file path."), ScrollDialog::InformationLevel::Error);
    return false;
  }
  return true;
}

SessionConfig::QuickAccessItem NewQuickAccessItemDialog::getItem() const {
  SessionConfig::QuickAccessItem item;
  item.m_path = m_pathInput->text().trimmed();
  item.m_openMode = static_cast<QuickAccessOpenMode>(m_openModeComboBox->currentData().toInt());

  // Attempt to resolve UUID for files that belong to an open notebook.
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (notebookSvc) {
    auto resolved = notebookSvc->resolvePathToNotebook(item.m_path);
    if (!resolved.isEmpty()) {
      auto notebookId = resolved[QStringLiteral("notebookId")].toString();
      auto relativePath = resolved[QStringLiteral("relativePath")].toString();
      auto fileInfo = notebookSvc->getFileInfo(notebookId, relativePath);
      if (!fileInfo.isEmpty() && fileInfo.contains(QStringLiteral("id"))) {
        item.m_uuid = fileInfo[QStringLiteral("id")].toString();
      }
    }
  }

  return item;
}
