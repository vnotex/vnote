#include "stickerdraggeometry.h"

#include <QtGlobal>

using namespace vnotex;

namespace {

// Border band width for the given overlay size: the nominal thickness, capped
// at a third of the smaller dimension so a tiny overlay still leaves a usable
// interior (and never resolves an interior point to two opposite edges).
int handleBand(const QSize &p_size) {
  const int smaller = qMin(p_size.width(), p_size.height());
  return qBound(1, kStickerHandleThickness, qMax(1, smaller / 3));
}

// Move the leading edge by p_delta while keeping the trailing edge anchored.
// The span never drops below 1 (the edge cannot cross its opposite).
void dragLeadingEdge(int p_delta, int *p_pos, int *p_span) {
  const int trailing = *p_pos + *p_span;
  int pos = *p_pos + p_delta;
  if (pos > trailing - 1) {
    pos = trailing - 1;
  }
  *p_pos = pos;
  *p_span = trailing - pos;
}

} // namespace

StickerDragZone vnotex::stickerZoneAt(const QSize &p_size, const QPoint &p_pos) {
  const int w = p_size.width();
  const int h = p_size.height();
  if (w <= 0 || h <= 0) {
    return StickerDragZone::None;
  }
  if (p_pos.x() < 0 || p_pos.y() < 0 || p_pos.x() >= w || p_pos.y() >= h) {
    return StickerDragZone::None;
  }

  const int band = handleBand(p_size);
  const bool left = p_pos.x() < band;
  const bool right = p_pos.x() >= w - band;
  const bool top = p_pos.y() < band;
  const bool bottom = p_pos.y() >= h - band;

  // Corners first, then edges; Left/Top win over Right/Bottom when the bands
  // overlap on a degenerate size.
  if (top && left) {
    return StickerDragZone::TopLeft;
  }
  if (top && right) {
    return StickerDragZone::TopRight;
  }
  if (bottom && left) {
    return StickerDragZone::BottomLeft;
  }
  if (bottom && right) {
    return StickerDragZone::BottomRight;
  }
  if (left) {
    return StickerDragZone::Left;
  }
  if (right) {
    return StickerDragZone::Right;
  }
  if (top) {
    return StickerDragZone::Top;
  }
  if (bottom) {
    return StickerDragZone::Bottom;
  }

  // Centred move-cross: a square of kStickerCrossSize, shrunk if the overlay
  // cannot host it at full size.
  const int cross = qMin(kStickerCrossSize, qMin(w, h) - 2 * band);
  if (cross > 0) {
    const int half = cross / 2;
    const int dx = qAbs(p_pos.x() - w / 2);
    const int dy = qAbs(p_pos.y() - h / 2);
    if (dx <= half && dy <= half) {
      return StickerDragZone::Move;
    }
  }

  return StickerDragZone::None;
}

StickerGeometry vnotex::stickerDragTarget(StickerDragZone p_zone, const StickerGeometry &p_origin,
                                          int p_dRow, int p_dCol) {
  StickerGeometry geo = p_origin;
  geo.rowSpan = qMax(1, geo.rowSpan);
  geo.colSpan = qMax(1, geo.colSpan);

  switch (p_zone) {
  case StickerDragZone::Move:
    geo.row += p_dRow;
    geo.col += p_dCol;
    break;
  case StickerDragZone::Left:
    dragLeadingEdge(p_dCol, &geo.col, &geo.colSpan);
    break;
  case StickerDragZone::Right:
    geo.colSpan = qMax(1, geo.colSpan + p_dCol);
    break;
  case StickerDragZone::Top:
    dragLeadingEdge(p_dRow, &geo.row, &geo.rowSpan);
    break;
  case StickerDragZone::Bottom:
    geo.rowSpan = qMax(1, geo.rowSpan + p_dRow);
    break;
  case StickerDragZone::TopLeft:
    dragLeadingEdge(p_dRow, &geo.row, &geo.rowSpan);
    dragLeadingEdge(p_dCol, &geo.col, &geo.colSpan);
    break;
  case StickerDragZone::TopRight:
    dragLeadingEdge(p_dRow, &geo.row, &geo.rowSpan);
    geo.colSpan = qMax(1, geo.colSpan + p_dCol);
    break;
  case StickerDragZone::BottomLeft:
    geo.rowSpan = qMax(1, geo.rowSpan + p_dRow);
    dragLeadingEdge(p_dCol, &geo.col, &geo.colSpan);
    break;
  case StickerDragZone::BottomRight:
    geo.rowSpan = qMax(1, geo.rowSpan + p_dRow);
    geo.colSpan = qMax(1, geo.colSpan + p_dCol);
    break;
  case StickerDragZone::None:
    break;
  }

  return geo;
}
