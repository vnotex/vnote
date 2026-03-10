#include "viewarea2.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSplitter>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <controllers/viewareacontroller.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/workspacecoreservice.h>
#include <core/viewwindowfactory.h>

#include "viewareahomewidget.h"
#include "viewsplit2.h"
#include "viewwindow2.h"

using namespace vnotex;

ViewArea2::ViewArea2(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  setContentsMargins(0, 0, 0, 0);

  m_stackedLayout = new QStackedLayout(this);
  m_stackedLayout->setContentsMargins(0, 0, 0, 0);

  // Page 0: Home screen.
  m_homeWidget = new ViewAreaHomeWidget(this);
  m_stackedLayout->addWidget(m_homeWidget);

  // Page 1: Contents screen (container for the splitter tree).
  m_contentsWidget = new QWidget(this);
  m_contentsWidget->setContentsMargins(0, 0, 0, 0);
  m_contentsLayout = new QVBoxLayout(m_contentsWidget);
  m_contentsLayout->setContentsMargins(0, 0, 0, 0);
  m_stackedLayout->addWidget(m_contentsWidget);

  // Start on the Home screen.
  m_stackedLayout->setCurrentIndex(HomeScreen);

  setupController();
}

ViewArea2::~ViewArea2() {
}

void ViewArea2::setupController() {
  m_controller = new ViewAreaController(m_services, this);

  connect(m_controller, &ViewAreaController::addFirstViewSplitRequested,
          this, &ViewArea2::onAddFirstViewSplitRequested);
  connect(m_controller, &ViewAreaController::splitRequested,
          this, &ViewArea2::onSplitRequested);
  connect(m_controller, &ViewAreaController::removeViewSplitRequested,
          this, &ViewArea2::onRemoveViewSplitRequested);
  connect(m_controller, &ViewAreaController::maximizeViewSplitRequested,
          this, &ViewArea2::onMaximizeViewSplitRequested);
  connect(m_controller, &ViewAreaController::distributeViewSplitsRequested,
          this, &ViewArea2::onDistributeViewSplitsRequested);
  connect(m_controller, &ViewAreaController::openBufferRequested,
          this, &ViewArea2::onOpenBufferRequested);
  connect(m_controller, &ViewAreaController::closeViewWindowRequested,
          this, &ViewArea2::onCloseViewWindowRequested);
  connect(m_controller, &ViewAreaController::setCurrentViewSplitRequested,
          this, &ViewArea2::onSetCurrentViewSplitRequested);
  connect(m_controller, &ViewAreaController::focusViewSplitRequested,
          this, &ViewArea2::onFocusViewSplitRequested);
  connect(m_controller, &ViewAreaController::moveViewWindowToSplitRequested,
          this, &ViewArea2::onMoveViewWindowToSplitRequested);
  connect(m_controller, &ViewAreaController::loadLayoutRequested,
          this, &ViewArea2::onLoadLayoutRequested);

  m_controller->subscribeToHooks();
}

ViewAreaController *ViewArea2::getController() const {
  return m_controller;
}

// ============ Split Factory ============

ViewSplit2 *ViewArea2::createViewSplit(const QString &p_workspaceId) {
  return new ViewSplit2(m_services, p_workspaceId, nullptr);
}

QSplitter *ViewArea2::createSplitter(Qt::Orientation p_orientation) {
  auto *splitter = new QSplitter(p_orientation, nullptr);
  splitter->setChildrenCollapsible(false);
  return splitter;
}

// ============ Layout Management ============

ViewSplit2 *ViewArea2::addFirstViewSplit(const QString &p_workspaceId) {
  Q_ASSERT(!m_topWidget);
  auto *split = createViewSplit(p_workspaceId);
  m_splits[p_workspaceId] = split;
  setTopWidget(split);
  return split;
}

ViewSplit2 *ViewArea2::splitAt(ViewSplit2 *p_split, Direction p_direction,
                               const QString &p_workspaceId) {
  Qt::Orientation orient =
      (p_direction == Direction::Left || p_direction == Direction::Right)
      ? Qt::Horizontal : Qt::Vertical;
  bool insertBefore = (p_direction == Direction::Left || p_direction == Direction::Up);

  auto *newSplit = createViewSplit(p_workspaceId);
  m_splits[p_workspaceId] = newSplit;
  auto *parentSplitter = findParentSplitter(p_split);

  if (!parentSplitter) {
    auto *splitter = createSplitter(orient);
    m_contentsLayout->removeWidget(p_split);
    if (insertBefore) {
      splitter->addWidget(newSplit);
      splitter->addWidget(p_split);
    } else {
      splitter->addWidget(p_split);
      splitter->addWidget(newSplit);
    }
    setTopWidget(splitter);
  } else if (parentSplitter->orientation() == orient) {
    int idx = parentSplitter->indexOf(p_split);
    int insertIdx = insertBefore ? idx : idx + 1;
    parentSplitter->insertWidget(insertIdx, newSplit);
  } else {
    int idx = parentSplitter->indexOf(p_split);
    auto *subSplitter = createSplitter(orient);
    parentSplitter->insertWidget(idx, subSplitter);
    if (insertBefore) {
      subSplitter->addWidget(newSplit);
      subSplitter->addWidget(p_split);
    } else {
      subSplitter->addWidget(p_split);
      subSplitter->addWidget(newSplit);
    }
  }

  return newSplit;
}

void ViewArea2::removeViewSplit(ViewSplit2 *p_split) {
  auto *parentSplitter = findParentSplitter(p_split);
  if (!parentSplitter) {
    m_contentsLayout->removeWidget(p_split);
    p_split->setParent(nullptr);
    m_topWidget = nullptr;
    return;
  }
  p_split->setParent(nullptr);
  if (parentSplitter->count() == 1) {
    unwrapSingleChildSplitter(parentSplitter);
  }
}

void ViewArea2::maximizeViewSplit(ViewSplit2 *p_split) {
  QWidget *target = p_split;
  auto *splitter = findParentSplitter(target);
  while (splitter) {
    const auto currentSizes = splitter->sizes();
    QList<int> sizes;
    int totalSize = 0;
    for (int i = 0; i < splitter->count(); ++i) { totalSize += currentSizes.value(i); }
    int targetIdx = splitter->indexOf(target);
    for (int i = 0; i < splitter->count(); ++i) {
      sizes.append(i == targetIdx ? totalSize : 0);
    }
    splitter->setSizes(sizes);
    target = splitter;
    splitter = findParentSplitter(target);
  }
}

void ViewArea2::distributeViewSplits() {
  auto *splitter = qobject_cast<QSplitter *>(m_topWidget);
  if (splitter) { distributeSplitter(splitter); }
}

ViewSplit2 *ViewArea2::findSplitByDirection(ViewSplit2 *p_split, Direction p_direction) const {
  Qt::Orientation orient =
      (p_direction == Direction::Left || p_direction == Direction::Right)
      ? Qt::Horizontal : Qt::Vertical;
  int step = (p_direction == Direction::Right || p_direction == Direction::Down) ? 1 : -1;

  QWidget *target = p_split;
  auto *splitter = findParentSplitter(target);
  while (splitter) {
    if (splitter->orientation() == orient) {
      int idx = splitter->indexOf(target);
      int newIdx = idx + step;
      if (newIdx >= 0 && newIdx < splitter->count()) {
        QWidget *adjacent = splitter->widget(newIdx);
        return (step > 0) ? findFirstViewSplit(adjacent) : findLastViewSplit(adjacent);
      }
    }
    target = splitter;
    splitter = findParentSplitter(target);
  }
  return nullptr;
}

// ============ State ============

QVector<ViewSplit2 *> ViewArea2::getAllViewSplits() const {
  QVector<ViewSplit2 *> splits;
  if (m_topWidget) { collectViewSplits(m_topWidget, splits); }
  return splits;
}

ViewSplit2 *ViewArea2::getCurrentViewSplit() const {
  for (auto *split : getAllViewSplits()) {
    if (split->isActive()) { return split; }
  }
  return nullptr;
}

int ViewArea2::getViewSplitCount() const { return getAllViewSplits().size(); }

// ============ Top Widget ============

QWidget *ViewArea2::getTopWidget() const { return m_topWidget; }

void ViewArea2::setTopWidget(QWidget *p_widget) {
  if (m_topWidget) { m_contentsLayout->removeWidget(m_topWidget); }
  m_topWidget = p_widget;
  if (m_topWidget) { m_contentsLayout->addWidget(m_topWidget); }
}

QJsonObject ViewArea2::saveLayout() const {
  QJsonObject widgetTree;
  if (m_topWidget) { widgetTree = serializeWidget(m_topWidget); }
  return m_controller->saveLayout(widgetTree);
}

void ViewArea2::loadLayout(const QJsonObject &p_layout) {
  if (!p_layout.isEmpty()) { m_controller->loadLayout(p_layout); }
}

// ============ ID resolution helpers ============

ViewSplit2 *ViewArea2::splitForWorkspace(const QString &p_workspaceId) const {
  return m_splits.value(p_workspaceId, nullptr);
}

ViewWindow2 *ViewArea2::windowForId(ID p_windowId) const {
  return m_windows.value(p_windowId, nullptr);
}

ID ViewArea2::idForWindow(ViewWindow2 *p_win) const {
  return m_windows.key(p_win, ViewAreaController::InvalidViewWindowId);
}

// ============ wireSplitSignals ============

void ViewArea2::wireSplitSignals(ViewSplit2 *p_split) {
  const QString wsId = p_split->getWorkspaceId();

  // Signals forwarded to controller (ID-based).
  connect(p_split, &ViewSplit2::focused, this,
          [this, wsId](ViewSplit2 *) {
            m_controller->setCurrentViewSplit(wsId, false);
          });
  connect(p_split, &ViewSplit2::viewWindowCloseRequested, this,
          [this](ViewWindow2 *w) {
            m_controller->closeViewWindow(idForWindow(w), false);
          });
  connect(p_split, &ViewSplit2::currentViewWindowChanged, this,
          [this](ViewWindow2 *w) {
            m_controller->setCurrentViewWindow(idForWindow(w));
          });
  connect(p_split, &ViewSplit2::splitRequested, this,
          [this, wsId](ViewSplit2 *, Direction d) {
            m_controller->splitViewSplit(wsId, d);
          });
  connect(p_split, &ViewSplit2::maximizeSplitRequested, this,
          [this, wsId](ViewSplit2 *) {
            m_controller->maximizeViewSplit(wsId);
          });
  connect(p_split, &ViewSplit2::distributeSplitsRequested, this,
          [this]() {
            m_controller->distributeViewSplits();
          });

  // Signals handled by ViewArea2 (need pointer context).
  connect(p_split, &ViewSplit2::removeSplitRequested, this,
          &ViewArea2::onRemoveSplitRequested);
  connect(p_split, &ViewSplit2::moveViewWindowOneSplitRequested, this,
          &ViewArea2::onMoveViewWindowOneSplitRequested);
}

// ============ Controller signal handlers ============

void ViewArea2::onAddFirstViewSplitRequested(const QString &p_workspaceId) {
  auto *split = addFirstViewSplit(p_workspaceId);
  wireSplitSignals(split);
  m_controller->setCurrentViewSplit(p_workspaceId, true);
  updateScreenVisibility();
}

void ViewArea2::onSplitRequested(const QString &p_workspaceId, Direction p_direction,
                                  const QString &p_newWorkspaceId) {
  auto *existingSplit = splitForWorkspace(p_workspaceId);
  if (!existingSplit) {
    qWarning() << "ViewArea2::onSplitRequested: workspace not found:" << p_workspaceId;
    return;
  }
  auto *newSplit = splitAt(existingSplit, p_direction, p_newWorkspaceId);
  wireSplitSignals(newSplit);
  m_controller->setCurrentViewSplit(p_newWorkspaceId, true);
}

void ViewArea2::onRemoveViewSplitRequested(const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) { return; }
  m_splits.remove(p_workspaceId);
  removeViewSplit(split);
  delete split;
}

void ViewArea2::onMaximizeViewSplitRequested(const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (split) { maximizeViewSplit(split); }
}

void ViewArea2::onDistributeViewSplitsRequested() {
  distributeViewSplits();
}

void ViewArea2::onOpenBufferRequested(const Buffer2 &p_buffer, const QString &p_fileType,
                                       const QString &p_workspaceId,
                                       const FileOpenSettings &p_settings) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    qWarning() << "ViewArea2::onOpenBufferRequested: workspace not found:" << p_workspaceId;
    return;
  }
  auto *factory = m_services.get<ViewWindowFactory>();
  if (!factory || !factory->hasCreator(p_fileType)) {
    qWarning() << "ViewArea2: no creator for file type" << p_fileType;
    return;
  }
  auto *win = factory->create(p_fileType, m_services, p_buffer, split);
  if (!win) {
    qWarning() << "ViewArea2: failed creating view window for type" << p_fileType;
    return;
  }
  ID id = m_nextWindowId++;
  m_windows[id] = win;
  split->addViewWindow(win);
  m_controller->onViewWindowOpened(id, p_buffer, p_settings);
  updateScreenVisibility();
}

void ViewArea2::onCloseViewWindowRequested(ID p_windowId, bool p_force) {
  auto *win = windowForId(p_windowId);
  if (!win) { return; }

  if (!win->aboutToClose(p_force)) { return; }

  // Find the owning split.
  ViewSplit2 *ownerSplit = nullptr;
  QString bufferId;
  QString workspaceId;
  for (auto it = m_splits.begin(); it != m_splits.end(); ++it) {
    if (it.value()->indexOf(win) != -1) {
      ownerSplit = it.value();
      workspaceId = it.key();
      break;
    }
  }
  if (ownerSplit) {
    Buffer2 buf = win->getBuffer();
    bufferId = buf.id();
    ownerSplit->takeViewWindow(win);
  }

  m_windows.remove(p_windowId);
  delete win;

  m_controller->onViewWindowClosed(p_windowId, bufferId, workspaceId);
  updateScreenVisibility();
}

void ViewArea2::onSetCurrentViewSplitRequested(const QString &p_workspaceId, bool p_focus) {
  for (auto *split : getAllViewSplits()) { split->setActive(false); }
  auto *split = splitForWorkspace(p_workspaceId);
  if (split) {
    split->setActive(true);
    if (p_focus) { split->focus(); }
  }
}

void ViewArea2::onFocusViewSplitRequested(const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (split) { split->focus(); }
}

void ViewArea2::onMoveViewWindowToSplitRequested(ID p_windowId,
                                                  const QString &p_srcWorkspaceId,
                                                  const QString &p_dstWorkspaceId) {
  auto *win = windowForId(p_windowId);
  auto *srcSplit = splitForWorkspace(p_srcWorkspaceId);
  auto *dstSplit = splitForWorkspace(p_dstWorkspaceId);
  if (!win || !srcSplit || !dstSplit) { return; }

  // Transfer buffer registration between workspaces.
  Buffer2 buf = win->getBuffer();
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    wsSvc->removeBuffer(p_srcWorkspaceId, buf.id());
    wsSvc->addBuffer(p_dstWorkspaceId, buf.id());
  }

  srcSplit->takeViewWindow(win);
  dstSplit->addViewWindow(win);
  dstSplit->setCurrentViewWindow(win);
}

// ============ ViewSplit2 signal handlers ============

void ViewArea2::onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                                   Direction p_direction) {
  auto *target = findSplitByDirection(p_split, p_direction);
  if (!target) {
    m_controller->splitViewSplit(p_split->getWorkspaceId(), p_direction);
    target = findSplitByDirection(p_split, p_direction);
    if (!target) { return; }
  }
  m_controller->moveViewWindowOneSplit(p_split->getWorkspaceId(),
                                        idForWindow(p_win), p_direction,
                                        target->getWorkspaceId());
}

void ViewArea2::onRemoveSplitRequested(ViewSplit2 *p_split) {
  m_controller->removeViewSplit(p_split->getWorkspaceId(), false, getViewSplitCount());
}

// ============ Layout serialization/deserialization ============

void ViewArea2::onLoadLayoutRequested(const QJsonObject &p_layout) {
  const QJsonObject tree = p_layout.value(QStringLiteral("splitterTree")).toObject();
  if (tree.isEmpty()) { return; }

  const QString type = tree.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("workspace")) {
    QString wsId = tree.value(QStringLiteral("workspaceId")).toString();
    auto *split = addFirstViewSplit(wsId);
    wireSplitSignals(split);
  } else if (type == QStringLiteral("splitter")) {
    Qt::Orientation orient =
        (tree.value(QStringLiteral("orientation")).toString() == QStringLiteral("horizontal"))
        ? Qt::Horizontal : Qt::Vertical;
    auto *splitter = createSplitter(orient);
    deserializeSplitterNode(tree, splitter);
    setTopWidget(splitter);
  }

  // Restore current workspace.
  const QString currentWsId = p_layout.value(QStringLiteral("currentWorkspaceId")).toString();
  if (!currentWsId.isEmpty()) {
    auto *split = splitForWorkspace(currentWsId);
    if (split) { m_controller->setCurrentViewSplit(currentWsId, true); }
  }
  updateScreenVisibility();
}

QJsonObject ViewArea2::serializeWidget(const QWidget *p_widget) {
  QJsonObject node;
  auto *splitter = qobject_cast<const QSplitter *>(p_widget);
  if (splitter) {
    node[QStringLiteral("type")] = QStringLiteral("splitter");
    node[QStringLiteral("orientation")] = (splitter->orientation() == Qt::Horizontal)
        ? QStringLiteral("horizontal") : QStringLiteral("vertical");
    QJsonArray sizes;
    for (int s : splitter->sizes()) { sizes.append(s); }
    node[QStringLiteral("sizes")] = sizes;
    QJsonArray children;
    for (int i = 0; i < splitter->count(); ++i) {
      children.append(serializeWidget(splitter->widget(i)));
    }
    node[QStringLiteral("children")] = children;
  } else {
    auto *split = qobject_cast<const ViewSplit2 *>(p_widget);
    if (split) {
      node[QStringLiteral("type")] = QStringLiteral("workspace");
      node[QStringLiteral("workspaceId")] = split->getWorkspaceId();
    }
  }
  return node;
}

void ViewArea2::deserializeSplitterNode(const QJsonObject &p_node, QSplitter *p_splitter) {
  const QJsonArray children = p_node.value(QStringLiteral("children")).toArray();
  for (const auto &childVal : children) {
    const QJsonObject child = childVal.toObject();
    const QString type = child.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("workspace")) {
      const QString wsId = child.value(QStringLiteral("workspaceId")).toString();
      auto *split = createViewSplit(wsId);
      m_splits[wsId] = split;
      wireSplitSignals(split);
      p_splitter->addWidget(split);
    } else if (type == QStringLiteral("splitter")) {
      Qt::Orientation orient =
          (child.value(QStringLiteral("orientation")).toString() == QStringLiteral("horizontal"))
          ? Qt::Horizontal : Qt::Vertical;
      auto *childSplitter = createSplitter(orient);
      p_splitter->addWidget(childSplitter);
      deserializeSplitterNode(child, childSplitter);
    }
  }
  const QJsonArray sizesArr = p_node.value(QStringLiteral("sizes")).toArray();
  if (!sizesArr.isEmpty()) {
    QList<int> sizes;
    for (const auto &s : sizesArr) { sizes.append(s.toInt()); }
    p_splitter->setSizes(sizes);
  }
}

// ============ Private helpers ============

void ViewArea2::collectViewSplits(QWidget *p_widget, QVector<ViewSplit2 *> &p_splits) const {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) { p_splits.append(split); return; }
  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter) {
    for (int i = 0; i < splitter->count(); ++i) {
      collectViewSplits(splitter->widget(i), p_splits);
    }
  }
}

void ViewArea2::distributeSplitter(QSplitter *p_splitter) {
  int cnt = p_splitter->count();
  if (cnt == 0) { return; }
  int total = (p_splitter->orientation() == Qt::Horizontal)
              ? p_splitter->width() : p_splitter->height();
  int each = total / cnt;
  QList<int> sizes;
  for (int i = 0; i < cnt; ++i) { sizes.append(each); }
  p_splitter->setSizes(sizes);
  for (int i = 0; i < cnt; ++i) {
    auto *child = qobject_cast<QSplitter *>(p_splitter->widget(i));
    if (child) { distributeSplitter(child); }
  }
}

QSplitter *ViewArea2::findParentSplitter(QWidget *p_widget) const {
  if (!p_widget) { return nullptr; }
  QWidget *parent = p_widget->parentWidget();
  while (parent && parent != this) {
    auto *splitter = qobject_cast<QSplitter *>(parent);
    if (splitter) { return splitter; }
    parent = parent->parentWidget();
  }
  return nullptr;
}

void ViewArea2::unwrapSingleChildSplitter(QSplitter *p_splitter) {
  Q_ASSERT(p_splitter->count() == 1);
  QWidget *child = p_splitter->widget(0);
  auto *grandParent = findParentSplitter(p_splitter);
  if (!grandParent) {
    m_contentsLayout->removeWidget(p_splitter);
    child->setParent(nullptr);
    p_splitter->setParent(nullptr);
    setTopWidget(child);
  } else {
    int idx = grandParent->indexOf(p_splitter);
    child->setParent(nullptr);
    p_splitter->setParent(nullptr);
    grandParent->insertWidget(idx, child);
  }
  p_splitter->deleteLater();
}

ViewSplit2 *ViewArea2::findFirstViewSplit(QWidget *p_widget) {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) { return split; }
  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter && splitter->count() > 0) { return findFirstViewSplit(splitter->widget(0)); }
  return nullptr;
}

ViewSplit2 *ViewArea2::findLastViewSplit(QWidget *p_widget) {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) { return split; }
  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter && splitter->count() > 0) {
    return findLastViewSplit(splitter->widget(splitter->count() - 1));
  }
  return nullptr;
}

// ============ Screen switching ============

void ViewArea2::showHomeScreen() {
  m_stackedLayout->setCurrentIndex(HomeScreen);
}

void ViewArea2::showContentsScreen() {
  m_stackedLayout->setCurrentIndex(ContentsScreen);
}

void ViewArea2::updateScreenVisibility() {
  const int targetPage = m_windows.isEmpty() ? HomeScreen : ContentsScreen;
  if (m_stackedLayout->currentIndex() != targetPage) {
    m_stackedLayout->setCurrentIndex(targetPage);
  }
}
