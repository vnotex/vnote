#include "viewareacontroller.h"

#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <QWidget>

#include <controllers/workspacewrapper.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/fileopensettings.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/iviewwindowcontent.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/workspacecoreservice.h>
#include <widgets/viewwindow2.h>

using namespace vnotex;

ViewAreaController::ViewAreaController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

ViewAreaController::~ViewAreaController() {
  // Clean up workspace wrappers. Hidden workspaces may still cache ViewWindows
  // (unparented QWidgets). Delete those before destroying the wrapper.
  for (auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it) {
    auto *wrapper = it.value();
    if (!wrapper->isVisible()) {
      auto windows = wrapper->takeAllViewWindows();
      for (auto *obj : windows) {
        auto *widget = qobject_cast<QWidget *>(obj);
        delete widget;
      }
    }
    delete wrapper;
  }
  m_workspaces.clear();
}

void ViewAreaController::setView(ViewAreaView *p_view) { m_view = p_view; }

void ViewAreaController::openBuffer(const Buffer2 &p_buffer, const FileOpenSettings &p_settings) {
  QString fileType;
  auto *ftSvc = m_services.get<FileTypeCoreService>();
  if (ftSvc) {
    FileType ft = ftSvc->getFileType(p_buffer.nodeId().relativePath);
    fileType = ft.m_typeName;
  }
  if (fileType.isEmpty()) {
    fileType = QStringLiteral("Text");
  }

  ViewWindowOpenEvent event;
  event.fileType = fileType;
  event.bufferId = p_buffer.id();
  event.nodeId = p_buffer.nodeId().relativePath;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc && wsSvc->fireViewWindowBeforeOpen(event)) {
    return;
  }
  if (m_currentWorkspaceId.isEmpty()) {
    QString wsId;
    if (wsSvc) {
      wsId = wsSvc->createWorkspace(generateWorkspaceName());
    }
    if (wsId.isEmpty()) {
      qWarning() << "ViewAreaController::openBuffer: failed to create workspace";
      return;
    }
    auto *wrapper = new WorkspaceWrapper(wsId, this);
    wrapper->setVisible(true);
    m_workspaces.insert(wsId, wrapper);
    if (!m_view) {
      return;
    }
    m_view->addFirstViewSplit(wsId);
  }

  if (m_currentWorkspaceId.isEmpty()) {
    qWarning() << "ViewAreaController: no current workspace";
    return;
  }
  if (!m_view) {
    return;
  }

  // Check if the buffer is already open in the current workspace.
  // If so, just activate the existing tab instead of creating a duplicate.
  ID existingWindowId = m_view->findWindowIdByBufferId(m_currentWorkspaceId, p_buffer.id());
  if (existingWindowId != InvalidViewWindowId) {
    qDebug() << "ViewAreaController::openBuffer: buffer already open in workspace,"
             << "activating existing window:" << existingWindowId;
    m_view->setCurrentBuffer(m_currentWorkspaceId, p_buffer.id(), true);
    setCurrentViewWindow(existingWindowId, p_buffer.id());
    // Apply file open settings to the existing window (scroll + highlight).
    m_view->applyFileOpenSettings(existingWindowId, p_settings);
    return;
  }

  m_view->openBuffer(p_buffer, fileType, m_currentWorkspaceId, p_settings);
}

void ViewAreaController::openWidgetContent(vnotex::IViewWindowContent *p_content) {
  if (!p_content) {
    return;
  }

  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "ViewAreaController::openWidgetContent: no BufferService";
    delete p_content;
    return;
  }

  Buffer2 buffer = bufferSvc->openVirtualBuffer(p_content->virtualAddress());
  if (!buffer.isValid()) {
    qWarning() << "ViewAreaController::openWidgetContent: failed to create virtual buffer for"
               << p_content->virtualAddress();
    delete p_content;
    return;
  }

  if (m_view) {
    QStringList workspaceIds = m_view->getVisibleWorkspaceIds();
    for (const auto &wsId : workspaceIds) {
      ID existingWindowId = m_view->findWindowIdByBufferId(wsId, buffer.id());
      if (existingWindowId != InvalidViewWindowId) {
        qDebug() << "ViewAreaController::openWidgetContent: content already open in workspace"
                 << wsId << "- activating existing window:" << existingWindowId;
        m_view->setCurrentViewSplit(wsId, true);
        m_view->setCurrentBuffer(wsId, buffer.id(), true);
        setCurrentViewWindow(existingWindowId, buffer.id());
        delete p_content;
        return;
      }
    }
  }

  if (m_currentWorkspaceId.isEmpty()) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    QString wsId;
    if (wsSvc) {
      wsId = wsSvc->createWorkspace(generateWorkspaceName());
    }
    if (wsId.isEmpty()) {
      qWarning() << "ViewAreaController::openWidgetContent: failed to create workspace";
      delete p_content;
      return;
    }
    auto *wrapper = new WorkspaceWrapper(wsId, this);
    wrapper->setVisible(true);
    m_workspaces.insert(wsId, wrapper);
    if (!m_view) {
      delete p_content;
      return;
    }
    m_view->addFirstViewSplit(wsId);
  }

  if (m_currentWorkspaceId.isEmpty() || !m_view) {
    qWarning() << "ViewAreaController::openWidgetContent: no current workspace";
    delete p_content;
    return;
  }

  m_view->openWidgetContent(p_content, buffer, m_currentWorkspaceId);
}

void ViewAreaController::onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                                            const FileOpenSettings &p_settings) {
  if (p_windowId == InvalidViewWindowId) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (m_shouldPropagateToCore) {
    if (wsSvc && !m_currentWorkspaceId.isEmpty()) {
      wsSvc->addBuffer(m_currentWorkspaceId, p_buffer.id());
    }
  }
  // Only track the current window during normal operation, not during session
  // restore. During restore, every opened buffer would overwrite m_currentWindowId,
  // leaving it pointing to the last-opened buffer instead of the intended current
  // one. The singleShot(0) in ViewArea2 re-syncs from ground-truth UI state after
  // all deferred Qt signals have settled.
  if (m_shouldPropagateToCore) {
    m_currentWindowId = p_windowId;
  }

  if (wsSvc) {
    ViewWindowOpenEvent ha;
    ha.bufferId = p_buffer.id();
    ha.nodeId = p_buffer.nodeId().relativePath;
    wsSvc->fireViewWindowAfterOpen(ha);
  }

  if (p_settings.m_focus) {
    if (m_view) {
      m_view->setCurrentViewSplit(m_currentWorkspaceId, true);
    }
  }
  emit windowsChanged();
  emit currentViewWindowChanged();
}

void ViewAreaController::onViewWindowClosed(ID p_windowId, const QString &p_bufferId,
                                            const QString &p_workspaceId) {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (m_shouldPropagateToCore) {
    if (wsSvc && !p_workspaceId.isEmpty() && !p_bufferId.isEmpty()) {
      wsSvc->removeBuffer(p_workspaceId, p_bufferId);
      // NOTE: vxcore auto-closes the buffer when it's removed from all workspaces
      // (see workspace_manager.cpp).
    }
  }
  if (m_currentWindowId == p_windowId) {
    m_currentWindowId = InvalidViewWindowId;
  }
  if (wsSvc) {
    ViewWindowCloseEvent ha;
    ha.bufferId = p_bufferId;
    wsSvc->fireViewWindowAfterClose(ha);
  }
  emit windowsChanged();
  checkCurrentViewWindowChange(p_workspaceId);

  // If workspace is now empty and there are other splits, remove it.
  // If single split, try to switch to a hidden workspace instead.
  // Suppressed during bulk close operations (removeWorkspace, closeAll).
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (!m_suppressAutoRemove && m_shouldPropagateToCore && !p_workspaceId.isEmpty()) {
    if (wsSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      if (bufferIds.isEmpty()) {
        if (totalSplitCount > 1) {
          // Multiple splits: remove the empty workspace and its split.
          qDebug() << "ViewAreaController: workspace" << p_workspaceId
                   << "is empty, removing split";
          removeViewSplit(p_workspaceId, false, true);
        } else {
          // Single split: try to switch to a hidden workspace.
          QStringList visibleIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
          QJsonArray allWs = wsSvc->listWorkspaces();
          QString hiddenWsId;
          for (int i = 0; i < allWs.size(); ++i) {
            QString wsId = allWs[i].toObject().value(QStringLiteral("id")).toString();
            if (!visibleIds.contains(wsId) && wsId != p_workspaceId) {
              hiddenWsId = wsId;
              break;
            }
          }
          if (!hiddenWsId.isEmpty()) {
            qDebug() << "ViewAreaController: workspace" << p_workspaceId
                     << "is empty (last split), switching to hidden workspace:" << hiddenWsId;
            // Update the split identity to point to the hidden workspace.
            // No windows to take/place since the workspace is empty.
            if (m_view) {
              m_view->updateSplitWorkspaceId(p_workspaceId, hiddenWsId);
            }
            // Show the hidden workspace's cached windows (if any).
            auto *targetWrapper = m_workspaces.value(hiddenWsId, nullptr);
            if (targetWrapper && targetWrapper->viewWindowCount() > 0) {
              int idx = targetWrapper->currentIndex();
              QVector<QObject *> wins = targetWrapper->takeAllViewWindows();
              if (m_view) {
                m_view->placeViewWindowsInSplit(hiddenWsId, wins, idx);
              }
              targetWrapper->setVisible(true);
            } else if (targetWrapper) {
              // Fresh hidden workspace — open its buffers from vxcore.
              targetWrapper->setVisible(true);
              auto *bufferSvc = m_services.get<BufferService>();
              QJsonObject wsConf = wsSvc->getWorkspace(hiddenWsId);
              QJsonArray hiddenBufIds = wsConf.value(QStringLiteral("bufferIds")).toArray();
              QString curBuf = wsConf.value(QStringLiteral("currentBufferId")).toString();
              m_currentWorkspaceId = hiddenWsId;
              for (int j = 0; j < hiddenBufIds.size(); ++j) {
                QString bid = hiddenBufIds[j].toString();
                if (!bid.isEmpty() && bufferSvc) {
                  openRestoredBuffer(bufferSvc, hiddenWsId, bid, false);
                }
              }
              if (!curBuf.isEmpty() && m_view) {
                m_view->setCurrentBuffer(hiddenWsId, curBuf, true);
              }
            }
            // Delete the empty workspace from vxcore and its wrapper.
            wsSvc->deleteWorkspace(p_workspaceId);
            if (m_workspaces.contains(p_workspaceId)) {
              auto *wrapper = m_workspaces.take(p_workspaceId);
              delete wrapper;
            }
            setCurrentViewSplit(hiddenWsId, true);
          }
          // Else: no hidden workspaces, single split with empty workspace.
          // This is the "home screen" state — nothing more to do.
        }
      }
    }
  }
}

bool ViewAreaController::closeViewWindow(ID p_windowId, bool p_force) {
  if (p_windowId == InvalidViewWindowId) {
    return false;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewWindowCloseEvent event;
  event.windowId = p_windowId;
  event.force = p_force;
  if (wsSvc && wsSvc->fireViewWindowBeforeClose(event)) {
    return false;
  }
  if (!m_view) {
    return false;
  }
  return m_view->closeViewWindow(p_windowId, p_force);
}

bool ViewAreaController::closeAll(const QVector<QString> &p_workspaceIds, bool p_force) {
  if (!m_view) {
    return true;
  }

  m_suppressAutoRemove = true;

  for (const auto &wsId : p_workspaceIds) {
    QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(wsId);
    for (ID winId : windowIds) {
      qDebug() << "ViewAreaController::closeAll: closing window:" << winId
               << "in workspace:" << wsId;
      if (!closeViewWindow(winId, p_force)) {
        // Close was cancelled. Abort entire closeAll.
        qDebug() << "ViewAreaController::closeAll: close cancelled, aborting";
        m_suppressAutoRemove = false;
        return false;
      }
    }
  }

  m_suppressAutoRemove = false;
  return true;
}

bool ViewAreaController::closeAllBuffersForQuit() {
  // Phase 1: Close all visible workspace windows with save prompts.
  QStringList visibleIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
  QVector<QString> visibleIdsVec;
  for (const auto &id : visibleIds) {
    visibleIdsVec.append(id);
  }
  if (!closeAll(visibleIdsVec, false)) {
    return false;
  }

  // Phase 2: Close hidden workspace windows.
  // Note: If user cancels during this phase, visible windows from Phase 1
  // are already closed (non-transactional). This is acceptable per spec.
  for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
    auto *wrapper = it.value();
    if (wrapper->isVisible()) {
      continue;
    }
    const auto &windows = wrapper->viewWindows();
    for (auto *obj : windows) {
      auto *win = qobject_cast<ViewWindow2 *>(obj);
      if (win && !win->aboutToClose(false)) {
        qInfo() << "ViewAreaController::closeAllBuffersForQuit:"
                << "hidden window cancel, aborting quit";
        return false;
      }
    }
  }

  return true;
}

void ViewAreaController::closeTabs(const QString &p_workspaceId, int p_referenceTabIndex,
                                   CloseTabMode p_mode) {
  if (!m_view || p_workspaceId.isEmpty()) {
    return;
  }

  QVector<ID> allWindowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
  if (allWindowIds.isEmpty()) {
    return;
  }

  if (p_referenceTabIndex < 0 || p_referenceTabIndex >= allWindowIds.size()) {
    if (p_mode != CloseTabMode::All) {
      return;
    }
  }

  // Determine which windows to close.
  QVector<ID> toClose;
  switch (p_mode) {
  case CloseTabMode::All:
    toClose = allWindowIds;
    break;
  case CloseTabMode::Others:
    for (int i = 0; i < allWindowIds.size(); ++i) {
      if (i != p_referenceTabIndex) {
        toClose.append(allWindowIds[i]);
      }
    }
    break;
  case CloseTabMode::ToTheLeft:
    for (int i = 0; i < p_referenceTabIndex; ++i) {
      toClose.append(allWindowIds[i]);
    }
    break;
  case CloseTabMode::ToTheRight:
    for (int i = p_referenceTabIndex + 1; i < allWindowIds.size(); ++i) {
      toClose.append(allWindowIds[i]);
    }
    break;
  }

  if (toClose.isEmpty()) {
    return;
  }

  // Close in reverse order to preserve indices.
  m_suppressAutoRemove = true;
  for (int i = toClose.size() - 1; i >= 0; --i) {
    if (!closeViewWindow(toClose[i], false)) {
      break; // Cancelled.
    }
  }
  m_suppressAutoRemove = false;
}

void ViewAreaController::splitViewSplit(const QString &p_workspaceId, Direction p_direction) {
  if (p_workspaceId.isEmpty()) {
    return;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewSplitCreateEvent event;
  event.workspaceId = p_workspaceId;
  event.direction = static_cast<int>(p_direction);
  if (wsSvc && wsSvc->fireViewSplitBeforeCreate(event)) {
    return;
  }

  // Get the actual current buffer from the view (ground truth, not vxcore which may be stale).
  QString currentBufferId;
  if (m_view) {
    currentBufferId = m_view->getCurrentBufferIdForWorkspace(p_workspaceId);
  }

  QString newWsId;
  if (wsSvc) {
    newWsId = wsSvc->createWorkspace(generateWorkspaceName());
  }
  if (newWsId.isEmpty()) {
    qWarning() << "ViewAreaController::splitViewSplit: failed to create workspace";
    return;
  }
  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  wrapper->setVisible(true);
  m_workspaces.insert(newWsId, wrapper);
  if (m_view) {
    m_view->split(p_workspaceId, p_direction, newWsId);
  }

  // Open the current buffer of the source workspace in the new split.
  // Set the new workspace as current — the new split becomes active (natural UX).
  if (!currentBufferId.isEmpty() && !newWsId.isEmpty()) {
    auto *bufferSvc = m_services.get<BufferService>();
    if (bufferSvc) {
      Buffer2 buf = bufferSvc->getBufferHandle(currentBufferId);
      if (buf.isValid()) {
        m_currentWorkspaceId = newWsId;
        FileOpenSettings settings;
        settings.m_focus = true;
        openBuffer(buf, settings);
      }
    }
  }

  // Distribute all splits evenly so the new split gets fair space.
  if (m_view) {
    m_view->distributeViewSplits();
  }

  if (wsSvc) {
    event.newWorkspaceId = newWsId;
    wsSvc->fireViewSplitAfterCreate(event);
  }
  emit viewSplitsCountChanged();
}

bool ViewAreaController::removeViewSplit(const QString &p_workspaceId, bool p_keepWorkspace,
                                         bool p_force) {
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (p_workspaceId.isEmpty() || totalSplitCount <= 1) {
    return false;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewSplitRemoveEvent event;
  event.workspaceId = p_workspaceId;
  event.keepWorkspace = p_keepWorkspace;
  if (wsSvc && wsSvc->fireViewSplitBeforeRemove(event)) {
    return false;
  }

  if (p_keepWorkspace) {
    // Hide-only mode: transfer ViewWindows to WorkspaceWrapper, remove split widget.
    // Workspace becomes hidden but remains in vxcore.
    auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
    if (wrapper && wrapper->isVisible() && m_view) {
      int currentIndex = 0;
      QVector<QObject *> windows = m_view->takeViewWindowsFromSplit(p_workspaceId, &currentIndex);
      wrapper->receiveViewWindows(windows, currentIndex);
      wrapper->setVisible(false);
    }
  } else {
    // Full removal mode: close all windows, delete workspace, then remove split.
    // Close windows one-by-one with abort-on-cancel.
    if (m_view) {
      m_suppressAutoRemove = true;

      QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
      for (ID winId : windowIds) {
        qDebug() << "ViewAreaController::removeViewSplit: closing window:" << winId
                 << "in workspace:" << p_workspaceId;
        if (!closeViewWindow(winId, p_force)) {
          qDebug() << "ViewAreaController::removeViewSplit: close cancelled, aborting";
          m_suppressAutoRemove = false;
          return false;
        }
      }

      m_suppressAutoRemove = false;
    }

    // Remove remaining buffers and delete workspace in vxcore.
    if (wsSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray remainingBuffers = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      for (int i = 0; i < remainingBuffers.size(); ++i) {
        QString bufferId = remainingBuffers[i].toString();
        if (!bufferId.isEmpty()) {
          wsSvc->removeBuffer(p_workspaceId, bufferId);
        }
      }
      wsSvc->deleteWorkspace(p_workspaceId);
    }

    // Destroy the WorkspaceWrapper.
    if (m_workspaces.contains(p_workspaceId)) {
      auto *wrapper = m_workspaces.take(p_workspaceId);
      auto leftover = wrapper->takeAllViewWindows();
      for (auto *obj : leftover) {
        delete qobject_cast<QWidget *>(obj);
      }
      delete wrapper;
    }
  }

  if (m_currentWorkspaceId == p_workspaceId) {
    m_currentWorkspaceId.clear();
    m_currentWindowId = InvalidViewWindowId;
  }
  if (m_view) {
    m_view->removeViewSplit(p_workspaceId);
  }
  if (wsSvc) {
    wsSvc->fireViewSplitAfterRemove(event);
  }
  emit viewSplitsCountChanged();
  return true;
}

void ViewAreaController::maximizeViewSplit(const QString &p_workspaceId) {
  if (m_view) {
    m_view->maximizeViewSplit(p_workspaceId);
  }
}

void ViewAreaController::distributeViewSplits() {
  if (m_view) {
    m_view->distributeViewSplits();
  }
}

void ViewAreaController::moveViewWindowOneSplit(const QString &p_srcWorkspaceId, ID p_windowId,
                                                Direction p_direction,
                                                const QString &p_dstWorkspaceId,
                                                const QString &p_bufferId) {
  if (p_srcWorkspaceId.isEmpty() || p_windowId == InvalidViewWindowId ||
      p_dstWorkspaceId.isEmpty()) {
    return;
  }

  // Cannot move to the same workspace.
  if (p_srcWorkspaceId == p_dstWorkspaceId) {
    return;
  }

  // Check if the buffer is already open in the destination workspace.
  // If so, just activate the existing tab instead of creating a duplicate.
  if (m_view && !p_bufferId.isEmpty()) {
    ID existingId = m_view->findWindowIdByBufferId(p_dstWorkspaceId, p_bufferId);
    if (existingId != InvalidViewWindowId) {
      qDebug() << "ViewAreaController::moveViewWindowOneSplit: buffer already in destination,"
               << "activating existing window:" << existingId;
      setCurrentViewSplit(p_dstWorkspaceId, true);
      setCurrentViewWindow(existingId, p_bufferId);
      return;
    }
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewWindowMoveEvent event;
  event.windowId = p_windowId;
  event.srcWorkspaceId = p_srcWorkspaceId;
  event.dstWorkspaceId = p_dstWorkspaceId;
  event.direction = static_cast<int>(p_direction);
  event.bufferId = p_bufferId;
  if (wsSvc && wsSvc->fireViewWindowBeforeMove(event)) {
    return;
  }

  // Transfer buffer registration between workspaces in vxcore.
  if (m_shouldPropagateToCore && !p_bufferId.isEmpty()) {
    if (wsSvc) {
      wsSvc->removeBuffer(p_srcWorkspaceId, p_bufferId);
      wsSvc->addBuffer(p_dstWorkspaceId, p_bufferId);
    }
  }

  if (m_view) {
    m_view->moveViewWindowToSplit(p_windowId, p_srcWorkspaceId, p_dstWorkspaceId);
  }
  setCurrentViewSplit(p_dstWorkspaceId, true);
  if (wsSvc) {
    wsSvc->fireViewWindowAfterMove(event);
  }
  emit windowsChanged();
}

ID ViewAreaController::getCurrentWindowId() const { return m_currentWindowId; }

QString ViewAreaController::getCurrentWorkspaceId() const { return m_currentWorkspaceId; }

void ViewAreaController::setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) {
  if (m_currentWorkspaceId == p_workspaceId && !p_workspaceId.isEmpty()) {
    if (p_focus && m_view) {
      m_view->focusViewSplit(p_workspaceId);
    }
    return;
  }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  ViewSplitActivateEvent event;
  event.workspaceId = p_workspaceId;
  if (wsSvc && wsSvc->fireViewSplitBeforeActivate(event)) {
    return;
  }
  m_currentWorkspaceId = p_workspaceId;
  if (m_shouldPropagateToCore) {
    if (wsSvc && !p_workspaceId.isEmpty()) {
      wsSvc->setCurrentWorkspace(p_workspaceId);
    }
  }
  if (m_view) {
    m_view->setCurrentViewSplit(p_workspaceId, p_focus);
  }
  if (p_focus && m_view) {
    m_view->focusViewSplit(p_workspaceId);
  }
  if (wsSvc) {
    wsSvc->fireViewSplitAfterActivate(event);
  }
}

void ViewAreaController::setCurrentViewWindow(ID p_windowId, const QString &p_bufferId) {
  if (m_currentWindowId == p_windowId) {
    return;
  }
  qDebug() << "ViewAreaController::setCurrentViewWindow: windowId:" << p_windowId
           << "bufferId:" << p_bufferId << "workspace:" << m_currentWorkspaceId;
  m_currentWindowId = p_windowId;

  // Update vxcore's current buffer tracking for the active workspace.
  if (m_shouldPropagateToCore && !p_bufferId.isEmpty() && !m_currentWorkspaceId.isEmpty()) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      wsSvc->setCurrentBuffer(m_currentWorkspaceId, p_bufferId);
    }
  }

  emit currentViewWindowChanged();
}

void ViewAreaController::focus() {
  if (!m_currentWorkspaceId.isEmpty() && m_view) {
    m_view->focusViewSplit(m_currentWorkspaceId);
  }
}

void ViewAreaController::setShouldPropagateToCore(bool p_enabled) {
  m_shouldPropagateToCore = p_enabled;
}

bool ViewAreaController::shouldPropagateToCore() const { return m_shouldPropagateToCore; }

void ViewAreaController::newWorkspace(const QString &p_currentWorkspaceId, const QString &p_name) {
  if (p_currentWorkspaceId.isEmpty()) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  QString newWsId = wsSvc->createWorkspace(p_name);
  if (newWsId.isEmpty()) {
    return;
  }

  // Create wrapper (not visible yet — switchWorkspace will manage visibility).
  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  m_workspaces.insert(newWsId, wrapper);

  switchWorkspace(p_currentWorkspaceId, newWsId);
}

void ViewAreaController::renameWorkspace(const QString &p_workspaceId, const QString &p_newName) {
  if (p_workspaceId.isEmpty() || p_newName.isEmpty()) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  wsSvc->renameWorkspace(p_workspaceId, p_newName);
}

QString ViewAreaController::getWorkspaceName(const QString &p_workspaceId) const {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return QString();
  }

  QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
  return wsConfig.value(QStringLiteral("name")).toString();
}

bool ViewAreaController::removeWorkspace(QString p_workspaceId, bool p_force) {
  if (p_workspaceId.isEmpty()) {
    return false;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return false;
  }

  // Step 1: Get workspace's buffer list from vxcore.
  QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
  QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();

  // Step 2: Close each ViewWindow one-by-one with abort-on-cancel.
  // Suppress auto-remove-empty-workspace during the close loop since we manage
  // the workspace lifecycle explicitly.
  if (!bufferIds.isEmpty() && m_view) {
    m_suppressAutoRemove = true;

    QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
    for (ID winId : windowIds) {
      qDebug() << "ViewAreaController::removeWorkspace: closing window:" << winId
               << "in workspace:" << p_workspaceId;
      if (!closeViewWindow(winId, p_force)) {
        // Close was cancelled (hook or aboutToClose returned false). Abort.
        qDebug() << "ViewAreaController::removeWorkspace: close cancelled, aborting";
        m_suppressAutoRemove = false;
        return false;
      }
    }

    m_suppressAutoRemove = false;
  }

  // Step 3: Remove remaining buffers from vxcore workspace.
  // onViewWindowClosed already called wsSvc->removeBuffer() for each closed window,
  // but there may be buffers without ViewWindows (e.g., hidden workspace case).
  QJsonObject updatedConfig = wsSvc->getWorkspace(p_workspaceId);
  QJsonArray remainingBuffers = updatedConfig.value(QStringLiteral("bufferIds")).toArray();
  for (int i = 0; i < remainingBuffers.size(); ++i) {
    QString bufferId = remainingBuffers[i].toString();
    if (!bufferId.isEmpty()) {
      qDebug() << "ViewAreaController::removeWorkspace: removing orphan buffer:" << bufferId;
      wsSvc->removeBuffer(p_workspaceId, bufferId);
    }
  }

  // Step 4: Post-removal — find a replacement for the split.
  QStringList visibleWorkspaceIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  bool splitStillShowsWorkspace = visibleWorkspaceIds.contains(p_workspaceId);

  if (splitStillShowsWorkspace) {
    // The split is still showing this workspace's identity. Find a replacement.
    QJsonArray allWorkspaces = wsSvc->listWorkspaces();
    QString targetWsId;
    for (int i = 0; i < allWorkspaces.size(); ++i) {
      QJsonObject ws = allWorkspaces[i].toObject();
      QString wsId = ws.value(QStringLiteral("id")).toString();
      if (wsId != p_workspaceId && !visibleWorkspaceIds.contains(wsId)) {
        targetWsId = wsId;
        break;
      }
    }

    if (!targetWsId.isEmpty()) {
      // Switch to a hidden workspace.
      qDebug() << "ViewAreaController::removeWorkspace: switching to hidden workspace:"
               << targetWsId;
      switchWorkspace(p_workspaceId, targetWsId);
    } else if (totalSplitCount > 1) {
      // No hidden workspace. Remove the split entirely.
      // Clear current tracking first so removeViewSplit doesn't try to re-enter
      // removeWorkspace (we're already handling the workspace deletion below).
      if (m_currentWorkspaceId == p_workspaceId) {
        m_currentWorkspaceId.clear();
        m_currentWindowId = InvalidViewWindowId;
      }
      qDebug() << "ViewAreaController::removeWorkspace: no hidden workspace, removing split";
      if (m_view) {
        m_view->removeViewSplit(p_workspaceId);
      }
      emit viewSplitsCountChanged();
    } else {
      // Last split. Create a new empty workspace.
      qDebug() << "ViewAreaController::removeWorkspace: last split, creating new workspace";
      QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
      if (!newWsId.isEmpty()) {
        auto *newWrapper = new WorkspaceWrapper(newWsId, this);
        m_workspaces.insert(newWsId, newWrapper);
        switchWorkspace(p_workspaceId, newWsId);
      }
    }
  }

  // Step 5: Delete the workspace in vxcore.
  qDebug() << "ViewAreaController::removeWorkspace: deleting workspace:" << p_workspaceId;
  wsSvc->deleteWorkspace(p_workspaceId);

  // Step 6: Destroy the WorkspaceWrapper.
  if (m_workspaces.contains(p_workspaceId)) {
    auto *wrapper = m_workspaces.take(p_workspaceId);
    auto leftover = wrapper->takeAllViewWindows();
    for (auto *obj : leftover) {
      delete qobject_cast<QWidget *>(obj);
    }
    delete wrapper;
  }

  return true;
}

void ViewAreaController::switchWorkspace(const QString &p_currentWorkspaceId,
                                         const QString &p_targetWorkspaceId) {
  if (p_currentWorkspaceId.isEmpty() || p_targetWorkspaceId.isEmpty()) {
    return;
  }
  if (p_currentWorkspaceId == p_targetWorkspaceId) {
    return;
  }

  // A workspace can only be shown in one ViewSplit at a time.
  // Reject switching to a workspace that is already visible in another split.
  if (m_view) {
    QStringList visibleIds = m_view->getVisibleWorkspaceIds();
    if (visibleIds.contains(p_targetWorkspaceId)) {
      qWarning() << "ViewAreaController::switchWorkspace: target workspace" << p_targetWorkspaceId
                 << "is already visible in another split";
      return;
    }
  }

  auto *currentWrapper = m_workspaces.value(p_currentWorkspaceId, nullptr);
  auto *targetWrapper = m_workspaces.value(p_targetWorkspaceId, nullptr);
  if (!currentWrapper || !targetWrapper) {
    qWarning() << "ViewAreaController::switchWorkspace: missing wrapper for" << p_currentWorkspaceId
               << "or" << p_targetWorkspaceId;
    return;
  }

  // Guard: current workspace must be visible (displayed in a split).
  // This can happen if a stale signal triggers a switch after the workspace
  // was already hidden by a previous switch.
  if (!currentWrapper->isVisible()) {
    qWarning() << "ViewAreaController::switchWorkspace: current workspace" << p_currentWorkspaceId
               << "is not visible, ignoring";
    return;
  }

  qDebug() << "ViewAreaController::switchWorkspace:" << p_currentWorkspaceId << "->"
           << p_targetWorkspaceId << "currentWrapper visible:" << currentWrapper->isVisible()
           << "windowCount:" << currentWrapper->viewWindowCount()
           << "targetWrapper visible:" << targetWrapper->isVisible()
           << "windowCount:" << targetWrapper->viewWindowCount();

  // Suppress vxcore propagation during reparenting to prevent
  // currentChanged signals from corrupting workspace state.
  // NOTE: Suppression starts AFTER takeViewWindowsFromSplit so that
  // buffer order sync (inside take) can propagate to vxcore.
  // This ensures hidden workspaces have correct tab order in vxcore
  // for session persistence.
  bool wasPropagating = m_shouldPropagateToCore;

  // Step 1: HIDE current workspace — take windows from split into wrapper.
  // Done BEFORE suppressing propagation so buffer order is synced to vxcore.
  if (m_view) {
    int currentIndex = 0;
    QVector<QObject *> windows =
        m_view->takeViewWindowsFromSplit(p_currentWorkspaceId, &currentIndex);
    currentWrapper->receiveViewWindows(windows, currentIndex);
    currentWrapper->setVisible(false);
  }

  m_shouldPropagateToCore = false;

  // Step 2: Update split identity (old workspace ID -> new workspace ID).
  if (m_view) {
    m_view->updateSplitWorkspaceId(p_currentWorkspaceId, p_targetWorkspaceId);
  }

  // Step 3: SHOW target workspace.
  targetWrapper->setVisible(true);
  if (targetWrapper->viewWindowCount() > 0) {
    // Target has cached windows — reparent them into the split.
    int targetIndex = targetWrapper->currentIndex();
    QVector<QObject *> windows = targetWrapper->takeAllViewWindows();
    if (m_view) {
      m_view->placeViewWindowsInSplit(p_targetWorkspaceId, windows, targetIndex);
    }
  } else {
    // Fresh workspace (no cached windows) — fall back to openRestoredBuffer pattern.
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    auto *bufferSvc = m_services.get<BufferService>();
    if (wsSvc && bufferSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_targetWorkspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      QString currentBufferId = wsConfig.value(QStringLiteral("currentBufferId")).toString();

      qDebug() << "ViewAreaController::switchWorkspace: fresh workspace" << p_targetWorkspaceId
               << "buffers:" << bufferIds << "currentBufferId:" << currentBufferId;

      m_currentWorkspaceId = p_targetWorkspaceId;
      for (int i = 0; i < bufferIds.size(); ++i) {
        QString bufferId = bufferIds[i].toString();
        if (!bufferId.isEmpty()) {
          openRestoredBuffer(bufferSvc, p_targetWorkspaceId, bufferId, false);
        }
      }

      if (!currentBufferId.isEmpty() && m_view) {
        m_view->setCurrentBuffer(p_targetWorkspaceId, currentBufferId, true);
      }
    }
  }

  m_shouldPropagateToCore = wasPropagating;
  setCurrentViewSplit(p_targetWorkspaceId, true);
}

void ViewAreaController::setBufferOrder(const QString &p_workspaceId,
                                        const QStringList &p_bufferIds) {
  if (p_workspaceId.isEmpty()) {
    return;
  }
  qDebug() << "ViewAreaController::setBufferOrder: workspace:" << p_workspaceId
           << "bufferIds:" << p_bufferIds;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    wsSvc->setBufferOrder(p_workspaceId, p_bufferIds);
  }
}

QJsonObject ViewAreaController::saveLayout(const QJsonObject &p_widgetTree) const {
  QJsonObject layout;
  layout[QStringLiteral("splitterTree")] = p_widgetTree;
  // Note: currentWorkspaceId is NOT saved here. It is persisted by vxcore
  // (via vxcore_shutdown) and restored by restoreSession().
  return layout;
}

void ViewAreaController::loadLayout(const QJsonObject &p_layout) {
  if (p_layout.isEmpty()) {
    return;
  }
  if (m_view) {
    m_view->loadLayout(p_layout);
  }
  emit viewSplitsCountChanged();
}

void ViewAreaController::restoreSession(const QStringList &p_layoutWorkspaceIds) {
  m_shouldPropagateToCore = false;

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  auto *bufferSvc = m_services.get<BufferService>();
  if (!wsSvc || !bufferSvc) {
    qWarning() << "ViewAreaController::restoreSession: services unavailable"
               << "wsSvc:" << (wsSvc != nullptr) << "bufferSvc:" << (bufferSvc != nullptr);
    return;
  }

  if (p_layoutWorkspaceIds.isEmpty()) {
    qInfo() << "ViewAreaController::restoreSession: no workspaces in layout";
    return;
  }

  qInfo() << "ViewAreaController::restoreSession: layout has" << p_layoutWorkspaceIds.size()
          << "workspaces";

  // Determine the current workspace. If the saved current workspace is not in the
  // layout, fall back to the first workspace in the layout.
  QString restoredCurrentWsId = wsSvc->getCurrentWorkspace();
  if (!p_layoutWorkspaceIds.contains(restoredCurrentWsId)) {
    restoredCurrentWsId = p_layoutWorkspaceIds.first();
    qDebug() << "ViewAreaController::restoreSession: saved current workspace not in layout,"
             << "falling back to:" << restoredCurrentWsId;
  }

  qDebug() << "ViewAreaController::restoreSession: restoring workspaces from layout:"
           << p_layoutWorkspaceIds << "currentWorkspace:" << restoredCurrentWsId;

  QStringList restoredWorkspaceIds;

  for (const auto &wsId : p_layoutWorkspaceIds) {
    // Get the full workspace config (includes bufferIds, currentBufferId).
    QJsonObject wsConfig = wsSvc->getWorkspace(wsId);
    if (wsConfig.isEmpty()) {
      // Workspace from layout no longer exists in vxcore. Create a replacement
      // so the split has a valid workspace backing.
      QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
      qWarning() << "  Workspace" << wsId << "not found in vxcore,"
                 << "created replacement:" << newWsId;
      if (!newWsId.isEmpty() && m_view) {
        auto *wrapper = new WorkspaceWrapper(newWsId, this);
        wrapper->setVisible(true);
        m_workspaces.insert(newWsId, wrapper);
        m_view->switchWorkspace(wsId, newWsId);
        restoredWorkspaceIds.append(newWsId);
        // Update restoredCurrentWsId if the stale one was the current.
        if (restoredCurrentWsId == wsId) {
          restoredCurrentWsId = newWsId;
        }
      }
      continue;
    }

    restoredWorkspaceIds.append(wsId);

    // Create WorkspaceWrapper for this restored workspace (visible in a split).
    if (!m_workspaces.contains(wsId)) {
      auto *wrapper = new WorkspaceWrapper(wsId, this);
      wrapper->setVisible(true);
      m_workspaces.insert(wsId, wrapper);
    }

    QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
    QString currentBufferId = wsConfig.value(QStringLiteral("currentBufferId")).toString();

    // Per-buffer metadata is now stored in "bufferMetadata" (native vxcore field).
    QJsonObject bufferMetadata = wsConfig.value(QStringLiteral("bufferMetadata")).toObject();

    qDebug() << "  Workspace" << wsId
             << "name:" << wsConfig.value(QStringLiteral("name")).toString()
             << "buffers:" << bufferIds << "currentBuffer:" << currentBufferId
             << "bufferMetadata keys:" << bufferMetadata.keys();

    if (bufferIds.isEmpty()) {
      qDebug() << "    (no buffers, skipping)";
      continue;
    }

    // Set this workspace as current so openBuffer routes to the right split.
    m_currentWorkspaceId = wsId;

    // Open all buffers in their saved order.
    for (int j = 0; j < bufferIds.size(); ++j) {
      QString bufferId = bufferIds[j].toString();
      if (bufferId.isEmpty()) {
        continue;
      }
      // Resolve saved view mode and cursor position from per-buffer metadata.
      ViewWindowMode mode = ViewWindowMode::Read;
      int lineNumber = -1;
      QJsonObject bufMeta = bufferMetadata.value(bufferId).toObject();
      QString modeStr = bufMeta.value(QStringLiteral("mode")).toString();
      if (modeStr == QStringLiteral("Edit")) {
        mode = ViewWindowMode::Edit;
      }
      lineNumber = bufMeta.value(QStringLiteral("cursorPosition")).toInt(-1);
      qDebug() << "    Opening buffer" << bufferId
               << (bufferId == currentBufferId ? "(current)" : "(non-current)")
               << "mode:" << modeStr << "cursor:" << lineNumber;
      openRestoredBuffer(bufferSvc, wsId, bufferId, false, mode, lineNumber);
    }

    // After all buffers are opened in order, set the current buffer's tab as active.
    if (!currentBufferId.isEmpty() && m_view) {
      bool shouldFocus = (wsId == restoredCurrentWsId);
      qDebug() << "    Setting current buffer:" << currentBufferId << "focus:" << shouldFocus;
      m_view->setCurrentBuffer(wsId, currentBufferId, shouldFocus);
    }
  }

  // Create WorkspaceWrappers for hidden workspaces (in vxcore but not in the layout).
  // These will be switched to later by the user.
  QJsonArray allWorkspaces = wsSvc->listWorkspaces();
  for (int i = 0; i < allWorkspaces.size(); ++i) {
    QJsonObject wsObj = allWorkspaces[i].toObject();
    QString wsId = wsObj.value(QStringLiteral("id")).toString();
    if (!m_workspaces.contains(wsId)) {
      auto *wrapper = new WorkspaceWrapper(wsId, this);
      // Not visible — no split is displaying it.
      m_workspaces.insert(wsId, wrapper);
    }
  }

  // Resolve current workspace: must be one that was actually restored.
  // If the saved current workspace was not restored, fall back to the first restored one.
  if (!restoredWorkspaceIds.contains(restoredCurrentWsId)) {
    if (!restoredWorkspaceIds.isEmpty()) {
      restoredCurrentWsId = restoredWorkspaceIds.first();
      qDebug() << "  Current workspace not restored, falling back to:" << restoredCurrentWsId;
    } else {
      restoredCurrentWsId.clear();
      qDebug() << "  No workspaces were restored";
    }
  }

  // Restore the current workspace from vxcore.
  // Clear m_currentWorkspaceId first so setCurrentViewSplit doesn't early-return
  // (it was already set during the loop). This ensures the view receives
  // setCurrentViewSplit() to mark the split as active.
  m_currentWorkspaceId.clear();
  if (!restoredCurrentWsId.isEmpty()) {
    qDebug() << "  Setting current workspace:" << restoredCurrentWsId;
    setCurrentViewSplit(restoredCurrentWsId, true);
  }

  qInfo() << "ViewAreaController::restoreSession: done";
  // Note: m_shouldPropagateToCore stays false. The caller is responsible for
  // re-enabling it after pending signals have been processed (e.g., via
  // QTimer::singleShot(0, ...)) to avoid deferred currentChanged signals
  // from addTab corrupting vxcore state.
}

void ViewAreaController::openRestoredBuffer(BufferService *p_bufferSvc,
                                            const QString &p_workspaceId, const QString &p_bufferId,
                                            bool p_focus, ViewWindowMode p_mode, int p_lineNumber) {
  Buffer2 buf = p_bufferSvc->getBufferHandle(p_bufferId);
  if (!buf.isValid()) {
    qWarning() << "    Failed to get buffer handle:" << p_bufferId;
    return;
  }

  // Resolve file type.
  QString fileType;
  auto *ftSvc = m_services.get<FileTypeCoreService>();
  if (ftSvc) {
    FileType ft = ftSvc->getFileType(buf.nodeId().relativePath);
    fileType = ft.m_typeName;
  }
  if (fileType.isEmpty()) {
    fileType = QStringLiteral("Text");
  }

  qDebug() << "    Opening restored buffer: buffer:" << p_bufferId
           << "path:" << buf.nodeId().relativePath << "type:" << fileType
           << "workspace:" << p_workspaceId << "focus:" << p_focus
           << "mode:" << static_cast<int>(p_mode) << "lineNumber:" << p_lineNumber;

  FileOpenSettings settings;
  settings.m_focus = p_focus;
  settings.m_mode = p_mode;
  settings.m_lineNumber = p_lineNumber;

  if (m_view) {
    m_view->openBuffer(buf, fileType, p_workspaceId, settings);
  }
}

void ViewAreaController::checkCurrentViewWindowChange(const QString &p_workspaceId) {
  Q_UNUSED(p_workspaceId)
  emit currentViewWindowChanged();
}

void ViewAreaController::notifyCurrentViewWindowChanged() {
  qDebug() << "ViewAreaController::notifyCurrentViewWindowChanged: currentWindowId:"
           << m_currentWindowId << "currentWorkspaceId:" << m_currentWorkspaceId;
  emit currentViewWindowChanged();
}

void ViewAreaController::subscribeToHooks() {
  auto *hookMgr = m_services.get<HookManager>();
  if (!hookMgr) {
    qWarning() << "ViewAreaController::subscribeToHooks: HookManager not available";
    return;
  }
  hookMgr->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [this](HookContext &p_ctx, const FileOpenEvent &p_event) {
        Q_UNUSED(p_ctx)
        onFileAfterOpen(p_event);
      },
      10);
  hookMgr->addAction<NodeRenameEvent>(
      HookNames::NodeAfterRename,
      [this](HookContext &p_ctx, const NodeRenameEvent &p_event) {
        Q_UNUSED(p_ctx)
        onNodeAfterRename(p_event);
      },
      10);
  hookMgr->addAction(
      HookNames::ConfigEditorChanged,
      [this](HookContext &p_ctx, const QVariantMap &) {
        Q_UNUSED(p_ctx)
        onEditorConfigChanged();
      },
      10);
}

void ViewAreaController::onFileAfterOpen(const FileOpenEvent &p_event) {
  const QString bufferId = p_event.bufferId;
  if (bufferId.isEmpty()) {
    qWarning() << "ViewAreaController::onFileAfterOpen: empty bufferId";
    return;
  }
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "ViewAreaController::onFileAfterOpen: BufferService unavailable";
    return;
  }
  Buffer2 buf = bufferSvc->getBufferHandle(bufferId);
  if (!buf.isValid()) {
    qWarning() << "ViewAreaController::onFileAfterOpen: invalid buffer" << bufferId;
    return;
  }
  FileOpenSettings settings;
  settings.m_mode = static_cast<ViewWindowMode>(p_event.mode);
  settings.m_forceMode = p_event.forceMode;
  settings.m_focus = p_event.focus;
  settings.m_newFile = p_event.newFile;
  settings.m_readOnly = p_event.readOnly;
  settings.m_lineNumber = p_event.lineNumber;
  settings.m_alwaysNewWindow = p_event.alwaysNewWindow;
  // Reconstruct SearchHighlightContext from event's search fields.
  if (!p_event.searchPatterns.isEmpty()) {
    settings.m_searchHighlight.m_patterns = p_event.searchPatterns;
    settings.m_searchHighlight.m_options = static_cast<FindOptions>(p_event.searchOptions);
    settings.m_searchHighlight.m_currentMatchLine = p_event.searchCurrentMatchLine;
    settings.m_searchHighlight.m_isValid = true;
  }
  openBuffer(buf, settings);
}

void ViewAreaController::onNodeAfterRename(const NodeRenameEvent &p_event) {
  const QString notebookId = p_event.notebookId;
  const QString relativePath = p_event.relativePath;
  const QString newName = p_event.newName;
  const bool isFolder = p_event.isFolder;

  if (notebookId.isEmpty() || relativePath.isEmpty() || newName.isEmpty()) {
    return;
  }

  // Derive new path: replace the last component of the old path with newName.
  QString newPath = relativePath;
  int lastSlash = newPath.lastIndexOf(QLatin1Char('/'));
  if (lastSlash >= 0) {
    newPath = newPath.left(lastSlash + 1) + newName;
  } else {
    newPath = newName;
  }

  // vxcore has already updated buffer paths internally during the rename.
  // We only need to update the cached NodeIdentifier on ViewWindow2 instances.

  if (!isFolder) {
    // File rename: single buffer affected.
    NodeIdentifier oldNodeId;
    oldNodeId.notebookId = notebookId;
    oldNodeId.relativePath = relativePath;

    NodeIdentifier newNodeId;
    newNodeId.notebookId = notebookId;
    newNodeId.relativePath = newPath;

    // Update visible windows via the view interface.
    if (m_view) {
      m_view->onNodeRenamed(oldNodeId, newNodeId);
    }

    // Update hidden workspace windows.
    for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
      auto *wrapper = it.value();
      if (wrapper->isVisible()) {
        continue;
      }
      const auto &windows = wrapper->viewWindows();
      for (auto *obj : windows) {
        auto *win = qobject_cast<ViewWindow2 *>(obj);
        if (win && win->getNodeId() == oldNodeId) {
          win->onNodeRenamed(newNodeId);
        }
      }
    }
  } else {
    // Folder rename: update all windows whose paths are under the old folder.
    QString oldPrefix = relativePath + QLatin1Char('/');
    QString newPrefix = newPath + QLatin1Char('/');

    // Collect old->new pairs for all affected windows.
    QVector<QPair<NodeIdentifier, NodeIdentifier>> renames;

    auto collectRenames = [&](const NodeIdentifier &nodeId) {
      if (nodeId.notebookId != notebookId) {
        return;
      }
      if (nodeId.relativePath.startsWith(oldPrefix)) {
        NodeIdentifier oldId = nodeId;
        NodeIdentifier newId;
        newId.notebookId = notebookId;
        newId.relativePath = newPrefix + nodeId.relativePath.mid(oldPrefix.size());
        renames.append({oldId, newId});
      }
    };

    // Scan visible windows.
    if (m_view) {
      // We'll iterate hidden workspaces' windows and visible workspace windows
      // by first collecting all renames, then applying them.
      for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
        auto *wrapper = it.value();
        const auto &windows = wrapper->viewWindows();
        for (auto *obj : windows) {
          auto *win = qobject_cast<ViewWindow2 *>(obj);
          if (win) {
            collectRenames(win->getNodeId());
          }
        }
      }
    }

    // Apply renames.
    for (const auto &pair : renames) {
      if (m_view) {
        m_view->onNodeRenamed(pair.first, pair.second);
      }
      // Update hidden workspace windows.
      for (auto it = m_workspaces.constBegin(); it != m_workspaces.constEnd(); ++it) {
        auto *wrapper = it.value();
        if (wrapper->isVisible()) {
          continue;
        }
        const auto &windows = wrapper->viewWindows();
        for (auto *obj : windows) {
          auto *win = qobject_cast<ViewWindow2 *>(obj);
          if (win && win->getNodeId() == pair.first) {
            win->onNodeRenamed(pair.second);
          }
        }
      }
    }
  }
}

void ViewAreaController::onEditorConfigChanged() {
  // Sync auto-save policy from config.
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto *bufferService = m_services.get<BufferService>();
  if (configMgr && bufferService) {
    bufferService->syncAutoSavePolicy(
        static_cast<int>(configMgr->getEditorConfig().getAutoSavePolicy()));
  }

  // Notify all view windows to reload their editor config.
  if (m_view) {
    m_view->notifyEditorConfigChanged();
  }
}

QString ViewAreaController::generateWorkspaceName() const {
  QSet<int> usedNumbers;
  static const QRegularExpression re(QStringLiteral("^Workspace (\\d+)$"));

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    QJsonArray workspaces = wsSvc->listWorkspaces();
    for (int i = 0; i < workspaces.size(); ++i) {
      QJsonObject wsObj = workspaces[i].toObject();
      QString name = wsObj.value(QStringLiteral("name")).toString();
      QRegularExpressionMatch match = re.match(name);
      if (match.hasMatch()) {
        usedNumbers.insert(match.captured(1).toInt());
      }
    }
  }

  int n = 1;
  while (usedNumbers.contains(n)) {
    ++n;
  }
  return QStringLiteral("Workspace %1").arg(n);
}
