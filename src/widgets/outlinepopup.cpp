#include "outlinepopup.h"

#include <QToolButton>

#include "outlineviewer.h"
#include <core/global.h>
#include <utils/widgetutils.h>

using namespace vnotex;

OutlinePopup::OutlinePopup(QToolButton *p_btn, QWidget *p_parent) : ButtonPopup(p_btn, p_parent) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() { m_viewer->setFocus(); });
}

void OutlinePopup::setupUI() {
  m_viewer = new OutlineViewer(tr("Outline"), this);
  m_viewer->setMinimumSize(320, 384);
  addWidget(m_viewer);
}

void OutlinePopup::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider) {
  m_viewer->setOutlineProvider(p_provider);
}

void OutlinePopup::showEvent(QShowEvent *p_event) {
  ButtonPopup::showEvent(p_event);

  // Move it to be right-aligned.
  if (m_button->isVisible()) {
    const auto p = pos();
    const auto btnRect = m_button->geometry();
    move(p.x() + btnRect.width() - geometry().width(), p.y());
  }
}
