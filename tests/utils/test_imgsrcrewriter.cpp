// test_imgsrcrewriter.cpp
//
// WebViewExporter rewrites the `src` of every `<img>` in the RENDERED html --
// once to embed a resource as a data URI, once to point at a copied resource
// folder. Both passes go through rewriteRenderedImgSrc().
//
// What is being pinned here is that every OTHER attribute survives. VNote now
// emits `<img … width="500" height="300" />` for a sized image, so a rewrite
// that regenerated the tag from `src` alone would silently drop the size from
// every export -- visible only by opening the exported file and finding the
// image at its natural size.
//
// This operates on rendered html, never on note source; note source has exactly
// one `<img>` parser, vte::scanHtmlImgTags().

#include <QString>
#include <QtTest>

#include <export/imgsrcrewriter.h>

namespace tests {

class TestImgSrcRewriter : public QObject {
  Q_OBJECT

private slots:
  void sizeSurvivesARewrite();
  void everyAttributeSurvivesARewrite();
  void dataUrisAreLeftAlone();
  void anUnresolvableSrcIsLeftAlone();
  void multipleImagesAreAllRewritten();
};

namespace {
// Stands in for WebUtils::toDataUri() / copyResource().
auto constant(const QString &p_value) {
  return [p_value](const QString &) { return p_value; };
}
} // namespace

void TestImgSrcRewriter::sizeSurvivesARewrite() {
  QString html = QStringLiteral("<img src=\"a.png\" width=\"500\" height=\"300\">");
  QVERIFY(
      vnotex::rewriteRenderedImgSrc(html, QLatin1Char('"'), constant(QStringLiteral("./r/a.png"))));
  QCOMPARE(html, QStringLiteral("<img src=\"./r/a.png\" width=\"500\" height=\"300\">"));

  // And with the size BEFORE src, which is where the exporter's first capture
  // group has to carry it.
  QString before = QStringLiteral("<img width=\"500\" height=\"300\" src=\"a.png\">");
  QVERIFY(vnotex::rewriteRenderedImgSrc(before, QLatin1Char('\''),
                                        constant(QStringLiteral("data:image/png;base64,AAA"))));
  QVERIFY2(before.contains(QStringLiteral("width=\"500\"")), qPrintable(before));
  QVERIFY2(before.contains(QStringLiteral("height=\"300\"")), qPrintable(before));
  QVERIFY2(before.contains(QStringLiteral("src='data:image/png;base64,AAA'")), qPrintable(before));
}

void TestImgSrcRewriter::everyAttributeSurvivesARewrite() {
  QString html = QStringLiteral(
      "<img class=\"c\" src=\"a.png\" alt=\"A\" title=\"T\" width=\"500\" loading=\"lazy\">");
  QVERIFY(vnotex::rewriteRenderedImgSrc(html, QLatin1Char('"'), constant(QStringLiteral("b.png"))));
  for (const auto &attr :
       {QStringLiteral("class=\"c\""), QStringLiteral("alt=\"A\""), QStringLiteral("title=\"T\""),
        QStringLiteral("width=\"500\""), QStringLiteral("loading=\"lazy\"")}) {
    QVERIFY2(html.contains(attr), qPrintable(html));
  }
  QVERIFY2(html.contains(QStringLiteral("src=\"b.png\"")), qPrintable(html));
  QVERIFY2(!html.contains(QStringLiteral("a.png")), qPrintable(html));
}

void TestImgSrcRewriter::dataUrisAreLeftAlone() {
  const QString original = QStringLiteral("<img src=\"data:image/png;base64,AAA\" width=\"10\">");
  QString html = original;
  QVERIFY(
      !vnotex::rewriteRenderedImgSrc(html, QLatin1Char('"'), constant(QStringLiteral("x.png"))));
  QCOMPARE(html, original);
}

void TestImgSrcRewriter::anUnresolvableSrcIsLeftAlone() {
  const QString original = QStringLiteral("<img src=\"a.png\" width=\"10\">");
  QString html = original;
  // An empty replacement means "could not resolve"; the tag must be untouched
  // rather than emptied.
  QVERIFY(!vnotex::rewriteRenderedImgSrc(html, QLatin1Char('"'), constant(QString())));
  QCOMPARE(html, original);
}

void TestImgSrcRewriter::multipleImagesAreAllRewritten() {
  QString html = QStringLiteral("<p><img src=\"a.png\" width=\"1\"> and "
                                "<img src=\"b.png\" height=\"2\"></p>");
  int calls = 0;
  QVERIFY(vnotex::rewriteRenderedImgSrc(html, QLatin1Char('"'), [&calls](const QString &p_src) {
    ++calls;
    return QStringLiteral("./r/") + p_src;
  }));
  QCOMPARE(calls, 2);
  QCOMPARE(html, QStringLiteral("<p><img src=\"./r/a.png\" width=\"1\"> and "
                                "<img src=\"./r/b.png\" height=\"2\"></p>"));
}

} // namespace tests

QTEST_APPLESS_MAIN(tests::TestImgSrcRewriter)
#include "test_imgsrcrewriter.moc"
