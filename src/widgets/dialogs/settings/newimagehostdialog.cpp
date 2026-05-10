#include "newimagehostdialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <controllers/imagehostcontroller.h>
#include <imagehost/iimagehostprovider.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NewImageHostDialog::NewImageHostDialog(ImageHostController *p_controller, QWidget *p_parent)
    : ScrollDialog(p_parent), m_controller(p_controller) {
  setupUI();
}

void NewImageHostDialog::setupUI() {
  auto widget = new QWidget(this);
  setCentralWidget(widget);

  auto mainLayout = WidgetsFactory::createFormLayout(widget);

  {
    m_typeComboBox = WidgetsFactory::createComboBox(widget);
    mainLayout->addRow(tr("Type"), m_typeComboBox);

    if (m_controller) {
      for (const auto &typeId : m_controller->availableTypeIds()) {
        m_typeComboBox->addItem(m_controller->typeDisplayName(typeId), typeId);
      }
    }
  }

  m_nameLineEdit = WidgetsFactory::createLineEdit(widget);
  mainLayout->addRow(tr("Name"), m_nameLineEdit);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Image Host"));
}

void NewImageHostDialog::acceptedButtonClicked() {
  if (validateInputs() && newImageHost()) {
    accept();
  }
}

bool NewImageHostDialog::validateInputs() {
  auto name = m_nameLineEdit->text();
  if (name.isEmpty()) {
    setInformationText(tr("Please specify a valid name for the image host."),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  if (m_controller) {
    auto providers = m_controller->getProviders();
    for (auto *p : providers) {
      if (p->getName() == name) {
        setInformationText(tr("Name conflicts with existing image host."),
                           ScrollDialog::InformationLevel::Error);
        return false;
      }
    }
  }

  return true;
}

bool NewImageHostDialog::newImageHost() {
  if (!m_controller) {
    setInformationText(tr("Image host controller not available."),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  auto typeId = m_typeComboBox->currentData().toString();
  m_newProvider = m_controller->addProvider(typeId, m_nameLineEdit->text());
  if (!m_newProvider) {
    setInformationText(tr("Failed to create image host (%1).").arg(m_nameLineEdit->text()),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  return true;
}

IImageHostProvider *NewImageHostDialog::getNewProvider() const { return m_newProvider; }
