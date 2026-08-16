#ifndef STICKERDRAGOVERLAY_H
#define STICKERDRAGOVERLAY_H

#include <QColor>
#include <QIcon>
#include <QPoint>
#include <QPointer>
#include <QWidget>

#include "stickerdraggeometry.h"

namespace vnotex {

// Edit-mode overlay drawn over one unlocked sticker's content area.
//
// Pure leaf presentational widget: it takes NO ServiceLocator (precedent:
// InlineBanner) and paints with QPainter using colors the board injects via
// setAccentColor()/setMoveIcon(), so it re-themes without any stylesheet or
// color literal.
//
// It covers the sticker's frame BELOW the header band, so the header's Remove
// affordance stays clickable while unlocked, while every mouse event over the
// sticker content is swallowed (unlocked == edit mode).
//
// It is also the keyboard route: it is focusable (Tab reaches every unlocked
// sticker), arrow keys request a one-cell move and Shift+arrows a one-cell
// resize. That is what replaces the removed header Move menu / resize dialog,
// so the board is not mouse-only.
//
// It owns no geometry policy: it reports gestures (zone + global positions, or
// keyboard deltas) and DashboardBoard turns them into candidate geometries.

class StickerDragOverlay : public QWidget {
  Q_OBJECT
public:
  explicit StickerDragOverlay(QWidget *p_parent = nullptr);

  // Color used by paintEvent for the border, handles and move-cross. Injected
  // by the board from ThemeService (the drop-indicator ghost owns the
  // valid/invalid pair; the overlay itself has no validity state).
  void setAccentColor(const QColor &p_accent);

  // Icon painted at the centre for the move affordance (the same move.svg the
  // header's Move menu used to show). Injected already tinted, because the
  // overlay has no ServiceLocator and cannot resolve a theme icon itself. When
  // unset, a plain drawn cross glyph is used instead.
  void setMoveIcon(const QIcon &p_icon);

  // Cover the parent below p_insetWidget (the frame's header band) and raise
  // above siblings. The widget pointer (not a fixed inset) is remembered so the
  // overlay re-derives the band after any parent resize / relayout — the header
  // has no valid geometry yet when the overlay is created.
  void syncGeometry(QWidget *p_insetWidget);

  // Abort any in-flight session. Idempotent, synchronous, emits nothing.
  void cancelDrag();

  bool isDragging() const { return m_dragging; }

signals:
  void dragStarted(vnotex::StickerDragZone p_zone, const QPoint &p_globalPos);
  void dragMoved(const QPoint &p_globalPos);
  void dragFinished();
  void dragCancelled();

  // Keyboard equivalents of the two drag gestures, in whole cells. Emitted only
  // while no mouse session is running.
  void moveRequested(int p_dRow, int p_dCol);
  void resizeRequested(int p_dRowSpan, int p_dColSpan);

protected:
  void paintEvent(QPaintEvent *p_event) override;
  void mousePressEvent(QMouseEvent *p_event) override;
  void mouseMoveEvent(QMouseEvent *p_event) override;
  void mouseReleaseEvent(QMouseEvent *p_event) override;
  void keyPressEvent(QKeyEvent *p_event) override;
  bool event(QEvent *p_event) override;
  bool eventFilter(QObject *p_watched, QEvent *p_event) override;

private:
  void updateCursor(const QPoint &p_pos);

  // Re-apply the covering geometry for the current parent size.
  void reapplyGeometry();

  QColor m_accent;
  QIcon m_moveIcon;

  QPointer<QWidget> m_insetWidget;

  bool m_dragging = false;
  StickerDragZone m_zone = StickerDragZone::None;
};

} // namespace vnotex

#endif // STICKERDRAGOVERLAY_H
