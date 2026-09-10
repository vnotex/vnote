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
#include <QMimeData>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <controllers/outlinecontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/widgetconfig.h>
#include <models/outlinemodel.h>
#include <views/outlineview.h>
#include <widgets/outlineprovider.h>

#include <widgets/editors/pdfvieweradapter.h>

#include <vxcore/vxcore.h>

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
  void outlineModelReorderDefaultsOff();
  void outlineModelExposesReorderEligibility();
  void adjacentReparentRequestsConfirmation();
  void outlineModelReplacementDisablesReordering();
  void producerNumberingGuaranteeSurvivesPanelToggles();
  void automaticPolicyDisplaysExpectedNames_data();
  void automaticPolicyDisplaysExpectedNames();
  void authoredNumbersIgnoreFillersButInspectRealEmptyHeading();
  void fillersDoNotConsumeAuthoredNumberSample();
  void numberedPdfRetainsDestinationlessAndDeepHeadings();
  void numberedGapOutlinePreservesMoveIndices();
  void sharedPatternUpdatesBothViewsWithoutLosingState();
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
    QVERIFY2(h.m_level <= 64,
             qPrintable(QStringLiteral("level %1 escaped the clamp").arg(h.m_level)));
    QVERIFY2(h.m_level >= -1,
             qPrintable(QStringLiteral("level %1 escaped the clamp").arg(h.m_level)));
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

void TestPdfViewerAdapterOutline::outlineModelReorderDefaultsOff() {
  vnotex::OutlineModel model;
  auto outline = QSharedPointer<vnotex::Outline>::create();
  outline->m_headings.append(vnotex::Outline::Heading(QStringLiteral("Heading"), 1));

  model.setOutline(outline);

  QVERIFY(!outline->m_reorderSupported);
  QVERIFY(!model.isReorderSupported());
  QCOMPARE(model.flags(QModelIndex()), Qt::NoItemFlags);
  const QModelIndex heading = model.index(0, 0);
  QVERIFY(!(model.flags(heading) & Qt::ItemIsDragEnabled));
  QVERIFY(!(model.flags(heading) & Qt::ItemIsDropEnabled));
}

void TestPdfViewerAdapterOutline::outlineModelExposesReorderEligibility() {
  vnotex::OutlineModel model;
  auto outline = QSharedPointer<vnotex::Outline>::create();
  outline->m_reorderSupported = true;
  vnotex::Outline::Heading top(QStringLiteral("Top"), 1);
  top.m_reorderable = true;
  vnotex::Outline::Heading deep(QStringLiteral("Deep"), 3);
  deep.m_reorderable = true;
  outline->m_headings = {top, deep};

  model.setOutline(outline);

  QVERIFY(model.isReorderSupported());
  QVERIFY(model.flags(QModelIndex()) & Qt::ItemIsDropEnabled);
  QCOMPARE(model.supportedDropActions(), Qt::DropActions(Qt::MoveAction));
  QVERIFY(model.mimeTypes().contains(QStringLiteral("application/x-vnote-outline-heading")));
  QMimeData dragData;
  dragData.setData(QStringLiteral("application/x-vnote-outline-heading"), QByteArrayLiteral("0"));
  QVERIFY(model.canDropMimeData(&dragData, Qt::MoveAction, -1, -1, QModelIndex()));
  QVERIFY(!model.canDropMimeData(&dragData, Qt::CopyAction, -1, -1, QModelIndex()));
  QVERIFY(!model.dropMimeData(&dragData, Qt::MoveAction, -1, -1, QModelIndex()));
  const QModelIndex topIndex = model.indexForHeadingIndex(0);
  const QModelIndex deepIndex = model.indexForHeadingIndex(1);
  QVERIFY(topIndex.isValid());
  QVERIFY(deepIndex.isValid());
  QCOMPARE(topIndex.data(vnotex::OutlineModel::HeadingLevelRole).toInt(), 1);
  QCOMPARE(deepIndex.data(vnotex::OutlineModel::HeadingLevelRole).toInt(), 3);
  QVERIFY(topIndex.data(vnotex::OutlineModel::ReorderableRole).toBool());
  QVERIFY(model.flags(topIndex) & Qt::ItemIsDragEnabled);
  QVERIFY(model.flags(topIndex) & Qt::ItemIsDropEnabled);

  const QModelIndex filler = model.index(0, 0, topIndex);
  QVERIFY(filler.isValid());
  QCOMPARE(filler.data(vnotex::OutlineModel::HeadingIndexRole).toInt(), -1);
  QVERIFY(!filler.data(vnotex::OutlineModel::ReorderableRole).toBool());
  QVERIFY(!(model.flags(filler) & Qt::ItemIsDragEnabled));
  QVERIFY(!(model.flags(filler) & Qt::ItemIsDropEnabled));
}

void TestPdfViewerAdapterOutline::outlineModelReplacementDisablesReordering() {
  vnotex::OutlineModel model;
  auto supported = QSharedPointer<vnotex::Outline>::create();
  supported->m_reorderSupported = true;
  supported->m_headings.append(vnotex::Outline::Heading(QStringLiteral("Heading"), 1));
  supported->m_headings[0].m_reorderable = true;
  model.setOutline(supported);
  QVERIFY(model.isReorderSupported());

  auto unsupported = QSharedPointer<vnotex::Outline>::create();
  unsupported->m_headings.append(vnotex::Outline::Heading(QStringLiteral("Other"), 1));
  model.setOutline(unsupported);
  QVERIFY(!model.isReorderSupported());
  QVERIFY(!(model.flags(model.index(0, 0)) & Qt::ItemIsDragEnabled));

  model.setOutline(QSharedPointer<vnotex::Outline>());
  QVERIFY(!model.isReorderSupported());
  QCOMPARE(model.flags(QModelIndex()), Qt::NoItemFlags);
}

void TestPdfViewerAdapterOutline::adjacentReparentRequestsConfirmation() {
  vnotex::ServiceLocator services;
  vnotex::OutlineController controller(services);
  auto provider = QSharedPointer<vnotex::OutlineProvider>::create();
  auto outline = QSharedPointer<vnotex::Outline>::create();
  outline->m_reorderSupported = true;
  outline->m_headings = {vnotex::Outline::Heading(QStringLiteral("A"), 1),
                         vnotex::Outline::Heading(QStringLiteral("B"), 2),
                         vnotex::Outline::Heading(QStringLiteral("C"), 1)};
  for (auto &heading : outline->m_headings) {
    heading.m_reorderable = true;
  }
  provider->setOutline(outline);
  controller.setOutlineProvider(provider);

  QSignalSpy confirmationSpy(&controller, &vnotex::OutlineController::reorderConfirmationRequested);
  QSignalSpy moveSpy(provider.data(), &vnotex::OutlineProvider::moveRequested);

  controller.requestItemMove(2, 1, vnotex::OutlineDropPosition::BelowItem);

  QCOMPARE(confirmationSpy.count(), 1);
  QCOMPARE(confirmationSpy.first().first().toString(), QStringLiteral("C"));
  controller.confirmReorder(true);
  QCOMPARE(moveSpy.count(), 1);
  QCOMPARE(moveSpy.first().at(0).toInt(), 2);
  QCOMPARE(moveSpy.first().at(1).toInt(), -1);
  QCOMPARE(moveSpy.first().at(2).toInt(), 2);
}

void TestPdfViewerAdapterOutline::producerNumberingGuaranteeSurvivesPanelToggles() {
  auto outline = QSharedPointer<vnotex::Outline>::create();
  outline->m_headings = {vnotex::Outline::Heading(QStringLiteral("A"), 1),
                         vnotex::Outline::Heading(QStringLiteral("B"), 1)};
  outline->m_hasSectionNumber = true;
  vnotex::OutlineModel model;
  model.setAutoSectionNumberEnabled(true);
  model.setOutline(outline);
  QCOMPARE(model.data(model.indexForHeadingIndex(0)).toString(), QStringLiteral("A"));
  QCOMPARE(model.data(model.indexForHeadingIndex(1)).toString(), QStringLiteral("B"));
  model.setAutoSectionNumberEnabled(false);
  QCOMPARE(model.data(model.indexForHeadingIndex(0)).toString(), QStringLiteral("A"));
  model.setAutoSectionNumberEnabled(true);
  auto raw = QSharedPointer<vnotex::Outline>::create(*outline);
  raw->m_hasSectionNumber = false;
  QVERIFY(!(*raw == *outline));
  model.setOutline(raw);
  QCOMPARE(model.data(model.indexForHeadingIndex(0)).toString(), QStringLiteral("1. A"));
  QCOMPARE(model.data(model.indexForHeadingIndex(1)).toString(), QStringLiteral("2. B"));
  model.setSectionNumberPattern(QStringLiteral("1.1)"));
  QCOMPARE(model.data(model.indexForHeadingIndex(1)).toString(), QStringLiteral("2) B"));
  outline->clear();
  QVERIFY(!outline->m_hasSectionNumber);
}

void TestPdfViewerAdapterOutline::automaticPolicyDisplaysExpectedNames_data() {
  QTest::addColumn<QVector<int>>("levels");
  QTest::addColumn<QStringList>("names");
  QTest::addColumn<QString>("pattern");
  QTest::addColumn<QStringList>("expected");

  const QString dot = QStringLiteral("1.1.");
  QTest::newRow("exempt-title") << QVector<int>{1, 2, 3, 2} << QStringList{"Title", "A", "B", "C"}
                                << dot << QStringList{"Title", "1. A", "1.1. B", "2. C"};
  QTest::newRow("multiple-h1") << QVector<int>{1, 2, 1} << QStringList{"A", "B", "C"} << dot
                               << QStringList{"1. A", "1.1. B", "2. C"};
  QTest::newRow("h1-after-first-heading")
      << QVector<int>{2, 1} << QStringList{"A", "B"} << dot << QStringList{"1.1. A", "2. B"};
  QTest::newRow("base-after-title") << QVector<int>{1, 3, 3} << QStringList{"Title", "A", "B"}
                                    << dot << QStringList{"Title", "1. A", "2. B"};
  QTest::newRow("skipped-levels") << QVector<int>{1, 2, 4, 2} << QStringList{"Title", "A", "B", "C"}
                                  << dot << QStringList{"Title", "1. A", "1.1.1. B", "2. C"};
  QTest::newRow("no-final-suffix") << QVector<int>{1, 2, 3} << QStringList{"Title", "A", "B"}
                                   << QStringLiteral("1.1") << QStringList{"Title", "1 A", "1.1 B"};
  QTest::newRow("closing-parenthesis")
      << QVector<int>{1, 2, 3} << QStringList{"Title", "A", "B"} << QStringLiteral("1.1)")
      << QStringList{"Title", "1) A", "1.1) B"};
  QTest::newRow("title-only") << QVector<int>{1} << QStringList{"Title"} << dot
                              << QStringList{"Title"};
}

void TestPdfViewerAdapterOutline::automaticPolicyDisplaysExpectedNames() {
  QFETCH(QVector<int>, levels);
  QFETCH(QStringList, names);
  QFETCH(QString, pattern);
  QFETCH(QStringList, expected);

  QVector<vnotex::Outline::Heading> raw;
  for (int i = 0; i < names.size(); ++i) {
    vnotex::Outline::Heading heading(names[i], levels[i]);
    heading.m_reorderable = true;
    raw.append(heading);
  }

  // Exercise both editor-style raw input and producers that already filled gaps.
  for (bool prefilled : {false, true}) {
    auto outline = QSharedPointer<vnotex::Outline>::create();
    outline->m_reorderSupported = true;
    if (prefilled) {
      vnotex::OutlineProvider::makePerfectHeadings(raw, outline->m_headings);
    } else {
      outline->m_headings = raw;
    }
    const auto supplied = outline->m_headings;
    vnotex::OutlineModel model;
    model.setAutoSectionNumberEnabled(true);
    model.setSectionNumberPattern(pattern);
    model.setOutline(outline);

    QStringList displayed;
    for (int i = 0; i < supplied.size(); ++i) {
      const auto index = model.indexForHeadingIndex(i);
      QVERIFY(index.isValid());
      QCOMPARE(index.data(vnotex::OutlineModel::HeadingIndexRole).toInt(), i);
      QCOMPARE(index.data(vnotex::OutlineModel::HeadingLevelRole).toInt(), supplied[i].m_level);
      QCOMPARE(index.data(vnotex::OutlineModel::ReorderableRole).toBool(),
               !supplied[i].m_isPlaceholder);
      QCOMPARE(bool(model.flags(index) & Qt::ItemIsDragEnabled), !supplied[i].m_isPlaceholder);
      if (supplied[i].m_isPlaceholder) {
        QCOMPARE(index.data(Qt::DisplayRole).toString(), supplied[i].m_name);
      } else {
        displayed.append(index.data(Qt::DisplayRole).toString());
      }
    }
    QCOMPARE(displayed, expected);
    QVERIFY(outline->m_headings == supplied);
    QVERIFY(!outline->m_hasSectionNumber);
  }
}

void TestPdfViewerAdapterOutline::authoredNumbersIgnoreFillersButInspectRealEmptyHeading() {
  QVector<vnotex::Outline::Heading> raw{vnotex::Outline::Heading(QStringLiteral("Title"), 1),
                                        vnotex::Outline::Heading(QStringLiteral("1. A"), 2),
                                        vnotex::Outline::Heading(QStringLiteral("1.1.1. B"), 4)};
  auto outline = QSharedPointer<vnotex::Outline>::create();
  vnotex::OutlineProvider::makePerfectHeadings(raw, outline->m_headings);
  QCOMPARE(outline->m_headings.size(), 4);
  QVERIFY(outline->m_headings[2].m_isPlaceholder);

  vnotex::OutlineModel model;
  model.setAutoSectionNumberEnabled(true);
  model.setOutline(outline);
  QCOMPARE(model.indexForHeadingIndex(0).data().toString(), QStringLiteral("Title"));
  QCOMPARE(model.indexForHeadingIndex(1).data().toString(), QStringLiteral("1. A"));
  QCOMPARE(model.indexForHeadingIndex(2).data().toString(), QStringLiteral("[EMPTY]"));
  QCOMPARE(model.indexForHeadingIndex(3).data().toString(), QStringLiteral("1.1.1. B"));

  // Same spelling and level as the gap-filler, but this is authored content.
  const vnotex::Outline::Heading realEmpty(QStringLiteral("[EMPTY]"), 3);
  QVERIFY(!(realEmpty == outline->m_headings[2]));
  auto changed = QSharedPointer<vnotex::Outline>::create(*outline);
  changed->m_headings[2].m_isPlaceholder = false;
  QVERIFY(!(*changed == *outline));

  raw.append(realEmpty);
  auto withRealEmpty = QSharedPointer<vnotex::Outline>::create();
  vnotex::OutlineProvider::makePerfectHeadings(raw, withRealEmpty->m_headings);
  model.setOutline(withRealEmpty);
  QCOMPARE(model.indexForHeadingIndex(1).data().toString(), QStringLiteral("1. 1. A"));
  QCOMPARE(model.indexForHeadingIndex(2).data().toString(), QStringLiteral("[EMPTY]"));
  QCOMPARE(model.indexForHeadingIndex(3).data().toString(), QStringLiteral("1.1.1. 1.1.1. B"));
  QCOMPARE(model.indexForHeadingIndex(4).data().toString(), QStringLiteral("1.2. [EMPTY]"));
  QCOMPARE(model.indexForHeadingIndex(4).data(vnotex::OutlineModel::HeadingIndexRole).toInt(), 4);
}

void TestPdfViewerAdapterOutline::fillersDoNotConsumeAuthoredNumberSample() {
  const QVector<vnotex::Outline::Heading> raw{
      vnotex::Outline::Heading(QStringLiteral("Title"), 1),
      vnotex::Outline::Heading(QStringLiteral("1 A"), 2),
      vnotex::Outline::Heading(QStringLiteral("1.1.1.1.1) B"), 6),
      vnotex::Outline::Heading(QStringLiteral("2. C"), 2),
      vnotex::Outline::Heading(QStringLiteral("3 D"), 2),
      vnotex::Outline::Heading(QStringLiteral("Mismatch"), 2)};
  auto outline = QSharedPointer<vnotex::Outline>::create();
  vnotex::OutlineProvider::makePerfectHeadings(raw, outline->m_headings);
  vnotex::OutlineModel model;
  model.setAutoSectionNumberEnabled(true);
  model.setOutline(outline);
  QCOMPARE(model.indexForHeadingIndex(1).data().toString(), QStringLiteral("1. 1 A"));
  QCOMPARE(model.indexForHeadingIndex(5).data().toString(),
           QStringLiteral("1.1.1.1.1. 1.1.1.1.1) B"));
  QCOMPARE(model.indexForHeadingIndex(8).data().toString(), QStringLiteral("4. Mismatch"));

  // Five actual matches suppress numbering even if the sixth actual heading does not match.
  outline = QSharedPointer<vnotex::Outline>::create(*outline);
  outline->m_headings.last().m_name = QStringLiteral("4) D");
  outline->m_headings.append(vnotex::Outline::Heading(QStringLiteral("Unnumbered sixth"), 2));
  model.setOutline(outline);
  QCOMPARE(model.indexForHeadingIndex(1).data().toString(), QStringLiteral("1 A"));
  QCOMPARE(model.indexForHeadingIndex(5).data().toString(), QStringLiteral("1.1.1.1.1) B"));
  QCOMPARE(model.indexForHeadingIndex(9).data().toString(), QStringLiteral("Unnumbered sixth"));
}

void TestPdfViewerAdapterOutline::numberedPdfRetainsDestinationlessAndDeepHeadings() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setOutline(QJsonArray{entry(QStringLiteral("Group"), 62, -1),
                                entry(QStringLiteral("Leaf"), 64, 7),
                                entry(QStringLiteral("Next"), 62, 8)});
  const auto &headings = adapter.getOutlineHeadings();
  QCOMPARE(headings.size(), 4);
  QVERIFY(!headings[0].m_isPlaceholder);
  QVERIFY(headings[1].m_isPlaceholder);
  QVERIFY(!headings[2].m_isPlaceholder);

  auto outline = QSharedPointer<vnotex::Outline>::create();
  for (const auto &heading : headings) {
    vnotex::Outline::Heading converted(heading.m_name, heading.m_level);
    converted.m_isPlaceholder = heading.m_isPlaceholder;
    outline->m_headings.append(converted);
  }
  vnotex::OutlineModel model;
  model.setAutoSectionNumberEnabled(true);
  model.setOutline(outline);
  const QStringList expected{"1. Group", "[EMPTY]", "1.1.1. Leaf", "2. Next"};
  for (int i = 0; i < expected.size(); ++i) {
    const auto index = model.indexForHeadingIndex(i);
    QCOMPARE(index.data(Qt::DisplayRole).toString(), expected[i]);
    QCOMPARE(index.data(vnotex::OutlineModel::HeadingIndexRole).toInt(), i);
    QVERIFY(!(model.flags(index) & Qt::ItemIsDragEnabled));
  }

  QSignalSpy scrollSpy(&adapter, &PdfViewerAdapter::outlineItemScrollRequested);
  adapter.scrollToOutlineItem(
      model.indexForHeadingIndex(0).data(vnotex::OutlineModel::HeadingIndexRole).toInt());
  adapter.scrollToOutlineItem(
      model.indexForHeadingIndex(1).data(vnotex::OutlineModel::HeadingIndexRole).toInt());
  QCOMPARE(scrollSpy.count(), 0);
  adapter.scrollToOutlineItem(
      model.indexForHeadingIndex(2).data(vnotex::OutlineModel::HeadingIndexRole).toInt());
  adapter.scrollToOutlineItem(
      model.indexForHeadingIndex(3).data(vnotex::OutlineModel::HeadingIndexRole).toInt());
  QCOMPARE(scrollSpy.count(), 2);
  QCOMPARE(scrollSpy.at(0).at(0).toInt(), 7);
  QCOMPARE(scrollSpy.at(1).at(0).toInt(), 8);
}

void TestPdfViewerAdapterOutline::numberedGapOutlinePreservesMoveIndices() {
  for (bool prefilled : {false, true}) {
    QVector<vnotex::Outline::Heading> raw{vnotex::Outline::Heading(QStringLiteral("Title"), 1),
                                          vnotex::Outline::Heading(QStringLiteral("A"), 2),
                                          vnotex::Outline::Heading(QStringLiteral("Deep"), 4),
                                          vnotex::Outline::Heading(QStringLiteral("B"), 2)};
    for (auto &heading : raw) {
      heading.m_reorderable = true;
    }
    auto outline = QSharedPointer<vnotex::Outline>::create();
    outline->m_reorderSupported = true;
    if (prefilled) {
      vnotex::OutlineProvider::makePerfectHeadings(raw, outline->m_headings);
    } else {
      outline->m_headings = raw;
    }
    auto provider = QSharedPointer<vnotex::OutlineProvider>::create();
    provider->setOutline(outline);
    vnotex::ServiceLocator services;
    vnotex::OutlineController controller(services);
    controller.setOutlineProvider(provider);
    auto *model = controller.model();
    const int deepHeadingIndex = prefilled ? 3 : 2;
    const int lastHeadingIndex = prefilled ? 4 : 3;
    const auto deep = model->indexForHeadingIndex(deepHeadingIndex);
    const auto last = model->indexForHeadingIndex(lastHeadingIndex);
    QCOMPARE(deep.data(Qt::DisplayRole).toString(), QStringLiteral("1.1.1. Deep"));
    QCOMPARE(last.data(Qt::DisplayRole).toString(), QStringLiteral("2. B"));
    QCOMPARE(deep.parent().data(Qt::DisplayRole).toString(), QStringLiteral("[EMPTY]"));
    QCOMPARE(deep.parent().data(vnotex::OutlineModel::HeadingIndexRole).toInt(),
             prefilled ? 2 : -1);
    QVERIFY(!(model->flags(deep.parent()) & Qt::ItemIsDragEnabled));

    QSignalSpy confirmationSpy(&controller,
                               &vnotex::OutlineController::reorderConfirmationRequested);
    QSignalSpy moveSpy(provider.data(), &vnotex::OutlineProvider::moveRequested);
    controller.requestItemMove(last.data(vnotex::OutlineModel::HeadingIndexRole).toInt(),
                               deep.data(vnotex::OutlineModel::HeadingIndexRole).toInt(),
                               vnotex::OutlineDropPosition::AboveItem);
    QCOMPARE(confirmationSpy.count(), 1);
    controller.confirmReorder(true);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.at(0).at(0).toInt(), lastHeadingIndex);
    QCOMPARE(moveSpy.at(0).at(1).toInt(), deepHeadingIndex);
    QCOMPARE(moveSpy.at(0).at(2).toInt(), 4);
  }
}

void TestPdfViewerAdapterOutline::sharedPatternUpdatesBothViewsWithoutLosingState() {
  vxcore_set_test_mode(1);
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &context), VXCORE_OK);
  QVERIFY(context != nullptr);
  const auto destroyContext = qScopeGuard([&]() { vxcore_context_destroy(context); });
  vnotex::ConfigCoreService configService(context);
  vnotex::ConfigMgr2 config(&configService);
  config.getWidgetConfig().setOutlineAutoSectionNumberEnabled(true);
  config.getEditorConfig().setSectionNumberPattern(QStringLiteral("1.1"));
  vnotex::HookManager hooks;
  vnotex::ServiceLocator services;
  services.registerService<vnotex::ConfigMgr2>(&config);
  services.registerService<vnotex::HookManager>(&hooks);
  vnotex::OutlineController dock(services);
  vnotex::OutlineController popup(services);
  vnotex::OutlineView dockView;
  vnotex::OutlineView popupView;
  dockView.setModel(dock.model());
  popupView.setModel(popup.model());
  dock.setView(&dockView);
  popup.setView(&popupView);
  auto outline = QSharedPointer<vnotex::Outline>::create();
  outline->m_headings = {vnotex::Outline::Heading(QStringLiteral("Title"), 1),
                         vnotex::Outline::Heading(QStringLiteral("A"), 2),
                         vnotex::Outline::Heading(QStringLiteral("Detail"), 3),
                         vnotex::Outline::Heading(QStringLiteral("B"), 2),
                         vnotex::Outline::Heading(QStringLiteral("More"), 3)};
  auto provider = QSharedPointer<vnotex::OutlineProvider>::create();
  provider->setOutline(outline);
  dock.setOutlineProvider(provider);
  popup.setOutlineProvider(provider);
  provider->setCurrentHeadingIndex(1);
  const QSet<int> dockExpanded{0, 1};
  const QSet<int> popupExpanded{0, 3};
  dockView.collapseAll();
  dockView.restoreExpansionState(dockExpanded);
  popupView.collapseAll();
  popupView.restoreExpansionState(popupExpanded);
  QCOMPARE(dockView.saveExpansionState(), dockExpanded);
  QCOMPARE(popupView.saveExpansionState(), popupExpanded);
  QCOMPARE(dockView.currentIndex().data().toString(), QStringLiteral("1 A"));
  QCOMPARE(popupView.currentIndex().data().toString(), QStringLiteral("1 A"));

  QSignalSpy clickedSpy(provider.data(), &vnotex::OutlineProvider::headingClicked);
  config.getEditorConfig().setSectionNumberPattern(QStringLiteral("1.1)"));
  hooks.doAction(vnotex::HookNames::ConfigEditorChanged);
  for (auto *controller : {&dock, &popup}) {
    QCOMPARE(controller->model()->indexForHeadingIndex(2).data().toString(),
             QStringLiteral("1.1) Detail"));
    QCOMPARE(controller->model()->getCurrentHeadingIndex(), 1);
    QCOMPARE(controller->view()->currentIndex().data().toString(), QStringLiteral("1) A"));
    QCOMPARE(
        controller->view()->currentIndex().data(vnotex::OutlineModel::HeadingIndexRole).toInt(), 1);
  }
  QCOMPARE(dockView.saveExpansionState(), dockExpanded);
  QCOMPARE(popupView.saveExpansionState(), popupExpanded);
  QCOMPARE(clickedSpy.count(), 0);
}

} // namespace tests

QTEST_MAIN(tests::TestPdfViewerAdapterOutline)
#include "test_pdfvieweradapter_outline.moc"
