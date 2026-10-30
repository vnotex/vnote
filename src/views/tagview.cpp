#include "tagview.h"

#include <QDrag>
#include <QDragLeaveEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QSortFilterProxyModel>

#include <gui/utils/treeviewutils.h>
#include <gui/utils/widgetutils.h>
#include <models/tagmodel.h>

using namespace vnotex;

TagView::TagView(QWidget *p_parent) : QTreeView(p_parent) { setupView(); }

void TagView::setupView() {
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setDragDropMode(QAbstractItemView::InternalMove);
  setDefaultDropAction(Qt::MoveAction);
  setDragDropOverwriteMode(true);
  setDropIndicatorShown(true);
  setAutoExpandDelay(600);

  setContextMenuPolicy(Qt::CustomContextMenu);

  setHeaderHidden(true);
  TreeViewUtils::applyIndentation(this);
  setUniformRowHeights(true);
  setAnimated(true);

  // Show horizontal scrollbar when content overflows.
  WidgetUtils::showHorizontalScrollbar(this);

  setEditTriggers(QAbstractItemView::NoEditTriggers);

  connect(this, &QTreeView::doubleClicked, this, &TagView::onItemActivated);
  connect(this, &QTreeView::customContextMenuRequested, this, &TagView::onContextMenuRequested);
}

void TagView::startDrag(Qt::DropActions p_supportedActions) {
  if (!model() || !selectionModel() || !(p_supportedActions & Qt::MoveAction)) {
    return;
  }
  const QModelIndexList rows = selectionModel()->selectedRows();
  if (rows.size() != 1) {
    return;
  }
  auto *mimeData = model()->mimeData(rows);
  if (!mimeData) {
    return;
  }

  auto *drag = new QDrag(this);
  drag->setMimeData(mimeData);
  drag->exec(Qt::MoveAction, Qt::MoveAction);
  drag->deleteLater();

  // Persistence belongs to the controller; base startDrag() may clear the source after Move.
  QDragLeaveEvent leaveEvent;
  QTreeView::dragLeaveEvent(&leaveEvent);
  viewport()->update();
}

QString TagView::tagNameFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return QString();
  }
  return p_index.data(TagModel::TagNameRole).toString();
}

QStringList TagView::selectedTagNames() const {
  QStringList tags;
  const QModelIndexList selected = selectedIndexes();
  for (const QModelIndex &idx : selected) {
    if (idx.column() == 0) {
      const QString name = tagNameFromIndex(idx);
      if (!name.isEmpty()) {
        tags.append(name);
      }
    }
  }
  return tags;
}

void TagView::selectionChanged(const QItemSelection &p_selected,
                               const QItemSelection &p_deselected) {
  QTreeView::selectionChanged(p_selected, p_deselected);
  emit tagsSelectionChanged(selectedTagNames());
}

void TagView::keyPressEvent(QKeyEvent *p_event) {
  if (p_event->key() == Qt::Key_Escape && p_event->modifiers() == Qt::NoModifier) {
    if (selectionModel() && selectionModel()->hasSelection()) {
      clearSelection();
      setCurrentIndex(QModelIndex());
      p_event->accept();
      return;
    }
  }
  QTreeView::keyPressEvent(p_event);
}

void TagView::onItemActivated(const QModelIndex &p_index) {
  const QString name = tagNameFromIndex(p_index);
  if (!name.isEmpty()) {
    emit tagActivated(name);
  }
}

void TagView::onContextMenuRequested(const QPoint &p_pos) {
  const QModelIndex idx = indexAt(p_pos);
  const QString name = tagNameFromIndex(idx);
  emit contextMenuRequested(name, viewport()->mapToGlobal(p_pos));
}

void TagView::selectAndScrollToTag(const QString &p_tagName) {
  if (p_tagName.isEmpty() || !model()) {
    return;
  }

  auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
  auto *tagModel = qobject_cast<TagModel *>(proxy ? proxy->sourceModel() : model());
  if (!tagModel) {
    return;
  }

  QModelIndex srcIdx = tagModel->indexFromTagName(p_tagName);
  if (!srcIdx.isValid()) {
    return;
  }

  QModelIndex viewIdx = proxy ? proxy->mapFromSource(srcIdx) : srcIdx;
  if (!viewIdx.isValid()) {
    return;
  }

  // Expand all ancestors so the tag is visible.
  QModelIndex ancestor = viewIdx.parent();
  while (ancestor.isValid()) {
    if (!isExpanded(ancestor)) {
      expand(ancestor);
    }
    ancestor = ancestor.parent();
  }

  selectionModel()->setCurrentIndex(viewIdx, QItemSelectionModel::ClearAndSelect |
                                                 QItemSelectionModel::Rows);
  scrollTo(viewIdx, QAbstractItemView::EnsureVisible);
}
