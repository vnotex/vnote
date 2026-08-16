#include <QtTest>

#include <widgets/dashboard/stickerdraggeometry.h>

using namespace vnotex;

namespace tests {

// Pure drag math: hit zones and raw candidate geometry. GUILESS — no widget is
// constructed, only QSize/QPoint value types are used.
class TestStickerDragGeometry : public QObject {
  Q_OBJECT

private slots:
  void testZoneCentreIsMove();
  void testZoneEdges();
  void testZoneCorners();
  void testZoneOutsideAndDeadBand();
  void testZoneDegenerateSize();

  void testTargetMoveTranslatesOnly();
  void testTargetRightGrowsColSpan();
  void testTargetLeftAnchorsRightEdge();
  void testTargetTopAnchorsBottomEdge();
  void testTargetSpansNeverInvert();
  void testTargetCornerCombines();
  void testTargetIsNotBoardClamped();
};

void TestStickerDragGeometry::testZoneCentreIsMove() {
  const QSize size(200, 120);
  QCOMPARE(stickerZoneAt(size, QPoint(100, 60)), StickerDragZone::Move);
}

void TestStickerDragGeometry::testZoneEdges() {
  const QSize size(200, 120);
  QCOMPARE(stickerZoneAt(size, QPoint(1, 60)), StickerDragZone::Left);
  QCOMPARE(stickerZoneAt(size, QPoint(198, 60)), StickerDragZone::Right);
  QCOMPARE(stickerZoneAt(size, QPoint(100, 1)), StickerDragZone::Top);
  QCOMPARE(stickerZoneAt(size, QPoint(100, 118)), StickerDragZone::Bottom);
}

void TestStickerDragGeometry::testZoneCorners() {
  const QSize size(200, 120);
  QCOMPARE(stickerZoneAt(size, QPoint(0, 0)), StickerDragZone::TopLeft);
  QCOMPARE(stickerZoneAt(size, QPoint(199, 0)), StickerDragZone::TopRight);
  QCOMPARE(stickerZoneAt(size, QPoint(0, 119)), StickerDragZone::BottomLeft);
  QCOMPARE(stickerZoneAt(size, QPoint(199, 119)), StickerDragZone::BottomRight);
}

void TestStickerDragGeometry::testZoneOutsideAndDeadBand() {
  const QSize size(200, 120);
  // Outside the widget.
  QCOMPARE(stickerZoneAt(size, QPoint(-1, 60)), StickerDragZone::None);
  QCOMPARE(stickerZoneAt(size, QPoint(200, 60)), StickerDragZone::None);
  // Between the border band and the centred cross: inert.
  QCOMPARE(stickerZoneAt(size, QPoint(40, 60)), StickerDragZone::None);
  // Empty size.
  QCOMPARE(stickerZoneAt(QSize(0, 0), QPoint(0, 0)), StickerDragZone::None);
}

void TestStickerDragGeometry::testZoneDegenerateSize() {
  // Smaller than 2*handle + cross: the documented precedence still resolves
  // deterministically (corners first, then Left/Top over Right/Bottom).
  const QSize tiny(6, 6);
  QCOMPARE(stickerZoneAt(tiny, QPoint(0, 0)), StickerDragZone::TopLeft);
  QCOMPARE(stickerZoneAt(tiny, QPoint(5, 5)), StickerDragZone::BottomRight);
  QCOMPARE(stickerZoneAt(tiny, QPoint(3, 0)), StickerDragZone::Top);
}

void TestStickerDragGeometry::testTargetMoveTranslatesOnly() {
  const StickerGeometry origin{2, 3, 2, 4};
  const StickerGeometry out = stickerDragTarget(StickerDragZone::Move, origin, 1, -2);
  QCOMPARE(out.row, 3);
  QCOMPARE(out.col, 1);
  QCOMPARE(out.rowSpan, origin.rowSpan);
  QCOMPARE(out.colSpan, origin.colSpan);
}

void TestStickerDragGeometry::testTargetRightGrowsColSpan() {
  const StickerGeometry origin{2, 3, 2, 4};
  const StickerGeometry out = stickerDragTarget(StickerDragZone::Right, origin, 0, 2);
  QCOMPARE(out.col, 3);
  QCOMPARE(out.colSpan, 6);
  QCOMPARE(out.row, origin.row);
  QCOMPARE(out.rowSpan, origin.rowSpan);
}

void TestStickerDragGeometry::testTargetLeftAnchorsRightEdge() {
  const StickerGeometry origin{2, 3, 2, 4};
  const StickerGeometry out = stickerDragTarget(StickerDragZone::Left, origin, 0, -2);
  QCOMPARE(out.col, 1);
  QCOMPARE(out.colSpan, 6);
  // Right edge unchanged.
  QCOMPARE(out.col + out.colSpan, origin.col + origin.colSpan);
}

void TestStickerDragGeometry::testTargetTopAnchorsBottomEdge() {
  const StickerGeometry origin{4, 1, 3, 2};
  const StickerGeometry out = stickerDragTarget(StickerDragZone::Top, origin, -2, 0);
  QCOMPARE(out.row, 2);
  QCOMPARE(out.rowSpan, 5);
  QCOMPARE(out.row + out.rowSpan, origin.row + origin.rowSpan);
}

void TestStickerDragGeometry::testTargetSpansNeverInvert() {
  const StickerGeometry origin{4, 4, 2, 2};

  // Shrinking past the opposite edge stops at span 1.
  const StickerGeometry right = stickerDragTarget(StickerDragZone::Right, origin, 0, -10);
  QCOMPARE(right.colSpan, 1);
  const StickerGeometry bottom = stickerDragTarget(StickerDragZone::Bottom, origin, -10, 0);
  QCOMPARE(bottom.rowSpan, 1);

  const StickerGeometry left = stickerDragTarget(StickerDragZone::Left, origin, 0, 10);
  QCOMPARE(left.colSpan, 1);
  QCOMPARE(left.col, origin.col + origin.colSpan - 1);
  const StickerGeometry top = stickerDragTarget(StickerDragZone::Top, origin, 10, 0);
  QCOMPARE(top.rowSpan, 1);
  QCOMPARE(top.row, origin.row + origin.rowSpan - 1);
}

void TestStickerDragGeometry::testTargetCornerCombines() {
  const StickerGeometry origin{3, 3, 2, 2};
  const StickerGeometry out = stickerDragTarget(StickerDragZone::TopLeft, origin, -1, -1);
  QCOMPARE(out.row, 2);
  QCOMPARE(out.col, 2);
  QCOMPARE(out.rowSpan, 3);
  QCOMPARE(out.colSpan, 3);

  const StickerGeometry br = stickerDragTarget(StickerDragZone::BottomRight, origin, 2, 1);
  QCOMPARE(br.row, 3);
  QCOMPARE(br.col, 3);
  QCOMPARE(br.rowSpan, 4);
  QCOMPARE(br.colSpan, 3);
}

void TestStickerDragGeometry::testTargetIsNotBoardClamped() {
  // Board bounds belong to DashboardController::previewStickerGeometry; the
  // pure math must let a negative row/col through untouched.
  const StickerGeometry origin{0, 0, 1, 1};
  const StickerGeometry out = stickerDragTarget(StickerDragZone::Move, origin, -3, -5);
  QCOMPARE(out.row, -3);
  QCOMPARE(out.col, -5);

  const StickerGeometry wide = stickerDragTarget(StickerDragZone::Right, origin, 0, 999);
  QCOMPARE(wide.colSpan, 1000);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestStickerDragGeometry)
#include "test_stickerdraggeometry.moc"
