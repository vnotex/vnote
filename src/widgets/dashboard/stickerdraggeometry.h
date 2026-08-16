#ifndef STICKERDRAGGEOMETRY_H
#define STICKERDRAGGEOMETRY_H

#include <QMetaType>
#include <QPoint>
#include <QSize>

#include <controllers/dashboardcontroller.h>

namespace vnotex {

// Which affordance of the drag overlay a point falls on.
enum class StickerDragZone {
  None,
  Move,
  Left,
  Right,
  Top,
  Bottom,
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight
};

// Thickness (in px) of the border band that resolves to a resize zone.
constexpr int kStickerHandleThickness = 6;

// Side (in px) of the centred move-cross hit/paint area.
constexpr int kStickerCrossSize = 28;

// Hit zone for a point inside an overlay of the given size.
//
// Precedence: corners > edges > cross > None. The border band is capped at a
// third of the smaller dimension so the result stays deterministic even when
// the overlay is smaller than 2*handle + cross; when the bands overlap, Left
// beats Right and Top beats Bottom.
StickerDragZone stickerZoneAt(const QSize &p_size, const QPoint &p_pos);

// Raw candidate geometry from a drag expressed in whole cell units.
//
// Applies ONLY the min-span (>= 1) / non-inversion rule: for Top/Left drags the
// opposite edge stays anchored. Board bounds are deliberately NOT applied here
// — DashboardController::previewStickerGeometry owns that clamp, so a negative
// row/col legitimately survives this function.
StickerGeometry stickerDragTarget(StickerDragZone p_zone, const StickerGeometry &p_origin,
                                  int p_dRow, int p_dCol);

} // namespace vnotex

// StickerDragOverlay carries the zone as a signal argument; declaring the
// metatype lets QVariant-based consumers (QSignalSpy in the tests) record it.
Q_DECLARE_METATYPE(vnotex::StickerDragZone)

#endif // STICKERDRAGGEOMETRY_H
