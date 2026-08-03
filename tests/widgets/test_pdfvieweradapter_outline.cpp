// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_pdfvieweradapter_outline.cpp
//
// C++ half of the PDF outline contract: PdfViewerAdapter's JSON -> Heading
// conversion, the makePerfectHeadings pass, and the index-based jump request.
//
// The wire contract, shared with src/data/extra/web/pdf.js/pdfviewercore.js:
//
//     { "name": <string>, "level": <1-based int>, "index": <int> }
//
// where `index` addresses the web side's destination array and is -1 for an
// entry that cannot be jumped to. The JS half of the same contract is covered by
// test_pdfviewercore_js; this test alone would NOT catch a buildOutline() that
// stopped emitting `index`, because every case here hand-feeds the JSON.

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include <widgets/editors/pdfvieweradapter.h>

namespace tests {

using vnotex::PdfViewerAdapter;

namespace {

QJsonObject entry(const QString &p_name, int p_level, int p_index) {
  QJsonObject obj;
  obj[QStringLiteral("name")] = p_name;
  obj[QStringLiteral("level")] = p_level;
  obj[QStringLiteral("index")] = p_index;
  return obj;
}

} // namespace

class TestPdfViewerAdapterOutline : public QObject {
  Q_OBJECT

private slots:
  void wellFormedNestedOutline();
  void emptyOutlineStillNotifies();
  void destinationlessEntryIsPresentButInert();
  void malformedIndexDegradesToMinusOne();
  void malformedNameAndLevelDoNotCrash();
  void hostileLevelIsClamped();
  void oversizedOutlineIsTruncated();
  void skippedLevelsGetInertFillers();
  void scrollEmitsDestinationIndexNotHeadingIndex();
  void outOfRangeScrollEmitsNothing();
  void clearOutlineEmptiesAndNotifies();
};

void TestPdfViewerAdapterOutline::wellFormedNestedOutline() {
  PdfViewerAdapter adapter;
  QSignalSpy changedSpy(&adapter, &PdfViewerAdapter::outlineChanged);

  QJsonArray arr;
  arr.append(entry(QStringLiteral("Chapter 1"), 1, 0));
  arr.append(entry(QStringLiteral("Section 1.1"), 2, 1));
  arr.append(entry(QStringLiteral("Section 1.2"), 2, 2));
  arr.append(entry(QStringLiteral("Chapter 2"), 1, 3));

  adapter.setOutline(arr);

  QCOMPARE(changedSpy.count(), 1);

  const auto &headings = adapter.getOutlineHeadings();
  QCOMPARE(headings.size(), 4);

  QCOMPARE(headings[0].m_name, QStringLiteral("Chapter 1"));
  QCOMPARE(headings[0].m_level, 1);
  QCOMPARE(headings[0].m_index, 0);

  QCOMPARE(headings[1].m_name, QStringLiteral("Section 1.1"));
  QCOMPARE(headings[1].m_level, 2);
  QCOMPARE(headings[1].m_index, 1);

  QCOMPARE(headings[2].m_name, QStringLiteral("Section 1.2"));
  QCOMPARE(headings[2].m_level, 2);
  QCOMPARE(headings[2].m_index, 2);

  QCOMPARE(headings[3].m_name, QStringLiteral("Chapter 2"));
  QCOMPARE(headings[3].m_level, 1);
  QCOMPARE(headings[3].m_index, 3);
}

void TestPdfViewerAdapterOutline::emptyOutlineStillNotifies() {
  PdfViewerAdapter adapter;
  QSignalSpy changedSpy(&adapter, &PdfViewerAdapter::outlineChanged);

  adapter.setOutline(QJsonArray());

  // A PDF with no bookmarks must still drive the provider, otherwise the dock
  // would keep showing the previous document's tree.
  QCOMPARE(changedSpy.count(), 1);
  QVERIFY(adapter.getOutlineHeadings().isEmpty());
}

// Regression gate for hazard 2: a bookmark carrying url/action/attachment/
// setOCGState instead of dest is emitted with index -1. Dropping it would
// re-level its children.
void TestPdfViewerAdapterOutline::destinationlessEntryIsPresentButInert() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy scrollSpy(&adapter, &PdfViewerAdapter::outlineItemScrollRequested);

  QJsonArray arr;
  arr.append(entry(QStringLiteral("Visit our website"), 1, -1));
  arr.append(entry(QStringLiteral("Child of the link"), 2, 0));

  adapter.setOutline(arr);

  const auto &headings = adapter.getOutlineHeadings();
  QCOMPARE(headings.size(), 2);
  QCOMPARE(headings[0].m_name, QStringLiteral("Visit our website"));
  QCOMPARE(headings[0].m_index, -1);
  // The child kept its level; no filler was inserted in front of it.
  QCOMPARE(headings[1].m_level, 2);
  QCOMPARE(headings[1].m_index, 0);

  adapter.scrollToOutlineItem(0);
  QCOMPARE(scrollSpy.count(), 0);

  adapter.scrollToOutlineItem(1);
  QCOMPARE(scrollSpy.count(), 1);
}

void TestPdfViewerAdapterOutline::malformedIndexDegradesToMinusOne() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy scrollSpy(&adapter, &PdfViewerAdapter::outlineItemScrollRequested);

  QJsonArray arr;

  // Absent "index".
  {
    QJsonObject obj;
    obj[QStringLiteral("name")] = QStringLiteral("absent");
    obj[QStringLiteral("level")] = 1;
    arr.append(obj);
  }
  // Explicit null.
  {
    QJsonObject obj;
    obj[QStringLiteral("name")] = QStringLiteral("null");
    obj[QStringLiteral("level")] = 1;
    obj[QStringLiteral("index")] = QJsonValue::Null;
    arr.append(obj);
  }
  // A string.
  {
    QJsonObject obj;
    obj[QStringLiteral("name")] = QStringLiteral("string");
    obj[QStringLiteral("level")] = 1;
    obj[QStringLiteral("index")] = QStringLiteral("2");
    arr.append(obj);
  }
  // A non-integral double.
  {
    QJsonObject obj;
    obj[QStringLiteral("name")] = QStringLiteral("float");
    obj[QStringLiteral("level")] = 1;
    obj[QStringLiteral("index")] = 1.5;
    arr.append(obj);
  }

  adapter.setOutline(arr);

  const auto &headings = adapter.getOutlineHeadings();
  QCOMPARE(headings.size(), 4);

  for (int i = 0; i < headings.size(); ++i) {
    // Explicitly NOT 0: 0 is a valid destination index, so degrading to it
    // would silently jump to the first bookmark's target.
    QVERIFY2(headings[i].m_index != 0, qPrintable(QStringLiteral("entry %1 degraded to 0").arg(i)));
    QCOMPARE(headings[i].m_index, -1);

    adapter.scrollToOutlineItem(i);
  }

  QCOMPARE(scrollSpy.count(), 0);
}

void TestPdfViewerAdapterOutline::malformedNameAndLevelDoNotCrash() {
  PdfViewerAdapter adapter;
  QSignalSpy changedSpy(&adapter, &PdfViewerAdapter::outlineChanged);

  QJsonArray arr;
  // Neither "name" nor "level".
  arr.append(QJsonObject());
  // Wrong types.
  {
    QJsonObject obj;
    obj[QStringLiteral("name")] = 42;
    obj[QStringLiteral("level")] = QStringLiteral("two");
    obj[QStringLiteral("index")] = 0;
    arr.append(obj);
  }
  // Not even an object.
  arr.append(QJsonValue(QStringLiteral("garbage")));

  adapter.setOutline(arr);

  QCOMPARE(changedSpy.count(), 1);
  QCOMPARE(adapter.getOutlineHeadings().size(), 3);
}

// setOutline() is a QWebChannel slot, so the payload is untrusted. An unclamped
// level is an allocation amplifier: OutlineProvider::makePerfectHeadings
// synthesizes one filler per SKIPPED level, so {level:1},{level:2000000000}
// would append ~2e9 headings on the UI thread. INT_MIN additionally overflows
// that function's `int curLevel = baseLevel - 1`.
void TestPdfViewerAdapterOutline::hostileLevelIsClamped() {
  PdfViewerAdapter adapter;

  QJsonArray arr;
  arr.append(entry(QStringLiteral("sane"), 1, 0));
  arr.append(entry(QStringLiteral("huge"), 2000000000, 1));
  arr.append(entry(QStringLiteral("tiny"), -2147483647 - 1, 2));
  arr.append(entry(QStringLiteral("just over the ceiling"), 65, 3));
  arr.append(entry(QStringLiteral("at the ceiling"), 64, 4));

  adapter.setOutline(arr);

  const auto &headings = adapter.getOutlineHeadings();

  // The fillers needed to reach level 64 are legitimate, but the total must stay
  // bounded by the clamp rather than by the hostile value.
  QVERIFY2(headings.size() < 200,
           qPrintable(QStringLiteral("outline exploded to %1 headings").arg(headings.size())));

  // Out-of-range levels collapse to the same -1 sentinel a garbage level gives,
  // so makePerfectHeadings may insert fillers ahead of the first real entry —
  // look entries up by name rather than by position.
  auto findByName = [&headings](const QString &p_name) -> PdfViewerAdapter::Heading {
    for (const auto &h : headings) {
      if (h.m_name == p_name) {
        return h;
      }
    }
    return PdfViewerAdapter::Heading();
  };

  const auto sane = findByName(QStringLiteral("sane"));
  QCOMPARE(sane.m_level, 1);
  QCOMPARE(sane.m_index, 0);

  for (const auto &h : headings) {
    QVERIFY2(h.m_level <= 64, qPrintable(QStringLiteral("level %1 escaped the clamp").arg(h.m_level)));
    QVERIFY2(h.m_level >= -1, qPrintable(QStringLiteral("level %1 escaped the clamp").arg(h.m_level)));
  }

  // Every hostile level was neutralized to the invalid sentinel.
  QCOMPARE(findByName(QStringLiteral("huge")).m_level, -1);
  QCOMPARE(findByName(QStringLiteral("tiny")).m_level, -1);
  QCOMPARE(findByName(QStringLiteral("just over the ceiling")).m_level, -1);

  // The entry exactly at the ceiling is still accepted and still jumpable.
  const auto ceiling = findByName(QStringLiteral("at the ceiling"));
  QCOMPARE(ceiling.m_level, 64);
  QCOMPARE(ceiling.m_index, 4);
}

// The C++ side must not trust the web side's own entry cap.
void TestPdfViewerAdapterOutline::oversizedOutlineIsTruncated() {
  PdfViewerAdapter adapter;

  QJsonArray arr;
  for (int i = 0; i < 6000; ++i) {
    arr.append(entry(QStringLiteral("h%1").arg(i), 1, i));
  }

  adapter.setOutline(arr);

  // All levels are 1, so no fillers are inserted and the count is exactly the cap.
  QCOMPARE(adapter.getOutlineHeadings().size(), 5000);
}

// Regression gate for hazard 1: makePerfectHeadings INSERTS filler headings for
// skipped levels, shifting positions relative to the JS destination array. The
// explicit m_index is what survives that.
void TestPdfViewerAdapterOutline::skippedLevelsGetInertFillers() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy scrollSpy(&adapter, &PdfViewerAdapter::outlineItemScrollRequested);

  QJsonArray arr;
  arr.append(entry(QStringLiteral("Top"), 1, 0));
  // Jumps from level 1 straight to level 3.
  arr.append(entry(QStringLiteral("Deep"), 3, 1));

  adapter.setOutline(arr);

  const auto &headings = adapter.getOutlineHeadings();
  QCOMPARE(headings.size(), 3);

  QCOMPARE(headings[0].m_name, QStringLiteral("Top"));
  QCOMPARE(headings[0].m_index, 0);

  // The synthesized filler must never be jumpable.
  QCOMPARE(headings[1].m_level, 2);
  QCOMPARE(headings[1].m_index, -1);

  // The real entry kept its ORIGINAL destination index even though it moved
  // from position 1 to position 2.
  QCOMPARE(headings[2].m_name, QStringLiteral("Deep"));
  QCOMPARE(headings[2].m_index, 1);

  adapter.scrollToOutlineItem(1);
  QCOMPARE(scrollSpy.count(), 0);
}

// The signal must carry the JS destination index, NOT the heading index. The
// fixture makes the two differ so conflating them fails the test.
void TestPdfViewerAdapterOutline::scrollEmitsDestinationIndexNotHeadingIndex() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy scrollSpy(&adapter, &PdfViewerAdapter::outlineItemScrollRequested);

  QJsonArray arr;
  // A destination-less entry first, so every later heading index is one ahead
  // of its destination index.
  arr.append(entry(QStringLiteral("External link"), 1, -1));
  arr.append(entry(QStringLiteral("Chapter 1"), 1, 0));
  arr.append(entry(QStringLiteral("Chapter 2"), 1, 1));

  adapter.setOutline(arr);

  adapter.scrollToOutlineItem(2);
  QCOMPARE(scrollSpy.count(), 1);
  QCOMPARE(scrollSpy.at(0).at(0).toInt(), 1);

  adapter.scrollToOutlineItem(1);
  QCOMPARE(scrollSpy.count(), 2);
  QCOMPARE(scrollSpy.at(1).at(0).toInt(), 0);
}

void TestPdfViewerAdapterOutline::outOfRangeScrollEmitsNothing() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy scrollSpy(&adapter, &PdfViewerAdapter::outlineItemScrollRequested);

  QJsonArray arr;
  arr.append(entry(QStringLiteral("Only"), 1, 0));
  adapter.setOutline(arr);

  adapter.scrollToOutlineItem(-1);
  adapter.scrollToOutlineItem(adapter.getOutlineHeadings().size());
  adapter.scrollToOutlineItem(999);
  QCOMPARE(scrollSpy.count(), 0);

  adapter.scrollToOutlineItem(0);
  QCOMPARE(scrollSpy.count(), 1);
}

void TestPdfViewerAdapterOutline::clearOutlineEmptiesAndNotifies() {
  PdfViewerAdapter adapter;

  QJsonArray arr;
  arr.append(entry(QStringLiteral("Chapter 1"), 1, 0));
  adapter.setOutline(arr);
  QCOMPARE(adapter.getOutlineHeadings().size(), 1);

  QSignalSpy changedSpy(&adapter, &PdfViewerAdapter::outlineChanged);
  adapter.clearOutline();

  QCOMPARE(changedSpy.count(), 1);
  QVERIFY(adapter.getOutlineHeadings().isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerAdapterOutline)
#include "test_pdfvieweradapter_outline.moc"
