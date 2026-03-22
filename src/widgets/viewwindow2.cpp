#include "viewwindow2.h"

#include <QApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMessageBox>
#include <QToolBar>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>

#include "editors/statuswidget.h"
#include "findandreplacewidget2.h"
#include "messageboxhelper.h"

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

  // Clear dirty/modified state when auto-save completes for our buffer.
  // Note: BufferService privately inherits QObject (via BufferCoreService),
  // so we use asQObject() + SIGNAL/SLOT string-based connect.
  auto *bufferService = m_services.get<BufferService>();
  if (bufferService) {
    connect(bufferService->asQObject(), SIGNAL(bufferAutoSaved(QString)), this,
            SLOT(onBufferAutoSaved(QString)));
  }
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

// ============ Rename Support ============

void ViewWindow2::onNodeRenamed(const NodeIdentifier &p_newNodeId) {
  m_buffer.setNodeId(p_newNodeId);
  emit nameChanged();
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
  if (!p_force && isModified()) {
    // Show Save/Discard/Cancel dialog for unsaved changes.
    int ret = MessageBoxHelper::questionSaveDiscardCancel(
        MessageBoxHelper::Question,
        tr("Do you want to save changes to \"%1\"?").arg(getName()),
        tr("Your changes will be lost if you don't save them."),
        QString(),
        this);

    if (ret == QMessageBox::Cancel) {
      return false;
    }

    if (ret == QMessageBox::Save) {
      // Attempt to save. On failure, offer Retry/Discard/Cancel (max 3 retries).
      static const int c_maxRetries = 3;
      for (int attempt = 0; attempt < c_maxRetries; ++attempt) {
        if (save()) {
          break;
        }

        // Save failed. Show retry dialog with proper buttons.
        QMessageBox::StandardButtons buttons =
            (attempt < c_maxRetries - 1)
                ? (QMessageBox::Retry | QMessageBox::Discard | QMessageBox::Cancel)
                : (QMessageBox::Discard | QMessageBox::Cancel);
        QMessageBox msgBox(QMessageBox::Warning,
                           tr("Save Changes"),
                           tr("Failed to save \"%1\".").arg(getName()),
                           buttons,
                           this);
        msgBox.setInformativeText(
            (attempt < c_maxRetries - 1)
                ? tr("Would you like to retry, discard changes, or cancel?")
                : tr("Maximum retries reached. Discard changes or cancel?"));
        msgBox.setDefaultButton(
            (attempt < c_maxRetries - 1) ? QMessageBox::Retry : QMessageBox::Cancel);
        int failRet = msgBox.exec();

        if (failRet == QMessageBox::Cancel) {
          return false;
        }
        if (failRet == QMessageBox::Discard) {
          break;
        }
        // QMessageBox::Retry => loop continues.
      }
    }

    // Save path completed (or Discard chosen). Unregister and close.
    if (m_buffer.isValid()) {
      auto *bufferService = m_services.get<BufferService>();
      if (bufferService) {
        bufferService->unregisterActiveWriter(m_buffer.id(),
                                              reinterpret_cast<quintptr>(this));
      }
    }
    return true;
  }

  // Force close or not modified: existing behavior.

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
      bufferService->unregisterActiveWriter(m_buffer.id(),
                                            reinterpret_cast<quintptr>(this));
    }
  }

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

void ViewWindow2::onBufferAutoSaved(const QString &p_bufferId) {
  if (p_bufferId != m_buffer.id()) {
    return;
  }
  m_editorDirty = false;
  m_lastKnownRevision = m_buffer.getRevision();
  setModified(false);
  emit statusChanged();
}

// ============ Find and Replace ============

void ViewWindow2::showFindAndReplaceWidget() {
  if (!m_findAndReplace) {
    m_findAndReplace = new FindAndReplaceWidget2(m_services, this);
    m_mainLayout->addWidget(m_findAndReplace);

    // Connect it to slots.
    connect(m_findAndReplace, &FindAndReplaceWidget2::findTextChanged, this,
            &ViewWindow2::handleFindTextChanged);
    connect(m_findAndReplace, &FindAndReplaceWidget2::findNextRequested, this,
            &ViewWindow2::findNext);
    connect(m_findAndReplace, &FindAndReplaceWidget2::replaceRequested, this,
            &ViewWindow2::replace);
    connect(m_findAndReplace, &FindAndReplaceWidget2::replaceAllRequested, this,
            &ViewWindow2::replaceAll);
    connect(m_findAndReplace, &FindAndReplaceWidget2::closed, this, [this]() {
      setFocus();
      handleFindAndReplaceWidgetClosed();
    });
    connect(m_findAndReplace, &FindAndReplaceWidget2::opened, this,
            &ViewWindow2::handleFindAndReplaceWidgetOpened);
  }

  m_findAndReplace->open(selectedText());
}

void ViewWindow2::hideFindAndReplaceWidget() {
  if (m_findAndReplace) {
    m_findAndReplace->close();
  }
}

bool ViewWindow2::findAndReplaceWidgetVisible() const {
  return m_findAndReplace && m_findAndReplace->isVisible();
}

void ViewWindow2::setFindAndReplaceReplaceEnabled(bool p_enabled) {
  if (m_findAndReplace) {
    m_findAndReplace->setReplaceEnabled(p_enabled);
  }
}

void ViewWindow2::keyPressEvent(QKeyEvent *p_event) {
  switch (p_event->key()) {
  case Qt::Key_Escape:
    if (findAndReplaceWidgetVisible()) {
      hideFindAndReplaceWidget();
      return;
    }
    break;

  default:
    break;
  }
  QFrame::keyPressEvent(p_event);
}

void ViewWindow2::wheelEvent(QWheelEvent *p_event) {
  if (p_event->modifiers() & Qt::ControlModifier) {
    QPoint angle = p_event->angleDelta();
    if (!angle.isNull() && (angle.y() != 0)) {
      zoom(angle.y() > 0);
    }
    p_event->accept();
    return;
  }
  QFrame::wheelEvent(p_event);
}

void ViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
}

void ViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  Q_UNUSED(p_texts);
  Q_UNUSED(p_options);
}

void ViewWindow2::handleReplace(const QString &p_text, FindOptions p_options,
                                const QString &p_replaceText) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
  Q_UNUSED(p_replaceText);
}

void ViewWindow2::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                   const QString &p_replaceText) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
  Q_UNUSED(p_replaceText);
}

void ViewWindow2::handleFindAndReplaceWidgetClosed() {}

void ViewWindow2::handleFindAndReplaceWidgetOpened() {}

void ViewWindow2::findNext(const QString &p_text, FindOptions p_options) {
  const QStringList texts(p_text);

  m_findInfo.m_texts = texts;
  m_findInfo.m_options = p_options;
  handleFindNext(texts, p_options);
}

void ViewWindow2::replace(const QString &p_text, FindOptions p_options,
                           const QString &p_replaceText) {
  m_findInfo.m_texts = QStringList(p_text);
  m_findInfo.m_options = p_options;
  handleReplace(p_text, p_options, p_replaceText);
}

void ViewWindow2::replaceAll(const QString &p_text, FindOptions p_options,
                              const QString &p_replaceText) {
  m_findInfo.m_texts = QStringList(p_text);
  m_findInfo.m_options = p_options;
  handleReplaceAll(p_text, p_options, p_replaceText);
}

void ViewWindow2::findNextOnLastFind(bool p_forward) {
  // Check if need to update the find info.
  if (m_findAndReplace && m_findAndReplace->isVisible()) {
    m_findInfo.m_texts = QStringList(m_findAndReplace->getFindText());
    m_findInfo.m_options = m_findAndReplace->getOptions();
  }

  if (m_findInfo.m_texts.isEmpty()) {
    return;
  }

  if (p_forward) {
    handleFindNext(m_findInfo.m_texts, m_findInfo.m_options & ~FindOption::FindBackward);
  } else {
    handleFindNext(m_findInfo.m_texts, m_findInfo.m_options | FindOption::FindBackward);
  }
}

void ViewWindow2::showFindResult(const QStringList &p_texts, int p_totalMatches,
                                  int p_currentMatchIndex) {
  if (p_texts.isEmpty() || p_texts[0].isEmpty()) {
    showMessage(QString());
    return;
  }

  if (p_totalMatches == 0) {
    showMessage(tr("Pattern not found: %1").arg(p_texts.join(QStringLiteral("; "))));
  } else {
    showMessage(tr("Match found: %1/%2").arg(p_currentMatchIndex + 1).arg(p_totalMatches));
  }
}

void ViewWindow2::showReplaceResult(const QString &p_text, int p_totalReplaces) {
  if (p_totalReplaces == 0) {
    showMessage(tr("Pattern not found: %1").arg(p_text));
  } else {
    showMessage(tr("Replaced %n match(es)", "", p_totalReplaces));
  }
}

QString ViewWindow2::selectedText() const { return QString(); }

void ViewWindow2::showZoomFactor(qreal p_factor) {
  showMessage(tr("Zoomed: %1%").arg(p_factor * 100));
}

void ViewWindow2::showZoomDelta(int p_delta) {
  showMessage(tr("Zoomed: %1%2").arg(p_delta > 0 ? "+" : "").arg(p_delta));
}

void ViewWindow2::handleEditorConfigChange() {
  // Default: no-op. Subclasses override to handle config changes.
}
