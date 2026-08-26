// CommentColorSwatch: the one way a CommentColor token is rendered as a chip in
// Qt chrome (the PDF tool menus, the comment dock combo, the page context menu).
//
// The centre-pixel assertions are the load-bearing ones: a silent parse failure
// paints black or transparent, which "the icon is non-null" would not catch.

#include <QtTest>

#include <QColor>
#include <QIcon>
#include <QImage>

#include <core/services/commenttypes.h>
#include <gui/utils/commentcolorswatch.h>

using namespace vnotex;

namespace tests {

namespace {

// The chip is composited over white; this is the same arithmetic the helper
// does, spelled independently so the test is not merely echoing it.
QColor overWhite(int p_r, int p_g, int p_b, double p_alpha) {
  return QColor::fromRgbF(1.0 * (1.0 - p_alpha) + (p_r / 255.0) * p_alpha,
                          1.0 * (1.0 - p_alpha) + (p_g / 255.0) * p_alpha,
                          1.0 * (1.0 - p_alpha) + (p_b / 255.0) * p_alpha);
}

QColor centrePixel(const QIcon &p_icon, int p_sizePx) {
  const auto image = p_icon.pixmap(p_sizePx, p_sizePx).toImage();
  return image.pixelColor(p_sizePx / 2, p_sizePx / 2);
}

// Two 8-bit channels may differ by one after the round trip through a pixmap.
bool nearlyEqual(const QColor &p_lhs, const QColor &p_rhs) {
  return qAbs(p_lhs.red() - p_rhs.red()) <= 1 && qAbs(p_lhs.green() - p_rhs.green()) <= 1 &&
         qAbs(p_lhs.blue() - p_rhs.blue()) <= 1 && qAbs(p_lhs.alpha() - p_rhs.alpha()) <= 1;
}

constexpr int c_size = 16;

} // namespace

class TestCommentColorSwatch : public QObject {
  Q_OBJECT

private slots:
  void parsesEveryDeclaredForm_data();
  void parsesEveryDeclaredForm();

  void rejectsMalformedColours_data();
  void rejectsMalformedColours();

  void everyTokenYieldsASizedIcon();

  void anInvalidTokenFallsBackToTheDefaultToken();

  void theCentrePixelIsTheColourOverWhite();

  void differentTokensProduceDifferentPixels();

  void anUnparseableResolvedColourFallsBackToThatTokensBuiltIn();

  void aDefaultResolverEqualsTheUnthemedOverload();

  void aThemedResolverWins();
};

void TestCommentColorSwatch::parsesEveryDeclaredForm_data() {
  QTest::addColumn<QString>("css");
  QTest::addColumn<QColor>("expected");

  QTest::newRow("#RRGGBB") << QStringLiteral("#204080") << QColor(0x20, 0x40, 0x80);
  QTest::newRow("#AARRGGBB") << QStringLiteral("#80204080") << QColor(0x20, 0x40, 0x80, 0x80);
  QTest::newRow("rgb") << QStringLiteral("rgb(1, 2, 3)") << QColor(1, 2, 3);
  QTest::newRow("rgb no spaces") << QStringLiteral("rgb(1,2,3)") << QColor(1, 2, 3);
  QTest::newRow("rgb lower bound") << QStringLiteral("rgb(0, 0, 0)") << QColor(0, 0, 0);
  QTest::newRow("rgb upper bound") << QStringLiteral("rgb(255, 255, 255)") << QColor(255, 255, 255);
  QTest::newRow("rgba alpha 1") << QStringLiteral("rgba(10, 20, 30, 1.0)")
                                << QColor(10, 20, 30, 255);
  QTest::newRow("rgba alpha 0") << QStringLiteral("rgba(10, 20, 30, 0.0)") << QColor(10, 20, 30, 0);
  QTest::newRow("rgba fractional")
      << QStringLiteral("rgba(255, 214, 0, 0.38)") << QColor::fromRgbF(1.0, 214 / 255.0, 0.0, 0.38);
  QTest::newRow("leading/trailing space") << QStringLiteral("  rgb(1, 2, 3)  ") << QColor(1, 2, 3);
}

void TestCommentColorSwatch::parsesEveryDeclaredForm() {
  QFETCH(QString, css);
  QFETCH(QColor, expected);

  const auto parsed = CommentColorSwatch::parseCssColor(css);
  QVERIFY2(parsed.isValid(), qPrintable(css));
  QVERIFY2(nearlyEqual(parsed, expected), qPrintable(parsed.name(QColor::HexArgb)));
}

void TestCommentColorSwatch::rejectsMalformedColours_data() {
  QTest::addColumn<QString>("css");

  QTest::newRow("empty") << QString();
  QTest::newRow("whitespace") << QStringLiteral("   ");
  QTest::newRow("component out of range") << QStringLiteral("rgb(300, 0, 0)");
  QTest::newRow("missing component") << QStringLiteral("rgb(1, 2)");
  QTest::newRow("trailing garbage") << QStringLiteral("rgb(1, 2, 3) drop table");
  QTest::newRow("leading garbage") << QStringLiteral("x rgb(1, 2, 3)");
  QTest::newRow("alpha out of range") << QStringLiteral("rgba(1, 2, 3, 2.0)");
  QTest::newRow("rgb with alpha") << QStringLiteral("rgb(1, 2, 3, 0.5)");
  QTest::newRow("rgba without alpha") << QStringLiteral("rgba(1, 2, 3)");
  QTest::newRow("unresolved theme token") << QStringLiteral("@palette#fg3_5");
  QTest::newRow("bare word") << QStringLiteral("chartreuse-ish");
  QTest::newRow("bad hex") << QStringLiteral("#zzz");
}

void TestCommentColorSwatch::rejectsMalformedColours() {
  QFETCH(QString, css);
  QVERIFY2(!CommentColorSwatch::parseCssColor(css).isValid(), qPrintable(css));
}

void TestCommentColorSwatch::everyTokenYieldsASizedIcon() {
  for (const auto &token : CommentColor::all()) {
    const auto icon = CommentColorSwatch::icon(token, c_size);
    QVERIFY2(!icon.isNull(), qPrintable(token));
    QCOMPARE(icon.pixmap(c_size, c_size).size(), QSize(c_size, c_size));
    // Every token must have a built-in, or the chip silently falls back.
    QVERIFY2(!CommentColorSwatch::builtInColor(token).isEmpty(), qPrintable(token));
  }
}

void TestCommentColorSwatch::anInvalidTokenFallsBackToTheDefaultToken() {
  const auto bogus = CommentColorSwatch::icon(QStringLiteral("chartreuse"), c_size);
  QVERIFY(!bogus.isNull());
  QCOMPARE(centrePixel(bogus, c_size),
           centrePixel(CommentColorSwatch::icon(CommentColor::defaultToken(), c_size), c_size));
}

void TestCommentColorSwatch::theCentrePixelIsTheColourOverWhite() {
  // yellow is rgba(255, 214, 0, 0.38).
  const auto icon = CommentColorSwatch::icon(QStringLiteral("yellow"), c_size);
  const auto actual = centrePixel(icon, c_size);
  const auto expected = overWhite(255, 214, 0, 0.38);
  QVERIFY2(nearlyEqual(actual, expected), qPrintable(actual.name(QColor::HexArgb)));
  // Specifically NOT transparent and NOT black, which is what a silent parse
  // failure would paint.
  QCOMPARE(actual.alpha(), 255);
  QVERIFY(actual != QColor(Qt::black));
}

void TestCommentColorSwatch::differentTokensProduceDifferentPixels() {
  const auto yellow =
      centrePixel(CommentColorSwatch::icon(QStringLiteral("yellow"), c_size), c_size);
  const auto blue = centrePixel(CommentColorSwatch::icon(QStringLiteral("blue"), c_size), c_size);
  QVERIFY(yellow != blue);
}

void TestCommentColorSwatch::anUnparseableResolvedColourFallsBackToThatTokensBuiltIn() {
  // ThemeService only checks that an override is non-empty and '@'-free, so a
  // theme can still hand back garbage.
  CommentColorSwatch::ColorResolver broken = [](const QString &) {
    return QStringLiteral("not-a-colour");
  };

  const auto actual =
      centrePixel(CommentColorSwatch::icon(broken, QStringLiteral("blue"), c_size), c_size);
  // BLUE's built-in, not the DEFAULT token's.
  QCOMPARE(actual, centrePixel(CommentColorSwatch::icon(QStringLiteral("blue"), c_size), c_size));
  QVERIFY(actual !=
          centrePixel(CommentColorSwatch::icon(CommentColor::defaultToken(), c_size), c_size));
}

void TestCommentColorSwatch::aDefaultResolverEqualsTheUnthemedOverload() {
  for (const auto &token : CommentColor::all()) {
    QCOMPARE(
        centrePixel(CommentColorSwatch::icon(CommentColorSwatch::ColorResolver(), token, c_size),
                    c_size),
        centrePixel(CommentColorSwatch::icon(token, c_size), c_size));
  }
}

void TestCommentColorSwatch::aThemedResolverWins() {
  CommentColorSwatch::ColorResolver themed = [](const QString &) {
    return QStringLiteral("rgb(0, 0, 255)");
  };

  const auto actual =
      centrePixel(CommentColorSwatch::icon(themed, QStringLiteral("yellow"), c_size), c_size);
  QVERIFY(nearlyEqual(actual, QColor(0, 0, 255)));
}

} // namespace tests

QTEST_MAIN(tests::TestCommentColorSwatch)
#include "test_commentcolorswatch.moc"
