#include "folderpropertiesdialog.h"

#include <QtWidgets>

#include "../widgetsfactory.h"
#include "exception.h"
#include "nodeinfowidget.h"
#include "notebook/node.h"
#include "notebook/notebook.h"
#include <core/events.h>
#include <core/vnotex.h>
#include <utils/pathutils.h>

using namespace vnotex;

FolderPropertiesDialog::FolderPropertiesDialog(Node *p_node, QWidget *p_parent)
    : ScrollDialog(p_parent), m_node(p_node) {
  Q_ASSERT(m_node);
  setupUI();

  m_infoWidget->getNameLineEdit()->selectAll();
  m_infoWidget->getNameLineEdit()->setFocus();
}

void FolderPropertiesDialog::setupUI() {
  setupNodeInfoWidget(this);
  setCentralWidget(m_infoWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(m_node->getName() + QStringLiteral(" ") + tr("Properties"));
}

void FolderPropertiesDialog::setupNodeInfoWidget(QWidget *p_parent) {
  m_infoWidget = new NodeInfoWidget(m_node, p_parent);
}

bool FolderPropertiesDialog::validateInputs() {
  bool valid = true;
  QString msg;

  valid = valid && validateNameInput(msg);
  setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                : ScrollDialog::InformationLevel::Error);
  return valid;
}

bool FolderPropertiesDialog::validateNameInput(QString &p_msg) {
  p_msg.clear();

  auto name = m_infoWidget->getName();
  if (name.isEmpty()) {
    p_msg = tr("Please specify a name for the folder.");
    return false;
  }

  Q_ASSERT(m_infoWidget->getParentNode() == m_node->getParent());
  if (!m_node->canRename(name)) {
    p_msg = tr("Name conflicts with existing folder.");
    return false;
  }

  return true;
}

void FolderPropertiesDialog::acceptedButtonClicked() {
  if (validateInputs() && saveFolderProperties()) {
    accept();
  }
}

bool FolderPropertiesDialog::saveFolderProperties() {
  try {
    if (m_infoWidget->getName() != m_node->getName()) {
      // Close the node first.
      auto event = QSharedPointer<Event>::create();
      emit VNoteX::getInst().nodeAboutToRename(m_node, event);
      if (!event->m_response.toBool()) {
        return false;
      }

      m_node->updateName(m_infoWidget->getName());
    }
  } catch (Exception &p_e) {
    QString msg = tr("Failed to save folder (%1) in (%2) (%3).")
                      .arg(m_node->getName(), m_node->getNotebook()->getName(), p_e.what());
    qCritical() << msg;
    setInformationText(msg, ScrollDialog::InformationLevel::Error);
    return false;
  }

  return true;
}
