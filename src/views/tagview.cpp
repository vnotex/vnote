#include "tagview.h"

#include <QItemSelectionModel>

#include <models/tagmodel.h>

using namespace vnotex;

TagView::TagView(QWidget *p_parent) : QTreeView(p_parent) { setupView(); }

void TagView::setupView() {
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  setContextMenuPolicy(Qt::CustomContextMenu);

  setHeaderHidden(true);
  setIndentation(16);
  setUniformRowHeights(true);
  setAnimated(true);

  setEditTriggers(QAbstractItemView::NoEditTriggers);

  connect(this, &QTreeView::doubleClicked, this, &TagView::onItemActivated);
  connect(this, &QTreeView::customContextMenuRequested, this,
          &TagView::onContextMenuRequested);
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

  auto *tagModel = qobject_cast<TagModel *>(model());
  if (!tagModel) {
    return;
  }

  QModelIndex idx = tagModel->indexFromTagName(p_tagName);
  if (!idx.isValid()) {
    return;
  }

  // Expand all ancestors so the tag is visible.
  QModelIndex ancestor = idx.parent();
  while (ancestor.isValid()) {
    if (!isExpanded(ancestor)) {
      expand(ancestor);
    }
    ancestor = ancestor.parent();
  }

  // Select and scroll.
  selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  scrollTo(idx, QAbstractItemView::EnsureVisible);
}
