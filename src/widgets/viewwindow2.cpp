#include "viewwindow2.h"

#include <QApplication>
#include <QDebug>
#include <QVBoxLayout>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>

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

// ============ Auto-Save Integration ============

void ViewWindow2::onEditorContentsChanged() {
  // Guard: invalid buffers should not be marked dirty.
  if (!m_buffer.isValid()) {
    return;
  }

  m_editorDirty = true;

  auto *bufferService = m_services.get<BufferService>();
  if (bufferService) {
    bufferService->markDirty(m_buffer.id());
  }
}

void ViewWindow2::onFocusGained() {
  if (!m_buffer.isValid()) {
    return;
  }

  // Register as active writer for this buffer.
  // Capture `this` in a lambda for the content fetch callback.
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
    m_editorDirty = false; // Content just loaded fresh.
  }

  emit focused(this);
}

void ViewWindow2::onFocusLost() {
  if (!m_buffer.isValid()) {
    return;
  }

  // Immediately sync dirty content to buffer on focus loss.
  // This ensures consistency when switching ViewWindows or leaving VNote.
  if (m_editorDirty) {
    auto *bufferService = m_services.get<BufferService>();
    if (bufferService) {
      bufferService->syncNow(m_buffer.id());
      m_lastKnownRevision = m_buffer.getRevision();
      m_editorDirty = false;
    }
  }
}
