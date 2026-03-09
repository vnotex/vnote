#include "renametagdialog.h"

#include <QFormLayout>

#include <notebook/tagi.h>
#include <utils/widgetutils.h>

#include "../lineeditwithsnippet.h"
#include "../widgetsfactory.h"

using namespace vnotex;

RenameTagDialog::RenameTagDialog(TagI *p_tagI, const QString &p_name, QWidget *p_parent)
    : ScrollDialog(p_parent), m_tagI(p_tagI), m_tagName(p_name) {
  setupUI();

  m_nameLineEdit->setFocus();
  WidgetUtils::selectBaseName(m_nameLineEdit);
}

void RenameTagDialog::setupUI() {
  auto mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  auto mainLayout = WidgetsFactory::createFormLayout(mainWidget);

  m_nameLineEdit = WidgetsFactory::createLineEditWithSnippet(m_tagName, mainWidget);
  mainLayout->addRow(tr("Name:"), m_nameLineEdit);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("Rename Tag"));
}

bool RenameTagDialog::validateInputs() {
  bool valid = true;
  QString msg;

  auto name = getTagName();
  if (name == m_tagName) {
    return true;
  }

  if (!Tag::isValidName(name)) {
    valid = false;
    msg = tr("Please specify a valid name for the tag.");
  } else if (m_tagI->findTag(name)) {
    valid = false;
    msg = tr("Name conflicts with existing tag.");
  }

  setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                : ScrollDialog::InformationLevel::Error);
  return valid;
}

bool RenameTagDialog::renameTag() {
  if (!m_tagI->renameTag(m_tagName, getTagName())) {
    setInformationText(tr("Failed to rename tag (%1) to (%2).").arg(m_tagName, getTagName()),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  return true;
}

void RenameTagDialog::acceptedButtonClicked() {
  if (validateInputs() && renameTag()) {
    accept();
  }
}

QString RenameTagDialog::getTagName() const { return m_nameLineEdit->evaluatedText(); }
