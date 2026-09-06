#include "outlineview.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QSortFilterProxyModel>

#include <gui/utils/treeviewutils.h>
#include <gui/utils/widgetutils.h>
#include <models/outlinemodel.h>

using namespace vnotex;

OutlineView::OutlineView(QWidget *p_parent) : QTreeView(p_parent) { setupView(); }

void OutlineView::setupView() {
  // Single column, no header.
  setHeaderHidden(true);

  // Single selection mode.
  setSelectionMode(QAbstractItemView::SingleSelection);

  // Animate expand/collapse transitions.
  setAnimated(true);

  // Share the indentation step with the other dock trees.
  TreeViewUtils::applyIndentation(this);

  // Show horizontal scrollbar when content overflows.
  WidgetUtils::showHorizontalScrollbar(this);

  // Connect clicked signal for mouse activation.
  // The currentChanged signal (for keyboard navigation) is connected in setModel()
  // because the selection model is recreated each time a model is set.
  connect(this, &QTreeView::clicked, this, &OutlineView::handleClicked);
}

void OutlineView::setReorderingEnabled(bool p_enabled) {
  if (m_reorderingEnabled == p_enabled) {
    return;
  }

  m_reorderingEnabled = p_enabled;
  updateDragDropMode();
}

void OutlineView::setFilterActive(bool p_active) {
  if (m_filterActive == p_active) {
    return;
  }

  m_filterActive = p_active;
  updateDragDropMode();
}

void OutlineView::updateDragDropMode() {
  const bool enabled = m_reorderingEnabled && !m_filterActive;
  setDragEnabled(enabled);
  setAcceptDrops(enabled);
  setDropIndicatorShown(enabled);
  setDragDropMode(enabled ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
  setDefaultDropAction(Qt::MoveAction);
}

void OutlineView::startDrag(Qt::DropActions p_supportedActions) {
  Q_UNUSED(p_supportedActions);

  const QModelIndex source = currentIndex();
  if (!m_reorderingEnabled || m_filterActive || !source.isValid() ||
      !source.data(OutlineModel::ReorderableRole).toBool()) {
    return;
  }

  auto *mimeData = new QMimeData();
  mimeData->setData("application/x-vnote-outline-heading",
                    QByteArray::number(source.data(OutlineModel::HeadingIndexRole).toInt()));

  m_draggedIndex = source;
  QDrag drag(this);
  drag.setMimeData(mimeData);
  drag.exec(Qt::MoveAction, Qt::MoveAction);
  m_draggedIndex = QPersistentModelIndex();
  clearDropIndicator();
}

void OutlineView::dragEnterEvent(QDragEnterEvent *p_event) {
  if (!m_reorderingEnabled || m_filterActive || p_event->source() != this ||
      !p_event->mimeData()->hasFormat("application/x-vnote-outline-heading") ||
      !(p_event->possibleActions() & Qt::MoveAction)) {
    p_event->ignore();
    clearDropIndicator();
    return;
  }

  p_event->setDropAction(Qt::MoveAction);
  p_event->accept();
}

void OutlineView::dragMoveEvent(QDragMoveEvent *p_event) {
  if (p_event->source() != this ||
      !p_event->mimeData()->hasFormat("application/x-vnote-outline-heading") ||
      !(p_event->possibleActions() & Qt::MoveAction)) {
    p_event->ignore();
    clearDropIndicator();
    return;
  }

  QTreeView::dragMoveEvent(p_event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const QModelIndex target = indexAt(p_event->position().toPoint());
#else
  const QModelIndex target = indexAt(p_event->pos());
#endif
  const auto position = target.isValid() ? dropIndicatorPosition() : OnViewport;
  if (!canAcceptDrop(target, position)) {
    p_event->ignore();
    clearDropIndicator();
    return;
  }

  p_event->setDropAction(Qt::MoveAction);
  p_event->accept();
}

void OutlineView::dropEvent(QDropEvent *p_event) {
  if (p_event->source() != this ||
      !p_event->mimeData()->hasFormat("application/x-vnote-outline-heading") ||
      !(p_event->possibleActions() & Qt::MoveAction)) {
    p_event->ignore();
    clearDropIndicator();
    return;
  }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const QModelIndex target = indexAt(p_event->position().toPoint());
#else
  const QModelIndex target = indexAt(p_event->pos());
#endif
  const auto position = target.isValid() ? dropIndicatorPosition() : OnViewport;
  if (!canAcceptDrop(target, position)) {
    p_event->ignore();
    clearDropIndicator();
    return;
  }

  const int sourceHeadingIndex = m_draggedIndex.data(OutlineModel::HeadingIndexRole).toInt();
  const int targetHeadingIndex =
      target.isValid() ? target.data(OutlineModel::HeadingIndexRole).toInt() : -1;
  p_event->setDropAction(Qt::MoveAction);
  p_event->accept();
  clearDropIndicator();
  OutlineDropPosition publicPosition = OutlineDropPosition::OnViewport;
  switch (position) {
  case OnItem:
    publicPosition = OutlineDropPosition::OnItem;
    break;
  case AboveItem:
    publicPosition = OutlineDropPosition::AboveItem;
    break;
  case BelowItem:
    publicPosition = OutlineDropPosition::BelowItem;
    break;
  case OnViewport:
    break;
  }
  emit itemMoveRequested(sourceHeadingIndex, targetHeadingIndex, publicPosition);
}

bool OutlineView::canAcceptDrop(const QModelIndex &p_target,
                                QAbstractItemView::DropIndicatorPosition p_position) const {
  if (!m_reorderingEnabled || m_filterActive || !m_draggedIndex.isValid() ||
      !m_draggedIndex.data(OutlineModel::ReorderableRole).toBool()) {
    return false;
  }

  if (p_position != OnViewport) {
    if (!p_target.isValid() || !p_target.data(OutlineModel::ReorderableRole).toBool() ||
        p_target == m_draggedIndex || isDraggedSubtreeIndex(p_target)) {
      return false;
    }
  }

  const int sourceLevel = m_draggedIndex.data(OutlineModel::HeadingLevelRole).toInt();
  int targetLevel = sourceLevel;
  if (p_position == OnItem) {
    targetLevel = p_target.data(OutlineModel::HeadingLevelRole).toInt() + 1;
  } else if (p_position == AboveItem || p_position == BelowItem) {
    targetLevel = p_target.data(OutlineModel::HeadingLevelRole).toInt();
  } else if (p_position == OnViewport) {
    targetLevel = rootHeadingLevelAfterMove();
  } else {
    return false;
  }

  const int delta = targetLevel - sourceLevel;
  return targetLevel >= 1 && maximumHeadingLevel(m_draggedIndex) + delta <= 6;
}

bool OutlineView::isDraggedSubtreeIndex(const QModelIndex &p_index) const {
  for (QModelIndex ancestor = p_index; ancestor.isValid(); ancestor = ancestor.parent()) {
    if (ancestor == m_draggedIndex) {
      return true;
    }
  }
  return false;
}

int OutlineView::maximumHeadingLevel(const QModelIndex &p_root) const {
  int maximum = p_root.data(OutlineModel::HeadingLevelRole).toInt();
  for (int row = 0; row < model()->rowCount(p_root); ++row) {
    maximum = qMax(maximum, maximumHeadingLevel(model()->index(row, 0, p_root)));
  }
  return maximum;
}

int OutlineView::rootHeadingLevelAfterMove() const {
  int minimum = 7;
  for (int row = 0; row < model()->rowCount(); ++row) {
    const QModelIndex index = model()->index(row, 0);
    if (!isDraggedSubtreeIndex(index)) {
      minimum = qMin(minimum, index.data(OutlineModel::HeadingLevelRole).toInt());
    }
  }
  return minimum == 7 ? m_draggedIndex.data(OutlineModel::HeadingLevelRole).toInt() : minimum;
}

void OutlineView::clearDropIndicator() {
  setDropIndicatorShown(false);
  setDropIndicatorShown(m_reorderingEnabled && !m_filterActive);
  viewport()->update();
}

void OutlineView::setModel(QAbstractItemModel *p_model) {
  QTreeView::setModel(p_model);

  // Reconnect selection model signal after model change.
  if (selectionModel()) {
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            &OutlineView::handleCurrentChanged);
  }
}

void OutlineView::handleCurrentChanged(const QModelIndex &p_current) {
  if (!m_muted) {
    activateHeading(p_current);
  }
}

void OutlineView::handleClicked(const QModelIndex &p_index) {
  if (!m_muted) {
    activateHeading(p_index);
  }
}

void OutlineView::activateHeading(const QModelIndex &p_index) {
  if (!p_index.isValid()) {
    return;
  }

  int headingIndex = p_index.data(OutlineModel::HeadingIndexRole).toInt();
  if (headingIndex >= 0) {
    emit headingActivated(headingIndex);
  }
}

void OutlineView::highlightHeading(int p_headingIndex) {
  auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
  auto *outlineModel = qobject_cast<OutlineModel *>(proxy ? proxy->sourceModel() : model());
  if (!outlineModel) {
    return;
  }

  QModelIndex srcIdx = outlineModel->indexForHeadingIndex(p_headingIndex);
  if (!srcIdx.isValid()) {
    return;
  }

  QModelIndex viewIdx = proxy ? proxy->mapFromSource(srcIdx) : srcIdx;
  if (!viewIdx.isValid()) {
    return;
  }

  m_muted = true;
  setCurrentIndex(viewIdx);
  scrollTo(viewIdx);
  m_muted = false;
}

QSet<int> OutlineView::saveExpansionState() const {
  QSet<int> expanded;
  if (!model()) {
    return expanded;
  }

  // Walk all indices in the model and record which ones are expanded.
  QModelIndexList stack;
  for (int i = 0; i < model()->rowCount(); ++i) {
    stack.append(model()->index(i, 0));
  }

  while (!stack.isEmpty()) {
    QModelIndex idx = stack.takeLast();
    if (isExpanded(idx)) {
      int headingIndex = idx.data(OutlineModel::HeadingIndexRole).toInt();
      if (headingIndex >= 0) {
        expanded.insert(headingIndex);
      }
      // Also check children.
      for (int i = 0; i < model()->rowCount(idx); ++i) {
        stack.append(model()->index(i, 0, idx));
      }
    }
  }

  return expanded;
}

void OutlineView::restoreExpansionState(const QSet<int> &p_expandedHeadingIndices) {
  if (!model() || p_expandedHeadingIndices.isEmpty()) {
    return;
  }

  // Walk all indices and expand those whose heading index is in the set.
  QModelIndexList stack;
  for (int i = 0; i < model()->rowCount(); ++i) {
    stack.append(model()->index(i, 0));
  }

  while (!stack.isEmpty()) {
    QModelIndex idx = stack.takeLast();
    int headingIndex = idx.data(OutlineModel::HeadingIndexRole).toInt();
    if (headingIndex >= 0 && p_expandedHeadingIndices.contains(headingIndex)) {
      expand(idx);
    }
    // Always check children (parent might not be in the set but child might be).
    for (int i = 0; i < model()->rowCount(idx); ++i) {
      stack.append(model()->index(i, 0, idx));
    }
  }
}

void OutlineView::expandToLevel(int p_level, int p_baseLevel) {
  int delta = p_level - p_baseLevel;
  if (delta <= 0) {
    collapseAll();
  } else {
    collapseAll();
    expandToDepth(delta - 1);
  }
}
