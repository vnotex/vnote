#include "outlineview.h"

#include <QItemSelectionModel>
#include <QSortFilterProxyModel>

#include <models/outlinemodel.h>

using namespace vnotex;

OutlineView::OutlineView(QWidget *p_parent) : QTreeView(p_parent) {
  setupView();
}

void OutlineView::setupView() {
  // Single column, no header.
  setHeaderHidden(true);

  // Single selection mode.
  setSelectionMode(QAbstractItemView::SingleSelection);

  // Animate expand/collapse transitions.
  setAnimated(true);

  // Show horizontal scrollbar when content overflows.
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Connect clicked signal for mouse activation.
  // The currentChanged signal (for keyboard navigation) is connected in setModel()
  // because the selection model is recreated each time a model is set.
  connect(this, &QTreeView::clicked, this, &OutlineView::handleClicked);
}

void OutlineView::setModel(QAbstractItemModel *p_model) {
  QTreeView::setModel(p_model);

  // Reconnect selection model signal after model change.
  if (selectionModel()) {
    connect(selectionModel(), &QItemSelectionModel::currentChanged,
            this, &OutlineView::handleCurrentChanged);
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
  auto *outlineModel =
      qobject_cast<OutlineModel *>(proxy ? proxy->sourceModel() : model());
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

void OutlineView::expandToLevel(int p_level, int p_baseLevel) {
  int delta = p_level - p_baseLevel;
  if (delta <= 0) {
    collapseAll();
  } else {
    collapseAll();
    expandToDepth(delta - 1);
  }
}
