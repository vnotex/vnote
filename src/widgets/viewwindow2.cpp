#include "viewwindow2.h"

#include <QApplication>
#include <QDebug>
#include <QToolBar>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>

#include "editors/statuswidget.h"

using namespace vnotex;

ViewWindow2::ViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                         QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services), m_buffer(p_buffer) {
  setupUI();

  // Initialize revision from buffer.
  m_lastKnownRevision = m_buffer.getRevision();

  // Track focus changes for auto-save integration.
  // Uses the same global pattern as the legacy ViewWindow.
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget *p_old, QWidget *p_now) {
    bool hasFocusNow = p_now && (p_now == this || isAncestorOf(p_now));
    bool hadFocusBefore = p_old && (p_old == this || isAncestorOf(p_old));
    if (hasFocusNow && !hadFocusBefore) {
      onFocusGained();
    } else if (!hasFocusNow && hadFocusBefore) {
      onFocusLost();
    }
  });
}

ViewWindow2::~ViewWindow2() {
  if (m_statusWidget) {
    m_statusWidget->setParent(nullptr);
  }
}

void ViewWindow2::setupUI() {
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);

  m_topLayout = new QVBoxLayout();
  m_topLayout->setContentsMargins(0, 0, 0, 0);
  m_topLayout->setSpacing(0);
  m_mainLayout->addLayout(m_topLayout, 0);

  // Central widget will be inserted at index 1 (between top and bottom).

  m_bottomLayout = new QVBoxLayout();
  m_bottomLayout->setContentsMargins(0, 0, 0, 0);
  m_bottomLayout->setSpacing(0);
  m_mainLayout->addLayout(m_bottomLayout, 0);
}

// ============ Buffer Access ============

const Buffer2 &ViewWindow2::getBuffer() const {
  return m_buffer;
}

Buffer2 &ViewWindow2::getBuffer() {
  return m_buffer;
}

const NodeIdentifier &ViewWindow2::getNodeId() const {
  return m_buffer.nodeId();
}

// ============ Window Identity ============

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
    name.append(QStringLiteral(" \u25CF"));
  }
  return name;
}

// ============ Mode ============

ViewWindowMode ViewWindow2::getMode() const {
  return m_mode;
}

bool ViewWindow2::isModified() const {
  // Use local dirty flag OR vxcore modified flag.
  // m_editorDirty is set immediately on keystroke;
  // m_buffer.isModified() is set after the auto-save timer syncs content to vxcore.
  return m_editorDirty || m_buffer.isModified();
}

// ============ Lifecycle ============

bool ViewWindow2::aboutToClose(bool p_force) {
  Q_UNUSED(p_force);

  // Sync pending changes before close.
  if (m_editorDirty && m_buffer.isValid()) {
    auto *bufferService = m_services.get<BufferService>();
    if (bufferService) {
      bufferService->syncNow(m_buffer.id());
      m_editorDirty = false;
    }
  }

  // Unregister as active writer to prevent dangling pointer in BufferService.
  if (m_buffer.isValid()) {
    auto *bufferService = m_services.get<BufferService>();
    if (bufferService) {
      bufferService->unregisterActiveWriter(m_buffer.id(), reinterpret_cast<quintptr>(this));
    }
  }

  // TODO(save-prompts): Implement unsaved-changes dialog here.
  return true;
}

// ============ Layout Slots ============

void ViewWindow2::addToolBar(QToolBar *p_bar) {
  Q_ASSERT(!m_toolBar);
  m_toolBar = p_bar;
  m_topLayout->addWidget(p_bar);
}

void ViewWindow2::addTopWidget(QWidget *p_widget) {
  m_topLayout->addWidget(p_widget);
}

void ViewWindow2::setCentralWidget(QWidget *p_widget) {
  if (m_centralWidget) {
    m_mainLayout->removeWidget(m_centralWidget);
    m_centralWidget->deleteLater();
  }
  m_centralWidget = p_widget;
  if (m_centralWidget) {
    // Insert after top layout (index 1), with stretch=1.
    m_mainLayout->insertWidget(1, m_centralWidget, 1);
    setFocusProxy(m_centralWidget);
    m_centralWidget->show();
  }
}

void ViewWindow2::addBottomWidget(QWidget *p_widget) {
  m_bottomLayout->addWidget(p_widget);
}

void ViewWindow2::setStatusWidget(const QSharedPointer<StatusWidget> &p_widget) {
  m_statusWidget = p_widget;
  m_bottomLayout->addWidget(p_widget.data());
  p_widget->show();
}

void ViewWindow2::showMessage(const QString &p_msg) {
  if (m_statusWidget) {
    m_statusWidget->showMessage(p_msg);
  }
}

QToolBar *ViewWindow2::createToolBar(QWidget *p_parent) {
  auto *toolBar = new QToolBar(p_parent);

  // Use configured icon size if available.
  int iconSize = 14;
  toolBar->setIconSize(QSize(iconSize, iconSize));

  return toolBar;
}

ServiceLocator &ViewWindow2::getServices() const {
  return m_services;
}

// ============ Auto-Save Integration ============

void ViewWindow2::onEditorContentsChanged() {
  if (!m_buffer.isValid()) {
    return;
  }

  m_editorDirty = true;

  auto *bufferService = m_services.get<BufferService>();
  if (bufferService) {
    bufferService->markDirty(m_buffer.id());
  }

  emit statusChanged();
}

bool ViewWindow2::save() {
  if (!m_buffer.isValid()) {
    return false;
  }

  // Sync editor content to vxcore buffer first.
  auto *bufferService = m_services.get<BufferService>();
  if (bufferService) {
    bufferService->syncNow(m_buffer.id());
  }

  // Save to disk via Buffer2 (fires hooks).
  bool ok = m_buffer.save();

  if (ok) {
    m_editorDirty = false;
    m_lastKnownRevision = m_buffer.getRevision();
    setModified(false);
    emit statusChanged();
  } else {
    showMessage(tr("Failed to save note (%1).").arg(getName()));
  }

  return ok;
}

void ViewWindow2::onFocusGained() {
  if (!m_buffer.isValid()) {
    return;
  }

  // Register as active writer for this buffer.
  auto *bufferService = m_services.get<BufferService>();
  if (bufferService) {
    bufferService->registerActiveWriter(
        m_buffer.id(), reinterpret_cast<quintptr>(this),
        [this]() { return getLatestContent(); });
  }

  // Check for external changes (from other ViewWindows or disk reload).
  int currentRev = m_buffer.getRevision();
  if (currentRev != m_lastKnownRevision) {
    syncEditorFromBuffer();
    m_lastKnownRevision = currentRev;
    m_editorDirty = false;
  }

  emit focused(this);
}

void ViewWindow2::onFocusLost() {
  if (!m_buffer.isValid()) {
    return;
  }

  // Immediately sync dirty content to buffer on focus loss.
  if (m_editorDirty) {
    auto *bufferService = m_services.get<BufferService>();
    if (bufferService) {
      bufferService->syncNow(m_buffer.id());
      m_lastKnownRevision = m_buffer.getRevision();
      m_editorDirty = false;
    }
  }
}
