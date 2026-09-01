// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_pdfvieweradapter_viewer.cpp
//
// C++ half of the viewer-control bridge: the QWebChannel-facing validation of
// setViewerState(), and the reload REPLAY that makes a theme switch come back to
// the page and zoom the user was looking at.
//
// The replay is the part that fails silently. PdfViewWindow2::handleThemeChanged()
// reloads the whole page, so "the toolbar jumps back to page 1 whenever you
// switch theme" is a real, frequent user-visible defect and not an edge case.

#include <limits>

#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest>

#include <widgets/editors/pdfvieweradapter.h>

using vnotex::PdfViewerAdapter;

namespace tests {

namespace {

// A state payload that passes every check. Cases override single fields to
// exercise one rejection at a time.
QJsonObject goodState(int p_page = 1, int p_pageCount = 10) {
  QJsonObject obj;
  obj.insert(QStringLiteral("page"), p_page);
  obj.insert(QStringLiteral("pageCount"), p_pageCount);
  obj.insert(QStringLiteral("scale"), 1.0);
  obj.insert(QStringLiteral("scaleValue"), QStringLiteral("auto"));
  obj.insert(QStringLiteral("rotation"), 0);
  obj.insert(QStringLiteral("scrollMode"), 0);
  obj.insert(QStringLiteral("spreadMode"), 0);
  obj.insert(QStringLiteral("cursorTool"), 0);
  obj.insert(QStringLiteral("sidebarOpen"), false);
  return obj;
}

} // namespace

class TestPdfViewerAdapterViewer : public QObject {
  Q_OBJECT

private slots:
  void aValidStateIsAccepted();
  void aStateOutsideThePageRangeIsRejectedWhole();
  void aNonFiniteOrOutOfBoundScaleIsRejected();
  void anUnalignedRotationIsRejected();
  void anOutOfRangeModeIsRejected();
  void anUnknownZoomValueIsRejected();
  void aMissingOrWrongTypedFieldIsRejected();
  void aStateWithNoDocumentIsNotAccepted();

  void aFreshAdapterHasNoReplayValue();
  void aPageReachedByScrollingIsReplayed();
  void theReplayBeatsTheReplacementDocumentsOwnDefaults();
  void aStateArrivingBeforeReadinessStillReplays();
  void oneShotVerbsAreNeverReplayed();
  void commandsAreDroppedWhileThePageIsNotReady();
};

void TestPdfViewerAdapterViewer::aValidStateIsAccepted() {
  PdfViewerAdapter adapter;
  QSignalSpy spy(&adapter, &PdfViewerAdapter::viewerStateChanged);

  auto state = goodState(7, 20);
  state.insert(QStringLiteral("scale"), 1.5);
  state.insert(QStringLiteral("scaleValue"), QStringLiteral("1.5"));
  state.insert(QStringLiteral("rotation"), 90);
  state.insert(QStringLiteral("scrollMode"), 3);
  state.insert(QStringLiteral("spreadMode"), 2);
  state.insert(QStringLiteral("cursorTool"), 1);
  state.insert(QStringLiteral("sidebarOpen"), true);
  adapter.setViewerState(state);

  QCOMPARE(spy.count(), 1);
  const auto &s = adapter.getViewerState();
  QVERIFY(s.m_valid);
  QCOMPARE(s.m_page, 7);
  QCOMPARE(s.m_pageCount, 20);
  QCOMPARE(s.m_scale, 1.5);
  QCOMPARE(s.m_scaleValue, QStringLiteral("1.5"));
  QCOMPARE(s.m_rotation, 90);
  QCOMPARE(s.m_scrollMode, 3);
  QCOMPARE(s.m_spreadMode, 2);
  QCOMPARE(s.m_cursorTool, 1);
  QCOMPARE(s.m_sidebarOpen, true);
}

// A rejected payload must leave the PREVIOUS state completely intact: a
// partially applied state would show a mix of two documents in the toolbar.
void TestPdfViewerAdapterViewer::aStateOutsideThePageRangeIsRejectedWhole() {
  PdfViewerAdapter adapter;
  adapter.setViewerState(goodState(3, 10));
  QCOMPARE(adapter.getViewerState().m_page, 3);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::viewerStateChanged);

  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state page")));
  adapter.setViewerState(goodState(11, 10));
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state page")));
  adapter.setViewerState(goodState(0, 10));

  QCOMPARE(spy.count(), 0);
  QCOMPARE(adapter.getViewerState().m_page, 3);
  QCOMPARE(adapter.getViewerState().m_pageCount, 10);
}

void TestPdfViewerAdapterViewer::aNonFiniteOrOutOfBoundScaleIsRejected() {
  PdfViewerAdapter adapter;

  auto infinite = goodState();
  infinite.insert(QStringLiteral("scale"), std::numeric_limits<double>::infinity());
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state scale")));
  adapter.setViewerState(infinite);
  QVERIFY(!adapter.getViewerState().m_valid);

  auto huge = goodState();
  huge.insert(QStringLiteral("scale"), 1000.0);
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state scale")));
  adapter.setViewerState(huge);
  QVERIFY(!adapter.getViewerState().m_valid);

  // A missing (non-numeric) scale is rejected too, rather than defaulting to 0.
  auto missing = goodState();
  missing.remove(QStringLiteral("scale"));
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state scale")));
  adapter.setViewerState(missing);
  QVERIFY(!adapter.getViewerState().m_valid);
}

void TestPdfViewerAdapterViewer::anUnalignedRotationIsRejected() {
  PdfViewerAdapter adapter;
  for (int rotation : {45, 1, 360, -90}) {
    auto state = goodState();
    state.insert(QStringLiteral("rotation"), rotation);
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("rejected viewer state rotation")));
    adapter.setViewerState(state);
    QVERIFY(!adapter.getViewerState().m_valid);
  }
}

void TestPdfViewerAdapterViewer::anOutOfRangeModeIsRejected() {
  PdfViewerAdapter adapter;

  auto scroll = goodState();
  scroll.insert(QStringLiteral("scrollMode"), 4);
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state scroll mode")));
  adapter.setViewerState(scroll);

  auto spread = goodState();
  spread.insert(QStringLiteral("spreadMode"), 3);
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state spread mode")));
  adapter.setViewerState(spread);

  auto cursor = goodState();
  cursor.insert(QStringLiteral("cursorTool"), 2);
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state cursor tool")));
  adapter.setViewerState(cursor);

  QVERIFY(!adapter.getViewerState().m_valid);
}

void TestPdfViewerAdapterViewer::anUnknownZoomValueIsRejected() {
  PdfViewerAdapter adapter;
  auto state = goodState();
  state.insert(QStringLiteral("scaleValue"), QStringLiteral("page-everything"));
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state scale value")));
  adapter.setViewerState(state);
  QVERIFY(!adapter.getViewerState().m_valid);

  // ...but the four presets and a plain number are all fine.
  for (const auto &value :
       {QStringLiteral("auto"), QStringLiteral("page-actual"), QStringLiteral("page-fit"),
        QStringLiteral("page-width"), QStringLiteral("1.37")}) {
    auto ok = goodState();
    ok.insert(QStringLiteral("scaleValue"), value);
    adapter.setViewerState(ok);
    QCOMPARE(adapter.getViewerState().m_scaleValue, value);
  }
}

// Reading through toInt(default) / toBool(default) would make a missing or
// wrong-typed field indistinguishable from a legitimate one -- silent
// acceptance of hostile input, which is exactly what this validator exists to
// prevent.
void TestPdfViewerAdapterViewer::aMissingOrWrongTypedFieldIsRejected() {
  PdfViewerAdapter adapter;
  adapter.setViewerState(goodState(3, 10));
  QCOMPARE(adapter.getViewerState().m_page, 3);

  const QStringList numericKeys = {QStringLiteral("page"),       QStringLiteral("pageCount"),
                                   QStringLiteral("scale"),      QStringLiteral("rotation"),
                                   QStringLiteral("scrollMode"), QStringLiteral("spreadMode"),
                                   QStringLiteral("cursorTool")};
  for (const auto &key : numericKeys) {
    auto missing = goodState(5, 10);
    missing.remove(key);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("rejected viewer state")));
    adapter.setViewerState(missing);

    auto wrongType = goodState(5, 10);
    wrongType.insert(key, QStringLiteral("2"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("rejected viewer state")));
    adapter.setViewerState(wrongType);
  }

  // A string where a bool belongs is not "false"; it is a lying page.
  auto badSidebar = goodState(5, 10);
  badSidebar.insert(QStringLiteral("sidebarOpen"), QStringLiteral("open"));
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("non-boolean sidebar flag")));
  adapter.setViewerState(badSidebar);

  auto badScaleValue = goodState(5, 10);
  badScaleValue.insert(QStringLiteral("scaleValue"), 1);
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("rejected viewer state scale value")));
  adapter.setViewerState(badScaleValue);

  // Nothing got through: the previous state is untouched.
  QCOMPARE(adapter.getViewerState().m_page, 3);
}

// The bridge publishes its defaults (`pageCount: 0`) as soon as the adapter
// arrives, which may be long before 'documentloaded'. Accepting that as a valid
// state would take the toolbar live on a blank window.
void TestPdfViewerAdapterViewer::aStateWithNoDocumentIsNotAccepted() {
  PdfViewerAdapter adapter;
  QSignalSpy spy(&adapter, &PdfViewerAdapter::viewerStateChanged);

  auto blank = goodState(1, 0);
  adapter.setViewerState(blank);

  QVERIFY(!adapter.getViewerState().m_valid);
  QCOMPARE(adapter.getViewerState().m_pageCount, 0);
  // Quietly, with no warning: this is the normal pre-load push, not an attack.
  QCOMPARE(spy.count(), 0);
}

// A newly opened window must come up at pdf.js's defaults, not at whatever the
// previous document in that process happened to be showing.
void TestPdfViewerAdapterViewer::aFreshAdapterHasNoReplayValue() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy pageSpy(&adapter, &PdfViewerAdapter::pageRequested);
  QSignalSpy zoomSpy(&adapter, &PdfViewerAdapter::zoomRequested);

  adapter.clearViewerState();
  QVERIFY(!adapter.isViewerReplayPending());

  adapter.setViewerState(goodState(1, 10));
  QCOMPARE(pageSpy.count(), 0);
  QCOMPARE(zoomSpy.count(), 0);
}

// The replay is refreshed by every ACCEPTED state push, NOT only by a toolbar
// request: a page reached by scrolling, by an outline click, from a sidebar
// thumbnail or with a keyboard shortcut must survive the reload too.
void TestPdfViewerAdapterViewer::aPageReachedByScrollingIsReplayed() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  // No toolbar request anywhere: only inbound state, as scrolling produces.
  auto scrolled = goodState(7, 20);
  scrolled.insert(QStringLiteral("scale"), 1.5);
  scrolled.insert(QStringLiteral("scaleValue"), QStringLiteral("1.5"));
  scrolled.insert(QStringLiteral("rotation"), 90);
  adapter.setViewerState(scrolled);

  // The reload.
  adapter.clearViewerState();
  QVERIFY(adapter.isViewerReplayPending());
  QVERIFY(!adapter.getViewerState().m_valid);
  adapter.setReady(false);
  adapter.setReady(true);

  QSignalSpy pageSpy(&adapter, &PdfViewerAdapter::pageRequested);
  QSignalSpy zoomSpy(&adapter, &PdfViewerAdapter::zoomRequested);
  QSignalSpy rotationSpy(&adapter, &PdfViewerAdapter::rotationRequested);

  // The replacement document reports its own page-1 / default-zoom state.
  adapter.setViewerState(goodState(1, 20));

  QCOMPARE(pageSpy.count(), 1);
  QCOMPARE(pageSpy.at(0).at(0).toInt(), 7);
  QCOMPARE(zoomSpy.count(), 1);
  QCOMPARE(zoomSpy.at(0).at(0).toString(), QStringLiteral("1.5"));
  QCOMPARE(rotationSpy.count(), 1);
  QCOMPARE(rotationSpy.at(0).at(0).toInt(), 90);
  QVERIFY(!adapter.isViewerReplayPending());
}

// The replacement document's OWN initial defaults must not be captured as the
// new replay values before the replay has run -- otherwise the second reload
// (a second theme switch) would come back to page 1.
void TestPdfViewerAdapterViewer::theReplayBeatsTheReplacementDocumentsOwnDefaults() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setViewerState(goodState(7, 20));

  // First reload.
  adapter.clearViewerState();
  adapter.setReady(false);
  adapter.setReady(true);
  adapter.setViewerState(goodState(1, 20));

  // The page then honours the replay and reports page 7 back.
  adapter.setViewerState(goodState(7, 20));

  // Second reload: page 7 must still be what comes back.
  adapter.clearViewerState();
  adapter.setReady(false);
  adapter.setReady(true);

  QSignalSpy pageSpy(&adapter, &PdfViewerAdapter::pageRequested);
  adapter.setViewerState(goodState(1, 20));
  QCOMPARE(pageSpy.count(), 1);
  QCOMPARE(pageSpy.at(0).at(0).toInt(), 7);
}

// The OTHER initialization order, and the one that loses the replay silently if
// the latch has only one half: the glue publishes the viewer state
// (setViewerAdapter -> setViewerState) BEFORE it calls setReady(true), so when
// the QWebChannel callback loses the race to 'documentloaded' the loaded state
// arrives while the bridge is still not-ready. 'documentloaded' never fires
// again for that document, so a replay merely "re-armed" here would wait
// forever.
void TestPdfViewerAdapterViewer::aStateArrivingBeforeReadinessStillReplays() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setViewerState(goodState(7, 20));

  adapter.clearViewerState();
  adapter.setReady(false);
  QVERIFY(adapter.isViewerReplayPending());

  QSignalSpy pageSpy(&adapter, &PdfViewerAdapter::pageRequested);

  // The replacement document reports its state BEFORE the bridge is ready.
  adapter.setViewerState(goodState(1, 20));
  QCOMPARE(pageSpy.count(), 0);
  QVERIFY(adapter.isViewerReplayPending());

  adapter.setReady(true);
  QCOMPARE(pageSpy.count(), 1);
  QCOMPARE(pageSpy.at(0).at(0).toInt(), 7);
  QVERIFY(!adapter.isViewerReplayPending());
}

// A transient verb only means something in a LIVE document. Replaying one after
// a reload would act on the replacement -- the same rule captureSelection()
// follows.
void TestPdfViewerAdapterViewer::oneShotVerbsAreNeverReplayed() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setViewerState(goodState(7, 20));

  adapter.showDocumentProperties();
  adapter.toggleSidebar();
  adapter.clearFind();

  adapter.clearViewerState();
  adapter.setReady(false);

  QSignalSpy propsSpy(&adapter, &PdfViewerAdapter::documentPropertiesRequested);
  QSignalSpy sidebarSpy(&adapter, &PdfViewerAdapter::sidebarToggleRequested);
  QSignalSpy findSpy(&adapter, &PdfViewerAdapter::findCleared);

  adapter.setReady(true);
  adapter.setViewerState(goodState(1, 20));

  QCOMPARE(propsSpy.count(), 0);
  QCOMPARE(sidebarSpy.count(), 0);
  QCOMPARE(findSpy.count(), 0);
}

void TestPdfViewerAdapterViewer::commandsAreDroppedWhileThePageIsNotReady() {
  PdfViewerAdapter adapter;
  // Never ready: there is no live page to command.
  QSignalSpy pageSpy(&adapter, &PdfViewerAdapter::pageRequested);
  QSignalSpy propsSpy(&adapter, &PdfViewerAdapter::documentPropertiesRequested);

  adapter.gotoPage(3);
  adapter.showDocumentProperties();

  QCOMPARE(pageSpy.count(), 0);
  QCOMPARE(propsSpy.count(), 0);

  // But the page IS recorded for the replay, so a window that requested a jump
  // while a reload was in flight still lands there.
  adapter.clearViewerState();
  QVERIFY(adapter.isViewerReplayPending());
  adapter.setReady(true);
  adapter.setViewerState(goodState(1, 10));
  QCOMPARE(pageSpy.count(), 1);
  QCOMPARE(pageSpy.at(0).at(0).toInt(), 3);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerAdapterViewer)
#include "test_pdfvieweradapter_viewer.moc"
