#include <QtTest>

#include <utils/scrollpreservationpolicy.h>

using namespace vnotex;

namespace tests {

class TestScrollPreservationPolicy : public QObject {
  Q_OBJECT

private slots:
  // Pixel-based tests (7 cases)
  void testPixelAtTop();
  void testPixelAtBottomGrew();
  void testPixelAtBottomShrank();
  void testPixelMiddlePreservedGrew();
  void testPixelMiddleClampedShrank();
  void testPixelOldMaxZeroAmbiguity();
  void testPixelDefensiveNegatives();

  // Line-based tests (4 cases)
  void testLineValidRestored();
  void testLineTopPreserved();
  void testLineInvalidSentinel();
  void testLineArbitraryNegativeCollapsed();
};

// =============================================================================
// Pixel-based tests
// =============================================================================

void TestScrollPreservationPolicy::testPixelAtTop() {
  // At top, file unchanged max
  // Input: (0, 100, 100) → expected: 0 (stay at top)
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(0, 100, 100), 0);
}

void TestScrollPreservationPolicy::testPixelAtBottomGrew() {
  // At bottom, file grew
  // Input: (100, 100, 200) → expected: 200 (tail-follow)
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(100, 100, 200), 200);
}

void TestScrollPreservationPolicy::testPixelAtBottomShrank() {
  // At bottom, file shrank
  // Input: (100, 100, 50) → expected: 50 (tail-follow even when shrinking)
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(100, 100, 50), 50);
}

void TestScrollPreservationPolicy::testPixelMiddlePreservedGrew() {
  // Middle preserved, file grew
  // Input: (40, 100, 200) → expected: 40 (value preserved)
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(40, 100, 200), 40);
}

void TestScrollPreservationPolicy::testPixelMiddleClampedShrank() {
  // Middle clamped, file shrank
  // Input: (80, 100, 50) → expected: 50
  // 80 != 100 so NOT at-bottom; middle clamp rule: min(80, 50) = 50
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(80, 100, 50), 50);
}

void TestScrollPreservationPolicy::testPixelOldMaxZeroAmbiguity() {
  // oldMax == 0 ambiguity resolved as at-end (tail-follow)
  // Input: (0, 0, 200) → expected: 200 (at-end wins precedence, NOT 0)
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(0, 0, 200), 200);
}

void TestScrollPreservationPolicy::testPixelDefensiveNegatives() {
  // Three sub-cases: negatives sanitized to 0, then rules applied

  // Sub-case 1: oldValue < 0 → sanitize to 0, then at-start rule
  // (-5, 100, 200) → 0
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(-5, 100, 200), 0);

  // Sub-case 2: oldMax < 0 → sanitize to 0, then oldValue >= oldMax → at-end
  // (50, -10, 200) → 200
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(50, -10, 200), 200);

  // Sub-case 3: newMax < 0 → sanitize to 0, then clamp
  // (50, 100, -1) → min(50, 0) = 0
  QCOMPARE(ScrollPreservationPolicy::computeRestoredScrollValue(50, 100, -1), 0);
}

// =============================================================================
// Line-based tests
// =============================================================================

void TestScrollPreservationPolicy::testLineValidRestored() {
  // Valid line number restored unchanged
  // Input: 42 → expected: 42
  QCOMPARE(ScrollPreservationPolicy::computeRestoredReadModeLine(42), 42);
}

void TestScrollPreservationPolicy::testLineTopPreserved() {
  // Line 0 (top of document) preserved
  // Input: 0 → expected: 0
  QCOMPARE(ScrollPreservationPolicy::computeRestoredReadModeLine(0), 0);
}

void TestScrollPreservationPolicy::testLineInvalidSentinel() {
  // Invalid sentinel -1 preserved as -1 (caller skips restore)
  // Input: -1 → expected: -1
  QCOMPARE(ScrollPreservationPolicy::computeRestoredReadModeLine(-1), -1);
}

void TestScrollPreservationPolicy::testLineArbitraryNegativeCollapsed() {
  // Arbitrary negative collapsed to invalid sentinel -1
  // Input: -42 → expected: -1
  QCOMPARE(ScrollPreservationPolicy::computeRestoredReadModeLine(-42), -1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestScrollPreservationPolicy)
#include "test_scrollpreservationpolicy.moc"
