#ifndef ENTRYPOPUPGEOMETRY_H
#define ENTRYPOPUPGEOMETRY_H

#include <QRect>
#include <QSize>

namespace vnotex {

// Pure geometry policy of the United Entry popup.
//
// Kept free of any widget/service dependency so it can be unit tested in
// isolation: it touches only QtCore value types.
class EntryPopupGeometry {
public:
  // All coordinates are global (screen) coordinates.
  struct Metrics {
    // Global x of the horizontal center of the anchor widget (the combo box).
    int m_anchorCenterX = 0;

    // Global y of the bottom edge of the anchor widget.
    int m_anchorBottomY = 0;

    // fontMetrics().averageCharWidth() of the popup.
    int m_charWidth = 0;

    // availableGeometry() of the screen the popup should live on. An invalid
    // or empty rect disables clamping (headless/unknown screen).
    QRect m_availableRect;
  };

  static QSize calculateSize(const Metrics &p_metrics);

  static QRect calculateGeometry(const Metrics &p_metrics);

  // Nominal width of the popup, in average character widths of its own font.
  // This is a sizing heuristic, not a guarantee of 80 rendered characters:
  // averageCharWidth() is font metadata, and proportional/CJK/fallback runs
  // vary.
  static const int c_widthChars;

  static const int c_minWidth;

  static const int c_maxWidth;

  static const int c_height;

  // Horizontal gap kept between the popup and the screen edges. Not applied
  // vertically: the popup sits flush under the anchor.
  static const int c_screenMargin;
};

} // namespace vnotex

#endif // ENTRYPOPUPGEOMETRY_H
