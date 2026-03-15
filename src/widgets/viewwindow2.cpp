#include "viewwindow2.h"

#include <QVBoxLayout>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>

using namespace vnotex;

ViewWindow2::ViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                         QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services), m_buffer(p_buffer) {
  setupUI();
}

ViewWindow2::~ViewWindow2() {
}

void ViewWindow2::setupUI() {
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);
}

const Buffer2 &ViewWindow2::getBuffer() const {
  return m_buffer;
}

Buffer2 &ViewWindow2::getBuffer() {
  return m_buffer;
}

const NodeIdentifier &ViewWindow2::getNodeId() const {
  return m_buffer.nodeId();
}

QIcon ViewWindow2::getIcon() const {
  return QIcon();
}

QString ViewWindow2::getName() const {
  // Extract filename from relative path.
  const QString path = m_buffer.nodeId().relativePath;
  int lastSlash = path.lastIndexOf(QLatin1Char('/'));
  if (lastSlash >= 0) {
    return path.mid(lastSlash + 1);
  }
  return path;
}

QString ViewWindow2::getTitle() const {
  QString name = getName();
  if (isModified()) {
    name.prepend(QLatin1Char('*'));
  }
  return name;
}

ViewWindowMode ViewWindow2::getMode() const {
  return m_mode;
}

bool ViewWindow2::isModified() const {
  return m_buffer.isModified();
}

bool ViewWindow2::aboutToClose(bool p_force) {
  Q_UNUSED(p_force);
  // TODO(save-prompts): Implement unsaved-changes dialog here.
  // When implemented:
  //   - p_force=true: skip dialog, discard changes
  //   - p_force=false: show save/discard/cancel dialog
  //   - Return false if user cancels (caller must abort the operation)
  return true;
}

void ViewWindow2::setCentralWidget(QWidget *p_widget) {
  if (m_centralWidget) {
    m_mainLayout->removeWidget(m_centralWidget);
    m_centralWidget->deleteLater();
  }
  m_centralWidget = p_widget;
  if (m_centralWidget) {
    m_mainLayout->addWidget(m_centralWidget);
  }
}

ServiceLocator &ViewWindow2::getServices() const {
  return m_services;
}
