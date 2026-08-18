#include <QtTest>

#include <vtextedit/markdownhighlighterdata.h>

#include <widgets/editors/imagelinklookup.h>

using namespace vnotex;
using vnotex::ImageLinkLookup::ImageLinkHit;

namespace tests {
// Positional lookup used by the editor's "Image" context submenu.
class TestImageLinkLookup : public QObject {
  Q_OBJECT
private slots:
  void testFindsCoveringLink();
  void testOutsideEveryRegion();
  void testCursorAtEndOfBlock();
  void testRegionSpanningBeyondBlock();
  void testEmptyLinks();
  void testHtmlImageIsFoundLikeAnyOther();
};

namespace {
vte::md::ImageLinkInfo makeLink(int p_start, int p_end, const QString &p_destination) {
  return vte::md::ImageLinkInfo(vte::md::ElementRegion(p_start, p_end), p_destination, 0, 0);
}
} // namespace

// Block text: "a ![x](p.png) b ![y](q.png)" starting at document position 100.
void TestImageLinkLookup::testFindsCoveringLink() {
  const QVector<vte::md::ImageLinkInfo> links{makeLink(102, 113, QStringLiteral("p.png")),
                                              makeLink(116, 127, QStringLiteral("q.png"))};

  int index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 102, 100, 27, &index), ImageLinkHit::Found);
  QCOMPARE(index, 0);

  index = -1;
  // Anywhere inside the region counts, including the last character.
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 112, 100, 27, &index), ImageLinkHit::Found);
  QCOMPARE(index, 0);

  index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 120, 100, 27, &index), ImageLinkHit::Found);
  QCOMPARE(index, 1);
}

void TestImageLinkLookup::testOutsideEveryRegion() {
  const QVector<vte::md::ImageLinkInfo> links{makeLink(102, 113, QStringLiteral("p.png"))};

  // Before, in the gap, and past the end -- and the index is left untouched.
  for (int pos : {100, 101, 113, 115}) {
    int index = -12345;
    QCOMPARE(ImageLinkLookup::imageLinkAt(links, pos, 100, 27, &index), ImageLinkHit::None);
    QCOMPARE(index, -12345);
  }
}

// A cursor resting exactly at the end of the block still resolves a region that
// ends there, so right-clicking after the closing `)` finds the image.
void TestImageLinkLookup::testCursorAtEndOfBlock() {
  const QVector<vte::md::ImageLinkInfo> links{makeLink(100, 111, QStringLiteral("p.png"))};

  int index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 111, 100, 11, &index), ImageLinkHit::Found);
  QCOMPARE(index, 0);

  // The same position in a longer block is past the region, so it is a miss.
  index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 111, 100, 27, &index), ImageLinkHit::None);
}

// A multiline image is reported at its true span now, so its region can run
// past the block the cursor is in. The caller cannot resolve it against a
// single block and must be told so rather than silently missing it.
void TestImageLinkLookup::testRegionSpanningBeyondBlock() {
  const QVector<vte::md::ImageLinkInfo> links{makeLink(100, 130, QStringLiteral("p.png"))};

  int index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 105, 100, 12, &index),
           ImageLinkHit::SpansBeyondBlock);
  QCOMPARE(index, 0);
}

void TestImageLinkLookup::testEmptyLinks() {
  int index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt({}, 105, 100, 12, &index), ImageLinkHit::None);
  QCOMPARE(index, -1);

  // A nullptr out-param must not crash.
  QCOMPARE(ImageLinkLookup::imageLinkAt({makeLink(100, 111, QStringLiteral("p.png"))}, 105, 100, 11,
                                        nullptr),
           ImageLinkHit::Found);
}
// The lookup is deliberately syntax-agnostic: it answers "is there an image
// under the cursor?" from regions alone. An HTML `<img>` gets the same Image
// submenu as a Markdown link, with no branch anywhere on the path. With the
// single-line rule (D8) a tag never spans blocks, so SpansBeyondBlock cannot
// occur for one.
void TestImageLinkLookup::testHtmlImageIsFoundLikeAnyOther() {
  // Block text: `x <img src="p.png" width="200"> y` at document position 100.
  const QString block = QStringLiteral("x <img src=\"p.png\" width=\"200\"> y");
  const int tagStart = block.indexOf(QLatin1Char('<'));
  const int tagEnd = block.indexOf(QLatin1Char('>')) + 1;

  vte::md::ImageLinkInfo html(vte::md::ElementRegion(100 + tagStart, 100 + tagEnd),
                              QStringLiteral("p.png"), 200, 0);
  html.m_syntax = vte::md::ImageLinkInfo::Syntax::Html;
  const QVector<vte::md::ImageLinkInfo> links{html};

  int index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 100 + tagStart, 100, block.size(), &index),
           ImageLinkHit::Found);
  QCOMPARE(index, 0);

  // Anywhere inside, including the last character.
  index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 100 + tagEnd - 1, 100, block.size(), &index),
           ImageLinkHit::Found);
  QCOMPARE(index, 0);
  QCOMPARE(links.at(index).m_syntax, vte::md::ImageLinkInfo::Syntax::Html);
  QCOMPARE(links.at(index).m_width, 200);

  // Outside it, nothing.
  index = -1;
  QCOMPARE(ImageLinkLookup::imageLinkAt(links, 100, 100, block.size(), &index), ImageLinkHit::None);
}

} // namespace tests

QTEST_APPLESS_MAIN(tests::TestImageLinkLookup)

#include "test_imagelinklookup.moc"
