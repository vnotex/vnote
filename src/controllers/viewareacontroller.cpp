#include "viewareacontroller.h"

#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <QWidget>

#include <controllers/workspacewrapper.h>
#include <core/fileopensettings.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/workspacecoreservice.h>

using namespace vnotex;

ViewAreaController::ViewAreaController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
}

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

void ViewAreaController::setView(ViewAreaView *p_view) {
  m_view = p_view;
}

void ViewAreaController::openBuffer(const Buffer2 &p_buffer,
                                    const FileOpenSettings &p_settings) {
  QString fileType;
  auto *ftSvc = m_services.get<FileTypeCoreService>();
  if (ftSvc) {
    FileType ft = ftSvc->getFileType(p_buffer.nodeId().relativePath);
    fileType = ft.m_typeName;
  }
  if (fileType.isEmpty()) {
    fileType = QStringLiteral("Text");
  }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("fileType")] = fileType;
  args[QStringLiteral("bufferId")] = p_buffer.id();
  args[QStringLiteral("nodeId")] = p_buffer.nodeId().relativePath;
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeOpen, args)) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
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
    if (!m_view) { return; }
    m_view->addFirstViewSplit(wsId);
  }

  if (m_currentWorkspaceId.isEmpty()) {
    qWarning() << "ViewAreaController: no current workspace";
    return;
  }
  if (!m_view) { return; }

  // Check if the buffer is already open in the current workspace.
  // If so, just activate the existing tab instead of creating a duplicate.
  ID existingWindowId = m_view->findWindowIdByBufferId(
      m_currentWorkspaceId, p_buffer.id());
  if (existingWindowId != InvalidViewWindowId) {
    qInfo() << "ViewAreaController::openBuffer: buffer already open in workspace,"
            << "activating existing window:" << existingWindowId;
    m_view->setCurrentBuffer(m_currentWorkspaceId, p_buffer.id(), true);
    setCurrentViewWindow(existingWindowId, p_buffer.id());
    return;
  }

  m_view->openBuffer(p_buffer, fileType, m_currentWorkspaceId, p_settings);
}

void ViewAreaController::onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                                            const FileOpenSettings &p_settings) {
  if (p_windowId == InvalidViewWindowId) { return; }

  if (m_shouldPropagateToCore) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc && !m_currentWorkspaceId.isEmpty()) {
      wsSvc->addBuffer(m_currentWorkspaceId, p_buffer.id());
    }
  }
  m_currentWindowId = p_windowId;

  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    QVariantMap ha;
    ha[QStringLiteral("bufferId")] = p_buffer.id();
    ha[QStringLiteral("nodeId")] = p_buffer.nodeId().relativePath;
    hookMgr->doAction(HookNames::ViewWindowAfterOpen, ha);
  }

  if (p_settings.m_focus) {
    if (m_view) { m_view->setCurrentViewSplit(m_currentWorkspaceId, true); }
  }
  emit windowsChanged();
  emit currentViewWindowChanged();
}

void ViewAreaController::onViewWindowClosed(ID p_windowId, const QString &p_bufferId,
                                            const QString &p_workspaceId) {
  if (m_shouldPropagateToCore) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc && !p_workspaceId.isEmpty() && !p_bufferId.isEmpty()) {
      wsSvc->removeBuffer(p_workspaceId, p_bufferId);
    }
  }
  if (m_currentWindowId == p_windowId) {
    m_currentWindowId = InvalidViewWindowId;
  }
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    QVariantMap ha;
    ha[QStringLiteral("bufferId")] = p_bufferId;
    hookMgr->doAction(HookNames::ViewWindowAfterClose, ha);
  }
  emit windowsChanged();
  checkCurrentViewWindowChange(p_workspaceId);

  // If workspace is now empty and there are other splits, remove it.
  // If single split, try to switch to a hidden workspace instead.
  // Suppressed during bulk close operations (removeWorkspace, closeAll).
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (!m_suppressAutoRemove && m_shouldPropagateToCore
      && !p_workspaceId.isEmpty()) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      if (bufferIds.isEmpty()) {
        if (totalSplitCount > 1) {
          // Multiple splits: remove the empty workspace and its split.
          qInfo() << "ViewAreaController: workspace" << p_workspaceId
                  << "is empty, removing split";
          removeViewSplit(p_workspaceId, false, true);
        } else {
          // Single split: try to switch to a hidden workspace.
          QStringList visibleIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
          QJsonArray allWs = wsSvc->listWorkspaces();
          QString hiddenWsId;
          for (int i = 0; i < allWs.size(); ++i) {
            QString wsId = allWs[i].toObject()
                               .value(QStringLiteral("id")).toString();
            if (!visibleIds.contains(wsId) && wsId != p_workspaceId) {
              hiddenWsId = wsId;
              break;
            }
          }
          if (!hiddenWsId.isEmpty()) {
            qInfo() << "ViewAreaController: workspace" << p_workspaceId
                    << "is empty (last split), switching to hidden workspace:"
                    << hiddenWsId;
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
              QJsonArray hiddenBufIds = wsConf.value(
                  QStringLiteral("bufferIds")).toArray();
              QString curBuf = wsConf.value(
                  QStringLiteral("currentBufferId")).toString();
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
  if (p_windowId == InvalidViewWindowId) { return false; }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("windowId")] = p_windowId;
  args[QStringLiteral("force")] = p_force;
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeClose, args)) {
    return false;
  }
  if (!m_view) { return false; }
  return m_view->closeViewWindow(p_windowId, p_force);
}

bool ViewAreaController::closeAll(const QVector<QString> &p_workspaceIds, bool p_force) {
  // TODO(save-prompts): Same as removeWorkspace — abort path depends on
  // ViewWindow2::aboutToClose() implementing save prompts.
  if (!m_view) { return true; }

  m_suppressAutoRemove = true;

  for (const auto &wsId : p_workspaceIds) {
    QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(wsId);
    for (ID winId : windowIds) {
      qInfo() << "ViewAreaController::closeAll: closing window:" << winId
              << "in workspace:" << wsId;
      if (!closeViewWindow(winId, p_force)) {
        // Close was cancelled. Abort entire closeAll.
        qInfo() << "ViewAreaController::closeAll: close cancelled, aborting";
        m_suppressAutoRemove = false;
        return false;
      }
    }
  }

  m_suppressAutoRemove = false;
  return true;
}

void ViewAreaController::splitViewSplit(const QString &p_workspaceId, Direction p_direction) {
  if (p_workspaceId.isEmpty()) { return; }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  args[QStringLiteral("direction")] = static_cast<int>(p_direction);
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeCreate, args)) { return; }

  // Get the actual current buffer from the view (ground truth, not vxcore which may be stale).
  QString currentBufferId;
  if (m_view) {
    currentBufferId = m_view->getCurrentBufferIdForWorkspace(p_workspaceId);
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  QString newWsId;
  if (wsSvc) { newWsId = wsSvc->createWorkspace(generateWorkspaceName()); }
  if (newWsId.isEmpty()) {
    qWarning() << "ViewAreaController::splitViewSplit: failed to create workspace";
    return;
  }
  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  wrapper->setVisible(true);
  m_workspaces.insert(newWsId, wrapper);
  if (m_view) { m_view->split(p_workspaceId, p_direction, newWsId); }

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
  if (m_view) { m_view->distributeViewSplits(); }

  if (hookMgr) {
    args[QStringLiteral("newWorkspaceId")] = newWsId;
    hookMgr->doAction(HookNames::ViewSplitAfterCreate, args);
  }
  emit viewSplitsCountChanged();
}

bool ViewAreaController::removeViewSplit(const QString &p_workspaceId,
                                         bool p_keepWorkspace, bool p_force) {
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (p_workspaceId.isEmpty() || totalSplitCount <= 1) { return false; }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  args[QStringLiteral("keepWorkspace")] = p_keepWorkspace;
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeRemove, args)) { return false; }

  if (p_keepWorkspace) {
    // Hide-only mode: transfer ViewWindows to WorkspaceWrapper, remove split widget.
    // Workspace becomes hidden but remains in vxcore.
    auto *wrapper = m_workspaces.value(p_workspaceId, nullptr);
    if (wrapper && wrapper->isVisible() && m_view) {
      int currentIndex = 0;
      QVector<QObject *> windows = m_view->takeViewWindowsFromSplit(
          p_workspaceId, &currentIndex);
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
        qInfo() << "ViewAreaController::removeViewSplit: closing window:" << winId
                << "in workspace:" << p_workspaceId;
        if (!closeViewWindow(winId, p_force)) {
          qInfo() << "ViewAreaController::removeViewSplit: close cancelled, aborting";
          m_suppressAutoRemove = false;
          return false;
        }
      }

      m_suppressAutoRemove = false;
    }

    // Remove remaining buffers and delete workspace in vxcore.
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
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
  if (m_view) { m_view->removeViewSplit(p_workspaceId); }
  if (hookMgr) { hookMgr->doAction(HookNames::ViewSplitAfterRemove, args); }
  emit viewSplitsCountChanged();
  return true;
}

void ViewAreaController::maximizeViewSplit(const QString &p_workspaceId) {
  if (m_view) { m_view->maximizeViewSplit(p_workspaceId); }
}

void ViewAreaController::distributeViewSplits() {
  if (m_view) { m_view->distributeViewSplits(); }
}

void ViewAreaController::moveViewWindowOneSplit(const QString &p_srcWorkspaceId,
                                                ID p_windowId, Direction p_direction,
                                                const QString &p_dstWorkspaceId,
                                                const QString &p_bufferId) {
  if (p_srcWorkspaceId.isEmpty() || p_windowId == InvalidViewWindowId
      || p_dstWorkspaceId.isEmpty()) { return; }

  // Cannot move to the same workspace.
  if (p_srcWorkspaceId == p_dstWorkspaceId) { return; }

  // Check if the buffer is already open in the destination workspace.
  // If so, just activate the existing tab instead of creating a duplicate.
  if (m_view && !p_bufferId.isEmpty()) {
    ID existingId = m_view->findWindowIdByBufferId(p_dstWorkspaceId, p_bufferId);
    if (existingId != InvalidViewWindowId) {
      qInfo() << "ViewAreaController::moveViewWindowOneSplit: buffer already in destination,"
              << "activating existing window:" << existingId;
      setCurrentViewSplit(p_dstWorkspaceId, true);
      setCurrentViewWindow(existingId, p_bufferId);
      return;
    }
  }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("windowId")] = p_windowId;
  args[QStringLiteral("srcWorkspaceId")] = p_srcWorkspaceId;
  args[QStringLiteral("dstWorkspaceId")] = p_dstWorkspaceId;
  args[QStringLiteral("direction")] = static_cast<int>(p_direction);
  args[QStringLiteral("bufferId")] = p_bufferId;
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeMove, args)) { return; }

  // Transfer buffer registration between workspaces in vxcore.
  if (m_shouldPropagateToCore && !p_bufferId.isEmpty()) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      wsSvc->removeBuffer(p_srcWorkspaceId, p_bufferId);
      wsSvc->addBuffer(p_dstWorkspaceId, p_bufferId);
    }
  }

  if (m_view) { m_view->moveViewWindowToSplit(p_windowId, p_srcWorkspaceId, p_dstWorkspaceId); }
  setCurrentViewSplit(p_dstWorkspaceId, true);
  if (hookMgr) { hookMgr->doAction(HookNames::ViewWindowAfterMove, args); }
  emit windowsChanged();
}

ID ViewAreaController::getCurrentWindowId() const { return m_currentWindowId; }

QString ViewAreaController::getCurrentWorkspaceId() const { return m_currentWorkspaceId; }

void ViewAreaController::setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) {
  if (m_currentWorkspaceId == p_workspaceId && !p_workspaceId.isEmpty()) {
    if (p_focus && m_view) { m_view->focusViewSplit(p_workspaceId); }
    return;
  }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeActivate, args)) { return; }
  m_currentWorkspaceId = p_workspaceId;
  if (m_shouldPropagateToCore) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc && !p_workspaceId.isEmpty()) { wsSvc->setCurrentWorkspace(p_workspaceId); }
  }
  if (m_view) { m_view->setCurrentViewSplit(p_workspaceId, p_focus); }
  if (p_focus && m_view) { m_view->focusViewSplit(p_workspaceId); }
  if (hookMgr) { hookMgr->doAction(HookNames::ViewSplitAfterActivate, args); }
}

void ViewAreaController::setCurrentViewWindow(ID p_windowId, const QString &p_bufferId) {
  if (m_currentWindowId == p_windowId) { return; }
  qInfo() << "ViewAreaController::setCurrentViewWindow: windowId:" << p_windowId
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

bool ViewAreaController::shouldPropagateToCore() const {
  return m_shouldPropagateToCore;
}

void ViewAreaController::newWorkspace(const QString &p_currentWorkspaceId) {
  if (p_currentWorkspaceId.isEmpty()) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
  if (newWsId.isEmpty()) {
    return;
  }

  // Create wrapper (not visible yet — switchWorkspace will manage visibility).
  auto *wrapper = new WorkspaceWrapper(newWsId, this);
  m_workspaces.insert(newWsId, wrapper);

  switchWorkspace(p_currentWorkspaceId, newWsId);
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
  // TODO(save-prompts): When ViewWindow2::aboutToClose() implements save prompts,
  // this loop will properly abort on cancel. Currently aboutToClose() always returns
  // true, so the abort path is untested with real user interaction.
  if (!bufferIds.isEmpty() && m_view) {
    m_suppressAutoRemove = true;

    QVector<ID> windowIds = m_view->getViewWindowIdsForWorkspace(p_workspaceId);
    for (ID winId : windowIds) {
      qInfo() << "ViewAreaController::removeWorkspace: closing window:" << winId
              << "in workspace:" << p_workspaceId;
      if (!closeViewWindow(winId, p_force)) {
        // Close was cancelled (hook or aboutToClose returned false). Abort.
        qInfo() << "ViewAreaController::removeWorkspace: close cancelled, aborting";
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
      qInfo() << "ViewAreaController::removeWorkspace: removing orphan buffer:" << bufferId;
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
      qInfo() << "ViewAreaController::removeWorkspace: switching to hidden workspace:"
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
      qInfo() << "ViewAreaController::removeWorkspace: no hidden workspace, removing split";
      if (m_view) { m_view->removeViewSplit(p_workspaceId); }
      emit viewSplitsCountChanged();
    } else {
      // Last split. Create a new empty workspace.
      qInfo() << "ViewAreaController::removeWorkspace: last split, creating new workspace";
      QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
      if (!newWsId.isEmpty()) {
        auto *newWrapper = new WorkspaceWrapper(newWsId, this);
        m_workspaces.insert(newWsId, newWrapper);
        switchWorkspace(p_workspaceId, newWsId);
      }
    }
  }

  // Step 5: Delete the workspace in vxcore.
  qInfo() << "ViewAreaController::removeWorkspace: deleting workspace:" << p_workspaceId;
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
      qWarning() << "ViewAreaController::switchWorkspace: target workspace"
                 << p_targetWorkspaceId << "is already visible in another split";
      return;
    }
  }

  auto *currentWrapper = m_workspaces.value(p_currentWorkspaceId, nullptr);
  auto *targetWrapper = m_workspaces.value(p_targetWorkspaceId, nullptr);
  if (!currentWrapper || !targetWrapper) {
    qWarning() << "ViewAreaController::switchWorkspace: missing wrapper for"
               << p_currentWorkspaceId << "or" << p_targetWorkspaceId;
    return;
  }

  // Guard: current workspace must be visible (displayed in a split).
  // This can happen if a stale signal triggers a switch after the workspace
  // was already hidden by a previous switch.
  if (!currentWrapper->isVisible()) {
    qWarning() << "ViewAreaController::switchWorkspace: current workspace"
               << p_currentWorkspaceId << "is not visible, ignoring";
    return;
  }

  qInfo() << "ViewAreaController::switchWorkspace:"
          << p_currentWorkspaceId << "->" << p_targetWorkspaceId
          << "currentWrapper visible:" << currentWrapper->isVisible()
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
    QVector<QObject *> windows = m_view->takeViewWindowsFromSplit(
        p_currentWorkspaceId, &currentIndex);
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

      qInfo() << "ViewAreaController::switchWorkspace: fresh workspace"
              << p_targetWorkspaceId << "buffers:" << bufferIds
              << "currentBufferId:" << currentBufferId;

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
  qInfo() << "ViewAreaController::setBufferOrder: workspace:" << p_workspaceId
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
  if (p_layout.isEmpty()) { return; }
  if (m_view) { m_view->loadLayout(p_layout); }
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

  // Log full vxcore workspace state at entry for debugging.
  QJsonArray allWsAtEntry = wsSvc->listWorkspaces();
  qInfo() << "ViewAreaController::restoreSession: vxcore has"
          << allWsAtEntry.size() << "workspaces, layout has"
          << p_layoutWorkspaceIds.size();
  for (int i = 0; i < allWsAtEntry.size(); ++i) {
    QJsonObject wsObj = allWsAtEntry[i].toObject();
    qInfo() << "  vxcore workspace:" << wsObj.value(QStringLiteral("id")).toString()
            << "name:" << wsObj.value(QStringLiteral("name")).toString()
            << "buffers:" << wsObj.value(QStringLiteral("bufferIds")).toArray();
  }

  // Determine the current workspace. If the saved current workspace is not in the
  // layout, fall back to the first workspace in the layout.
  QString restoredCurrentWsId = wsSvc->getCurrentWorkspace();
  if (!p_layoutWorkspaceIds.contains(restoredCurrentWsId)) {
    restoredCurrentWsId = p_layoutWorkspaceIds.first();
    qInfo() << "ViewAreaController::restoreSession: saved current workspace not in layout,"
            << "falling back to:" << restoredCurrentWsId;
  }

  qInfo() << "ViewAreaController::restoreSession: restoring workspaces from layout:"
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

    qInfo() << "  Workspace" << wsId
            << "name:" << wsConfig.value(QStringLiteral("name")).toString()
            << "buffers:" << bufferIds
            << "currentBuffer:" << currentBufferId;

    if (bufferIds.isEmpty()) {
      qInfo() << "    (no buffers, skipping)";
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
      qInfo() << "    Opening buffer" << bufferId
              << (bufferId == currentBufferId ? "(current)" : "(non-current)");
      openRestoredBuffer(bufferSvc, wsId, bufferId, false);
    }

    // After all buffers are opened in order, set the current buffer's tab as active.
    if (!currentBufferId.isEmpty() && m_view) {
      bool shouldFocus = (wsId == restoredCurrentWsId);
      qInfo() << "    Setting current buffer:" << currentBufferId
              << "focus:" << shouldFocus;
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
      qInfo() << "  Current workspace not restored, falling back to:" << restoredCurrentWsId;
    } else {
      restoredCurrentWsId.clear();
      qInfo() << "  No workspaces were restored";
    }
  }

  // Restore the current workspace from vxcore.
  // Clear m_currentWorkspaceId first so setCurrentViewSplit doesn't early-return
  // (it was already set during the loop). This ensures the view receives
  // setCurrentViewSplit() to mark the split as active.
  m_currentWorkspaceId.clear();
  if (!restoredCurrentWsId.isEmpty()) {
    qInfo() << "  Setting current workspace:" << restoredCurrentWsId;
    setCurrentViewSplit(restoredCurrentWsId, true);
  }

  qInfo() << "ViewAreaController::restoreSession: done";
  // Note: m_shouldPropagateToCore stays false. The caller is responsible for
  // re-enabling it after pending signals have been processed (e.g., via
  // QTimer::singleShot(0, ...)) to avoid deferred currentChanged signals
  // from addTab corrupting vxcore state.
}

void ViewAreaController::openRestoredBuffer(BufferService *p_bufferSvc,
                                            const QString &p_workspaceId,
                                            const QString &p_bufferId, bool p_focus) {
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

  qInfo() << "    Opening restored buffer: buffer:" << p_bufferId
          << "path:" << buf.nodeId().relativePath
          << "type:" << fileType << "workspace:" << p_workspaceId
          << "focus:" << p_focus;

  FileOpenSettings settings;
  settings.m_focus = p_focus;

  if (m_view) { m_view->openBuffer(buf, fileType, p_workspaceId, settings); }
}

void ViewAreaController::checkCurrentViewWindowChange(const QString &p_workspaceId) {
  Q_UNUSED(p_workspaceId)
  emit currentViewWindowChanged();
}

void ViewAreaController::subscribeToHooks() {
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    hookMgr->addAction(HookNames::FileAfterOpen,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_ctx)
          onFileAfterOpen(p_args);
        }, 10);
  }
}

void ViewAreaController::onFileAfterOpen(const QVariantMap &p_args) {
  const QString bufferId = p_args.value(QStringLiteral("bufferId")).toString();
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
  FileOpenSettings settings = FileOpenSettings::fromVariantMap(p_args);
  openBuffer(buf, settings);
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
