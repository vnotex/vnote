// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_commentservice.cpp
//
// Covers the two halves of the `comments.json` sidecar store that are easy to
// get silently wrong:
//
//   * WHERE the store lives (bundled UUID assets folder vs sibling file vs
//     external file) — a wrong answer here does not fail, it just loses the
//     user's comments somewhere else;
//   * the SCHEMA round trip, in particular that an anchor type this build does
//     not implement survives untouched, so an older PDF-only VNote cannot eat a
//     newer build's Markdown comments.
//
// The asynchronous write path is exercised end to end (schedule -> worker ->
// QSaveFile commit -> queued saveFinished) rather than by poking the queue.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>

#include <core/nodeidentifier.h>
#include <core/services/commentservice.h>
#include <core/services/commenttypes.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>

#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestCommentService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  // ---- schema ----
  void schemaRoundTripsAKnownAnchor();
  void schemaPreservesAnUnknownAnchorType();
  void schemaPreservesUnknownTopLevelKeys();
  void schemaDropsStructurallyInvalidComments();
  void schemaOrderingIsStable();
  void schemaNormalizesAnUnknownColor();
  void everyColorTokenHasACapitalizedDisplayName();
  void inkAnchorValidation_data();
  void inkAnchorValidation();

  void inkOpacityDefaultsToSolidAndIsNormalizedInkOnly();
  void freeTextAnchorValidation_data();
  void freeTextAnchorValidation();
  void anchorDispatchCoversEveryKnownType();
  void anchorValidationRejectsBadGeometry_data();
  void anchorValidationRejectsBadGeometry();

  // ---- location ----
  void locationForAnExternalFileIsASibling();
  void locationForARawNotebookIsASibling();
  void locationForABundledNotebookIsTheAssetsFolder();
  void locationIsInvalidForAVirtualOrEmptyPath();

  // ---- io ----
  void savingCreatesTheParentDirectoryAndRoundTrips();
  void loadingAMissingStoreYieldsAnEmptySet();
  void loadingAMalformedStoreIsAnError();
  void readOnlyNotebookWritesAreRejectedBeforeTouchingDisk();
  void shutdownDrainsPendingWrites();
  void lifecycleOpsAreOrderedBehindPendingSaves();
  void rapidSavesCoalesceToTheNewestSnapshot();
  void savingEmitsStoreDirtyForANotebookOnly();

  // ---- sibling lifecycle ----
  void renamingARawFileMovesItsSidecar();
  void deletingARawFileRemovesItsSidecar();
  void movingOntoAnExistingSidecarLeavesBothInPlace();

private:
  static Comment makeComment(int p_page, const QString &p_text);

  // A raw notebook rooted in a temp dir, plus the identifier of one file in it.
  QString createRawNotebook(const QString &p_name);

  // Lifecycle operations are QUEUED behind pending saves rather than run inline,
  // so a test that checks the filesystem immediately would race the worker.
  void waitForIdle(const NodeIdentifier &p_nodeId);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebooks = nullptr;
  NotebookIoGate *m_gate = nullptr;
  HookManager *m_hooks = nullptr;
  CommentService *m_service = nullptr;
  TempDirFixture *m_tmp = nullptr;
};

void TestCommentService::initTestCase() {
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_hooks = new HookManager(this);
  m_notebooks = new NotebookCoreService(m_context);
  m_notebooks->setHookManager(m_hooks);
  m_gate = new NotebookIoGate();
  m_service = new CommentService(m_notebooks, m_gate, m_hooks);
}

void TestCommentService::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;
  delete m_gate;
  m_gate = nullptr;
  delete m_notebooks;
  m_notebooks = nullptr;
  delete m_tmp;
  m_tmp = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestCommentService::init() {
  delete m_tmp;
  m_tmp = new TempDirFixture();
  QVERIFY(m_tmp->isValid());
}

void TestCommentService::waitForIdle(const NodeIdentifier &p_nodeId) {
  for (int i = 0; i < 250 && m_service->isBusy(p_nodeId); ++i) {
    QTest::qWait(20);
  }
  QVERIFY2(!m_service->isBusy(p_nodeId), "the comment queue never drained");
  QTest::qWait(50);
}

Comment TestCommentService::makeComment(int p_page, const QString &p_text) {
  QVector<QVector<double>> quads;
  quads.append(QVector<double>{10, 20, 110, 20, 110, 40, 10, 40});
  return Comment::create(PdfQuadsAnchor::make(p_page, quads, QStringLiteral("quoted")), p_text,
                         QStringLiteral("green"));
}

// ============ Schema ============

void TestCommentService::schemaRoundTripsAKnownAnchor() {
  CommentSet set;
  set.m_comments.append(makeComment(2, QStringLiteral("hello")));

  const auto reparsed = CommentSet::fromJson(set.toJson());
  QCOMPARE(reparsed.m_version, CommentSet::currentVersion());
  QCOMPARE(reparsed.m_comments.size(), 1);

  const auto &comment = reparsed.m_comments.first();
  QCOMPARE(comment.m_id, set.m_comments.first().m_id);
  QCOMPARE(comment.m_text, QStringLiteral("hello"));
  QCOMPARE(comment.m_color, QStringLiteral("green"));
  QVERIFY(comment.hasKnownAnchorType());
  QCOMPARE(PdfQuadsAnchor::page(comment.m_anchor), 2);
  QCOMPARE(PdfQuadsAnchor::text(comment.m_anchor), QStringLiteral("quoted"));
}

// The forward-compatibility guarantee: a PDF-only build must be able to load,
// hold and rewrite a Markdown comment created by a newer VNote.
void TestCommentService::schemaPreservesAnUnknownAnchorType() {
  QJsonObject anchor;
  anchor.insert(QStringLiteral("type"), QStringLiteral("markdown-range"));
  anchor.insert(QStringLiteral("startOffset"), 42);
  anchor.insert(QStringLiteral("endOffset"), 99);
  anchor.insert(QStringLiteral("customThing"), QStringLiteral("keep me"));

  QJsonObject comment;
  comment.insert(QStringLiteral("id"), QStringLiteral("abc"));
  comment.insert(QStringLiteral("text"), QStringLiteral("from the future"));
  comment.insert(QStringLiteral("color"), QStringLiteral("blue"));
  comment.insert(QStringLiteral("anchor"), anchor);

  QJsonArray comments;
  comments.append(comment);
  QJsonObject doc;
  doc.insert(QStringLiteral("version"), 1);
  doc.insert(QStringLiteral("comments"), comments);

  const auto set = CommentSet::fromJson(doc);
  QCOMPARE(set.m_comments.size(), 1);
  QVERIFY(!set.m_comments.first().hasKnownAnchorType());
  QVERIFY(set.m_comments.first().isValid());

  const auto rewritten = CommentSet::fromJson(set.toJson());
  QCOMPARE(rewritten.m_comments.size(), 1);
  QCOMPARE(rewritten.m_comments.first().m_anchor, anchor);
  QCOMPARE(rewritten.m_comments.first().m_text, QStringLiteral("from the future"));
}

void TestCommentService::schemaPreservesUnknownTopLevelKeys() {
  QJsonObject doc;
  doc.insert(QStringLiteral("version"), 1);
  doc.insert(QStringLiteral("comments"), QJsonArray());
  doc.insert(QStringLiteral("futureSection"), QStringLiteral("do not eat"));

  QJsonObject commentObj = makeComment(0, QStringLiteral("x")).toJson();
  commentObj.insert(QStringLiteral("futureField"), 7);
  QJsonArray comments;
  comments.append(commentObj);
  doc.insert(QStringLiteral("comments"), comments);

  const auto rewritten = CommentSet::fromJson(CommentSet::fromJson(doc).toJson()).toJson();
  QCOMPARE(rewritten.value(QStringLiteral("futureSection")).toString(),
           QStringLiteral("do not eat"));
  QCOMPARE(rewritten.value(QStringLiteral("comments"))
               .toArray()
               .first()
               .toObject()
               .value(QStringLiteral("futureField"))
               .toInt(),
           7);
}

void TestCommentService::schemaDropsStructurallyInvalidComments() {
  QJsonArray comments;
  // No id.
  comments.append(QJsonObject{{QStringLiteral("anchor"), makeComment(0, QString()).m_anchor}});
  // No anchor type.
  comments.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("a")},
                              {QStringLiteral("anchor"), QJsonObject()}});
  // Known type, broken geometry.
  QJsonObject badAnchor;
  badAnchor.insert(QStringLiteral("type"), PdfQuadsAnchor::type());
  badAnchor.insert(QStringLiteral("page"), 0);
  badAnchor.insert(QStringLiteral("quads"), QJsonArray());
  comments.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("b")},
                              {QStringLiteral("anchor"), badAnchor}});
  // One good one.
  comments.append(makeComment(0, QStringLiteral("good")).toJson());

  QJsonObject doc;
  doc.insert(QStringLiteral("comments"), comments);

  const auto set = CommentSet::fromJson(doc);
  QCOMPARE(set.m_comments.size(), 1);
  QCOMPARE(set.m_comments.first().m_text, QStringLiteral("good"));
}

// Stable ordering is what keeps a git/sync diff readable and a conflict
// resolvable, so it is a contract, not an implementation detail.
void TestCommentService::schemaOrderingIsStable() {
  CommentSet set;
  auto a = makeComment(5, QStringLiteral("five"));
  auto b = makeComment(1, QStringLiteral("one"));
  auto c = makeComment(1, QStringLiteral("one-too"));
  a.m_id = QStringLiteral("zzz");
  b.m_id = QStringLiteral("mmm");
  c.m_id = QStringLiteral("aaa");
  set.m_comments << a << b << c;

  const auto first = QJsonDocument(set.toJson()).toJson(QJsonDocument::Compact);

  // Shuffle the in-memory order; the serialized bytes must not move.
  CommentSet shuffled;
  shuffled.m_comments << c << a << b;
  const auto second = QJsonDocument(shuffled.toJson()).toJson(QJsonDocument::Compact);

  QCOMPARE(second, first);

  // And the order itself is (page, id).
  const auto parsed = CommentSet::fromJson(set.toJson());
  QCOMPARE(parsed.m_comments[0].m_id, QStringLiteral("aaa"));
  QCOMPARE(parsed.m_comments[1].m_id, QStringLiteral("mmm"));
  QCOMPARE(parsed.m_comments[2].m_id, QStringLiteral("zzz"));
}

// A literal hex in the store would be unreadable after a theme switch, and an
// unknown token would render unstyled. Both collapse to the default.
void TestCommentService::schemaNormalizesAnUnknownColor() {
  auto comment = makeComment(0, QString());
  auto obj = comment.toJson();
  obj.insert(QStringLiteral("color"), QStringLiteral("#ff00ff"));

  QCOMPARE(Comment::fromJson(obj).m_color, CommentColor::defaultToken());

  obj.insert(QStringLiteral("color"), QStringLiteral("chartreuse"));
  QCOMPARE(Comment::fromJson(obj).m_color, CommentColor::defaultToken());
}

// The comment dock and the PDF page context menu both build their color lists
// from CommentColor::all() and label them with displayName(). A token with no
// entry would surface as a raw lowercase identifier in the UI (which is what it
// did before) or as an empty row, and adding a 6th color must not silently
// offer it in one place and not the other.
void TestCommentService::everyColorTokenHasACapitalizedDisplayName() {
  const auto tokens = CommentColor::all();
  QVERIFY2(!tokens.isEmpty(), "the color schema is empty; the gate would be vacuous");

  QSet<QString> seen;
  for (const auto &token : tokens) {
    const auto name = CommentColor::displayName(token);

    QVERIFY2(!name.isEmpty(),
             qPrintable(QStringLiteral("no display name for token '%1'").arg(token)));
    QVERIFY2(name != token,
             qPrintable(QStringLiteral("token '%1' has no display name and fell back to the raw "
                                       "identifier")
                            .arg(token)));
    QVERIFY2(name.at(0).isUpper(),
             qPrintable(QStringLiteral("display name '%1' must start with a capital").arg(name)));
    QVERIFY2(!seen.contains(name),
             qPrintable(QStringLiteral("duplicate display name '%1'").arg(name)));
    seen.insert(name);
  }

  // An unknown token warns and falls back to itself rather than rendering blank.
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*no display name.*")));
  QCOMPARE(CommentColor::displayName(QStringLiteral("chartreuse")), QStringLiteral("chartreuse"));
}
namespace {

QJsonArray flatStroke(int p_points) {
  QJsonArray stroke;
  for (int i = 0; i < p_points; ++i) {
    stroke.append(i * 1.0);
    stroke.append(i * 2.0);
  }
  return stroke;
}

QJsonObject inkAnchor(int p_page, const QJsonArray &p_strokes, double p_width) {
  QJsonObject a;
  a.insert(QStringLiteral("type"), PdfInkAnchor::type());
  a.insert(QStringLiteral("page"), p_page);
  a.insert(QStringLiteral("strokes"), p_strokes);
  a.insert(QStringLiteral("width"), p_width);
  return a;
}

} // namespace

void TestCommentService::inkAnchorValidation_data() {
  QTest::addColumn<QJsonObject>("anchor");
  QTest::addColumn<bool>("valid");

  QJsonArray one;
  one.append(flatStroke(3));

  QTest::newRow("good") << inkAnchor(0, one, 1.5) << true;
  // A single point is a legal dot.
  QJsonArray dot;
  dot.append(flatStroke(1));
  QTest::newRow("single point dot") << inkAnchor(2, dot, 1.0) << true;

  QTest::newRow("negative page") << inkAnchor(-1, one, 1.5) << false;
  QTest::newRow("no strokes") << inkAnchor(0, QJsonArray(), 1.5) << false;

  // Flat x,y pairs, so an odd coordinate count is malformed.
  QJsonArray odd;
  QJsonArray oddStroke = flatStroke(2);
  oddStroke.removeLast();
  odd.append(oddStroke);
  QTest::newRow("odd coordinate count") << inkAnchor(0, odd, 1.5) << false;

  QJsonArray empty;
  empty.append(QJsonArray());
  QTest::newRow("empty stroke") << inkAnchor(0, empty, 1.5) << false;

  QJsonArray nonFinite;
  QJsonArray bad;
  bad.append(0.0);
  bad.append(std::numeric_limits<double>::infinity());
  nonFinite.append(bad);
  QTest::newRow("non-finite point") << inkAnchor(0, nonFinite, 1.5) << false;

  QTest::newRow("width too small") << inkAnchor(0, one, 0.0) << false;
  QTest::newRow("width too large") << inkAnchor(0, one, 1000.0) << false;

  QJsonArray tooMany;
  for (int i = 0; i <= PdfInkAnchor::maxStrokesPerComment(); ++i) {
    tooMany.append(flatStroke(2));
  }
  QTest::newRow("over the stroke cap") << inkAnchor(0, tooMany, 1.5) << false;

  // The real hazard: strokes that are each individually legal, and few enough
  // to pass the stroke cap, whose AGGREGATE point count explodes. Bounding the
  // two caps independently would let this through.
  QJsonArray aggregate;
  const int perStroke = PdfInkAnchor::maxPointsPerComment() / 4;
  for (int i = 0; i < 8; ++i) {
    aggregate.append(flatStroke(perStroke));
  }
  QVERIFY(aggregate.size() <= PdfInkAnchor::maxStrokesPerComment());
  QTest::newRow("over the aggregate point cap") << inkAnchor(0, aggregate, 1.5) << false;

  // Just under the aggregate cap is still fine.
  QJsonArray justUnder;
  justUnder.append(flatStroke(PdfInkAnchor::maxPointsPerComment()));
  QTest::newRow("at the aggregate point cap") << inkAnchor(0, justUnder, 1.5) << true;

  // A page must be an EXACT non-negative integer: 0.5 would validate against
  // page 0, then be stored verbatim and never render.
  QJsonObject fractional = inkAnchor(0, one, 1.5);
  fractional.insert(QStringLiteral("page"), 0.5);
  QTest::newRow("fractional page") << fractional << false;

  QJsonObject wrongType = inkAnchor(0, one, 1.5);
  wrongType.insert(QStringLiteral("type"), PdfQuadsAnchor::type());
  QTest::newRow("wrong type") << wrongType << false;

  // Opacity is OPTIONAL: an anchor written before the field existed must stay
  // valid, or every legacy scribble would vanish from the page.
  QTest::newRow("legacy anchor without opacity") << inkAnchor(0, one, 1.5) << true;

  QJsonObject withOpacity = inkAnchor(0, one, 1.5);
  withOpacity.insert(QStringLiteral("opacity"), 0.35);
  QTest::newRow("in-range opacity") << withOpacity << true;

  // A PRESENT but malformed opacity is REJECTED, not clamped: anchors arriving
  // from the page are untrusted (the adapter copies them verbatim).
  QJsonObject hugeOpacity = inkAnchor(0, one, 1.5);
  hugeOpacity.insert(QStringLiteral("opacity"), 1.0e9);
  QTest::newRow("opacity above the range") << hugeOpacity << false;

  QJsonObject zeroOpacity = inkAnchor(0, one, 1.5);
  zeroOpacity.insert(QStringLiteral("opacity"), 0.0);
  QTest::newRow("opacity below the range") << zeroOpacity << false;

  QJsonObject nanOpacity = inkAnchor(0, one, 1.5);
  nanOpacity.insert(QStringLiteral("opacity"), std::numeric_limits<double>::quiet_NaN());
  QTest::newRow("non-finite opacity") << nanOpacity << false;

  QJsonObject stringOpacity = inkAnchor(0, one, 1.5);
  stringOpacity.insert(QStringLiteral("opacity"), QStringLiteral("0.5"));
  QTest::newRow("string opacity") << stringOpacity << false;
}

void TestCommentService::inkAnchorValidation() {
  QFETCH(QJsonObject, anchor);
  QFETCH(bool, valid);
  QCOMPARE(PdfInkAnchor::isValid(anchor), valid);
  // The generic dispatch must reach the SAME verdict. Every row here is either
  // a pdf-ink anchor (dispatch -> PdfInkAnchor::isValid) or the wrong-type row,
  // whose type is pdf-quads and which must be rejected by the quads validator
  // it dispatches to -- so in both cases the expected result is `valid`.
  QCOMPARE(isAnchorStructurallyValid(anchor), valid);
}

void TestCommentService::inkOpacityDefaultsToSolidAndIsNormalizedInkOnly() {
  // A legacy anchor with no opacity key reads as fully opaque, so an old
  // scribble keeps rendering exactly as it always did.
  QJsonArray strokes;
  strokes.append(flatStroke(3));
  QCOMPARE(PdfInkAnchor::opacity(inkAnchor(0, strokes, 1.5)), 1.0);

  // make() clamps, like the width.
  const auto anchor = PdfInkAnchor::make(0, {{0.0, 0.0, 1.0, 1.0}}, 1.5, 0.35);
  QCOMPARE(PdfInkAnchor::opacity(anchor), 0.35);
  QVERIFY(PdfInkAnchor::isValid(anchor));
  QCOMPARE(PdfInkAnchor::opacity(PdfInkAnchor::make(0, {{0.0, 0.0, 1.0, 1.0}}, 1.5, 9.0)),
           PdfInkAnchor::maxOpacity());
  QCOMPARE(PdfInkAnchor::opacity(PdfInkAnchor::make(0, {{0.0, 0.0, 1.0, 1.0}}, 1.5, -1.0)),
           PdfInkAnchor::minOpacity());
  // The default keeps every existing call site emitting a solid stroke.
  QCOMPARE(PdfInkAnchor::opacity(PdfInkAnchor::make(0, {{0.0, 0.0, 1.0, 1.0}}, 1.5)), 1.0);

  // Tool options: ink carries opacity, nothing else does.
  QVERIFY(PdfToolOptions::hasOpacity(PdfToolOptions::inkTool()));
  QVERIFY(!PdfToolOptions::hasOpacity(PdfToolOptions::highlightTool()));
  QVERIFY(!PdfToolOptions::hasOpacity(PdfToolOptions::freeTextTool()));

  const auto inkDefaults = PdfToolOptions::defaults(PdfToolOptions::inkTool());
  QCOMPARE(inkDefaults.value(PdfToolOptions::opacityKey()).toDouble(),
           PdfToolOptions::defaultOpacity());
  QVERIFY(!PdfToolOptions::defaults(PdfToolOptions::highlightTool())
               .contains(PdfToolOptions::opacityKey()));
  QVERIFY(!PdfToolOptions::defaults(PdfToolOptions::freeTextTool())
               .contains(PdfToolOptions::opacityKey()));

  // Same split as the width: non-finite -> default (a NaN carries no intent),
  // finite but out of range -> clamped. The EXACT value is asserted, because
  // "the result validates" passes for both.
  const auto normalizedOf = [](double p_value) {
    QJsonObject raw;
    raw.insert(PdfToolOptions::opacityKey(), p_value);
    return PdfToolOptions::normalize(PdfToolOptions::inkTool(), raw)
        .value(PdfToolOptions::opacityKey())
        .toDouble();
  };
  QCOMPARE(normalizedOf(0.35), 0.35);
  QCOMPARE(normalizedOf(1.0e9), PdfInkAnchor::maxOpacity());
  QCOMPARE(normalizedOf(-5.0), PdfInkAnchor::minOpacity());
  QCOMPARE(normalizedOf(std::numeric_limits<double>::quiet_NaN()),
           PdfToolOptions::defaultOpacity());

  QJsonObject wrongType;
  wrongType.insert(PdfToolOptions::opacityKey(), QStringLiteral("0.5"));
  QCOMPARE(PdfToolOptions::normalize(PdfToolOptions::inkTool(), wrongType)
               .value(PdfToolOptions::opacityKey())
               .toDouble(),
           PdfToolOptions::defaultOpacity());

  // The key is never emitted for a tool that does not carry it.
  QVERIFY(!PdfToolOptions::normalize(PdfToolOptions::freeTextTool(), wrongType)
               .contains(PdfToolOptions::opacityKey()));

  // A stored comment survives the document round trip with the EXACT value --
  // "it still validates" would pass for a silently defaulted 1.0 too.
  CommentSet set;
  set.m_comments.append(Comment::create(PdfInkAnchor::make(1, {{0.0, 0.0, 1.0, 1.0}}, 1.5, 0.35),
                                        QString(), CommentColor::defaultToken()));
  const auto reloaded = CommentSet::fromJson(set.toJson());
  QCOMPARE(reloaded.m_comments.size(), 1);
  QCOMPARE(PdfInkAnchor::opacity(reloaded.m_comments.at(0).m_anchor), 0.35);
}

void TestCommentService::freeTextAnchorValidation_data() {
  QTest::addColumn<QJsonObject>("anchor");
  QTest::addColumn<bool>("valid");

  QTest::newRow("good") << PdfFreeTextAnchor::make(0, 100.0, 640.0, 12.0) << true;
  QTest::newRow("negative page") << PdfFreeTextAnchor::make(-1, 1.0, 1.0, 12.0) << false;

  QJsonObject noX = PdfFreeTextAnchor::make(0, 1.0, 1.0, 12.0);
  noX.remove(QStringLiteral("x"));
  QTest::newRow("missing x") << noX << false;

  QJsonObject badY = PdfFreeTextAnchor::make(0, 1.0, 1.0, 12.0);
  badY.insert(QStringLiteral("y"), std::numeric_limits<double>::quiet_NaN());
  QTest::newRow("NaN y") << badY << false;

  QJsonObject tinyFont = PdfFreeTextAnchor::make(0, 1.0, 1.0, 12.0);
  tinyFont.insert(QStringLiteral("fontSize"), 0.5);
  QTest::newRow("font too small") << tinyFont << false;

  QJsonObject hugeFont = PdfFreeTextAnchor::make(0, 1.0, 1.0, 12.0);
  hugeFont.insert(QStringLiteral("fontSize"), 5000.0);
  QTest::newRow("font too large") << hugeFont << false;

  QJsonObject fractionalPage = PdfFreeTextAnchor::make(0, 1.0, 1.0, 12.0);
  fractionalPage.insert(QStringLiteral("page"), 2.5);
  QTest::newRow("fractional page") << fractionalPage << false;
}

void TestCommentService::freeTextAnchorValidation() {
  QFETCH(QJsonObject, anchor);
  QFETCH(bool, valid);
  QCOMPARE(PdfFreeTextAnchor::isValid(anchor), valid);
}

// The dispatch is what Comment::isValid(), the sort key and the adapter all go
// through, so a new anchor type that is not wired into all three would render
// but never persist (or vice versa).
void TestCommentService::anchorDispatchCoversEveryKnownType() {
  QVector<QVector<double>> quads;
  quads.append(QVector<double>{0, 0, 10, 0, 10, 10, 0, 10});

  const QVector<QJsonObject> known = {PdfQuadsAnchor::make(1, quads, QStringLiteral("q")),
                                      PdfInkAnchor::make(2, {{0.0, 0.0, 5.0, 5.0}}, 1.5),
                                      PdfFreeTextAnchor::make(3, 10.0, 20.0, 12.0)};

  const QVector<int> pages = {1, 2, 3};
  for (int i = 0; i < known.size(); ++i) {
    QVERIFY2(isKnownAnchorType(known[i]), qPrintable(QStringLiteral("row %1").arg(i)));
    QVERIFY2(isAnchorStructurallyValid(known[i]), qPrintable(QStringLiteral("row %1").arg(i)));
    QCOMPARE(anchorPage(known[i]), pages[i]);

    Comment c;
    c.m_id = QStringLiteral("x");
    c.m_anchor = known[i];
    QVERIFY(c.isValid());
    QVERIFY(c.hasKnownAnchorType());
  }

  // An unknown type stays valid-but-opaque, and has no page.
  QJsonObject future;
  future.insert(QStringLiteral("type"), QStringLiteral("markdown-range"));
  QVERIFY(!isKnownAnchorType(future));
  QVERIFY(isAnchorStructurallyValid(future));
  QCOMPARE(anchorPage(future), -1);

  // A typeless anchor is not valid at all.
  QVERIFY(!isAnchorStructurallyValid(QJsonObject()));
}
void TestCommentService::anchorValidationRejectsBadGeometry_data() {
  QTest::addColumn<QJsonObject>("anchor");
  QTest::addColumn<bool>("valid");

  QJsonArray goodQuad;
  for (int i = 0; i < 8; ++i) {
    goodQuad.append(i * 1.5);
  }

  auto base = [&goodQuad](int p_page, const QJsonArray &p_quads) {
    QJsonObject a;
    a.insert(QStringLiteral("type"), PdfQuadsAnchor::type());
    a.insert(QStringLiteral("page"), p_page);
    a.insert(QStringLiteral("quads"), p_quads);
    return a;
  };

  QJsonArray oneQuad;
  oneQuad.append(goodQuad);
  QTest::newRow("good") << base(0, oneQuad) << true;

  QTest::newRow("negative page") << base(-1, oneQuad) << false;
  QTest::newRow("no quads") << base(0, QJsonArray()) << false;

  QJsonArray shortQuad;
  for (int i = 0; i < 6; ++i) {
    shortQuad.append(i);
  }
  QJsonArray shortQuads;
  shortQuads.append(shortQuad);
  QTest::newRow("quad of 6") << base(0, shortQuads) << false;

  QJsonArray stringQuad;
  for (int i = 0; i < 8; ++i) {
    stringQuad.append(QStringLiteral("1"));
  }
  QJsonArray stringQuads;
  stringQuads.append(stringQuad);
  QTest::newRow("non-numeric coordinates") << base(0, stringQuads) << false;

  QJsonArray tooMany;
  for (int i = 0; i <= PdfQuadsAnchor::maxQuadsPerComment(); ++i) {
    tooMany.append(goodQuad);
  }
  QTest::newRow("over the quad cap") << base(0, tooMany) << false;

  QJsonObject wrongType = base(0, oneQuad);
  wrongType.insert(QStringLiteral("type"), QStringLiteral("markdown-range"));
  QTest::newRow("wrong type") << wrongType << false;

  QJsonObject fractionalPage = base(0, oneQuad);
  fractionalPage.insert(QStringLiteral("page"), 1.5);
  QTest::newRow("fractional page") << fractionalPage << false;

  QJsonObject nanPage = base(0, oneQuad);
  nanPage.insert(QStringLiteral("page"), std::numeric_limits<double>::quiet_NaN());
  QTest::newRow("NaN page") << nanPage << false;
}

void TestCommentService::anchorValidationRejectsBadGeometry() {
  QFETCH(QJsonObject, anchor);
  QFETCH(bool, valid);
  QCOMPARE(PdfQuadsAnchor::isValid(anchor), valid);
}

// ============ Location ============

QString TestCommentService::createRawNotebook(const QString &p_name) {
  const QString root = QDir(m_tmp->path()).filePath(p_name);
  if (!QDir().mkpath(root)) {
    return QString();
  }

  const QString configJson = QStringLiteral("{\"name\": \"%1\", \"version\": \"1\"}").arg(p_name);
  return m_notebooks->createNotebook(root, configJson, NotebookType::Raw);
}

// An external file has no notebook at all: relativePath already holds the
// absolute on-disk path.
void TestCommentService::locationForAnExternalFileIsASibling() {
  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("paper.pdf"));

  const auto location = m_service->resolveLocation(nodeId);
  QVERIFY(location.isValid());
  QCOMPARE(location.m_kind, CommentService::Location::Kind::Sibling);
  QVERIFY(location.m_notebookId.isEmpty());
  QVERIFY2(!location.needsIoGate(), "an external file touches no notebook working tree");
  QCOMPARE(location.m_storePath, nodeId.relativePath + CommentService::siblingSuffix());
}

void TestCommentService::locationForARawNotebookIsASibling() {
  const QString notebookId = createRawNotebook(QStringLiteral("raw-nb"));
  QVERIFY2(!notebookId.isEmpty(), "failed to create the raw notebook fixture");

  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());
  QVERIFY(!root.isEmpty());
  QVERIFY(QFile(QDir(root).filePath(QStringLiteral("paper.pdf"))).open(QIODevice::WriteOnly));

  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = QStringLiteral("paper.pdf");

  const auto location = m_service->resolveLocation(nodeId);
  QVERIFY(location.isValid());
  QCOMPARE(location.m_kind, CommentService::Location::Kind::Sibling);
  QCOMPARE(location.m_notebookId, notebookId);
  QVERIFY(location.m_storePath.endsWith(QStringLiteral("paper.pdf.comments.json")));
}

void TestCommentService::locationForABundledNotebookIsTheAssetsFolder() {
  const QString root = QDir(m_tmp->path()).filePath(QStringLiteral("bundled-nb"));
  QVERIFY(QDir().mkpath(root));

  const QString configJson = QStringLiteral("{\"name\": \"bundled-nb\", \"version\": \"1\"}");
  const QString notebookId = m_notebooks->createNotebook(root, configJson, NotebookType::Bundled);
  QVERIFY2(!notebookId.isEmpty(), "failed to create the bundled notebook fixture");

  QVERIFY(!m_notebooks->createFile(notebookId, QString(), QStringLiteral("paper.pdf")).isEmpty());

  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = QStringLiteral("paper.pdf");

  const auto location = m_service->resolveLocation(nodeId);
  QVERIFY2(location.isValid(), "bundled location did not resolve");
  QCOMPARE(location.m_kind, CommentService::Location::Kind::Attachments);
  QVERIFY2(location.needsIoGate(), "a bundled write races git staging and MUST take the gate");
  QVERIFY(location.m_storePath.endsWith(QLatin1Char('/') + CommentService::storeFileName()));

  // getAttachmentsFolder() already appends the file UUID and does NOT create the
  // directory, so the store path must sit inside a folder that does not exist yet.
  const QFileInfo info(location.m_storePath);
  QVERIFY2(!info.dir().exists(), "the UUID assets folder must be created lazily, at write time");
}

void TestCommentService::locationIsInvalidForAVirtualOrEmptyPath() {
  QVERIFY(!m_service->resolveLocation(NodeIdentifier()).isValid());

  NodeIdentifier virtualId;
  virtualId.relativePath = QStringLiteral("vx://home");
  QVERIFY(!m_service->resolveLocation(virtualId).isValid());
}

// ============ IO ============

void TestCommentService::savingCreatesTheParentDirectoryAndRoundTrips() {
  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("nested/deeper/paper.pdf"));

  CommentSet set;
  set.m_comments.append(makeComment(3, QStringLiteral("a note")));

  QSignalSpy spy(m_service, &CommentService::saveFinished);
  m_service->scheduleSave(nodeId, set, 7);
  QVERIFY2(spy.wait(5000), "the save never completed");
  QCOMPARE(spy.count(), 1);
  // The generation is echoed back so the caller can tell WHICH snapshot landed
  // and keep a failed one dirty.
  QCOMPARE(spy.at(0).at(1).toULongLong(), 7ull);
  QVERIFY2(spy.at(0).at(2).toBool(), qPrintable(spy.at(0).at(3).toString()));

  const auto loaded = m_service->load(nodeId);
  QCOMPARE(loaded.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(loaded.m_comments.m_comments.size(), 1);
  QCOMPARE(loaded.m_comments.m_comments.first().m_text, QStringLiteral("a note"));
  QCOMPARE(PdfQuadsAnchor::page(loaded.m_comments.m_comments.first().m_anchor), 3);
}

void TestCommentService::loadingAMissingStoreYieldsAnEmptySet() {
  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("never-commented.pdf"));
  QCOMPARE(m_service->load(nodeId).m_status, CommentService::LoadResult::Status::Missing);
}

void TestCommentService::loadingAMalformedStoreIsAnError() {
  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("broken.pdf"));

  QFile file(nodeId.relativePath + CommentService::siblingSuffix());
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("{ this is not json");
  file.close();

  // Reported as Error, NOT as an empty set. The caller must be able to tell
  // "no store yet" from "the store is unreadable", or the next edit would
  // overwrite a file the user could still recover by hand.
  const auto result = m_service->load(nodeId);
  QCOMPARE(result.m_status, CommentService::LoadResult::Status::Error);
  QVERIFY(!result.isUsable());
  QVERIFY(!result.m_error.isEmpty());
}

void TestCommentService::rapidSavesCoalesceToTheNewestSnapshot() {
  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("busy.pdf"));

  QSignalSpy spy(m_service, &CommentService::saveFinished);

  for (int i = 0; i < 20; ++i) {
    CommentSet set;
    set.m_comments.append(makeComment(0, QStringLiteral("revision %1").arg(i)));
    m_service->scheduleSave(nodeId, set);
  }

  QVERIFY(spy.wait(5000));
  // Drain any trailing completions.
  while (m_service->isBusy(nodeId)) {
    QVERIFY(spy.wait(5000));
  }
  QTest::qWait(50);

  const auto loaded = m_service->load(nodeId);
  QCOMPARE(loaded.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(loaded.m_comments.m_comments.size(), 1);
  QCOMPARE(loaded.m_comments.m_comments.first().m_text, QStringLiteral("revision 19"));

  QVERIFY2(spy.count() < 20,
           qPrintable(QStringLiteral("expected coalescing, got %1 writes").arg(spy.count())));
}

// The sidecar is written with a plain QSaveFile, so vxcore emits no file event.
// Without this signal a synced notebook would never commit it.
void TestCommentService::savingEmitsStoreDirtyForANotebookOnly() {
  const QString notebookId = createRawNotebook(QStringLiteral("dirty-nb"));
  QVERIFY(!notebookId.isEmpty());
  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());
  QVERIFY(QFile(QDir(root).filePath(QStringLiteral("doc.pdf"))).open(QIODevice::WriteOnly));

  NodeIdentifier inNotebook;
  inNotebook.notebookId = notebookId;
  inNotebook.relativePath = QStringLiteral("doc.pdf");

  QSignalSpy dirtySpy(m_service, &CommentService::storeDirty);
  QSignalSpy doneSpy(m_service, &CommentService::saveFinished);

  CommentSet set;
  set.m_comments.append(makeComment(0, QStringLiteral("x")));
  m_service->scheduleSave(inNotebook, set);
  QVERIFY(doneSpy.wait(5000));
  QTest::qWait(50);
  QCOMPARE(dirtySpy.count(), 1);
  QCOMPARE(dirtySpy.at(0).at(0).toString(), notebookId);

  // An external file belongs to no notebook, so there is nothing to sync.
  dirtySpy.clear();
  doneSpy.clear();
  NodeIdentifier external;
  external.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("outside.pdf"));
  m_service->scheduleSave(external, set);
  QVERIFY(doneSpy.wait(5000));
  QTest::qWait(50);
  QCOMPARE(dirtySpy.count(), 0);
}

// ============ Sibling lifecycle ============

void TestCommentService::renamingARawFileMovesItsSidecar() {
  const QString notebookId = createRawNotebook(QStringLiteral("rename-nb"));
  QVERIFY(!notebookId.isEmpty());
  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());

  const QString oldFile = QDir(root).filePath(QStringLiteral("paper.pdf"));
  QVERIFY(QFile(oldFile).open(QIODevice::WriteOnly));
  const QString oldStore = oldFile + CommentService::siblingSuffix();
  QFile store(oldStore);
  QVERIFY(store.open(QIODevice::WriteOnly));
  store.write("{\"version\":1,\"comments\":[]}");
  store.close();

  QVERIFY(m_notebooks->renameFile(notebookId, QStringLiteral("paper.pdf"),
                                  QStringLiteral("renamed.pdf")));

  NodeIdentifier oldId;
  oldId.notebookId = notebookId;
  oldId.relativePath = QStringLiteral("paper.pdf");
  waitForIdle(oldId);

  const QString newStore =
      QDir(root).filePath(QStringLiteral("renamed.pdf")) + CommentService::siblingSuffix();
  QVERIFY2(QFile::exists(newStore), "the sidecar did not follow the rename");
  QVERIFY2(!QFile::exists(oldStore), "the old sidecar was orphaned");
}

void TestCommentService::deletingARawFileRemovesItsSidecar() {
  const QString notebookId = createRawNotebook(QStringLiteral("delete-nb"));
  QVERIFY(!notebookId.isEmpty());
  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());

  const QString file = QDir(root).filePath(QStringLiteral("gone.pdf"));
  QVERIFY(QFile(file).open(QIODevice::WriteOnly));
  const QString storePath = file + CommentService::siblingSuffix();
  QFile store(storePath);
  QVERIFY(store.open(QIODevice::WriteOnly));
  store.write("{\"version\":1,\"comments\":[]}");
  store.close();

  QVERIFY(m_notebooks->deleteFile(notebookId, QStringLiteral("gone.pdf")));

  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = QStringLiteral("gone.pdf");
  waitForIdle(nodeId);

  QVERIFY2(!QFile::exists(storePath), "the orphaned sidecar was left behind");
}

// Destroying someone's comments to satisfy a move is never acceptable: the
// mover wins the file name, the orphan stays put for manual recovery.
void TestCommentService::movingOntoAnExistingSidecarLeavesBothInPlace() {
  const QString notebookId = createRawNotebook(QStringLiteral("collide-nb"));
  QVERIFY(!notebookId.isEmpty());
  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());

  const QString sourceFile = QDir(root).filePath(QStringLiteral("a.pdf"));
  QVERIFY(QFile(sourceFile).open(QIODevice::WriteOnly));
  QFile sourceStore(sourceFile + CommentService::siblingSuffix());
  QVERIFY(sourceStore.open(QIODevice::WriteOnly));
  sourceStore.write("{\"version\":1,\"comments\":[],\"marker\":\"source\"}");
  sourceStore.close();

  const QString destStorePath =
      QDir(root).filePath(QStringLiteral("b.pdf")) + CommentService::siblingSuffix();
  QFile destStore(destStorePath);
  QVERIFY(destStore.open(QIODevice::WriteOnly));
  destStore.write("{\"version\":1,\"comments\":[],\"marker\":\"destination\"}");
  destStore.close();

  m_notebooks->renameFile(notebookId, QStringLiteral("a.pdf"), QStringLiteral("b.pdf"));

  NodeIdentifier sourceId;
  sourceId.notebookId = notebookId;
  sourceId.relativePath = QStringLiteral("a.pdf");
  waitForIdle(sourceId);

  QVERIFY2(QFile::exists(destStorePath), "the destination sidecar was destroyed");
  QFile check(destStorePath);
  QVERIFY(check.open(QIODevice::ReadOnly));
  QVERIFY2(QString::fromUtf8(check.readAll()).contains(QStringLiteral("destination")),
           "the destination sidecar was overwritten");
}

// vxcore refuses asset writes on a read-only notebook before touching disk; a
// direct QSaveFile would bypass that guard, so the service re-applies it.
void TestCommentService::readOnlyNotebookWritesAreRejectedBeforeTouchingDisk() {
  const QString notebookId = createRawNotebook(QStringLiteral("ro-nb"));
  QVERIFY(!notebookId.isEmpty());
  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());
  const QString file = QDir(root).filePath(QStringLiteral("locked.pdf"));
  QVERIFY(QFile(file).open(QIODevice::WriteOnly));

  QCOMPARE(vxcore_notebook_set_read_only(m_context, notebookId.toUtf8().constData(), true),
           VXCORE_OK);
  QVERIFY(m_notebooks->isNotebookReadOnly(notebookId));

  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = QStringLiteral("locked.pdf");

  QSignalSpy rejected(m_service, &CommentService::saveRejectedReadOnly);
  QSignalSpy finished(m_service, &CommentService::saveFinished);

  CommentSet set;
  set.m_comments.append(makeComment(0, QStringLiteral("nope")));
  m_service->scheduleSave(nodeId, set);

  // Emitted SYNCHRONOUSLY on the calling thread, before any queue insertion, so
  // the disk is never touched at all.
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(finished.count(), 0);
  QVERIFY(!m_service->isBusy(nodeId));
  QVERIFY2(!QFile::exists(file + CommentService::siblingSuffix()),
           "a read-only rejection must not have touched the disk");

  // Lifting the flag makes the same save succeed, proving the guard - not some
  // unrelated failure - was what stopped it.
  QCOMPARE(vxcore_notebook_set_read_only(m_context, notebookId.toUtf8().constData(), false),
           VXCORE_OK);
  m_service->scheduleSave(nodeId, set);
  QVERIFY(finished.wait(5000));
  QVERIFY(QFile::exists(file + CommentService::siblingSuffix()));
}

// A dispatched worker must still commit its newest snapshot, or the user's last
// edit is lost at shutdown.
void TestCommentService::shutdownDrainsPendingWrites() {
  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(m_tmp->path()).filePath(QStringLiteral("closing.pdf"));

  // A local service, so shutting it down does not affect the shared fixture.
  CommentService service(m_notebooks, m_gate, nullptr);

  CommentSet set;
  set.m_comments.append(makeComment(0, QStringLiteral("last words")));
  service.scheduleSave(nodeId, set);

  QVERIFY2(service.shutdown(5000), "shutdown did not drain");
  QVERIFY(!service.isBusy(nodeId));

  const auto loaded = m_service->load(nodeId);
  QCOMPARE(loaded.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(loaded.m_comments.m_comments.first().m_text, QStringLiteral("last words"));

  // Post-shutdown work is refused rather than silently queued forever.
  service.scheduleSave(nodeId, CommentSet());
  QVERIFY(!service.isBusy(nodeId));
}

// The race the per-file FIFO exists to remove: a rename that ran inline used to
// find no sidecar (nothing written yet) and do nothing, after which the pending
// worker recreated it under the OLD name.
void TestCommentService::lifecycleOpsAreOrderedBehindPendingSaves() {
  const QString notebookId = createRawNotebook(QStringLiteral("race-nb"));
  QVERIFY(!notebookId.isEmpty());
  const QString root = m_notebooks->buildAbsolutePath(notebookId, QString());

  const QString oldFile = QDir(root).filePath(QStringLiteral("racy.pdf"));
  QVERIFY(QFile(oldFile).open(QIODevice::WriteOnly));

  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = QStringLiteral("racy.pdf");

  CommentSet set;
  set.m_comments.append(makeComment(0, QStringLiteral("written before the rename")));

  QSignalSpy finished(m_service, &CommentService::saveFinished);

  // Schedule the write and rename IMMEDIATELY, without letting the worker run:
  // the sidecar almost certainly does not exist yet when the hook fires.
  m_service->scheduleSave(nodeId, set);
  QVERIFY(m_notebooks->renameFile(notebookId, QStringLiteral("racy.pdf"),
                                  QStringLiteral("renamed.pdf")));

  if (finished.isEmpty()) {
    QVERIFY(finished.wait(5000));
  }
  // Let the queued Move job run too.
  NodeIdentifier oldId = nodeId;
  waitForIdle(oldId);

  const QString oldStore = oldFile + CommentService::siblingSuffix();
  const QString newStore =
      QDir(root).filePath(QStringLiteral("renamed.pdf")) + CommentService::siblingSuffix();

  QVERIFY2(!QFile::exists(oldStore), "the pending write recreated the sidecar under the OLD name");
  QVERIFY2(QFile::exists(newStore), "the sidecar did not end up under the new name");

  NodeIdentifier newId;
  newId.notebookId = notebookId;
  newId.relativePath = QStringLiteral("renamed.pdf");
  const auto loaded = m_service->load(newId);
  QCOMPARE(loaded.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(loaded.m_comments.m_comments.first().m_text,
           QStringLiteral("written before the rename"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCommentService)
#include "test_commentservice.moc"
