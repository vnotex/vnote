#include <QtTest>

#include <utils/cursorpreservationpolicy.h>

using namespace vnotex;

namespace tests {

class TestCursorPreservationPolicy : public QObject {
  Q_OBJECT

private slots:
  void testLine_data();
  void testLine();

  void testOffset_data();
  void testOffset();
};

void TestCursorPreservationPolicy::testLine_data() {
  QTest::addColumn<int>("oldLine");
  QTest::addColumn<int>("newBlockCount");
  QTest::addColumn<int>("expected");

  QTest::newRow("not captured") << -1 << 10 << -1;
  QTest::newRow("very negative") << -42 << 10 << -1;
  QTest::newRow("empty document") << 3 << 0 << -1;
  QTest::newRow("negative block count") << 3 << -1 << -1;
  QTest::newRow("in range") << 3 << 10 << 3;
  QTest::newRow("last block") << 9 << 10 << 9;
  QTest::newRow("beyond EOF") << 99 << 10 << 9;
  QTest::newRow("line 0, single block") << 0 << 1 << 0;
  QTest::newRow("beyond EOF, single block") << 5 << 1 << 0;
}

void TestCursorPreservationPolicy::testLine() {
  QFETCH(int, oldLine);
  QFETCH(int, newBlockCount);
  QFETCH(int, expected);

  QCOMPARE(CursorPreservationPolicy::computeRestoredCursorLine(oldLine, newBlockCount), expected);
}

void TestCursorPreservationPolicy::testOffset_data() {
  QTest::addColumn<int>("oldOffset");
  QTest::addColumn<int>("newBlockTextLength");
  QTest::addColumn<int>("expected");

  QTest::newRow("not captured") << -1 << 10 << 0;
  QTest::newRow("very negative") << -42 << 10 << 0;
  QTest::newRow("start of line") << 0 << 10 << 0;
  QTest::newRow("in range") << 4 << 10 << 4;
  QTest::newRow("end of line") << 10 << 10 << 10;
  QTest::newRow("beyond line end") << 99 << 10 << 10;
  QTest::newRow("empty block") << 7 << 0 << 0;
  QTest::newRow("negative length") << 7 << -1 << 0;
}

void TestCursorPreservationPolicy::testOffset() {
  QFETCH(int, oldOffset);
  QFETCH(int, newBlockTextLength);
  QFETCH(int, expected);

  QCOMPARE(CursorPreservationPolicy::computeRestoredCursorOffset(oldOffset, newBlockTextLength),
           expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCursorPreservationPolicy)
#include "test_cursorpreservationpolicy.moc"
