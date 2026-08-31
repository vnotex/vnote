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
  void testWhitespaceIsNotAmbiguous();
  void testNonAsciiByteOrdering();
  void testStrictWeakOrdering();
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

  // Negative numbers
  // -1 is before 0 because '-' is sorted first.
  QTest::newRow("-1<0") << "-1" << "0" << true;
  QTest::newRow("-100<-200") << "-100" << "-200" << true;

  // Big integers should not cause overflow errors.
  QTest::newRow("Big numbers") << "100000000000000000000"
                               << "1000000000000000000000000000000" << true;

  // Multiple digit groups.
  // Decimal numbers like this are sorted as if they were section labels in a
  // book.
  QTest::newRow("1.0<2.0") << "1.0" << "2.0" << true;
  QTest::newRow("1.1<1.2") << "1.1" << "1.2" << true;
  QTest::newRow("1.2<1.15") << "1.2" << "1.15" << true;

  // Wide fixed-width numeric fields (years, dates, zero-padded ids) are
  // compared left-aligned, not by magnitude. Issue #2741: comparing them by
  // magnitude made "人员管理-2026" (4 digits) sort before "人员管理-20220301"
  // (8 digits), scattering same-year notes through the list.
  QTest::newRow("date 2022<2026") << "report-2022" << "report-2026" << true;
  QTest::newRow("date 20220301<2026") << "report-20220301" << "report-2026" << true;
  QTest::newRow("date 2026<20260115") << "report-2026" << "report-20260115" << true;
  QTest::newRow("date 20221115<20260115") << "report-20221115" << "report-20260115" << true;
  QTest::newRow("date 202601<20260115") << "report-202601" << "report-20260115" << true;
  QTest::newRow("date 2026>20220301") << "report-2026" << "report-20220301" << false;

  // Counters stay below the wide-field threshold and keep numeric ordering.
  QTest::newRow("counter 9<123") << "Note 9" << "Note 123" << true;
  QTest::newRow("counter 999<1000") << "Note 999" << "Note 1000" << true;

  // Accepted trade-off of the width heuristic: a counter that reaches the
  // threshold is compared as a fixed-width field, so magnitude ordering is lost
  // above 4 digits. Locked in so it cannot change silently.
  QTest::newRow("counter 10000<9999") << "Note 10000" << "Note 9999" << true;

  // Mixed 3/4/8-digit runs must stay transitive: short runs are numerically
  // below every wide run, and wide runs order lexicographically among themselves.
  QTest::newRow("mixed 999<1000") << "999" << "1000" << true;
  QTest::newRow("mixed 1000<10000000") << "1000" << "10000000" << true;
  QTest::newRow("mixed 999<10000000") << "999" << "10000000" << true;

  // A zero-leading run takes the existing left-aligned path regardless of width.
  QTest::newRow("zeropad 09999<1000") << "09999" << "1000" << true;
  QTest::newRow("zeropad 1000<10000") << "1000" << "10000" << true;
  QTest::newRow("zeropad 09999<10000") << "09999" << "10000" << true;

  // The raw-byte tie-break must never override a digit-run result: byte order
  // would put "Note2" after "Note 10" (because of the space), natural order
  // must not.
  QTest::newRow("tiebreak Note2<Note10") << "Note2" << "Note 10" << true;
  QTest::newRow("tiebreak Note10>Note2") << "Note 10" << "Note2" << false;
}

void TestStringUtils::testStrictWeakOrdering() {
  // NotebookNodeProxyModel::lessThan() feeds this to std::sort via
  // QSortFilterProxyModel; a comparator that is not a strict weak ordering can
  // crash. Assert irreflexivity, asymmetry and transitivity over names that
  // span every branch: short runs, wide runs, zero-padded runs, whitespace and
  // case variants, and non-ASCII.
  const QStringList names = {"999",
                             "1000",
                             "09999",
                             "10000000",
                             "Note 9",
                             "Note 999",
                             "Note 1000",
                             "Note2",
                             "Note 10",
                             "report-2022",
                             "report-20220301",
                             "report-2026",
                             "report-20260115",
                             "alpha",
                             "ALPHA",
                             "Report 2026",
                             "Report2026",
                             QStringLiteral("\u4eba\u5458\u7ba1\u7406-2026"),
                             QStringLiteral("\u8d22\u52a1\u7ba1\u7406-20220301"),
                             ""};

  for (const auto &a : names) {
    QVERIFY2(!naturalCompare(a, a), qPrintable(QString("irreflexivity: %1").arg(a)));
  }

  for (const auto &a : names) {
    for (const auto &b : names) {
      QVERIFY2(!(naturalCompare(a, b) && naturalCompare(b, a)),
               qPrintable(QString("asymmetry: %1 / %2").arg(a, b)));
    }
  }

  for (const auto &a : names) {
    for (const auto &b : names) {
      if (!naturalCompare(a, b)) {
        continue;
      }
      for (const auto &c : names) {
        if (!naturalCompare(b, c)) {
          continue;
        }
        QVERIFY2(naturalCompare(a, c),
                 qPrintable(QString("transitivity: %1 < %2 < %3").arg(a, b, c)));
      }
    }
  }
}

void TestStringUtils::testWhitespaceIsNotAmbiguous() {
  // Internal whitespace is ignored when ordering, but two names that differ
  // only by whitespace must still get a deterministic, antisymmetric order --
  // otherwise their position in the explorer is arbitrary.
  QVERIFY(naturalCompare("Report 2026", "Report2026") !=
          naturalCompare("Report2026", "Report 2026"));
  QVERIFY(naturalCompare("A B", "AB") != naturalCompare("AB", "A B"));
}

void TestStringUtils::testNonAsciiByteOrdering() {
  // Names are compared as UTF-8 bytes. Bytes >= 0x80 must be compared as
  // unsigned, so non-ASCII sorts after ASCII (code point order), and must be
  // left untouched by the ASCII-only case folding so distinct CJK names never
  // collapse to equal. (The implementation no longer calls <ctype.h>, so this
  // holds under every locale; the test itself does not switch locales.)
  QVERIFY(naturalCompare(QStringLiteral("Alpha"), QStringLiteral("\u4eba\u5458")));
  QVERIFY(!naturalCompare(QStringLiteral("\u4eba\u5458"), QStringLiteral("Alpha")));

  // Distinct CJK names must never compare equal.
  const QString a = QStringLiteral("\u4eba\u5458\u7ba1\u7406");
  const QString b = QStringLiteral("\u8d22\u52a1\u7ba1\u7406");
  QVERIFY(naturalCompare(a, b) != naturalCompare(b, a));
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
