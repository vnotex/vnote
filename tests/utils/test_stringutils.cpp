// test_stringutils.cpp - Tests for vnotex::naturalCompare
#include <QtTest>

#include <utils/stringutils.h>

using namespace vnotex;

namespace tests {

class TestStringUtils : public QObject {
  Q_OBJECT

private slots:
  void testNaturalCompare_data();
  void testNaturalCompare();

  void testSymmetry();
  void testIdenticalNames();
};

void TestStringUtils::testNaturalCompare_data() {
  QTest::addColumn<QString>("lhs");
  QTest::addColumn<QString>("rhs");
  QTest::addColumn<bool>("expectedLess");

  // Numeric sequence
  QTest::newRow("note1<note2") << "Note 1" << "Note 2" << true;
  QTest::newRow("note2<note10") << "Note 2" << "Note 10" << true;
  QTest::newRow("note10<note20") << "Note 10" << "Note 20" << true;
  QTest::newRow("note1<note10") << "Note 1" << "Note 10" << true;
  QTest::newRow("note2>note1") << "Note 2" << "Note 1" << false;
  QTest::newRow("note10>note2") << "Note 10" << "Note 2" << false;

  // Chapter sequence
  QTest::newRow("ch9<ch10") << "Chapter 9" << "Chapter 10" << true;
  QTest::newRow("ch10<ch12") << "Chapter 10" << "Chapter 12" << true;
  QTest::newRow("ch1<ch2") << "Chapter 1" << "Chapter 2" << true;

  // Leading numbers
  QTest::newRow("1<2") << "1 Introduction" << "2 Background" << true;
  QTest::newRow("2<10") << "2 Background" << "10 Conclusion" << true;
  QTest::newRow("1<10") << "1 Introduction" << "10 Conclusion" << true;
  QTest::newRow("10>2") << "10 Conclusion" << "2 Background" << false;

  // Only numbers (leading zeros: treated as integer values 1, 2, 10)
  QTest::newRow("001<002") << "001" << "002" << true;
  QTest::newRow("002<010") << "002" << "010" << true;
  QTest::newRow("001<010") << "001" << "010" << true;
  QTest::newRow("010>002") << "010" << "002" << false;

  // Pure alpha
  QTest::newRow("Alpha<Beta") << "Alpha" << "Beta" << true;
  QTest::newRow("Beta<Gamma") << "Beta" << "Gamma" << true;
  QTest::newRow("Gamma>Alpha") << "Gamma" << "Alpha" << false;

  // Mixed alpha/numeric
  QTest::newRow("Alpha<Beta1") << "Alpha" << "Beta 1" << true;
  QTest::newRow("Beta1<Beta2") << "Beta 1" << "Beta 2" << true;
  QTest::newRow("Beta2<Beta10") << "Beta 2" << "Beta 10" << true;
  QTest::newRow("Beta10<Gamma") << "Beta 10" << "Gamma" << true;
  QTest::newRow("Alpha<Gamma") << "Alpha" << "Gamma" << true;

  // Empty strings
  QTest::newRow("empty<A") << "" << "A" << true;
  QTest::newRow("empty<1") << "" << "1" << true;
  QTest::newRow("A>empty") << "A" << "" << false;

  // Notes in folder
  QTest::newRow("note.md<note1.md") << "note.md" << "note1.md" << true;
  QTest::newRow("note<note1") << "note" << "note1" << true;

  // Case insensitive (numeric)
  QTest::newRow("note1<NOTE2") << "note 1" << "NOTE 2" << true;
  QTest::newRow("NOTE2<Note10") << "NOTE 2" << "Note 10" << true;
  QTest::newRow("note1<Note10") << "note 1" << "Note 10" << true;
  QTest::newRow("Note10>NOTE2") << "Note 10" << "NOTE 2" << false;

  // Case insensitive (alpha: same word variants compare equal, neither less)
  QTest::newRow("alpha==ALPHA") << "alpha" << "ALPHA" << false;
  QTest::newRow("ALPHA==Alpha") << "ALPHA" << "Alpha" << false;
  QTest::newRow("alpha==Alpha") << "alpha" << "Alpha" << false;
}

void TestStringUtils::testNaturalCompare() {
  QFETCH(QString, lhs);
  QFETCH(QString, rhs);
  QFETCH(bool, expectedLess);

  QCOMPARE(naturalCompare(lhs, rhs), expectedLess);
}

void TestStringUtils::testSymmetry() {
  // For any non-equal pair: naturalCompare(a,b) == !naturalCompare(b,a)
  const QStringList pairs[] = {
      {"Note 1", "Note 10"}, {"Chapter 9", "Chapter 10"},  {"Alpha", "Beta"},
      {"note 1", "NOTE 2"},  {"1 Intro", "10 Conclusion"},
  };

  for (const auto &pair : pairs) {
    const QString &a = pair[0];
    const QString &b = pair[1];
    QVERIFY2(naturalCompare(a, b) != naturalCompare(b, a),
             qPrintable(QString("Symmetry failed for: %1 vs %2").arg(a, b)));
  }
}

void TestStringUtils::testIdenticalNames() {
  // Strict weak ordering: a < a must be false
  QVERIFY(!naturalCompare("Note 1", "Note 1"));
  QVERIFY(!naturalCompare("Chapter 10", "Chapter 10"));
  QVERIFY(!naturalCompare("Alpha", "Alpha"));
  QVERIFY(!naturalCompare("", ""));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestStringUtils)
#include "test_stringutils.moc"
