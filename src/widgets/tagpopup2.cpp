#include "tagpopup2.h"

#include <QLabel>

#include "tagviewer2.h"

using namespace vnotex;

TagPopup2::TagPopup2(ServiceLocator &p_services, QToolButton *p_btn, QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent, Alignment::Right), m_services(p_services) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() {
    m_viewer->setNodeId(m_nodeId);
    if (m_viewer->isTagSupported()) {
      m_viewer->setVisible(true);
      m_unsupportedLabel->setVisible(false);
    } else {
      m_viewer->setVisible(false);
      m_unsupportedLabel->setVisible(true);
    }
    m_viewer->setFocus();
  });

  connect(this, &QMenu::aboutToHide, this, [this]() { m_viewer->save(); });
}

void TagPopup2::setupUI() {
  m_viewer = new TagViewer2(m_services, this);
  m_viewer->setMinimumSize(320, 384);
  addWidget(m_viewer);

  m_unsupportedLabel = new QLabel(tr("Tags not supported for this notebook type"), this);
  m_unsupportedLabel->setAlignment(Qt::AlignCenter);
  m_unsupportedLabel->setMinimumSize(320, 384);
  m_unsupportedLabel->setVisible(false);
  addWidget(m_unsupportedLabel);
}

void TagPopup2::setNodeId(const NodeIdentifier &p_nodeId) { m_nodeId = p_nodeId; }
