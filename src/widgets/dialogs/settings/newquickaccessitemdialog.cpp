#include "newquickaccessitemdialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>

#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NewQuickAccessItemDialog::NewQuickAccessItemDialog(QWidget *p_parent) : ScrollDialog(p_parent) {
  setupUI();
}

void NewQuickAccessItemDialog::setupUI() {
  auto widget = new QWidget(this);
  setCentralWidget(widget);

  auto mainLayout = WidgetsFactory::createFormLayout(widget);

  {
    m_pathInput = new LocationInputWithBrowseButton(widget);
    m_pathInput->setPlaceholderText(tr("File path"));
    mainLayout->addRow(tr("File:"), m_pathInput);

    connect(m_pathInput, &LocationInputWithBrowseButton::clicked, this, [this]() {
      auto filePath =
          QFileDialog::getOpenFileName(this, tr("Select Quick Access File"), QString());
      if (!filePath.isEmpty()) {
        m_pathInput->setText(filePath);
      }
    });
  }

  {
    m_openModeComboBox = WidgetsFactory::createComboBox(widget);
    m_openModeComboBox->addItem(tr("Default"), static_cast<int>(QuickAccessOpenMode::Default));
    m_openModeComboBox->addItem(tr("Read"), static_cast<int>(QuickAccessOpenMode::Read));
    m_openModeComboBox->addItem(tr("Edit"), static_cast<int>(QuickAccessOpenMode::Edit));
    mainLayout->addRow(tr("Open Mode:"), m_openModeComboBox);
  }

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Quick Access Item"));
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
  item.m_openMode =
      static_cast<QuickAccessOpenMode>(m_openModeComboBox->currentData().toInt());
  return item;
}
