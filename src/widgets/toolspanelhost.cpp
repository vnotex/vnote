#include "toolspanelhost.h"

#include <QApplication>
#include <QVBoxLayout>

using namespace vnotex;

ToolsPanelHost::ToolsPanelHost(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);
}

ToolsPanelHost::~ToolsPanelHost() { releasePanel(); }

QWidget *ToolsPanelHost::getPanel() const { return m_panel.data(); }

void ToolsPanelHost::clearBinding() {
  disconnect(m_ownerDestroyedConn);
  disconnect(m_panelDestroyedConn);
  m_owner.clear();
  m_panel.clear();
  setFocusProxy(nullptr);
}

void ToolsPanelHost::releasePanel() {
  QPointer<QWidget> owner = m_owner;
  QPointer<QWidget> panel = m_panel;
  // Hide/reparent can dispatch events. No callback may see the outgoing binding.
  clearBinding();
  if (!panel) {
    return;
  }
  m_layout->removeWidget(panel);
  panel->hide();
  if (panel) {
    if (owner) {
      panel->setParent(owner);
    } else {
      delete panel.data();
    }
  }
}

void ToolsPanelHost::setPanel(QWidget *p_owner, QWidget *p_panel) {
  QPointer<QWidget> owner = p_owner;
  QPointer<QWidget> panel = p_panel;
  if (!owner || !panel) {
    owner.clear();
    panel.clear();
  } else if (owner == panel || panel->isAncestorOf(this)) {
    Q_ASSERT_X(false, "ToolsPanelHost::setPanel", "Invalid tools panel ownership");
    owner.clear();
    panel.clear();
  }
  if (owner == m_owner && panel == m_panel) {
    return;
  }

  QPointer<QWidget> focus = QApplication::focusWidget();
  if (focus && m_panel && (focus == m_panel || m_panel->isAncestorOf(focus))) {
    focus.clear();
  }
  QPointer<QWidget> focusWindow = focus ? focus->window() : nullptr;
  releasePanel();

  if (owner && panel) {
    m_owner = owner;
    m_panel = panel;
    m_panelDestroyedConn = connect(panel, &QObject::destroyed, this, [this]() {
      // Qt removes the dying widget from its layout; never touch it here.
      clearBinding();
      emit panelChanged();
    });
    m_ownerDestroyedConn = connect(owner, &QObject::destroyed, this, [this]() {
      QPointer<QWidget> panel = m_panel;
      clearBinding();
      if (panel) {
        m_layout->removeWidget(panel);
        delete panel.data();
      }
      emit panelChanged();
    });
    panel->setParent(this);
    if (m_panel) {
      m_layout->addWidget(m_panel);
      setFocusProxy(m_panel);
      m_panel->show();
    }
  }

  if (focus && focusWindow && focus->isVisible() && focus->isEnabled() &&
      focus->window() == focusWindow && QApplication::activeWindow() == focusWindow &&
      QApplication::focusWidget() != focus) {
    focus->setFocus(Qt::OtherFocusReason);
  }
  emit panelChanged();
}
