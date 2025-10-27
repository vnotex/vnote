#include "newfolderdialog.h"

#include <QLineEdit>

#include <core/exception.h>
#include <notebook/node.h>
#include <notebook/notebook.h>
#include <utils/pathutils.h>

#include "../widgetsfactory.h"
#include "nodeinfowidget.h"

using namespace vnotex;

NewFolderDialog::NewFolderDialog(Node *p_node, QWidget *p_parent) : ScrollDialog(p_parent) {
  Q_ASSERT(p_node && p_node->isLoaded());
  setupUI(p_node);

  m_infoWidget->getNameLineEdit()->setFocus();
}

void NewFolderDialog::setupUI(const Node *p_node) {
  setupNodeInfoWidget(p_node, this);
  setCentralWidget(m_infoWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Folder"));
}

void NewFolderDialog::setupNodeInfoWidget(const Node *p_node, QWidget *p_parent) {
  m_infoWidget = new NodeInfoWidget(p_node, Node::Flag::Container, p_parent);
}

bool NewFolderDialog::validateInputs() {
  bool valid = true;
  QString msg;

  valid = valid && validateNameInput(msg);
  setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                : ScrollDialog::InformationLevel::Error);
  return valid;
}

bool NewFolderDialog::validateNameInput(QString &p_msg) {
  p_msg.clear();

  auto name = m_infoWidget->getName();
  if (name.isEmpty() || !PathUtils::isLegalFileName(name)) {
    p_msg = tr("Please specify a name for the folder.");
    return false;
  }

  if (!m_infoWidget->getParentNode()->isLegalNameForNewChild(name)) {
    p_msg = tr("Name conflicts with existing or built-in folder.");
    return false;
  }

  return true;
}

void NewFolderDialog::acceptedButtonClicked() {
  if (validateInputs() && newFolder()) {
    accept();
  }
}

bool NewFolderDialog::newFolder() {
  m_newNode.clear();

  Notebook *notebook = const_cast<Notebook *>(m_infoWidget->getNotebook());
  Node *parentNode = const_cast<Node *>(m_infoWidget->getParentNode());
  try {
    m_newNode = notebook->newNode(parentNode, Node::Flag::Container, m_infoWidget->getName());
  } catch (Exception &p_e) {
    QString msg = tr("Failed to create folder under (%1) in (%2) (%3).")
                      .arg(parentNode->getName(), notebook->getName(), p_e.what());
    qCritical() << msg;
    setInformationText(msg, ScrollDialog::InformationLevel::Error);
    return false;
  }

  emit notebook->nodeUpdated(parentNode);
  return true;
}

const QSharedPointer<Node> &NewFolderDialog::getNewNode() const { return m_newNode; }
