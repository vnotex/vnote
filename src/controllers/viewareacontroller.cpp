#include "viewareacontroller.h"

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
      wsId = wsSvc->createWorkspace(QStringLiteral("default"));
    }
    emit addFirstViewSplitRequested(wsId);
  }

  if (m_currentWorkspaceId.isEmpty()) {
    qWarning() << "ViewAreaController: no current workspace";
    return;
  }
  emit openBufferRequested(p_buffer, fileType, m_currentWorkspaceId, p_settings);
}

void ViewAreaController::onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                                            const FileOpenSettings &p_settings) {
  if (p_windowId == InvalidViewWindowId) { return; }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc && !m_currentWorkspaceId.isEmpty()) {
    wsSvc->addBuffer(m_currentWorkspaceId, p_buffer.id());
    wsSvc->setCurrentBuffer(m_currentWorkspaceId, p_buffer.id());
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
    emit setCurrentViewSplitRequested(m_currentWorkspaceId, true);
  }
  emit windowsChanged();
  emit currentViewWindowChanged();
}

void ViewAreaController::onViewWindowClosed(ID p_windowId, const QString &p_bufferId,
                                            const QString &p_workspaceId) {
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc && !p_workspaceId.isEmpty() && !p_bufferId.isEmpty()) {
    wsSvc->removeBuffer(p_workspaceId, p_bufferId);
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
  emit closeViewWindowRequested(p_windowId, p_force);
  return true;
}

bool ViewAreaController::closeAll(const QVector<QString> &p_workspaceIds, bool p_force) {
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
  if (wsSvc) { newWsId = wsSvc->createWorkspace(QStringLiteral("split")); }
  emit splitRequested(p_workspaceId, p_direction, newWsId);
  if (hookMgr) {
    args[QStringLiteral("newWorkspaceId")] = newWsId;
    hookMgr->doAction(HookNames::ViewSplitAfterCreate, args);
  }
  emit viewSplitsCountChanged();
}

bool ViewAreaController::removeViewSplit(const QString &p_workspaceId, bool p_force,
                                         int p_totalSplitCount) {
  Q_UNUSED(p_force)
  if (p_workspaceId.isEmpty() || p_totalSplitCount <= 1) { return false; }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeRemove, args)) { return false; }
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) { wsSvc->deleteWorkspace(p_workspaceId); }
  if (m_currentWorkspaceId == p_workspaceId) {
    m_currentWorkspaceId.clear();
    m_currentWindowId = InvalidViewWindowId;
  }
  emit removeViewSplitRequested(p_workspaceId);
  if (hookMgr) { hookMgr->doAction(HookNames::ViewSplitAfterRemove, args); }
  emit viewSplitsCountChanged();
  return true;
}

void ViewAreaController::maximizeViewSplit(const QString &p_workspaceId) {
  emit maximizeViewSplitRequested(p_workspaceId);
}

void ViewAreaController::distributeViewSplits() {
  emit distributeViewSplitsRequested();
}

void ViewAreaController::moveViewWindowOneSplit(const QString &p_srcWorkspaceId,
                                                ID p_windowId, Direction p_direction,
                                                const QString &p_dstWorkspaceId) {
  if (p_srcWorkspaceId.isEmpty() || p_windowId == InvalidViewWindowId
      || p_dstWorkspaceId.isEmpty()) { return; }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("windowId")] = p_windowId;
  args[QStringLiteral("srcWorkspaceId")] = p_srcWorkspaceId;
  args[QStringLiteral("dstWorkspaceId")] = p_dstWorkspaceId;
  args[QStringLiteral("direction")] = static_cast<int>(p_direction);
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeMove, args)) { return; }
  // Buffer transfer deferred to ViewArea2 (windowId->ViewWindow2* is a view concern).
  emit moveViewWindowToSplitRequested(p_windowId, p_srcWorkspaceId, p_dstWorkspaceId);
  setCurrentViewSplit(p_dstWorkspaceId, true);
  if (hookMgr) { hookMgr->doAction(HookNames::ViewWindowAfterMove, args); }
  emit windowsChanged();
}

ID ViewAreaController::getCurrentWindowId() const { return m_currentWindowId; }

QString ViewAreaController::getCurrentWorkspaceId() const { return m_currentWorkspaceId; }

void ViewAreaController::setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) {
  if (m_currentWorkspaceId == p_workspaceId && !p_workspaceId.isEmpty()) {
    if (p_focus) { emit focusViewSplitRequested(p_workspaceId); }
    return;
  }
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_workspaceId;
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeActivate, args)) { return; }
  m_currentWorkspaceId = p_workspaceId;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc && !p_workspaceId.isEmpty()) { wsSvc->setCurrentWorkspace(p_workspaceId); }
  emit setCurrentViewSplitRequested(p_workspaceId, p_focus);
  if (p_focus) { emit focusViewSplitRequested(p_workspaceId); }
  if (hookMgr) { hookMgr->doAction(HookNames::ViewSplitAfterActivate, args); }
}

void ViewAreaController::setCurrentViewWindow(ID p_windowId) {
  if (m_currentWindowId == p_windowId) { return; }
  m_currentWindowId = p_windowId;
  emit currentViewWindowChanged();
}

void ViewAreaController::focus() {
  if (!m_currentWorkspaceId.isEmpty()) {
    emit focusViewSplitRequested(m_currentWorkspaceId);
  }
}

QJsonObject ViewAreaController::saveLayout(const QJsonObject &p_widgetTree) const {
  QJsonObject layout;
  layout[QStringLiteral("splitterTree")] = p_widgetTree;
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) { layout[QStringLiteral("currentWorkspaceId")] = wsSvc->getCurrentWorkspace(); }
  return layout;
}

void ViewAreaController::loadLayout(const QJsonObject &p_layout) {
  if (p_layout.isEmpty()) { return; }
  emit loadLayoutRequested(p_layout);
  emit viewSplitsCountChanged();
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
