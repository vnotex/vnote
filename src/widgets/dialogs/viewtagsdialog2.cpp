#include "viewtagsdialog2.h"

#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>

#include "../tagviewer2.h"

using namespace vnotex;

static const char *kNodeNameLabelName = "viewTagsNodeNameLabel";
static const char *kTagViewerName = "viewTagsTagViewer";

ViewTagsDialog2::ViewTagsDialog2(ServiceLocator &p_services, const QList<NodeIdentifier> &p_nodeIds,
                                 QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  setupUI();

  if (p_nodeIds.size() == 1) {
    // Extract leaf name from relative path.
    const QString &relativePath = p_nodeIds.first().relativePath;
    m_nodeNameLabel->setText(relativePath.mid(relativePath.lastIndexOf(QLatin1Char('/')) + 1));
  } else {
    m_nodeNameLabel->setText(tr("%n file(s)", "", p_nodeIds.size()));
  }

  m_tagViewer->setNodeIds(p_nodeIds);
  m_tagViewer->setFocus();

  // A target whose current tags could not be read is excluded from the tri-state,
  // so the states shown do not describe it. Say so rather than letting the user
  // act on a picture that silently omits some of their selection.
  const int unreadable = m_tagViewer->unreadableTargetCount();
  if (unreadable > 0) {
    setInformationText(
        tr("%n selected file(s) could not be read and are not reflected below.", "", unreadable),
        InformationLevel::Warning);
  }
}

QSet<QString> ViewTagsDialog2::addedTags() const { return m_tagViewer->addedTags(); }

QSet<QString> ViewTagsDialog2::removedTags() const { return m_tagViewer->removedTags(); }

void ViewTagsDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  m_nodeNameLabel = new QLabel(mainWidget);
  m_nodeNameLabel->setObjectName(QLatin1String(kNodeNameLabelName));
  layout->addRow(tr("Name"), m_nodeNameLabel);

  m_tagViewer = new TagViewer2(m_services, mainWidget);
  m_tagViewer->setObjectName(QLatin1String(kTagViewerName));
  layout->addRow(tr("Tags"), m_tagViewer);

  setCentralWidget(mainWidget);
  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setWindowTitle(tr("Tags"));
}

void ViewTagsDialog2::keyPressEvent(QKeyEvent *p_event) {
  if (p_event->key() == Qt::Key_Enter || p_event->key() == Qt::Key_Return) {
    // Prevent Enter from closing the dialog.
    // TagViewer2 uses Enter for inline tag creation.
    return;
  }

  ScrollDialog::keyPressEvent(p_event);
}
