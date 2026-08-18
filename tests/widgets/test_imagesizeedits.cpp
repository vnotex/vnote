// test_imagesizeedits.cpp
//
// planHtmlImageSizeEdits() is what `Image > Set Size…` applies to an existing
// HTML `<img>`. It is span arithmetic over live document text, and every defect
// in it corrupts the tag rather than merely producing a wrong size, so it is
// tested directly instead of only through the editor.
//
// The two hazards it exists to contain:
//
//   * a removal walks BACKWARDS over the whitespace separating the attribute
//     from the one before it. With absolute document positions that walk must
//     be floored, or a run of whitespace before the tag lets it eat the tag.
//   * inserting a missing dimension is a ZERO-LENGTH span, and it sits exactly
//     where an adjacent removal's widened start lands. std::sort gives no tie
//     order, so applying the insertion first would leave the removal's stored
//     end pointing past the inserted text and delete part of it.

#include <QString>
#include <QVector>
#include <QtTest>

#include <vtextedit/htmlimgscanner.h>

#include <widgets/editors/imagesizeedits.h>

namespace tests {

class TestImageSizeEdits : public QObject {
  Q_OBJECT

private slots:
  void updatesAnExistingDimension();
  void insertsAMissingDimensionAfterSrc();
  void insertsBothMissingDimensionsInCanonicalOrder();
  void removesEveryOccurrenceOfAClearedDimension();
  void keepsTheFirstAndDropsLaterDuplicatesWhenSetting();
  void insertionAndAdjacentRemovalDoNotCorruptTheTag();
  void aDimensionBeforeSrcSwallowsItsSeparator();
  void aTagAtANonZeroOffsetIsNotEatenByLeadingWhitespace();
  void preservesAttributesVNoteDidNotAuthor();
};

namespace {
// Apply a plan the way MarkdownEditor does, and return the resulting document.
QString apply(const QString &p_content, const QVector<vnotex::SpanEdit> &p_edits) {
  QString out = p_content;
  for (const auto &edit : p_edits) {
    out.replace(edit.m_start, edit.m_end - edit.m_start, edit.m_text);
  }
  return out;
}

// Plan and apply, given a document whose tag starts at @p_regionStart.
QString resize(const QString &p_content, int p_regionStart, int p_width, int p_height) {
  vte::RawTextState state;
  const auto tags = vte::scanHtmlImgTags(p_content.mid(p_regionStart), p_regionStart, &state);
  if (tags.size() != 1) {
    return QStringLiteral("<%1 tags>").arg(tags.size());
  }
  return apply(p_content, vnotex::planHtmlImageSizeEdits(p_content, p_regionStart, tags.first(),
                                                         p_width, p_height));
}
} // namespace

void TestImageSizeEdits::updatesAnExistingDimension() {
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"100\">"), 0, 500, 0),
           QStringLiteral("<img src=\"a.png\" width=\"500\">"));
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"100\" height=\"200\">"), 0, 5, 6),
           QStringLiteral("<img src=\"a.png\" width=\"5\" height=\"6\">"));
  // An unquoted value is replaced by the canonical quoted spelling.
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=100>"), 0, 7, 0),
           QStringLiteral("<img src=\"a.png\" width=\"7\">"));
}

void TestImageSizeEdits::insertsAMissingDimensionAfterSrc() {
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" alt=\"A\">"), 0, 500, 0),
           QStringLiteral("<img src=\"a.png\" width=\"500\" alt=\"A\">"));
  // Also when `src` is not first.
  QCOMPARE(resize(QStringLiteral("<img alt=\"A\" src=\"a.png\">"), 0, 0, 300),
           QStringLiteral("<img alt=\"A\" src=\"a.png\" height=\"300\">"));
}

void TestImageSizeEdits::insertsBothMissingDimensionsInCanonicalOrder() {
  // ONE coalesced insertion, so the two can never race for the same position.
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\">"), 0, 500, 300),
           QStringLiteral("<img src=\"a.png\" width=\"500\" height=\"300\">"));
}

void TestImageSizeEdits::removesEveryOccurrenceOfAClearedDimension() {
  // Unmasking only the second would leave the image silently still sized.
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"100\" width=\"200\">"), 0, 0, 0),
           QStringLiteral("<img src=\"a.png\">"));
  QCOMPARE(
      resize(QStringLiteral("<img src=\"a.png\" width=\"1\" alt=\"A\" height=\"2\">"), 0, 0, 0),
      QStringLiteral("<img src=\"a.png\" alt=\"A\">"));
  // Clearing one and keeping the other.
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"1\" height=\"2\">"), 0, 0, 9),
           QStringLiteral("<img src=\"a.png\" height=\"9\">"));
}

void TestImageSizeEdits::keepsTheFirstAndDropsLaterDuplicatesWhenSetting() {
  // First-wins is the HTML5 read rule, so the first occurrence is the one that
  // is updated; leaving a later duplicate behind would be harmless today but
  // becomes the effective value the moment the first is removed.
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"100\" width=\"200\">"), 0, 500, 0),
           QStringLiteral("<img src=\"a.png\" width=\"500\">"));
}

// The tie-order hazard: a missing width is inserted at exactly the position an
// adjacent height removal widens back to.
void TestImageSizeEdits::insertionAndAdjacentRemovalDoNotCorruptTheTag() {
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" height=\"2\">"), 0, 500, 0),
           QStringLiteral("<img src=\"a.png\" width=\"500\">"));
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"1\">"), 0, 0, 300),
           QStringLiteral("<img src=\"a.png\" height=\"300\">"));
  // And with both: insert height, remove width, in one plan.
  QCOMPARE(resize(QStringLiteral("<img src=\"a.png\" width=\"1\" width=\"2\">"), 0, 0, 300),
           QStringLiteral("<img src=\"a.png\" height=\"300\">"));
}

// A dimension BEFORE `src` cannot reach the insertion point, so it is floored
// at the tag start and still swallows its separator -- no double space is left
// behind, and the insertion after `src` is unaffected.
void TestImageSizeEdits::aDimensionBeforeSrcSwallowsItsSeparator() {
  QCOMPARE(resize(QStringLiteral("<img width=\"1\" src=\"a.png\">"), 0, 0, 0),
           QStringLiteral("<img src=\"a.png\">"));
  QCOMPARE(resize(QStringLiteral("<img width=\"1\" src=\"a.png\">"), 0, 0, 300),
           QStringLiteral("<img src=\"a.png\" height=\"300\">"));
  QCOMPARE(resize(QStringLiteral("<img width=\"1\" height=\"2\" src=\"a.png\">"), 0, 0, 0),
           QStringLiteral("<img src=\"a.png\">"));
  // The tag start itself is never eaten.
  QCOMPARE(resize(QStringLiteral("<img   width=\"1\"   src=\"a.png\">"), 0, 0, 0),
           QStringLiteral("<img   src=\"a.png\">"));
}

// The absolute-vs-relative indexing hazard: with the tag at a nonzero offset
// behind a run of whitespace, an unfloored backwards walk eats the tag itself.
void TestImageSizeEdits::aTagAtANonZeroOffsetIsNotEatenByLeadingWhitespace() {
  const QString content = QStringLiteral("          \n<img src=\"a.png\" width=\"1\">\n");
  const int regionStart = content.indexOf(QLatin1Char('<'));
  QVERIFY(regionStart > 0);

  vte::RawTextState state;
  const auto tags = vte::scanHtmlImgTags(content.mid(regionStart), regionStart, &state);
  QCOMPARE(tags.size(), 1);

  const QString result =
      apply(content, vnotex::planHtmlImageSizeEdits(content, regionStart, tags.first(), 0, 0));
  QCOMPARE(result, QStringLiteral("          \n<img src=\"a.png\">\n"));

  // Everything before the tag is untouched.
  QVERIFY2(result.startsWith(QStringLiteral("          \n")), qPrintable(result));
}

void TestImageSizeEdits::preservesAttributesVNoteDidNotAuthor() {
  const QString content = QStringLiteral("<img class=\"c\" src=\"a.png\" style=\"border:0\" "
                                         "data-id=\"7\" width=\"1\" loading=\"lazy\">");
  const QString result = resize(content, 0, 500, 300);

  for (const auto &attr : {QStringLiteral("class=\"c\""), QStringLiteral("style=\"border:0\""),
                           QStringLiteral("data-id=\"7\""), QStringLiteral("loading=\"lazy\"")}) {
    QVERIFY2(result.contains(attr), qPrintable(result));
  }
  QVERIFY2(result.contains(QStringLiteral("width=\"500\"")), qPrintable(result));
  QVERIFY2(result.contains(QStringLiteral("height=\"300\"")), qPrintable(result));

  // Still exactly one well-formed tag with the same destination.
  vte::RawTextState state;
  const auto tags = vte::scanHtmlImgTags(result, 0, &state);
  QCOMPARE(tags.size(), 1);
  QCOMPARE(tags.first().src(), QStringLiteral("a.png"));
  QCOMPARE(tags.first().width(), 500);
  QCOMPARE(tags.first().height(), 300);
}

} // namespace tests

QTEST_APPLESS_MAIN(tests::TestImageSizeEdits)
#include "test_imagesizeedits.moc"
