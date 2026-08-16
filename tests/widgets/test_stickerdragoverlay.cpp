#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

#include <widgets/dashboard/stickerdragoverlay.h>

using namespace vnotex;

namespace tests {

// GUI test for the edit-mode overlay: geometry syncing, gesture signals, and
// the "unlocked swallows content input" rule. Needs a QApplication.
class TestStickerDragOverlay : public QObject {
  Q_OBJECT

private slots:
  void testConstructorStateIsAccessible();
  void testCoversParentBelowHeaderWhenShown();
  void testParentResizeResyncsGeometry();
  void testDragEmitsStartMoveFinish();
  void testEscapeCancelsSession();
  void testPressOutsideAnyZoneIsSwallowed();
  void testCancelDragIsIdempotent();
  void testArrowKeysRequestMove();
  void testShiftArrowKeysRequestResize();
  void testKeyboardIsInertDuringMouseDrag();

private:
  // Parent frame with a fixed-height "header" band, already shown.
  QWidget *makeParent(QWidget **p_header) const;
};

QWidget *TestStickerDragOverlay::makeParent(QWidget **p_header) const {
  auto *parent = new QWidget();
  parent->resize(300, 200);
  auto *header = new QWidget(parent);
  header->setGeometry(0, 0, 300, 20);
  if (p_header) {
    *p_header = header;
  }
  parent->show();
  return parent;
}

void TestStickerDragOverlay::testConstructorStateIsAccessible() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));

  auto *overlay = new StickerDragOverlay(parent.data());
  // Keyboard reachable, and self-describing BEFORE any mouse event: tabbing
  // straight to an overlay must announce the move/resize keys.
  QCOMPARE(overlay->focusPolicy(), Qt::StrongFocus);
  QVERIFY(!overlay->accessibleDescription().isEmpty());
  QVERIFY(overlay->hasMouseTracking());
}

void TestStickerDragOverlay::testCoversParentBelowHeaderWhenShown() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QVERIFY(overlay->isVisible());
  QCOMPARE(overlay->geometry(), QRect(0, 20, 300, 180));
}

void TestStickerDragOverlay::testParentResizeResyncsGeometry() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  parent->resize(400, 300);
  QTRY_COMPARE(overlay->geometry(), QRect(0, 20, 400, 280));
}

void TestStickerDragOverlay::testDragEmitsStartMoveFinish() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QSignalSpy startSpy(overlay, &StickerDragOverlay::dragStarted);
  QSignalSpy moveSpy(overlay, &StickerDragOverlay::dragMoved);
  QSignalSpy finishSpy(overlay, &StickerDragOverlay::dragFinished);

  // Left edge -> Left zone.
  const QPoint edge(1, overlay->height() / 2);
  QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, edge);
  QCOMPARE(startSpy.count(), 1);
  QCOMPARE(startSpy.at(0).at(0).value<StickerDragZone>(), StickerDragZone::Left);
  QVERIFY(overlay->isDragging());

  QTest::mouseMove(overlay, edge + QPoint(40, 0));
  QTRY_VERIFY(moveSpy.count() >= 1);

  QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, edge + QPoint(40, 0));
  QCOMPARE(finishSpy.count(), 1);
  QVERIFY(!overlay->isDragging());
}

void TestStickerDragOverlay::testEscapeCancelsSession() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QSignalSpy cancelSpy(overlay, &StickerDragOverlay::dragCancelled);
  QSignalSpy finishSpy(overlay, &StickerDragOverlay::dragFinished);

  const QPoint centre(overlay->width() / 2, overlay->height() / 2);
  QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, centre);
  QVERIFY(overlay->isDragging());

  QTest::keyClick(overlay, Qt::Key_Escape);
  QCOMPARE(cancelSpy.count(), 1);
  QVERIFY(!overlay->isDragging());

  // The trailing release must not produce a commit.
  QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, centre);
  QCOMPARE(finishSpy.count(), 0);
}

void TestStickerDragOverlay::testPressOutsideAnyZoneIsSwallowed() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));

  // A content widget under the overlay must never see the press.
  auto *content = new QWidget(parent.data());
  content->setGeometry(0, 20, 300, 180);
  parent->show();
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();
  overlay->raise();

  QSignalSpy startSpy(overlay, &StickerDragOverlay::dragStarted);
  // A dead-band point: inside the overlay but on neither a handle nor the cross.
  const QPoint dead(40, overlay->height() / 2);
  QCOMPARE(stickerZoneAt(overlay->size(), dead), StickerDragZone::None);

  QWidget *hit = parent->childAt(overlay->mapTo(parent.data(), dead));
  QCOMPARE(hit, static_cast<QWidget *>(overlay));

  QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, dead);
  QCOMPARE(startSpy.count(), 0);
  QVERIFY(!overlay->isDragging());
}

void TestStickerDragOverlay::testCancelDragIsIdempotent() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QSignalSpy cancelSpy(overlay, &StickerDragOverlay::dragCancelled);

  overlay->cancelDrag();
  overlay->cancelDrag();
  QVERIFY(!overlay->isDragging());
  QCOMPARE(cancelSpy.count(), 0);

  const QPoint centre(overlay->width() / 2, overlay->height() / 2);
  QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, centre);
  QVERIFY(overlay->isDragging());
  overlay->cancelDrag();
  overlay->cancelDrag();
  QVERIFY(!overlay->isDragging());
  QCOMPARE(cancelSpy.count(), 0);
  QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, centre);
}

void TestStickerDragOverlay::testArrowKeysRequestMove() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QSignalSpy moveSpy(overlay, &StickerDragOverlay::moveRequested);
  QSignalSpy resizeSpy(overlay, &StickerDragOverlay::resizeRequested);

  QTest::keyClick(overlay, Qt::Key_Up);
  QTest::keyClick(overlay, Qt::Key_Down);
  QTest::keyClick(overlay, Qt::Key_Left);
  QTest::keyClick(overlay, Qt::Key_Right);

  QCOMPARE(moveSpy.count(), 4);
  QCOMPARE(moveSpy.at(0), QVariantList({-1, 0}));
  QCOMPARE(moveSpy.at(1), QVariantList({1, 0}));
  QCOMPARE(moveSpy.at(2), QVariantList({0, -1}));
  QCOMPARE(moveSpy.at(3), QVariantList({0, 1}));
  QCOMPARE(resizeSpy.count(), 0);

  // An unrelated key is not a geometry gesture.
  QTest::keyClick(overlay, Qt::Key_A);
  QCOMPARE(moveSpy.count(), 4);
}

void TestStickerDragOverlay::testShiftArrowKeysRequestResize() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QSignalSpy moveSpy(overlay, &StickerDragOverlay::moveRequested);
  QSignalSpy resizeSpy(overlay, &StickerDragOverlay::resizeRequested);

  QTest::keyClick(overlay, Qt::Key_Right, Qt::ShiftModifier);
  QTest::keyClick(overlay, Qt::Key_Up, Qt::ShiftModifier);

  QCOMPARE(resizeSpy.count(), 2);
  QCOMPARE(resizeSpy.at(0), QVariantList({0, 1}));
  QCOMPARE(resizeSpy.at(1), QVariantList({-1, 0}));
  QCOMPARE(moveSpy.count(), 0);
}

void TestStickerDragOverlay::testKeyboardIsInertDuringMouseDrag() {
  QWidget *header = nullptr;
  QScopedPointer<QWidget> parent(makeParent(&header));
  QVERIFY(QTest::qWaitForWindowExposed(parent.data()));

  auto *overlay = new StickerDragOverlay(parent.data());
  overlay->syncGeometry(header);
  overlay->show();

  QSignalSpy moveSpy(overlay, &StickerDragOverlay::moveRequested);
  QSignalSpy resizeSpy(overlay, &StickerDragOverlay::resizeRequested);

  const QPoint centre(overlay->width() / 2, overlay->height() / 2);
  QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, centre);
  QVERIFY(overlay->isDragging());

  // A mouse session owns the geometry; the keyboard must not commit behind it.
  QTest::keyClick(overlay, Qt::Key_Right);
  QTest::keyClick(overlay, Qt::Key_Right, Qt::ShiftModifier);
  QCOMPARE(moveSpy.count(), 0);
  QCOMPARE(resizeSpy.count(), 0);

  QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, centre);
}

} // namespace tests

QTEST_MAIN(tests::TestStickerDragOverlay)
#include "test_stickerdragoverlay.moc"
