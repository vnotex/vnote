#include "stickerdragoverlay.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

using namespace vnotex;

namespace {
// Side of a painted handle square.
constexpr int kHandleSquare = kStickerHandleThickness * 2;

// Qt 6 deprecates QMouseEvent::pos()/globalPos(); the Qt 5.15 (win64-windows7)
// variant has no position()/globalPosition(). Route both through one helper.
QPoint eventPos(const QMouseEvent *p_event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return p_event->position().toPoint();
#else
  return p_event->pos();
#endif
}

QPoint eventGlobalPos(const QMouseEvent *p_event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return p_event->globalPosition().toPoint();
#else
  return p_event->globalPos();
#endif
}
} // namespace

StickerDragOverlay::StickerDragOverlay(QWidget *p_parent) : QWidget(p_parent) {
  // Mouse tracking is required or hover cursors never fire (no button is held).
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_Hover, true);
  setCursor(Qt::ArrowCursor);
  // Unconditional: a user who tabs straight here must hear the available keys
  // without first hovering the widget with a mouse.
  setAccessibleDescription(tr("Sticker edit mode. Drag the centre to move or an edge to resize; "
                              "arrow keys move, Shift with arrow keys resizes."));
  if (p_parent) {
    p_parent->installEventFilter(this);
  }
}

void StickerDragOverlay::setAccentColor(const QColor &p_accent) {
  m_accent = p_accent;
  update();
}

void StickerDragOverlay::setMoveIcon(const QIcon &p_icon) {
  m_moveIcon = p_icon;
  update();
}

void StickerDragOverlay::syncGeometry(QWidget *p_insetWidget) {
  m_insetWidget = p_insetWidget;
  reapplyGeometry();
}

void StickerDragOverlay::reapplyGeometry() {
  QWidget *p = parentWidget();
  if (!p) {
    return;
  }
  const int inset = m_insetWidget ? qMax(0, m_insetWidget->geometry().bottom() + 1) : 0;
  const int height = qMax(0, p->height() - inset);
  setGeometry(0, inset, p->width(), height);
  raise();
}

void StickerDragOverlay::cancelDrag() {
  m_dragging = false;
  m_zone = StickerDragZone::None;
}

bool StickerDragOverlay::eventFilter(QObject *p_watched, QEvent *p_event) {
  if (p_watched == parentWidget() &&
      (p_event->type() == QEvent::Resize || p_event->type() == QEvent::LayoutRequest ||
       p_event->type() == QEvent::Show)) {
    reapplyGeometry();
  }
  return QWidget::eventFilter(p_watched, p_event);
}

void StickerDragOverlay::paintEvent(QPaintEvent *p_event) {
  Q_UNUSED(p_event)
  if (!m_accent.isValid()) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRect box = rect().adjusted(1, 1, -2, -2);
  if (box.width() <= 0 || box.height() <= 0) {
    return;
  }

  QPen pen(m_accent);
  // A focused overlay is the keyboard target (arrows move, Shift+arrows
  // resize), so make it visibly distinct from its unfocused siblings.
  pen.setWidth(hasFocus() ? 2 : 1);
  pen.setStyle(hasFocus() ? Qt::SolidLine : Qt::DashLine);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(box);

  // Eight filled handle squares: four corners + four edge midpoints.
  const int half = kHandleSquare / 2;
  const int cx = box.center().x();
  const int cy = box.center().y();
  const QPoint anchors[8] = {box.topLeft(),
                             QPoint(cx, box.top()),
                             box.topRight(),
                             QPoint(box.left(), cy),
                             QPoint(box.right(), cy),
                             box.bottomLeft(),
                             QPoint(cx, box.bottom()),
                             box.bottomRight()};
  painter.setPen(Qt::NoPen);
  painter.setBrush(m_accent);
  for (const QPoint &anchor : anchors) {
    painter.drawRect(QRect(anchor.x() - half, anchor.y() - half, kHandleSquare, kHandleSquare));
  }

  // Centred move affordance: the themed move icon injected by the board (the
  // same one the removed header Move button used), or a plain drawn cross when
  // no icon is available.
  const int cross = qMin(kStickerCrossSize, qMin(box.width(), box.height()));
  if (cross >= 8) {
    const QRect crossRect(cx - cross / 2, cy - cross / 2, cross, cross);
    if (!m_moveIcon.isNull()) {
      m_moveIcon.paint(&painter, crossRect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    } else {
      QPen glyphPen(m_accent);
      glyphPen.setWidth(2);
      painter.setPen(glyphPen);
      painter.setBrush(Qt::NoBrush);
      const int arm = cross / 4;
      painter.drawLine(cx - arm, cy, cx + arm, cy);
      painter.drawLine(cx, cy - arm, cx, cy + arm);
    }
  }
}

void StickerDragOverlay::updateCursor(const QPoint &p_pos) {
  switch (stickerZoneAt(size(), p_pos)) {
  case StickerDragZone::Move:
    setCursor(Qt::SizeAllCursor);
    break;
  case StickerDragZone::Left:
  case StickerDragZone::Right:
    setCursor(Qt::SizeHorCursor);
    break;
  case StickerDragZone::Top:
  case StickerDragZone::Bottom:
    setCursor(Qt::SizeVerCursor);
    break;
  case StickerDragZone::TopLeft:
  case StickerDragZone::BottomRight:
    setCursor(Qt::SizeFDiagCursor);
    break;
  case StickerDragZone::TopRight:
  case StickerDragZone::BottomLeft:
    setCursor(Qt::SizeBDiagCursor);
    break;
  case StickerDragZone::None:
    setCursor(Qt::ArrowCursor);
    break;
  }
}

void StickerDragOverlay::mousePressEvent(QMouseEvent *p_event) {
  // Every press is swallowed: unlocked means edit mode, so the sticker content
  // below must never see input.
  p_event->accept();

  if (m_dragging && p_event->button() == Qt::RightButton) {
    cancelDrag();
    emit dragCancelled();
    return;
  }
  if (p_event->button() != Qt::LeftButton || m_dragging) {
    return;
  }

  const QPoint pos = eventPos(p_event);
  const StickerDragZone zone = stickerZoneAt(size(), pos);
  if (zone == StickerDragZone::None) {
    return;
  }

  m_dragging = true;
  m_zone = zone;
  // Focus (not grabKeyboard) so Esc arrives; nothing has to be released later.
  setFocus(Qt::MouseFocusReason);
  emit dragStarted(zone, eventGlobalPos(p_event));
}

void StickerDragOverlay::mouseMoveEvent(QMouseEvent *p_event) {
  p_event->accept();
  if (!m_dragging) {
    updateCursor(eventPos(p_event));
    return;
  }
  emit dragMoved(eventGlobalPos(p_event));
}

void StickerDragOverlay::mouseReleaseEvent(QMouseEvent *p_event) {
  p_event->accept();
  if (!m_dragging || p_event->button() != Qt::LeftButton) {
    return;
  }
  cancelDrag();
  emit dragFinished();
}

void StickerDragOverlay::keyPressEvent(QKeyEvent *p_event) {
  if (m_dragging) {
    if (p_event->key() == Qt::Key_Escape) {
      cancelDrag();
      emit dragCancelled();
      p_event->accept();
      return;
    }
    QWidget::keyPressEvent(p_event);
    return;
  }

  // Keyboard route (no mouse session): arrows move by a cell, Shift+arrows
  // resize by a cell. This is what replaces the removed Move menu / resize
  // dialog for keyboard and assistive-technology users.
  int dRow = 0;
  int dCol = 0;
  switch (p_event->key()) {
  case Qt::Key_Up:
    dRow = -1;
    break;
  case Qt::Key_Down:
    dRow = 1;
    break;
  case Qt::Key_Left:
    dCol = -1;
    break;
  case Qt::Key_Right:
    dCol = 1;
    break;
  default:
    QWidget::keyPressEvent(p_event);
    return;
  }

  if (p_event->modifiers() & Qt::ShiftModifier) {
    emit resizeRequested(dRow, dCol);
  } else {
    emit moveRequested(dRow, dCol);
  }
  p_event->accept();
}

bool StickerDragOverlay::event(QEvent *p_event) {
  // The border is drawn differently while focused, so repaint on focus changes.
  if (p_event->type() == QEvent::FocusIn || p_event->type() == QEvent::FocusOut) {
    update();
  }

  // Two zones need two different tooltips, which a single setToolTip() cannot
  // express.
  if (p_event->type() == QEvent::ToolTip) {
    auto *helpEvent = static_cast<QHelpEvent *>(p_event);
    const StickerDragZone zone = stickerZoneAt(size(), helpEvent->pos());
    if (zone == StickerDragZone::Move) {
      QToolTip::showText(helpEvent->globalPos(), tr("Drag to move (or use the arrow keys)"), this);
    } else if (zone != StickerDragZone::None) {
      QToolTip::showText(helpEvent->globalPos(),
                         tr("Drag to resize (or use Shift with the arrow keys)"), this);
    } else {
      QToolTip::hideText();
    }
    p_event->accept();
    return true;
  }
  return QWidget::event(p_event);
}
