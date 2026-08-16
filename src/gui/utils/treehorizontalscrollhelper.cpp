#include "treehorizontalscrollhelper.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QHeaderView>
#include <QTimer>
#include <QTreeView>

#include <gui/utils/widgetutils.h>

using namespace vnotex;

TreeHorizontalScrollHelper::TreeHorizontalScrollHelper(QTreeView *p_view)
    : QObject(p_view), m_view(p_view) {
  Q_ASSERT(m_view);

  m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  auto *hdr = m_view->header();
  if (hdr) {
    // A hidden header is still active: stretchLastSection would pin column 0 to
    // the viewport width and no scrollbar could ever appear.
    hdr->setStretchLastSection(false);
  }

  m_timer = new QTimer(this);
  m_timer->setSingleShot(true);
  m_timer->setInterval(0);
  connect(m_timer, &QTimer::timeout, this, &TreeHorizontalScrollHelper::recompute);

  m_view->viewport()->installEventFilter(this);

  connect(m_view, &QTreeView::expanded, this, &TreeHorizontalScrollHelper::scheduleRecompute);
  connect(m_view, &QTreeView::collapsed, this, &TreeHorizontalScrollHelper::scheduleRecompute);

  attachModel();
  scheduleRecompute();
}

bool TreeHorizontalScrollHelper::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (m_view && p_obj == m_view->viewport()) {
    switch (p_event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
      scheduleRecompute();
      break;

    case QEvent::Paint:
      // Cheap pointer check to catch a runtime model swap (there is no
      // modelChanged signal on QAbstractItemView).
      if (m_model != m_view->model()) {
        attachModel();
        scheduleRecompute();
      }
      break;

    default:
      break;
    }
  }

  return QObject::eventFilter(p_obj, p_event);
}

void TreeHorizontalScrollHelper::scheduleRecompute() {
  if (m_timer && !m_timer->isActive()) {
    m_timer->start();
  }
}

void TreeHorizontalScrollHelper::attachModel() {
  if (!m_view) {
    return;
  }

  auto *model = m_view->model();
  if (m_model == model) {
    return;
  }

  if (m_model) {
    disconnect(m_model, nullptr, this, nullptr);
  }

  m_model = model;

  if (m_model) {
    connect(m_model, &QAbstractItemModel::modelReset, this,
            &TreeHorizontalScrollHelper::scheduleRecompute);
    connect(m_model, &QAbstractItemModel::rowsInserted, this,
            &TreeHorizontalScrollHelper::scheduleRecompute);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this,
            &TreeHorizontalScrollHelper::scheduleRecompute);
    connect(m_model, &QAbstractItemModel::layoutChanged, this,
            &TreeHorizontalScrollHelper::scheduleRecompute);
    connect(m_model, &QAbstractItemModel::dataChanged, this,
            &TreeHorizontalScrollHelper::scheduleRecompute);
  }
}

void TreeHorizontalScrollHelper::recompute() {
  if (!m_view || m_updating) {
    return;
  }

  attachModel();

  if (!m_view->model() || m_view->model()->columnCount() <= 0) {
    return;
  }

  m_updating = true;

  // The section only exists once a model with at least one column is set.
  // The helper owns the width; ResizeToContents would fight setColumnWidth().
  auto *hdr = m_view->header();
  if (hdr && hdr->count() > 0 && hdr->sectionResizeMode(0) != QHeaderView::Interactive) {
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
  }

  // QTreeView::sizeHintForColumn() is protected, so let the view size the column
  // to its contents first and read the result back. The section stays
  // Interactive, so this is a one-shot measurement rather than a resize policy.
  m_view->resizeColumnToContents(0);
  const int content = m_view->columnWidth(0) + m_view->frameWidth() * 2;
  const int width = qMax(m_view->viewport()->width(), content);
  if (m_view->columnWidth(0) != width) {
    m_view->setColumnWidth(0, width);
  }

  m_updating = false;
}

// Defined here rather than in widgetutils.cpp on purpose: many test targets
// compile widgetutils.cpp without this helper, and keeping the definition in
// this TU keeps that dependency out of their link line.
void WidgetUtils::showHorizontalScrollbar(QTreeView *p_view) {
  if (!p_view) {
    return;
  }

  // Parented to @p_view, thus no need to keep the pointer.
  new TreeHorizontalScrollHelper(p_view);
}
