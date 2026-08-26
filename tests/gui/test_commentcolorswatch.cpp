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
  // Sampled from the IMAGE's own centre, not p_sizePx/2: the pixmap is
  // device-pixel-ratio aware, so on a scaled display it is larger than
  // p_sizePx.
  const auto image = p_icon.pixmap(p_sizePx, p_sizePx).toImage();
  return image.pixelColor(image.width() / 2, image.height() / 2);
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

  void theCheckedStateIsTickedOnTheChip();

  void theTickContrastsWithADarkFill();
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
    QCOMPARE(icon.pixmap(c_size, c_size).deviceIndependentSize().toSize(), QSize(c_size, c_size));
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

// A checkable QAction that carries an icon gets NO checkmark and NO radio dot --
// the icon takes over the indicator column -- so every theme marks "checked"
// with `QMenu::icon:checked { border: 2px solid ... }`. Qt draws that around the
// icon SUB-CONTROL rect, and at fractional device pixel ratios it clips into a
// PARTIAL box (top and bottom edges only at 1.5), which reads as a rendering
// fault rather than a selection cue.
//
// So the selected marker is painted into the pixmap instead, where nothing can
// clip it. Qt picks QIcon::On for a checked action, so this costs the caller
// nothing beyond suppressing the theme rule.
void TestCommentColorSwatch::theCheckedStateIsTickedOnTheChip() {
  const auto icon = CommentColorSwatch::icon(QStringLiteral("yellow"), c_size);

  const auto off = icon.pixmap(c_size, c_size, QIcon::Normal, QIcon::Off).toImage();
  const auto on = icon.pixmap(c_size, c_size, QIcon::Normal, QIcon::On).toImage();

  // Both states exist and are genuinely different artwork.
  QVERIFY(!off.isNull());
  QVERIFY(!on.isNull());
  QVERIFY(off != on);
  QCOMPARE(on.size(), off.size());

  const auto fill = off.pixelColor(off.width() / 2, off.height() / 2);

  // BOTH states keep the chip border: nothing else delimits a pale swatch
  // against a pale menu, and the theme ring is no longer available to do it.
  QVERIFY2(!nearlyEqual(off.pixelColor(0, 0), fill), "Off state must keep its chip border");
  QVERIFY2(!nearlyEqual(on.pixelColor(0, 0), fill), "On state must keep its chip border too");

  // The fill is preserved away from the tick, so a checked row does not appear
  // to change colour.
  const int sx = qRound(off.width() * 0.2);
  QVERIFY(nearlyEqual(on.pixelColor(sx, sx), off.pixelColor(sx, sx)));

  // The tick itself: On has strongly contrasting pixels that Off does not.
  const auto contrastingPixels = [&fill](const QImage &p_img) {
    int n = 0;
    for (int y = 0; y < p_img.height(); ++y) {
      for (int x = 0; x < p_img.width(); ++x) {
        if (qAbs(p_img.pixelColor(x, y).lightnessF() - fill.lightnessF()) > 0.35) {
          ++n;
        }
      }
    }
    return n;
  };
  QVERIFY2(contrastingPixels(on) > contrastingPixels(off) + c_size / 2,
           "On state must carry a visible tick");

  // pixmap() with no state argument means Off, which is what the comment dock's
  // combo and every non-menu caller gets.
  QCOMPARE(icon.pixmap(c_size, c_size).toImage(), off);

  // Holds for every token, not just yellow.
  for (const auto &token : CommentColor::all()) {
    const auto each = CommentColorSwatch::icon(token, c_size);
    QVERIFY2(each.pixmap(c_size, c_size, QIcon::Normal, QIcon::On).toImage() !=
                 each.pixmap(c_size, c_size, QIcon::Normal, QIcon::Off).toImage(),
             qPrintable(token));
  }
}

// The tick must stay visible if a theme overrides a token with an opaque dark
// colour, so its colour is computed from the fill rather than assumed dark.
void TestCommentColorSwatch::theTickContrastsWithADarkFill() {
  CommentColorSwatch::ColorResolver dark = [](const QString &) {
    return QStringLiteral("rgb(0, 0, 0)");
  };
  const auto on = CommentColorSwatch::icon(dark, QStringLiteral("yellow"), c_size)
                      .pixmap(c_size, c_size, QIcon::Normal, QIcon::On)
                      .toImage();

  bool light = false;
  for (int y = 0; y < on.height() && !light; ++y) {
    for (int x = 0; x < on.width(); ++x) {
      if (on.pixelColor(x, y).lightnessF() > 0.7) {
        light = true;
        break;
      }
    }
  }
  QVERIFY2(light, "A dark fill must get a light tick");
}

} // namespace tests

QTEST_MAIN(tests::TestCommentColorSwatch)
#include "test_commentcolorswatch.moc"
