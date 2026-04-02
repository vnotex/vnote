#include "searchresultview.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>

using namespace vnotex;

SearchResultView::SearchResultView(QWidget *p_parent) : QTreeView(p_parent) { setupView(); }

SearchResultView::~SearchResultView() {}

void SearchResultView::setupView() {
  setHeaderHidden(true);
  setAlternatingRowColors(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setRootIsDecorated(true);
  setUniformRowHeights(true);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setIndentation(16);
}

void SearchResultView::setModel(QAbstractItemModel *p_model) {
  // Disconnect from old model
  if (model()) {
    disconnect(model(), &QAbstractItemModel::modelReset, this, &SearchResultView::expandAll);
  }

  QTreeView::setModel(p_model);

  // Connect to new model's modelReset to auto-expand
  if (p_model) {
    connect(p_model, &QAbstractItemModel::modelReset, this, &SearchResultView::expandAll);
    expandAll();
  }
}

void SearchResultView::mouseDoubleClickEvent(QMouseEvent *p_event) {
  QModelIndex idx = indexAt(p_event->pos());
  if (idx.isValid()) {
    emit resultActivated(idx);
    p_event->accept();
    return;
  }

  QTreeView::mouseDoubleClickEvent(p_event);
}

void SearchResultView::keyPressEvent(QKeyEvent *p_event) {
  switch (p_event->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter: {
    QModelIndex idx = currentIndex();
    if (idx.isValid()) {
      emit resultActivated(idx);
      p_event->accept();
      return;
    }
    break;
  }
  default:
    break;
  }

  QTreeView::keyPressEvent(p_event);
}

void SearchResultView::contextMenuEvent(QContextMenuEvent *p_event) {
  QModelIndex idx = indexAt(p_event->pos());
  emit contextMenuRequested(idx, p_event->globalPos());
  p_event->accept();
}
