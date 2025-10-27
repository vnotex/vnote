#include "newimagehostdialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <imagehost/imagehostmgr.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NewImageHostDialog::NewImageHostDialog(QWidget *p_parent) : ScrollDialog(p_parent) { setupUI(); }

void NewImageHostDialog::setupUI() {
  auto widget = new QWidget(this);
  setCentralWidget(widget);

  auto mainLayout = WidgetsFactory::createFormLayout(widget);

  {
    m_typeComboBox = WidgetsFactory::createComboBox(widget);
    mainLayout->addRow(tr("Type:"), m_typeComboBox);

    for (int type = static_cast<int>(ImageHost::GitHub);
         type < static_cast<int>(ImageHost::MaxHost); ++type) {
      m_typeComboBox->addItem(ImageHost::typeString(static_cast<ImageHost::Type>(type)), type);
    }
  }

  m_nameLineEdit = WidgetsFactory::createLineEdit(widget);
  mainLayout->addRow(tr("Name:"), m_nameLineEdit);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Image Host"));
}

void NewImageHostDialog::acceptedButtonClicked() {
  if (validateInputs() && newImageHost()) {
    accept();
  }
}

bool NewImageHostDialog::validateInputs() {
  bool valid = true;
  QString msg;

  auto name = m_nameLineEdit->text();
  if (name.isEmpty()) {
    msg = tr("Please specify a valid name for the image host.");
    valid = false;
  } else if (ImageHostMgr::getInst().find(name)) {
    msg = tr("Name conflicts with existing image host.");
    valid = false;
  }

  if (!valid) {
    setInformationText(msg, ScrollDialog::InformationLevel::Error);
    return false;
  }

  return true;
}

bool NewImageHostDialog::newImageHost() {
  m_imageHost = ImageHostMgr::getInst().newImageHost(
      static_cast<ImageHost::Type>(m_typeComboBox->currentData().toInt()), m_nameLineEdit->text());
  if (!m_imageHost) {
    setInformationText(tr("Failed to create image host (%1).").arg(m_nameLineEdit->text()),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  return true;
}

ImageHost *NewImageHostDialog::getNewImageHost() const { return m_imageHost; }
