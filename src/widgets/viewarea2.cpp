#include "viewarea2.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSplitter>
#include <QVBoxLayout>

#include <controllers/viewareacontroller.h>
#include <core/servicelocator.h>

#include "viewsplit2.h"
#include "viewwindow2.h"

using namespace vnotex;

ViewArea2::ViewArea2(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  setContentsMargins(0, 0, 0, 0);

  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);

  setupController();
}

ViewArea2::~ViewArea2() {
}

void ViewArea2::setupController() {
  m_controller = new ViewAreaController(m_services, this);

  // Wire controller signals to our slots.
  connect(m_controller, &ViewAreaController::addFirstViewSplitRequested, this,
          &ViewArea2::onAddFirstViewSplitRequested);
  connect(m_controller, &ViewAreaController::splitRequested, this,
          &ViewArea2::onSplitRequested);
  connect(m_controller, &ViewAreaController::removeViewSplitRequested, this,
          &ViewArea2::onRemoveViewSplitRequested);
  connect(m_controller, &ViewAreaController::maximizeViewSplitRequested, this,
          &ViewArea2::onMaximizeViewSplitRequested);
  connect(m_controller, &ViewAreaController::distributeViewSplitsRequested, this,
          &ViewArea2::onDistributeViewSplitsRequested);
  connect(m_controller, &ViewAreaController::viewWindowCreated, this,
          &ViewArea2::onViewWindowCreated);
  connect(m_controller, &ViewAreaController::setCurrentViewSplitRequested, this,
          &ViewArea2::onSetCurrentViewSplitRequested);
  connect(m_controller, &ViewAreaController::focusViewSplitRequested, this,
          &ViewArea2::onFocusViewSplitRequested);
  connect(m_controller, &ViewAreaController::moveViewWindowToSplit, this,
          &ViewArea2::onMoveViewWindowToSplit);
  connect(m_controller, &ViewAreaController::loadLayoutRequested, this,
          &ViewArea2::onLoadLayoutRequested);

  // Subscribe to hooks.
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
  setTopWidget(split);
  return split;
}

ViewSplit2 *ViewArea2::splitAt(ViewSplit2 *p_split, Direction p_direction,
                               const QString &p_workspaceId) {
  Qt::Orientation orient =
      (p_direction == Direction::Left || p_direction == Direction::Right) ? Qt::Horizontal
                                                                           : Qt::Vertical;
  bool insertBefore = (p_direction == Direction::Left || p_direction == Direction::Up);

  auto *newSplit = createViewSplit(p_workspaceId);
  auto *parentSplitter = findParentSplitter(p_split);

  if (!parentSplitter) {
    auto *splitter = createSplitter(orient);
    m_mainLayout->removeWidget(p_split);
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
    m_mainLayout->removeWidget(p_split);
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
    for (int i = 0; i < splitter->count(); ++i) {
      totalSize += currentSizes.value(i);
    }
    int targetIdx = splitter->indexOf(target);
    for (int i = 0; i < splitter->count(); ++i) {
      if (i == targetIdx) {
        sizes.append(totalSize);
      } else {
        sizes.append(0);
      }
    }
    splitter->setSizes(sizes);
    target = splitter;
    splitter = findParentSplitter(target);
  }
}

void ViewArea2::distributeViewSplits() {
  auto *splitter = qobject_cast<QSplitter *>(m_topWidget);
  if (splitter) {
    distributeSplitter(splitter);
  }
}

ViewSplit2 *ViewArea2::findSplitByDirection(ViewSplit2 *p_split, Direction p_direction) const {
  Qt::Orientation orient =
      (p_direction == Direction::Left || p_direction == Direction::Right) ? Qt::Horizontal
                                                                           : Qt::Vertical;
  int step = (p_direction == Direction::Right || p_direction == Direction::Down) ? 1 : -1;

  QWidget *target = p_split;
  auto *splitter = findParentSplitter(target);

  while (splitter) {
    if (splitter->orientation() == orient) {
      int idx = splitter->indexOf(target);
      int newIdx = idx + step;
      if (newIdx >= 0 && newIdx < splitter->count()) {
        QWidget *adjacent = splitter->widget(newIdx);
        if (step > 0) {
          return findFirstViewSplit(adjacent);
        } else {
          return findLastViewSplit(adjacent);
        }
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
  if (m_topWidget) {
    collectViewSplits(m_topWidget, splits);
  }
  return splits;
}

ViewSplit2 *ViewArea2::getCurrentViewSplit() const {
  for (auto *split : getAllViewSplits()) {
    if (split->isActive()) {
      return split;
    }
  }

  return nullptr;
}

int ViewArea2::getViewSplitCount() const {
  return getAllViewSplits().size();
}

// ============ Top Widget ============

QWidget *ViewArea2::getTopWidget() const {
  return m_topWidget;
}

void ViewArea2::setTopWidget(QWidget *p_widget) {
  if (m_topWidget) {
    m_mainLayout->removeWidget(m_topWidget);
  }

  m_topWidget = p_widget;

  if (m_topWidget) {
    m_mainLayout->addWidget(m_topWidget);
  }
}

QJsonObject ViewArea2::saveLayout() const {
  auto splits = getAllViewSplits();
  auto serializer = [](const QWidget *w) { return serializeWidget(w); };
  return m_controller->saveLayout(splits, serializer);
}

void ViewArea2::loadLayout(const QJsonObject &p_layout) {
  if (!p_layout.isEmpty()) {
    m_controller->loadLayout(p_layout);
  }
}

// ============ Controller signal handlers ============

void ViewArea2::onAddFirstViewSplitRequested(const QString &p_workspaceId) {
  auto *split = addFirstViewSplit(p_workspaceId);
  m_controller->connectSplitSignals(split);

  // Wire signals that ViewArea2 handles instead of the controller.
  connect(split, &ViewSplit2::moveViewWindowOneSplitRequested, this,
          &ViewArea2::onMoveViewWindowOneSplitRequested);
  connect(split, &ViewSplit2::removeSplitRequested, this, &ViewArea2::onRemoveSplitRequested);

  m_controller->setCurrentViewSplit(split, true);
}

void ViewArea2::onSplitRequested(ViewSplit2 *p_split, Direction p_direction,
                                 const QString &p_workspaceId) {
  auto *newSplit = splitAt(p_split, p_direction, p_workspaceId);
  m_controller->connectSplitSignals(newSplit);

  // Wire signals that ViewArea2 handles instead of the controller.
  connect(newSplit, &ViewSplit2::moveViewWindowOneSplitRequested, this,
          &ViewArea2::onMoveViewWindowOneSplitRequested);
  connect(newSplit, &ViewSplit2::removeSplitRequested, this, &ViewArea2::onRemoveSplitRequested);

  m_controller->setCurrentViewSplit(newSplit, true);
}

void ViewArea2::onRemoveViewSplitRequested(ViewSplit2 *p_split) {
  removeViewSplit(p_split);
  delete p_split;
}

void ViewArea2::onMaximizeViewSplitRequested(ViewSplit2 *p_split) {
  maximizeViewSplit(p_split);
}

void ViewArea2::onDistributeViewSplitsRequested() {
  distributeViewSplits();
}

void ViewArea2::onViewWindowCreated(ViewWindow2 *p_win, ViewSplit2 *p_split) {
  p_split->addViewWindow(p_win);
}

void ViewArea2::onSetCurrentViewSplitRequested(ViewSplit2 *p_split, bool p_focus) {
  // Deactivate all splits, then activate the requested one.
  for (auto *split : getAllViewSplits()) {
    split->setActive(false);
  }

  if (p_split) {
    p_split->setActive(true);
    if (p_focus) {
      p_split->focus();
    }
  }
}

void ViewArea2::onFocusViewSplitRequested(ViewSplit2 *p_split) {
  if (p_split) {
    p_split->focus();
  }
}

void ViewArea2::onMoveViewWindowToSplit(ViewSplit2 *p_sourceSplit, ViewWindow2 *p_win,
                                        ViewSplit2 *p_targetSplit) {
  p_sourceSplit->takeViewWindow(p_win);
  p_targetSplit->addViewWindow(p_win);
  p_targetSplit->setCurrentViewWindow(p_win);
}

void ViewArea2::onLoadLayoutRequested(const QJsonObject &p_layout) {
  const QJsonObject tree = p_layout.value(QStringLiteral("splitterTree")).toObject();
  if (tree.isEmpty()) {
    return;
  }

  if (tree.value(QStringLiteral("type")).toString() == QStringLiteral("workspace")) {
    QString wsId = tree.value(QStringLiteral("workspaceId")).toString();
    auto *split = addFirstViewSplit(wsId);
    m_controller->connectSplitSignals(split);
    connect(split, &ViewSplit2::moveViewWindowOneSplitRequested, this,
            &ViewArea2::onMoveViewWindowOneSplitRequested);
    connect(split, &ViewSplit2::removeSplitRequested, this, &ViewArea2::onRemoveSplitRequested);
  } else if (tree.value(QStringLiteral("type")).toString() == QStringLiteral("splitter")) {
    Qt::Orientation orient =
        (tree.value(QStringLiteral("orientation")).toString() == QStringLiteral("horizontal"))
            ? Qt::Horizontal
            : Qt::Vertical;
    auto *splitter = createSplitter(orient);
    deserializeSplitterNode(tree, splitter);
    setTopWidget(splitter);
  }

  // Restore current workspace.
  QString currentWsId = p_layout.value(QStringLiteral("currentWorkspaceId")).toString();
  if (!currentWsId.isEmpty()) {
    for (auto *split : getAllViewSplits()) {
      if (split->getWorkspaceId() == currentWsId) {
        m_controller->setCurrentViewSplit(split, true);
        break;
      }
    }
  }
}

// ============ ViewSplit2 signal handlers ============

void ViewArea2::onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                                   Direction p_direction) {
  // Resolve the target split. If none exists, split first.
  auto *target = findSplitByDirection(p_split, p_direction);
  if (!target) {
    // Create a new split in the requested direction.
    m_controller->splitViewSplit(p_split, p_direction);
    // After the synchronous signal handling, the new split exists.
    target = findSplitByDirection(p_split, p_direction);
    if (!target) {
      return;
    }
  }

  m_controller->moveViewWindowOneSplit(p_split, p_win, p_direction, target);
}

void ViewArea2::onRemoveSplitRequested(ViewSplit2 *p_split) {
  m_controller->removeViewSplit(p_split, false, getViewSplitCount());
}

// ============ Serialization (moved from controller) ============

QJsonObject ViewArea2::serializeWidget(const QWidget *p_widget) {
  QJsonObject node;

  auto *splitter = qobject_cast<const QSplitter *>(p_widget);
  if (splitter) {
    node[QStringLiteral("type")] = QStringLiteral("splitter");
    node[QStringLiteral("orientation")] = (splitter->orientation() == Qt::Horizontal)
                                              ? QStringLiteral("horizontal")
                                              : QStringLiteral("vertical");

    QJsonArray sizes;
    for (int s : splitter->sizes()) {
      sizes.append(s);
    }
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
  QJsonArray children = p_node.value(QStringLiteral("children")).toArray();
  for (const auto &childVal : children) {
    QJsonObject child = childVal.toObject();
    QString type = child.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("workspace")) {
      QString wsId = child.value(QStringLiteral("workspaceId")).toString();
      auto *split = createViewSplit(wsId);
      m_controller->connectSplitSignals(split);
      connect(split, &ViewSplit2::moveViewWindowOneSplitRequested, this,
              &ViewArea2::onMoveViewWindowOneSplitRequested);
      connect(split, &ViewSplit2::removeSplitRequested, this, &ViewArea2::onRemoveSplitRequested);
      p_splitter->addWidget(split);
    } else if (type == QStringLiteral("splitter")) {
      Qt::Orientation orient =
          (child.value(QStringLiteral("orientation")).toString() == QStringLiteral("horizontal"))
              ? Qt::Horizontal
              : Qt::Vertical;
      auto *childSplitter = createSplitter(orient);
      p_splitter->addWidget(childSplitter);
      deserializeSplitterNode(child, childSplitter);
    }
  }

  QJsonArray sizesArr = p_node.value(QStringLiteral("sizes")).toArray();
  if (!sizesArr.isEmpty()) {
    QList<int> sizes;
    for (const auto &s : sizesArr) {
      sizes.append(s.toInt());
    }
    p_splitter->setSizes(sizes);
  }
}

// ============ Private helpers ============

void ViewArea2::collectViewSplits(QWidget *p_widget, QVector<ViewSplit2 *> &p_splits) const {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) {
    p_splits.append(split);
    return;
  }

  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter) {
    for (int i = 0; i < splitter->count(); ++i) {
      collectViewSplits(splitter->widget(i), p_splits);
    }
  }
}

void ViewArea2::distributeSplitter(QSplitter *p_splitter) {
  int cnt = p_splitter->count();
  if (cnt == 0) {
    return;
  }

  int total =
      (p_splitter->orientation() == Qt::Horizontal) ? p_splitter->width() : p_splitter->height();
  int each = total / cnt;

  QList<int> sizes;
  for (int i = 0; i < cnt; ++i) {
    sizes.append(each);
  }
  p_splitter->setSizes(sizes);

  for (int i = 0; i < cnt; ++i) {
    auto *childSplitter = qobject_cast<QSplitter *>(p_splitter->widget(i));
    if (childSplitter) {
      distributeSplitter(childSplitter);
    }
  }
}

ViewSplit2 *ViewArea2::findFirstViewSplit(QWidget *p_widget) {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) {
    return split;
  }

  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter && splitter->count() > 0) {
    return findFirstViewSplit(splitter->widget(0));
  }

  return nullptr;
}

ViewSplit2 *ViewArea2::findLastViewSplit(QWidget *p_widget) {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) {
    return split;
  }

  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter && splitter->count() > 0) {
    return findLastViewSplit(splitter->widget(splitter->count() - 1));
  }

  return nullptr;
}

void ViewArea2::unwrapSingleChildSplitter(QSplitter *p_splitter) {
  Q_ASSERT(p_splitter->count() == 1);
  QWidget *child = p_splitter->widget(0);

  auto *grandParent = findParentSplitter(p_splitter);
  if (!grandParent) {
    m_mainLayout->removeWidget(p_splitter);
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

QSplitter *ViewArea2::findParentSplitter(QWidget *p_widget) const {
  if (!p_widget) {
    return nullptr;
  }

  QWidget *parent = p_widget->parentWidget();
  while (parent && parent != this) {
    auto *splitter = qobject_cast<QSplitter *>(parent);
    if (splitter) {
      return splitter;
    }
    parent = parent->parentWidget();
  }

  return nullptr;
}
