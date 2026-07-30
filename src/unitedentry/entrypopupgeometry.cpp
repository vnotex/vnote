#include "entrypopupgeometry.h"

#include <QPoint>
#include <QtGlobal>

#include <limits>

using namespace vnotex;

const int EntryPopupGeometry::c_widthChars = 80;

const int EntryPopupGeometry::c_minWidth = 400;

const int EntryPopupGeometry::c_maxWidth = 900;

const int EntryPopupGeometry::c_height = 400;

const int EntryPopupGeometry::c_screenMargin = 8;

namespace {
// True when the available rect can be used for clamping at all.
bool canClamp(const QRect &p_rect) { return p_rect.isValid() && !p_rect.isEmpty(); }

// The horizontal margin actually honored on a screen of the given extent.
//
// Reduced on screens too narrow to honor the nominal margin so that the size
// clamp and the position clamp can never disagree: without this, an available
// rect narrower than 2 * c_screenMargin would yield a width of
// qMax(1, w - 16) while minX is still avail.x() + 8, placing the popup outside
// the rect despite the qMax(minX, ...) guard.
int effectiveMargin(int p_extent) {
  return qMin(EntryPopupGeometry::c_screenMargin, qMax(0, (p_extent - 1) / 2));
}

// Bring a 64-bit intermediate back into QRect's coordinate type without
// wrapping. The geometry math runs in qint64 so that a pathological char width
// or anchor coordinate cannot overflow before the clamps apply.
//
// The upper bound is extent-aware because QRect stores the *inclusive* end
// coordinate: QRect(QPoint(x, y), size) evaluates x + width - 1 internally, so
// even the intermediate x + width must be representable (Qt 6 debug builds use
// checked integers and assert otherwise). On the clamped path the screen bounds
// already guarantee this and the adjustment is a no-op; it only bites when
// clamping is disabled (invalid/unknown screen) and the anchor sits near
// INT_MAX.
int clampOrigin(qint64 p_value, int p_extent) {
  const qint64 lo = static_cast<qint64>(std::numeric_limits<int>::min());
  const qint64 hi = static_cast<qint64>(std::numeric_limits<int>::max()) - p_extent;
  return static_cast<int>(qBound(lo, p_value, qMax(lo, hi)));
}

// The single size implementation, taking the margin its caller already
// computed. Both public entry points route through this so the size clamp and
// the position clamp always use the *same* margin value; deriving the margin
// independently in the two is a bug.
QSize sizeWithMargin(const EntryPopupGeometry::Metrics &p_metrics, bool p_clamp, int p_marginX) {
  // A non-positive char width (headless font metrics, unknown font) must not
  // collapse the popup; fall back to a width of 1 so the floor takes over.
  const int charW = qMax(1, p_metrics.m_charWidth);

  const qint64 target = static_cast<qint64>(charW) * EntryPopupGeometry::c_widthChars;
  int w = static_cast<int>(qBound<qint64>(static_cast<qint64>(EntryPopupGeometry::c_minWidth),
                                          target,
                                          static_cast<qint64>(EntryPopupGeometry::c_maxWidth)));
  int h = EntryPopupGeometry::c_height;

  if (p_clamp) {
    // >= 1 by construction of effectiveMargin().
    w = qMin(w, p_metrics.m_availableRect.width() - 2 * p_marginX);

    // No vertical margin: the popup sits flush under the anchor, so reserving
    // one would only shrink it on screens barely taller than c_height.
    h = qMin(h, p_metrics.m_availableRect.height());
  }

  return QSize(w, h);
}
} // namespace

QSize EntryPopupGeometry::calculateSize(const Metrics &p_metrics) {
  const bool clamp = canClamp(p_metrics.m_availableRect);
  const int marginX = clamp ? effectiveMargin(p_metrics.m_availableRect.width()) : 0;
  return sizeWithMargin(p_metrics, clamp, marginX);
}

QRect EntryPopupGeometry::calculateGeometry(const Metrics &p_metrics) {
  const bool clamp = canClamp(p_metrics.m_availableRect);
  // Computed once and reused by both the size and the position clamp below.
  const int marginX = clamp ? effectiveMargin(p_metrics.m_availableRect.width()) : 0;

  const QSize size = sizeWithMargin(p_metrics, clamp, marginX);

  // Center the popup horizontally on the anchor and hang it below. Integer
  // centering truncates: unclamped widths are always even (c_widthChars is
  // even, as are both bounds), so this is exact; an odd width can only come
  // from the narrow-screen clamp below, where the popup fills the whole
  // clampable band and x is pinned to the left margin anyway. Both halves of
  // that rule are pinned by a test.
  qint64 x = static_cast<qint64>(p_metrics.m_anchorCenterX) - size.width() / 2;
  qint64 y = p_metrics.m_anchorBottomY;

  if (clamp) {
    const QRect &avail = p_metrics.m_availableRect;

    // Half-open arithmetic throughout: never QRect::right()/bottom(), which
    // are off by one.
    const qint64 availX = avail.x();
    const qint64 minX = availX + marginX;
    // The qMax() ordering is load-bearing, not defensive decoration: qBound()
    // asserts when min > max, and on a screen narrower than the popup the
    // naive maxX falls below minX.
    const qint64 maxX = qMax(minX, availX + avail.width() - size.width() - marginX);
    x = qBound(minX, x, maxX);

    const qint64 minY = avail.y();
    // Clamping upward pushes the popup over the toolbar rather than flipping
    // it above the anchor. Acceptable: it only triggers when the available
    // height is under ~c_height plus the toolbar offset.
    const qint64 maxY = qMax(minY, minY + avail.height() - size.height());
    y = qBound(minY, y, maxY);
  }

  return QRect(QPoint(clampOrigin(x, size.width()), clampOrigin(y, size.height())), size);
}
