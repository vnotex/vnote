#include "viewareacontroller.h"

#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>

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
    if (!m_view) { return; }
    m_view->addFirstViewSplit(wsId);
  }

  if (m_currentWorkspaceId.isEmpty()) {
    qWarning() << "ViewAreaController: no current workspace";
    return;
  }
  if (!m_view) { return; }
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
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (m_shouldPropagateToCore && !p_workspaceId.isEmpty() && totalSplitCount > 1) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      if (bufferIds.isEmpty()) {
        qInfo() << "ViewAreaController: workspace" << p_workspaceId
                << "is empty, removing split";
        removeViewSplit(p_workspaceId, true);
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
  m_view->closeViewWindow(p_windowId, p_force);
  return true;
}

bool ViewAreaController::closeAll(const QVector<QString> &p_workspaceIds, bool p_force) {
  // Close all view windows in the given workspaces.
  // TODO: Implement proper close-all with unsaved-changes handling.
  Q_UNUSED(p_workspaceIds) Q_UNUSED(p_force)
  return true;
}

void ViewAreaController::splitViewSplit(const QString &p_workspaceId, Direction p_direction) {
  if (p_workspaceId.isEmpty()) { return; }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  args[QStringLiteral("direction")] = static_cast<int>(p_direction);
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeCreate, args)) { return; }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  QString newWsId;
  if (wsSvc) { newWsId = wsSvc->createWorkspace(generateWorkspaceName()); }
  if (m_view) { m_view->split(p_workspaceId, p_direction, newWsId); }
  if (hookMgr) {
    args[QStringLiteral("newWorkspaceId")] = newWsId;
    hookMgr->doAction(HookNames::ViewSplitAfterCreate, args);
  }
  emit viewSplitsCountChanged();
}

bool ViewAreaController::removeViewSplit(const QString &p_workspaceId, bool p_force) {
  Q_UNUSED(p_force)
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
  if (p_workspaceId.isEmpty() || totalSplitCount <= 1) { return false; }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeRemove, args)) { return false; }
  if (m_shouldPropagateToCore) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      // Close buffers that belong to this workspace before deleting it.
      QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
      QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
      auto *bufferSvc = m_services.get<BufferService>();
      if (bufferSvc) {
        for (int i = 0; i < bufferIds.size(); ++i) {
          QString bufferId = bufferIds[i].toString();
          if (!bufferId.isEmpty()) {
            bufferSvc->closeBuffer(bufferId);
          }
        }
      }
      wsSvc->deleteWorkspace(p_workspaceId);
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

  switchWorkspace(p_currentWorkspaceId, newWsId);
}

void ViewAreaController::removeWorkspace(QString p_workspaceId) {
  if (p_workspaceId.isEmpty()) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (!wsSvc) {
    return;
  }

  // Collect buffer IDs of the workspace being removed.
  QJsonObject wsConfig = wsSvc->getWorkspace(p_workspaceId);
  QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();

  // Build a set of buffer IDs that exist in OTHER workspaces (to avoid closing shared buffers).
  QSet<QString> sharedBufferIds;
  QJsonArray allWorkspaces = wsSvc->listWorkspaces();
  for (int i = 0; i < allWorkspaces.size(); ++i) {
    QJsonObject otherWs = allWorkspaces[i].toObject();
    QString otherWsId = otherWs.value(QStringLiteral("id")).toString();
    if (otherWsId == p_workspaceId) {
      continue;
    }
    QJsonArray otherBufferIds = otherWs.value(QStringLiteral("bufferIds")).toArray();
    for (int j = 0; j < otherBufferIds.size(); ++j) {
      sharedBufferIds.insert(otherBufferIds[j].toString());
    }
  }

  // Close buffers that are exclusive to this workspace.
  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    for (int i = 0; i < bufferIds.size(); ++i) {
      QString bufferId = bufferIds[i].toString();
      if (!bufferId.isEmpty() && !sharedBufferIds.contains(bufferId)) {
        qInfo() << "ViewAreaController::removeWorkspace: closing exclusive buffer:" << bufferId;
        bufferSvc->closeBuffer(bufferId);
      }
    }
  }

  // Find a hidden workspace to switch to (one not visible in any split).
  QStringList visibleWorkspaceIds = m_view ? m_view->getVisibleWorkspaceIds() : QStringList();
  int totalSplitCount = m_view ? m_view->getViewSplitCount() : 0;
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
    // Switch to the hidden workspace, then delete the old one.
    qInfo() << "ViewAreaController::removeWorkspace: switching to hidden workspace:" << targetWsId;
    switchWorkspace(p_workspaceId, targetWsId);
  } else if (totalSplitCount > 1) {
    // No hidden workspace available. Remove the split entirely.
    qInfo() << "ViewAreaController::removeWorkspace: no hidden workspace, removing split";
    removeViewSplit(p_workspaceId, true);
    // deleteWorkspace is already called by removeViewSplit.
    return;
  } else {
    // Last split, no hidden workspace. Just clear the tabs (switchWorkspace
    // to nothing is not possible). The workspace stays but is now empty.
    qInfo() << "ViewAreaController::removeWorkspace: last split, clearing workspace";
    // The view windows are still shown — switch to
    // a new empty workspace to clear them.
    QString newWsId = wsSvc->createWorkspace(generateWorkspaceName());
    if (!newWsId.isEmpty()) {
      switchWorkspace(p_workspaceId, newWsId);
    }
  }

  // Delete the removed workspace from vxcore.
  qInfo() << "ViewAreaController::removeWorkspace: deleting workspace:" << p_workspaceId;
  wsSvc->deleteWorkspace(p_workspaceId);
}

void ViewAreaController::switchWorkspace(const QString &p_currentWorkspaceId,
                                          const QString &p_targetWorkspaceId) {
  if (p_currentWorkspaceId.isEmpty() || p_targetWorkspaceId.isEmpty()) {
    return;
  }
  if (p_currentWorkspaceId == p_targetWorkspaceId) {
    return;
  }

  // Suppress vxcore propagation during tab teardown/rebuild to prevent
  // currentChanged signals from corrupting workspace state.
  bool wasPropagating = m_shouldPropagateToCore;
  m_shouldPropagateToCore = false;

  // View clears old tabs and updates mapping.
  if (m_view) { m_view->switchWorkspace(p_currentWorkspaceId, p_targetWorkspaceId); }
  setCurrentViewSplit(p_targetWorkspaceId, true);

  // Open buffers belonging to the new workspace (if any).
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  auto *bufferSvc = m_services.get<BufferService>();
  if (wsSvc && bufferSvc) {
    QJsonObject wsConfig = wsSvc->getWorkspace(p_targetWorkspaceId);
    QJsonArray bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
    QString currentBufferId = wsConfig.value(QStringLiteral("currentBufferId")).toString();

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

  m_shouldPropagateToCore = wasPropagating;
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
