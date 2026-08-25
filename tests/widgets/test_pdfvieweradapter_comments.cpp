// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_pdfvieweradapter_comments.cpp
//
// The C++ half of the comment bridge. Two independent concerns:
//
// 1. BOUNDS. Every comment slot is exposed over QWebChannel, so its arguments
//    are attacker-controlled by contract: ANY script in the viewer page can
//    call them. The adapter must re-validate everything and must not rely on
//    the web side's own limits (the JS caps in pdfviewercore.js are deliberately
//    independent).
//
// 2. RELOAD REPLAY. The adapter OUTLIVES the web page, and
//    WebViewAdapter::setReady() early-returns when the value is unchanged. A
//    comment set published while a reload is in flight therefore targets a
//    destroyed page, and the replacement page's setReady(true) would be a no-op.
//    The latch tested here is what makes the set arrive exactly once on the new
//    QWebChannel.

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>

#include <core/services/commenttypes.h>
#include <widgets/editors/pdfvieweradapter.h>

using namespace vnotex;

namespace tests {

class TestPdfViewerAdapterComments : public QObject {
  Q_OBJECT

private slots:
  void addIsRejectedBeforeTheDocumentReportsItsPageCount();
  void addAcceptsAValidAnchor();
  void addRejectsAnchors_data();
  void addRejectsAnchors();
  void addRejectsAPageBeyondTheDocument();
  void addNormalizesAnUnknownColor();
  void addTruncatesOverlongAnchorText();
  void documentPageCountIsBounded_data();
  void documentPageCountIsBounded();
  void idBearingSlotsAreLengthBounded();

  void commentsArePublishedImmediatelyWhenReady();
  void commentsAreLatchedAndReplayedOnceAfterAReload();
  void onlyTheNewestSetIsReplayed();
  void clearCommentsDropsTheLatch();

  void captureSelectionForwardsTheColor();
  void captureSelectionNormalizesAnUnknownColor();
  void captureSelectionIsDroppedWhenThePageIsNotReady();

  void inkAndFreeTextAnchorsAreAccepted();
  void newAnchorTypesAreStillPageBounded();
  void toolAndColourAreLatchedAcrossAReload();
  void toolFinishedFromTheWebSideClearsTheTool();
  void unknownAnchorTypesCannotBeMinted();

private:
  static QJsonObject validAnchor(int p_page = 0, const QString &p_text = QStringLiteral("q"));
  static QJsonArray oneComment(const QString &p_id);
};

QJsonObject TestPdfViewerAdapterComments::validAnchor(int p_page, const QString &p_text) {
  QVector<QVector<double>> quads;
  quads.append(QVector<double>{0, 0, 10, 0, 10, 10, 0, 10});
  return PdfQuadsAnchor::make(p_page, quads, p_text);
}

QJsonArray TestPdfViewerAdapterComments::oneComment(const QString &p_id) {
  QJsonObject comment;
  comment.insert(QStringLiteral("id"), p_id);
  QJsonArray arr;
  arr.append(comment);
  return arr;
}

// The page-count bound has no safe default: until the document reports its size
// there is no ceiling to check an anchor against, so everything is refused.
void TestPdfViewerAdapterComments::addIsRejectedBeforeTheDocumentReportsItsPageCount() {
  PdfViewerAdapter adapter;
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  QCOMPARE(adapter.getDocumentPageCount(), 0);
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*rejected.*")));
  adapter.requestAddComment(validAnchor(0), QStringLiteral("yellow"));

  QCOMPARE(spy.count(), 0);
}

void TestPdfViewerAdapterComments::addAcceptsAValidAnchor() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(10);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  adapter.requestAddComment(validAnchor(3), QStringLiteral("blue"));

  QCOMPARE(spy.count(), 1);
  const auto anchor = spy.at(0).at(0).toJsonObject();
  QCOMPARE(PdfQuadsAnchor::page(anchor), 3);
  QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("blue"));
}

void TestPdfViewerAdapterComments::addRejectsAnchors_data() {
  QTest::addColumn<QJsonObject>("anchor");

  QTest::newRow("empty") << QJsonObject();

  QJsonObject unknownType = validAnchor();
  unknownType.insert(QStringLiteral("type"), QStringLiteral("markdown-range"));
  // An anchor type this build cannot render must never be MINTED here; it may
  // only be carried through from the store.
  QTest::newRow("unsupported type") << unknownType;

  QJsonObject noQuads = validAnchor();
  noQuads.insert(QStringLiteral("quads"), QJsonArray());
  QTest::newRow("no quads") << noQuads;

  QJsonObject negativePage = validAnchor();
  negativePage.insert(QStringLiteral("page"), -1);
  QTest::newRow("negative page") << negativePage;

  QJsonObject badQuad = validAnchor();
  QJsonArray shortQuad;
  for (int i = 0; i < 4; ++i) {
    shortQuad.append(i);
  }
  QJsonArray badQuads;
  badQuads.append(shortQuad);
  badQuad.insert(QStringLiteral("quads"), badQuads);
  QTest::newRow("malformed quad") << badQuad;

  QJsonObject tooManyQuads = validAnchor();
  QJsonArray quads;
  QJsonArray quad;
  for (int i = 0; i < 8; ++i) {
    quad.append(1.0);
  }
  for (int i = 0; i <= PdfQuadsAnchor::maxQuadsPerComment(); ++i) {
    quads.append(quad);
  }
  tooManyQuads.insert(QStringLiteral("quads"), quads);
  QTest::newRow("over the quad cap") << tooManyQuads;

  // NaN/Inf would poison the overlay projection rather than fail loudly.
  QJsonObject nanQuad = validAnchor();
  QJsonArray nanQuads;
  QJsonArray q;
  for (int i = 0; i < 8; ++i) {
    q.append(std::numeric_limits<double>::infinity());
  }
  nanQuads.append(q);
  nanQuad.insert(QStringLiteral("quads"), nanQuads);
  QTest::newRow("non-finite coordinates") << nanQuad;
}

void TestPdfViewerAdapterComments::addRejectsAnchors() {
  QFETCH(QJsonObject, anchor);

  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(10);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*rejected.*")));
  adapter.requestAddComment(anchor, QStringLiteral("yellow"));

  QCOMPARE(spy.count(), 0);
}

void TestPdfViewerAdapterComments::addRejectsAPageBeyondTheDocument() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(5);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*rejected.*")));
  adapter.requestAddComment(validAnchor(5), QStringLiteral("yellow"));
  QCOMPARE(spy.count(), 0);

  adapter.requestAddComment(validAnchor(4), QStringLiteral("yellow"));
  QCOMPARE(spy.count(), 1);
}

// A literal color must never reach the store: it would be wrong in 11 of 12
// themes and could not follow a theme switch.
void TestPdfViewerAdapterComments::addNormalizesAnUnknownColor() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(2);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  adapter.requestAddComment(validAnchor(0), QStringLiteral("#ff00ff"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(1).toString(), CommentColor::defaultToken());
}

// Truncated, not rejected: an over-long selection is a plausible user action.
void TestPdfViewerAdapterComments::addTruncatesOverlongAnchorText() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(2);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  const QString huge(PdfQuadsAnchor::maxAnchorTextLength() * 4, QLatin1Char('x'));
  adapter.requestAddComment(validAnchor(0, huge), QStringLiteral("yellow"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(PdfQuadsAnchor::text(spy.at(0).at(0).toJsonObject()).size(),
           PdfQuadsAnchor::maxAnchorTextLength());
}

void TestPdfViewerAdapterComments::documentPageCountIsBounded_data() {
  QTest::addColumn<int>("reported");
  QTest::addColumn<int>("accepted");

  QTest::newRow("normal") << 42 << 42;
  QTest::newRow("zero") << 0 << 0;
  QTest::newRow("negative") << -5 << 0;
  QTest::newRow("absurd") << 100000000 << 0;
}

void TestPdfViewerAdapterComments::documentPageCountIsBounded() {
  QFETCH(int, reported);
  QFETCH(int, accepted);

  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(reported);
  QCOMPARE(adapter.getDocumentPageCount(), accepted);
}

void TestPdfViewerAdapterComments::idBearingSlotsAreLengthBounded() {
  PdfViewerAdapter adapter;
  QSignalSpy selectSpy(&adapter, &PdfViewerAdapter::selectCommentRequested);
  QSignalSpy deleteSpy(&adapter, &PdfViewerAdapter::deleteCommentRequested);

  const QString huge(4096, QLatin1Char('a'));
  adapter.requestSelectComment(huge);
  adapter.requestDeleteComment(huge);
  adapter.requestSelectComment(QString());
  adapter.requestDeleteComment(QString());
  QCOMPARE(selectSpy.count(), 0);
  QCOMPARE(deleteSpy.count(), 0);

  adapter.requestSelectComment(QStringLiteral("real-id"));
  adapter.requestDeleteComment(QStringLiteral("real-id"));
  QCOMPARE(selectSpy.count(), 1);
  QCOMPARE(deleteSpy.count(), 1);
}

// ============ Reload replay ============

void TestPdfViewerAdapterComments::commentsArePublishedImmediatelyWhenReady() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::commentsUpdated);
  adapter.setComments(oneComment(QStringLiteral("a")));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toJsonArray().size(), 1);
}

// The regression this whole latch exists for.
void TestPdfViewerAdapterComments::commentsAreLatchedAndReplayedOnceAfterAReload() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::commentsUpdated);

  // A reload begins: the window drives the adapter to not-ready.
  adapter.setReady(false);
  spy.clear();

  // C++ publishes while the replacement page is still loading. Nothing may be
  // emitted at a destroyed page.
  adapter.setComments(oneComment(QStringLiteral("during-reload")));
  QCOMPARE(spy.count(), 0);

  // The replacement page's QWebChannel comes up.
  adapter.setReady(true);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toJsonArray().first().toObject().value(QStringLiteral("id")).toString(),
           QStringLiteral("during-reload"));

  // ...and exactly once: a second ready transition with no new set is silent.
  spy.clear();
  adapter.setReady(false);
  adapter.setReady(true);
  QCOMPARE(spy.count(), 0);
}

// Latched, not queued: intermediate snapshots must not be replayed.
void TestPdfViewerAdapterComments::onlyTheNewestSetIsReplayed() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setReady(false);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::commentsUpdated);
  adapter.setComments(oneComment(QStringLiteral("v1")));
  adapter.setComments(oneComment(QStringLiteral("v2")));
  adapter.setComments(oneComment(QStringLiteral("v3")));
  QCOMPARE(spy.count(), 0);

  adapter.setReady(true);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toJsonArray().first().toObject().value(QStringLiteral("id")).toString(),
           QStringLiteral("v3"));
}

// clearComments() runs when the page is being torn down, so its empty set must
// NOT be latched — replaying it would blank the replacement document's real set.
void TestPdfViewerAdapterComments::clearCommentsDropsTheLatch() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setReady(false);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::commentsUpdated);
  adapter.setComments(oneComment(QStringLiteral("stale")));
  adapter.clearComments();
  QCOMPARE(adapter.getDocumentPageCount(), 0);

  adapter.setReady(true);
  QCOMPARE(spy.count(), 0);
  QVERIFY(adapter.getComments().isEmpty());
}

// ============ Context-menu capture ============
//
// The DISCOVERABLE way to create a highlight. Alt+drag is only a shortcut; a
// feature reachable solely by a modifier key the user has to guess is not
// reachable at all, so this route has to keep working.

void TestPdfViewerAdapterComments::captureSelectionForwardsTheColor() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::captureSelectionRequested);
  adapter.captureSelection(QStringLiteral("green"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("green"));
}

void TestPdfViewerAdapterComments::captureSelectionNormalizesAnUnknownColor() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::captureSelectionRequested);
  adapter.captureSelection(QStringLiteral("#ff00ff"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), CommentColor::defaultToken());
}

// Deliberately NOT latched: a selection only exists in a live document, so
// replaying this after a reload would act on whatever happened to be selected
// in the REPLACEMENT document.
void TestPdfViewerAdapterComments::captureSelectionIsDroppedWhenThePageIsNotReady() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setReady(false);

  QSignalSpy spy(&adapter, &PdfViewerAdapter::captureSelectionRequested);
  adapter.captureSelection(QStringLiteral("blue"));
  QCOMPARE(spy.count(), 0);

  adapter.setReady(true);
  QCOMPARE(spy.count(), 0);
}

// === Ink and free text ===

void TestPdfViewerAdapterComments::inkAndFreeTextAnchorsAreAccepted() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(10);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  adapter.requestAddComment(PdfInkAnchor::make(2, {{0.0, 0.0, 5.0, 5.0, 9.0, 1.0}}, 1.5),
                            QStringLiteral("blue"));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toJsonObject().value(QStringLiteral("type")).toString(),
           PdfInkAnchor::type());

  adapter.requestAddComment(PdfFreeTextAnchor::make(3, 100.0, 640.0, 12.0), QStringLiteral("pink"));
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(1).at(0).toJsonObject().value(QStringLiteral("type")).toString(),
           PdfFreeTextAnchor::type());

  // The quads-only `text` truncation must not be applied to types with no such field.
  QVERIFY(!spy.at(0).at(0).toJsonObject().contains(QStringLiteral("text")));
  QVERIFY(!spy.at(1).at(0).toJsonObject().contains(QStringLiteral("text")));
}

// The page ceiling comes from the shared dispatch, so it must hold for the new
// types too and not only for quads.
void TestPdfViewerAdapterComments::newAnchorTypesAreStillPageBounded() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(5);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*rejected.*")));
  adapter.requestAddComment(PdfInkAnchor::make(5, {{0.0, 0.0, 1.0, 1.0}}, 1.0),
                            CommentColor::defaultToken());
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*rejected.*")));
  adapter.requestAddComment(PdfFreeTextAnchor::make(99, 1.0, 1.0, 12.0),
                            CommentColor::defaultToken());
  QCOMPARE(spy.count(), 0);

  adapter.requestAddComment(PdfInkAnchor::make(4, {{0.0, 0.0, 1.0, 1.0}}, 1.0),
                            CommentColor::defaultToken());
  QCOMPARE(spy.count(), 1);
}

// The toolbar keeps showing the armed tool across a document reload, so the
// replacement page must be told about it or the toggle would look pressed while
// the page sat in reading mode.
void TestPdfViewerAdapterComments::toolAndColourAreLatchedAcrossAReload() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy tools(&adapter, &PdfViewerAdapter::toolChanged);
  QSignalSpy colors(&adapter, &PdfViewerAdapter::commentColorChanged);

  adapter.setTool(PdfViewerAdapter::Tool::Ink);
  adapter.setCommentColor(QStringLiteral("purple"));
  QCOMPARE(tools.last().at(0).toString(), QStringLiteral("ink"));
  QCOMPARE(colors.last().at(0).toString(), QStringLiteral("purple"));

  tools.clear();
  colors.clear();
  adapter.clearComments();
  adapter.setReady(false);
  QCOMPARE(tools.count(), 0);

  adapter.setReady(true);
  QCOMPARE(tools.count(), 1);
  QCOMPARE(tools.at(0).at(0).toString(), QStringLiteral("ink"));
  QCOMPARE(colors.at(0).at(0).toString(), QStringLiteral("purple"));
  QCOMPARE(adapter.getTool(), PdfViewerAdapter::Tool::Ink);
}

void TestPdfViewerAdapterComments::toolFinishedFromTheWebSideClearsTheTool() {
  PdfViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setTool(PdfViewerAdapter::Tool::FreeText);

  QSignalSpy finished(&adapter, &PdfViewerAdapter::toolFinished);
  adapter.notifyToolFinished();

  QCOMPARE(finished.count(), 1);
  QVERIFY2(adapter.getTool() == PdfViewerAdapter::Tool::None,
           "the adapter must mirror the web side, or the toolbar and page disagree");
}

// The web side may CARRY an unknown anchor type through from the store, but it
// must never be able to invent one.
void TestPdfViewerAdapterComments::unknownAnchorTypesCannotBeMinted() {
  PdfViewerAdapter adapter;
  adapter.setDocumentPageCount(10);
  QSignalSpy spy(&adapter, &PdfViewerAdapter::addCommentRequested);

  QJsonObject future;
  future.insert(QStringLiteral("type"), QStringLiteral("markdown-range"));
  future.insert(QStringLiteral("page"), 0);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*unsupported type.*")));
  adapter.requestAddComment(future, CommentColor::defaultToken());
  QCOMPARE(spy.count(), 0);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerAdapterComments)
#include "test_pdfvieweradapter_comments.moc"
