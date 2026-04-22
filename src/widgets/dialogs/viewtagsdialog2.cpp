#include "viewtagsdialog2.h"

#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>

#include "../tagviewer2.h"

using namespace vnotex;

ViewTagsDialog2::ViewTagsDialog2(ServiceLocator &p_services, const NodeIdentifier &p_nodeId,
                                 QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  setupUI();

  // Extract leaf name from relative path.
  const QString name =
      p_nodeId.relativePath.mid(p_nodeId.relativePath.lastIndexOf(QLatin1Char('/')) + 1);
  m_nodeNameLabel->setText(name);

  m_tagViewer->setNodeId(p_nodeId);
  m_tagViewer->setFocus();
}

void ViewTagsDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  m_nodeNameLabel = new QLabel(mainWidget);
  layout->addRow(tr("Name:"), m_nodeNameLabel);

  m_tagViewer = new TagViewer2(m_services, mainWidget);
  layout->addRow(tr("Tags:"), m_tagViewer);

  setCentralWidget(mainWidget);
  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setWindowTitle(tr("Tags"));
}

void ViewTagsDialog2::acceptedButtonClicked() {
  m_tagViewer->save();
  accept();
}

void ViewTagsDialog2::keyPressEvent(QKeyEvent *p_event) {
  if (p_event->key() == Qt::Key_Enter || p_event->key() == Qt::Key_Return) {
    // Prevent Enter from closing the dialog.
    // TagViewer2 uses Enter for inline tag creation.
    return;
  }

  ScrollDialog::keyPressEvent(p_event);
}
