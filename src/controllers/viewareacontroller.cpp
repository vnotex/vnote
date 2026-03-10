#include "viewareacontroller.h"

#include <QJsonArray>
#include <QSplitter>
#include <QVariantMap>

#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/workspacecoreservice.h>
#include <core/viewwindowfactory.h>
#include <widgets/viewsplit2.h>
#include <widgets/viewwindow2.h>

using namespace vnotex;

ViewAreaController::ViewAreaController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
}

ViewAreaController::~ViewAreaController() {
}

void ViewAreaController::openBuffer(const Buffer2 &p_buffer, const QString &p_fileType) {
  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("fileType")] = p_fileType;
  args[QStringLiteral("bufferId")] = p_buffer.id();
  args[QStringLiteral("nodeId")] = p_buffer.nodeId().relativePath;
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeOpen, args)) {
    return;
  }

  auto *factory = m_services.get<ViewWindowFactory>();
  if (!factory || !factory->hasCreator(p_fileType)) {
    qWarning() << "ViewAreaController: no creator for file type" << p_fileType;
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();

  // If there is no current split, ask the view to create the first one.
  if (!m_currentSplit) {
    QString wsId;
    if (wsSvc) {
      wsId = wsSvc->createWorkspace(QStringLiteral("default"));
    }
    // Emit signal — ViewArea2 will create the split and call setCurrentViewSplit().
    // Signal-slot with direct connection means m_currentSplit is set when we return.
    emit addFirstViewSplitRequested(wsId);
  }

  if (!m_currentSplit) {
    qWarning() << "ViewAreaController: no current split after addFirstViewSplitRequested";
    return;
  }

  auto *win = factory->create(p_fileType, m_services, p_buffer, m_currentSplit);
  if (!win) {
    qWarning() << "ViewAreaController: failed creating view window for type" << p_fileType;
    return;
  }

  if (wsSvc) {
    wsSvc->addBuffer(m_currentSplit->getWorkspaceId(), p_buffer.id());
  }

  // Tell the view to add this window to the current split.
  emit viewWindowCreated(win, m_currentSplit);

  setCurrentViewSplit(m_currentSplit, true);
  setCurrentViewWindow(win);

  if (hookMgr) {
    hookMgr->doAction(HookNames::ViewWindowAfterOpen, args);
  }

  emit windowsChanged();
}

bool ViewAreaController::closeViewWindow(ViewWindow2 *p_win, bool p_force) {
  if (!p_win) {
    return false;
  }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("bufferId")] = p_win->getBuffer().id();
  args[QStringLiteral("nodeId")] = p_win->getNodeId().relativePath;
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeClose, args)) {
    return false;
  }

  if (!p_win->aboutToClose(p_force)) {
    return false;
  }

  // Find the owner split by walking the parent chain.
  ViewSplit2 *ownerSplit = qobject_cast<ViewSplit2 *>(p_win->parentWidget());
  if (ownerSplit) {
    ownerSplit->takeViewWindow(p_win);

    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      wsSvc->removeBuffer(ownerSplit->getWorkspaceId(), p_win->getBuffer().id());
    }
  }

  delete p_win;

  if (hookMgr) {
    hookMgr->doAction(HookNames::ViewWindowAfterClose, args);
  }

  checkCurrentViewWindowChange(m_currentSplit);
  emit windowsChanged();
  return true;
}

bool ViewAreaController::closeAll(const QVector<ViewSplit2 *> &p_splits, bool p_force) {
  for (auto *split : p_splits) {
    const auto windows = split->getAllViewWindows();
    for (auto *win : windows) {
      if (!closeViewWindow(win, p_force) && !p_force) {
        return false;
      }
    }
  }

  return true;
}

void ViewAreaController::splitViewSplit(ViewSplit2 *p_split, Direction p_direction) {
  if (!p_split) {
    return;
  }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_split->getWorkspaceId();
  args[QStringLiteral("direction")] = static_cast<int>(p_direction);
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeCreate, args)) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  QString wsId;
  if (wsSvc) {
    wsId = wsSvc->createWorkspace(QStringLiteral("split"));
  }

  // Tell the view to create the split. View will call connectSplitSignals on the new split.
  emit splitRequested(p_split, p_direction, wsId);

  if (hookMgr) {
    hookMgr->doAction(HookNames::ViewSplitAfterCreate, args);
  }

  // The newly created split becomes the current split via setCurrentViewSplit
  // (called by ViewArea2 after handling splitRequested).
  emit viewSplitsCountChanged();
}

bool ViewAreaController::removeViewSplit(ViewSplit2 *p_split, bool p_force, int p_totalSplitCount) {
  if (!p_split) {
    return false;
  }

  // Cannot remove the last split.
  if (p_totalSplitCount <= 1) {
    return false;
  }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("workspaceId")] = p_split->getWorkspaceId();
  if (hookMgr && hookMgr->doAction(HookNames::ViewSplitBeforeRemove, args)) {
    return false;
  }

  // Close all windows in this split first.
  const auto windows = p_split->getAllViewWindows();
  for (auto *win : windows) {
    if (!closeViewWindow(win, p_force) && !p_force) {
      return false;
    }
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    wsSvc->deleteWorkspace(p_split->getWorkspaceId());
  }

  // If we're removing the current split, clear tracking.
  if (m_currentSplit == p_split) {
    m_currentSplit = nullptr;
  }

  // Tell the view to remove the split from layout and delete it.
  emit removeViewSplitRequested(p_split);

  if (hookMgr) {
    hookMgr->doAction(HookNames::ViewSplitAfterRemove, args);
  }

  emit viewSplitsCountChanged();
  return true;
}

void ViewAreaController::maximizeViewSplit(ViewSplit2 *p_split) {
  emit maximizeViewSplitRequested(p_split);
}

void ViewAreaController::distributeViewSplits() {
  emit distributeViewSplitsRequested();
}

void ViewAreaController::moveViewWindowOneSplit(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                                Direction p_direction,
                                                ViewSplit2 *p_targetSplit) {
  if (!p_split || !p_win || !p_targetSplit) {
    return;
  }

  auto *hookMgr = m_services.get<HookManager>();
  QVariantMap args;
  args[QStringLiteral("bufferId")] = p_win->getBuffer().id();
  args[QStringLiteral("direction")] = static_cast<int>(p_direction);
  if (hookMgr && hookMgr->doAction(HookNames::ViewWindowBeforeMove, args)) {
    return;
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    wsSvc->removeBuffer(p_split->getWorkspaceId(), p_win->getBuffer().id());
    wsSvc->addBuffer(p_targetSplit->getWorkspaceId(), p_win->getBuffer().id());
  }

  // Tell the view to reparent the window.
  emit moveViewWindowToSplit(p_split, p_win, p_targetSplit);

  setCurrentViewSplit(p_targetSplit, true);

  if (hookMgr) {
    hookMgr->doAction(HookNames::ViewWindowAfterMove, args);
  }

  emit windowsChanged();
}

ViewWindow2 *ViewAreaController::getCurrentViewWindow() const {
  return m_currentWindow;
}

void ViewAreaController::setCurrentViewSplit(ViewSplit2 *p_split, bool p_focus) {
  m_currentSplit = p_split;

  // Tell the view to update visual active state.
  emit setCurrentViewSplitRequested(p_split, p_focus);

  checkCurrentViewWindowChange(p_split);
}

void ViewAreaController::setCurrentViewWindow(ViewWindow2 *p_win) {
  m_currentWindow = p_win;
  checkCurrentViewWindowChange(m_currentSplit);
}

void ViewAreaController::focus() {
  if (m_currentSplit) {
    emit focusViewSplitRequested(m_currentSplit);
  }
}

void ViewAreaController::checkCurrentViewWindowChange(ViewSplit2 *p_activeSplit) {
  ViewWindow2 *activeWindow = nullptr;
  if (p_activeSplit) {
    activeWindow = p_activeSplit->getCurrentViewWindow();
  }

  if (activeWindow != m_currentWindow) {
    m_currentWindow = activeWindow;
    emit currentViewWindowChanged();
  }
}

void ViewAreaController::onFileAfterOpen(const QVariantMap &p_args) {
  const QString bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  const QString notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  const QString filePath = p_args.value(QStringLiteral("filePath")).toString();

  if (bufferId.isEmpty()) {
    qWarning() << "ViewAreaController::onFileAfterOpen: empty bufferId";
    return;
  }

  // Construct NodeIdentifier from hook args.
  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = filePath;

  // Get a Buffer2 handle for the already-open buffer.
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    qWarning() << "ViewAreaController::onFileAfterOpen: BufferService not available";
    return;
  }
  Buffer2 buf = bufferSvc->getBufferHandle(bufferId, nodeId);
  if (!buf.isValid()) {
    qWarning() << "ViewAreaController::onFileAfterOpen: invalid buffer handle for" << bufferId;
    return;
  }

  // Determine file type from file path.
  QString fileType;
  auto *ftSvc = m_services.get<FileTypeCoreService>();
  if (ftSvc) {
    FileType ft = ftSvc->getFileType(filePath);
    fileType = ft.m_typeName;
  }
  if (fileType.isEmpty()) {
    fileType = QStringLiteral("text");
  }

  // Create ViewWindow2 via the existing openBuffer() method.
  openBuffer(buf, fileType);
}

void ViewAreaController::connectSplitSignals(ViewSplit2 *p_split) {
  connect(p_split, &ViewSplit2::focused, this,
          [this](ViewSplit2 *s) { setCurrentViewSplit(s, false); });
  connect(p_split, &ViewSplit2::viewWindowCloseRequested, this,
          [this](ViewWindow2 *w) { closeViewWindow(w, false); });
  connect(p_split, &ViewSplit2::currentViewWindowChanged, this,
          [this](ViewWindow2 *w) { setCurrentViewWindow(w); });
  connect(p_split, &ViewSplit2::splitRequested, this,
          [this](ViewSplit2 *s, Direction d) { splitViewSplit(s, d); });
  // removeSplitRequested is handled by ViewArea2, not controller directly,
  // because ViewArea2 knows the total split count.
  connect(p_split, &ViewSplit2::maximizeSplitRequested, this,
          [this](ViewSplit2 *s) { maximizeViewSplit(s); });
  connect(p_split, &ViewSplit2::distributeSplitsRequested, this,
          [this]() { distributeViewSplits(); });
  // moveViewWindowOneSplitRequested is handled by ViewArea2, not controller directly,
  // because ViewArea2 needs to resolve the target split first.
}

QJsonObject ViewAreaController::saveLayout(
    const QVector<ViewSplit2 *> &p_splits,
    const std::function<QJsonObject(const QWidget *)> &p_serializeWidget) const {
  QJsonObject layout;

  // The view provides the serialized widget tree — we just store workspace metadata.
  // Find the top widget by walking up from the first split.
  if (!p_splits.isEmpty()) {
    QWidget *topWidget = p_splits.first();
    // Walk to the topmost splitter.
    while (auto *parentSplitter = qobject_cast<QSplitter *>(topWidget->parentWidget())) {
      // Stop if we hit the ViewArea2 itself (its parent is the layout).
      if (!qobject_cast<QSplitter *>(parentSplitter->parentWidget())) {
        topWidget = parentSplitter;
        break;
      }
      topWidget = parentSplitter;
    }
    layout[QStringLiteral("splitterTree")] = p_serializeWidget(topWidget);
  }

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    layout[QStringLiteral("currentWorkspaceId")] = wsSvc->getCurrentWorkspace();
  }

  return layout;
}

void ViewAreaController::loadLayout(const QJsonObject &p_layout) {
  if (p_layout.isEmpty()) {
    return;
  }

  // Emit signal — ViewArea2 handles the actual widget creation from JSON.
  emit loadLayoutRequested(p_layout);

  emit viewSplitsCountChanged();
}

void ViewAreaController::subscribeToHooks() {
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    hookMgr->addAction(
        HookNames::FileAfterOpen,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_ctx);
          onFileAfterOpen(p_args);
        },
        10);
  }
}
