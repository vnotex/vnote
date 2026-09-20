#include "outlinepopup.h"

#include "outlineviewer.h"

using namespace vnotex;

OutlinePopup::OutlinePopup(ServiceLocator &p_services, QToolButton *p_btn, QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent, Alignment::Right), m_services(p_services) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() { m_viewer->setFocus(); });
}

void OutlinePopup::setupUI() {
  m_viewer = new OutlineViewer(m_services, tr("Outline"), this);
  m_viewer->setMinimumSize(320, 384);
  addWidget(m_viewer);
}

void OutlinePopup::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider) {
  m_viewer->setOutlineProvider(p_provider);
}
