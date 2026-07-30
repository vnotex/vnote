#include <QtTest>

#include <QPoint>
#include <QRect>
#include <QSize>

#include <limits>

#include <unitedentry/entrypopupgeometry.h>

using namespace vnotex;

namespace tests {

class TestEntryPopupGeometry : public QObject {
  Q_OBJECT

private slots:
  void testWidthFromCharWidth();
  void testWidthWindowsDefaultFont();
  void testWidthClampsUpToMinimum();
  void testWidthClampsDownToMaximum();
  void testWidthDegenerateCharWidth();
  void testHeightOnTallScreen();
  void testNarrowScreenShrinksWidth();
  void testUltraNarrowScreen();
  void testShortScreenShrinksHeight();
  void testCenteringUnderAnchor();
  void testWidthParityAndCentering();
  void testClampLeftEdge();
  void testClampRightEdge();
  void testClampBottomEdge();
  void testClampTopEdge();
  void testSecondaryMonitorNegativeX();
  void testSecondaryMonitorNegativeXY();
  void testNoAvailableRect();
  void testExtremeInputs();

private:
  static EntryPopupGeometry::Metrics metrics(int p_charWidth, const QRect &p_avail,
                                             int p_anchorCenterX = 0, int p_anchorBottomY = 0);
};

EntryPopupGeometry::Metrics TestEntryPopupGeometry::metrics(int p_charWidth, const QRect &p_avail,
                                                            int p_anchorCenterX,
                                                            int p_anchorBottomY) {
  EntryPopupGeometry::Metrics m;
  m.m_charWidth = p_charWidth;
  m.m_availableRect = p_avail;
  m.m_anchorCenterX = p_anchorCenterX;
  m.m_anchorBottomY = p_anchorBottomY;
  return m;
}

// 1. A normal font on a large screen yields charWidth * c_widthChars.
void TestEntryPopupGeometry::testWidthFromCharWidth() {
  const QSize size = EntryPopupGeometry::calculateSize(metrics(8, QRect(0, 0, 1920, 1080)));
  QCOMPARE(size.width(), 640);
  QVERIFY(size.width() >= EntryPopupGeometry::c_minWidth);
  QVERIFY(size.width() <= EntryPopupGeometry::c_maxWidth);
}

// 2. Segoe UI 9-10pt (the Windows default) reports an average char width of 6.
void TestEntryPopupGeometry::testWidthWindowsDefaultFont() {
  const QSize size = EntryPopupGeometry::calculateSize(metrics(6, QRect(0, 0, 1920, 1080)));
  QCOMPARE(size.width(), 480);
}

// 3. A tiny font clamps up to the floor.
void TestEntryPopupGeometry::testWidthClampsUpToMinimum() {
  const QSize size = EntryPopupGeometry::calculateSize(metrics(4, QRect(0, 0, 1920, 1080)));
  QCOMPARE(size.width(), EntryPopupGeometry::c_minWidth);
}

// 4. A large font clamps down to the cap.
void TestEntryPopupGeometry::testWidthClampsDownToMaximum() {
  const QSize size = EntryPopupGeometry::calculateSize(metrics(14, QRect(0, 0, 1920, 1080)));
  QCOMPARE(size.width(), EntryPopupGeometry::c_maxWidth);
}

// 5. A zero or negative char width must not collapse the popup.
void TestEntryPopupGeometry::testWidthDegenerateCharWidth() {
  const QRect avail(0, 0, 1920, 1080);
  QCOMPARE(EntryPopupGeometry::calculateSize(metrics(0, avail)).width(),
           EntryPopupGeometry::c_minWidth);
  QCOMPARE(EntryPopupGeometry::calculateSize(metrics(-5, avail)).width(),
           EntryPopupGeometry::c_minWidth);
}

// 6. Height is the fixed constant whenever the screen is tall enough.
void TestEntryPopupGeometry::testHeightOnTallScreen() {
  const QSize size = EntryPopupGeometry::calculateSize(metrics(8, QRect(0, 0, 1920, 1080)));
  QCOMPARE(size.height(), EntryPopupGeometry::c_height);
}

// 7. A screen narrower than the popup shrinks it and pins it to the left margin.
void TestEntryPopupGeometry::testNarrowScreenShrinksWidth() {
  const QRect avail(0, 0, 500, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 250, 100));
  QCOMPARE(rect.width(), 500 - 2 * EntryPopupGeometry::c_screenMargin);
  QCOMPARE(rect.x(), avail.x() + EntryPopupGeometry::c_screenMargin);
  QVERIFY(avail.contains(rect));
}

// 8. An ultra-narrow screen must not violate the qBound() precondition and must
// still yield a rect fully inside the available area.
void TestEntryPopupGeometry::testUltraNarrowScreen() {
  {
    const QRect avail(0, 0, 10, 600);
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 5, 100));
    // effectiveMargin(10) == 4, so the popup keeps a width of 2.
    QCOMPARE(rect.width(), 2);
    QVERIFY(avail.contains(rect));
  }
  {
    const QRect avail(0, 0, 1, 600);
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 0, 100));
    // effectiveMargin(1) == 0, so the popup keeps a width of 1.
    QCOMPARE(rect.width(), 1);
    QVERIFY(avail.contains(rect));
  }
}

// 9. A screen shorter than c_height shrinks the popup and pins it to the top.
void TestEntryPopupGeometry::testShortScreenShrinksHeight() {
  const QRect avail(0, 0, 1920, 300);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 960, 100));
  QCOMPARE(rect.height(), 300);
  QCOMPARE(rect.y(), avail.y());
  QVERIFY(avail.contains(rect));
}

// 10. With the anchor mid-screen the popup is centered on it and hangs below.
void TestEntryPopupGeometry::testCenteringUnderAnchor() {
  const QRect avail(0, 0, 1920, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 960, 100));
  QCOMPARE(rect.width(), 640);
  QCOMPARE(rect.x(), 960 - 640 / 2);
  QCOMPARE(rect.y(), 100);
  QVERIFY(avail.contains(rect));
}

// 11. Pin the integer centering rule. Unclamped widths are always even
// (charWidth * 80, or one of the even bounds), so centering is exact; an odd
// width can only come from the narrow-screen clamp, where the popup fills the
// whole clampable band and x is pinned to the left margin.
void TestEntryPopupGeometry::testWidthParityAndCentering() {
  const QRect wide(0, 0, 1920, 1080);
  for (int charWidth = 1; charWidth <= 20; ++charWidth) {
    const QSize size = EntryPopupGeometry::calculateSize(metrics(charWidth, wide));
    QVERIFY2(size.width() % 2 == 0, qPrintable(QStringLiteral("odd width for charWidth %1: %2")
                                                   .arg(charWidth)
                                                   .arg(size.width())));
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(charWidth, wide, 960, 100));
    QCOMPARE(rect.x(), 960 - size.width() / 2);
  }

  // Odd width via the narrow-screen path: 501 - 2 * 8 == 485.
  const QRect narrow(0, 0, 501, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, narrow, 250, 100));
  QCOMPARE(rect.width(), 485);
  QCOMPARE(rect.x(), narrow.x() + EntryPopupGeometry::c_screenMargin);
  QVERIFY(narrow.contains(rect));
}

// 12. An anchor near, or far outside, the left edge pins the popup to the margin.
void TestEntryPopupGeometry::testClampLeftEdge() {
  const QRect avail(0, 0, 1920, 1080);
  for (int anchorX : {5, -5000}) {
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, anchorX, 100));
    QCOMPARE(rect.x(), avail.x() + EntryPopupGeometry::c_screenMargin);
    QVERIFY(avail.contains(rect));
  }
}

// 13. An anchor near, or far outside, the right edge pins the popup to the margin.
void TestEntryPopupGeometry::testClampRightEdge() {
  const QRect avail(0, 0, 1920, 1080);
  for (int anchorX : {1915, 5000}) {
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, anchorX, 100));
    QCOMPARE(rect.x() + rect.width(),
             avail.x() + avail.width() - EntryPopupGeometry::c_screenMargin);
    QVERIFY(avail.contains(rect));
  }
}

// 14. An anchor near, or far below, the bottom edge pushes the popup up flush
// with the bottom of the available rect (no vertical margin).
void TestEntryPopupGeometry::testClampBottomEdge() {
  const QRect avail(0, 0, 1920, 1080);
  for (int anchorY : {1075, 5000}) {
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 960, anchorY));
    QCOMPARE(rect.y() + rect.height(), avail.y() + avail.height());
    QVERIFY(avail.contains(rect));
  }
}

// 15. An anchor far above the top pins the popup to the top of the rect.
void TestEntryPopupGeometry::testClampTopEdge() {
  const QRect avail(0, 0, 1920, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 960, -5000));
  QCOMPARE(rect.y(), avail.y());
  QVERIFY(avail.contains(rect));
}

// 16. A monitor left of the primary has a negative origin; clamping must land
// inside that rect, not at 0.
void TestEntryPopupGeometry::testSecondaryMonitorNegativeX() {
  const QRect avail(-1920, 0, 1920, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, -1919, 100));
  QCOMPARE(rect.x(), avail.x() + EntryPopupGeometry::c_screenMargin);
  QVERIFY(avail.contains(rect));

  const QRect far = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 5000, 100));
  QCOMPARE(far.x() + far.width(), avail.x() + avail.width() - EntryPopupGeometry::c_screenMargin);
  QVERIFY(avail.contains(far));
}

// 17. Same, for a monitor with both coordinates negative.
void TestEntryPopupGeometry::testSecondaryMonitorNegativeXY() {
  const QRect avail(-1920, -1080, 1920, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, -1919, -99999));
  QCOMPARE(rect.x(), avail.x() + EntryPopupGeometry::c_screenMargin);
  QCOMPARE(rect.y(), avail.y());
  QVERIFY(avail.contains(rect));

  const QRect below = EntryPopupGeometry::calculateGeometry(metrics(8, avail, -960, 99999));
  QCOMPARE(below.y() + below.height(), avail.y() + avail.height());
  QVERIFY(avail.contains(below));
}

// 18. An invalid or empty available rect disables clamping entirely.
void TestEntryPopupGeometry::testNoAvailableRect() {
  const QVector<QRect> rects{QRect(), QRect(0, 0, 0, 0), QRect(10, 10, -5, -5)};
  for (const auto &avail : rects) {
    const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 100, 50));
    QCOMPARE(rect.width(), 640);
    QCOMPARE(rect.height(), EntryPopupGeometry::c_height);
    QCOMPARE(rect.x(), 100 - 640 / 2);
    QCOMPARE(rect.y(), 50);
  }
}

// 19. Extreme integer inputs must neither overflow nor violate the qBound()
// precondition. None of these is reachable from real widget geometry or font
// metrics; they pin that the clamp guarantee holds over the whole input domain.
void TestEntryPopupGeometry::testExtremeInputs() {
  const int intMin = std::numeric_limits<int>::min();
  const int intMax = std::numeric_limits<int>::max();
  const QRect avail(0, 0, 1920, 1080);

  // A pathological char width must still hit the cap, not wrap into the floor.
  QCOMPARE(EntryPopupGeometry::calculateSize(metrics(intMax, avail)).width(),
           EntryPopupGeometry::c_maxWidth);

  // An anchor at the extremes clamps to the corresponding screen edge.
  const QRect farLeft = EntryPopupGeometry::calculateGeometry(metrics(8, avail, intMin, 100));
  QCOMPARE(farLeft.x(), avail.x() + EntryPopupGeometry::c_screenMargin);
  QVERIFY(avail.contains(farLeft));

  const QRect farRight = EntryPopupGeometry::calculateGeometry(metrics(8, avail, intMax, 100));
  QCOMPARE(farRight.x() + farRight.width(),
           avail.x() + avail.width() - EntryPopupGeometry::c_screenMargin);
  QVERIFY(avail.contains(farRight));

  const QRect farUp = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 960, intMin));
  QCOMPARE(farUp.y(), avail.y());
  QVERIFY(avail.contains(farUp));

  const QRect farDown = EntryPopupGeometry::calculateGeometry(metrics(8, avail, 960, intMax));
  QCOMPARE(farDown.y() + farDown.height(), avail.y() + avail.height());
  QVERIFY(avail.contains(farDown));

  // A screen rect whose half-open end lands on INT_MAX must not overflow.
  const QRect huge(intMax - 4000, 0, 4000, 1080);
  const QRect rect = EntryPopupGeometry::calculateGeometry(metrics(8, huge, intMax, 100));
  QCOMPARE(rect.x() + rect.width(), huge.x() + huge.width() - EntryPopupGeometry::c_screenMargin);
  QVERIFY(huge.contains(rect));

  // Unclamped path with extreme anchors: QRect stores the inclusive end
  // coordinate and evaluates x + width - 1 internally, so the origin must be
  // pulled back far enough that even the intermediate x + width is
  // representable. All assertions are done in qint64 so the test itself cannot
  // overflow.
  for (int anchor : {intMax, intMin}) {
    const QRect free = EntryPopupGeometry::calculateGeometry(metrics(8, QRect(), anchor, anchor));
    QCOMPARE(free.size(), QSize(640, EntryPopupGeometry::c_height));
    QVERIFY(free.isValid());
    QVERIFY(static_cast<qint64>(free.x()) + free.width() <= static_cast<qint64>(intMax));
    QVERIFY(static_cast<qint64>(free.y()) + free.height() <= static_cast<qint64>(intMax));
    QVERIFY(static_cast<qint64>(free.x()) >= static_cast<qint64>(intMin));
    QVERIFY(static_cast<qint64>(free.y()) >= static_cast<qint64>(intMin));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestEntryPopupGeometry)
#include "test_entrypopupgeometry.moc"
