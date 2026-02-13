#include "viewtagsdialog.h"

#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>

#include <notebook/node.h>

#include "../tagviewer.h"
#include "../widgetsfactory.h"

using namespace vnotex;

ViewTagsDialog::ViewTagsDialog(Node *p_node, QWidget *p_parent) : Dialog(p_parent) {
  setupUI();

  setNode(p_node);

  m_tagViewer->setFocus();
}

void ViewTagsDialog::setupUI() {
  auto mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  auto mainLayout = WidgetsFactory::createFormLayout(mainWidget);

  m_nodeNameLabel = new QLabel(mainWidget);
  mainLayout->addRow(tr("Name:"), m_nodeNameLabel);

  m_tagViewer = new TagViewer(false, mainWidget);
  mainLayout->addRow(tr("Tags:"), m_tagViewer);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("Tags"));
}

void ViewTagsDialog::acceptedButtonClicked() {
  m_tagViewer->save();
  accept();
}

void ViewTagsDialog::setNode(Node *p_node) {
  if (m_node == p_node) {
    return;
  }

  m_node = p_node;
  Q_ASSERT(m_node);

  m_nodeNameLabel->setText(m_node->getName());
  m_tagViewer->setNode(m_node);
}

void ViewTagsDialog::keyPressEvent(QKeyEvent *p_event) {
  if (p_event->key() == Qt::Key_Enter || p_event->key() == Qt::Key_Return) {
    // Prevent it from closing the dialog.
    return;
  }

  Dialog::keyPressEvent(p_event);
}
