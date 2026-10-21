#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <utils/markdownhtmlrenderer.h>

using namespace vnotex;

namespace tests {

class TestMarkdownHtmlRenderer : public QObject {
  Q_OBJECT
private slots:
  void directAndReusedAgree();
  void exactSourceAndProfileRequired();
  void outputOnlyOptionsReuseDocument();
  void headingOnlyDoesNotRenderHtml();
  void emptyDocumentIsSuccessful();
  void deepHeadingKeepsTextWithoutStackGrowth();
  void headingIdsReserveGeneratedSuffixes();
  void relocatedFootnoteHeadingsUseRenderOrder();
  void unusedFootnoteHeadingsDoNotReserveIds_data();
  void unusedFootnoteHeadingsDoNotReserveIds();
  void sourceLinesAndViewerMarkers();
  void nativeLinksShareUrlPolicy();
  void nativeFeatureCorpus_data();
  void nativeFeatureCorpus();
};

void TestMarkdownHtmlRenderer::directAndReusedAgree() {
  const auto source =
      QByteArrayLiteral("# 1. Native *heading*\n\n# [[note|Wiki]]\n\n[TOC]\n\n"
                        "- [x] Done\n\n| A | B |\n| - | - |\n| one | two |\n\n"
                        "A ~~deleted~~ ==marked== H~2~O x^2^ and https://example.org.\n\n"
                        "```cpp\nint value = 1;\n```\n\n$E=mc^2$\n");
  const MarkdownHtmlOptions options;
  const auto document =
      vte::MarkdownDocument::parse(source, MarkdownHtmlRenderer::parseOptions(options));
  QVERIFY(document);
  const auto reused = MarkdownHtmlRenderer::render(source, options, document);
  const auto direct = MarkdownHtmlRenderer::render(source, options);
  QVERIFY2(reused.error.isEmpty(), qPrintable(reused.error));
  QVERIFY2(direct.error.isEmpty(), qPrintable(direct.error));
  QCOMPARE(reused.document.data(), document.data());
  QVERIFY(direct.document.data() != document.data());
  QCOMPARE(reused.html, direct.html);
  QCOMPARE(reused.headings.size(), direct.headings.size());
  for (int i = 0; i < direct.headings.size(); ++i) {
    QCOMPARE(reused.headings[i].line, direct.headings[i].line);
    QCOMPARE(reused.headings[i].level, direct.headings[i].level);
    QCOMPARE(reused.headings[i].title, direct.headings[i].title);
    QCOMPARE(reused.headings[i].anchor, direct.headings[i].anchor);
  }
}

void TestMarkdownHtmlRenderer::exactSourceAndProfileRequired() {
  const auto source = QByteArrayLiteral("# Before\n\n<b>raw</b> https://example.org\n");
  MarkdownHtmlOptions options;
  const auto original = MarkdownHtmlRenderer::render(source, options);
  QVERIFY(original.error.isEmpty());
  QVERIFY(original.document);

  const auto replacement = MarkdownHtmlRenderer::render(
      QByteArrayLiteral("# After!\n\n<b>raw</b> https://example.org\n"), options,
      original.document);
  QVERIFY(replacement.error.isEmpty());
  QVERIFY(replacement.document.data() != original.document.data());
  QCOMPARE(replacement.headings.first().anchor, QStringLiteral("after"));

  options.htmlEnabled = false;
  const auto escaped = MarkdownHtmlRenderer::render(source, options, original.document);
  QVERIFY(escaped.error.isEmpty());
  QVERIFY(escaped.document.data() != original.document.data());
  QVERIFY(escaped.html.contains(QStringLiteral("&lt;b&gt;raw&lt;/b&gt;")));
  QVERIFY(!escaped.html.contains(QStringLiteral("<b>raw</b>")));

  options = {};
  options.linkify = false;
  const auto literal = MarkdownHtmlRenderer::render(source, options, original.document);
  QVERIFY(literal.error.isEmpty());
  QVERIFY(literal.document.data() != original.document.data());
  QVERIFY(!literal.html.contains(QStringLiteral("href=\"https://example.org\"")));
  QVERIFY(literal.html.contains(QStringLiteral("https://example.org")));
}

void TestMarkdownHtmlRenderer::outputOnlyOptionsReuseDocument() {
  const auto source = QByteArrayLiteral("first line\nsecond line\n");
  MarkdownHtmlOptions options;
  const auto original = MarkdownHtmlRenderer::render(source, options);
  QVERIFY(original.error.isEmpty());
  options.autoBreaks = true;
  const auto breaks = MarkdownHtmlRenderer::render(source, options, original.document);
  QVERIFY(breaks.error.isEmpty());
  QCOMPARE(breaks.document.data(), original.document.data());
  QVERIFY(breaks.html.contains(QStringLiteral("<br")));
  QVERIFY(!original.html.contains(QStringLiteral("<br")));
}

void TestMarkdownHtmlRenderer::headingOnlyDoesNotRenderHtml() {
  const auto source = QByteArrayLiteral("# Label\n\n```mermaid\ngraph LR\n```\n");
  const auto result = MarkdownHtmlRenderer::render(source, {}, {}, false);
  QVERIFY(result.error.isEmpty());
  QVERIFY(result.document);
  QVERIFY(result.html.isNull());
  QCOMPARE(result.headings.size(), 1);
  QCOMPARE(result.headings.first().anchor, QStringLiteral("label"));
  const auto full = MarkdownHtmlRenderer::render(source, {}, result.document);
  QVERIFY(full.error.isEmpty());
  QVERIFY(!full.html.isEmpty());
  QCOMPARE(full.document.data(), result.document.data());
  QCOMPARE(full.headings.first().anchor, result.headings.first().anchor);
}

void TestMarkdownHtmlRenderer::deepHeadingKeepsTextWithoutStackGrowth() {
  const QByteArray emphasis(50000, '*');
  const QByteArray source = QByteArrayLiteral("# ") + emphasis + 'x' + emphasis + '\n';
  const auto result = MarkdownHtmlRenderer::render(source, {});
  QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
  QCOMPARE(result.headings.size(), 1);
  QCOMPARE(result.headings.first().title, QStringLiteral("x"));
  QCOMPARE(result.headings.first().anchor, QStringLiteral("x"));
  QVERIFY(result.html.contains(QStringLiteral("x</strong>")));
}

void TestMarkdownHtmlRenderer::emptyDocumentIsSuccessful() {
  const auto result = MarkdownHtmlRenderer::render({}, {});
  QVERIFY(result.error.isEmpty());
  QVERIFY(result.document);
  QVERIFY(result.headings.isEmpty());
  QVERIFY(result.html.isEmpty());
  QVERIFY(!result.html.isNull());
}

void TestMarkdownHtmlRenderer::headingIdsReserveGeneratedSuffixes() {
  const auto result = MarkdownHtmlRenderer::render(QByteArrayLiteral("# a\n# a\n# a-1\n# a\n"), {});
  QVERIFY(result.error.isEmpty());
  const QStringList expected{QStringLiteral("a"), QStringLiteral("a-1"), QStringLiteral("a-1-1"),
                             QStringLiteral("a-2")};
  QCOMPARE(result.headings.size(), expected.size());
  for (int i = 0; i < expected.size(); ++i) {
    QCOMPARE(result.headings[i].anchor, expected[i]);
    QVERIFY(result.html.contains(QStringLiteral("id=\"") + expected[i] + QLatin1Char('"')));
  }
}

void TestMarkdownHtmlRenderer::relocatedFootnoteHeadingsUseRenderOrder() {
  const auto source = QByteArrayLiteral("[^a]: # Same\n\n# Same\n\nUse[^a]\n");
  const auto direct = MarkdownHtmlRenderer::render(source, {});
  QVERIFY2(direct.error.isEmpty(), qPrintable(direct.error));
  QVERIFY(direct.document);
  QCOMPARE(direct.headings.size(), 2);
  QCOMPARE(direct.headings[0].line, 0);
  QCOMPARE(direct.headings[0].anchor, QStringLiteral("same-1"));
  QCOMPARE(direct.headings[1].line, 2);
  QCOMPARE(direct.headings[1].anchor, QStringLiteral("same"));

  // Both the viewer and editor-side helper use the same IDs without reordering the outline.
  const auto editorHeadings = MarkdownHtmlRenderer::buildHeadings(direct.document->headings());
  QCOMPARE(editorHeadings.size(), direct.headings.size());
  const auto reused = MarkdownHtmlRenderer::render(source, {}, direct.document);
  QVERIFY(reused.error.isEmpty());
  QCOMPARE(reused.document.data(), direct.document.data());
  QCOMPARE(reused.html, direct.html);
  const auto headingOnly = MarkdownHtmlRenderer::render(source, {}, direct.document, false);
  QVERIFY(headingOnly.error.isEmpty());
  QVERIFY(headingOnly.html.isNull());
  for (int i = 0; i < direct.headings.size(); ++i) {
    QCOMPARE(editorHeadings[i].line, direct.headings[i].line);
    QCOMPARE(editorHeadings[i].anchor, direct.headings[i].anchor);
    QCOMPARE(headingOnly.headings[i].anchor, direct.headings[i].anchor);
  }

  const QRegularExpression headingTag(QStringLiteral("<h1\\b[^>]*>"));
  auto tags = headingTag.globalMatch(direct.html);
  QVERIFY(tags.hasNext());
  const auto mainTag = tags.next().captured();
  QVERIFY(mainTag.contains(QStringLiteral("id=\"same\"")));
  QVERIFY(mainTag.contains(QStringLiteral("data-source-line=\"2\"")));
  QVERIFY(tags.hasNext());
  const auto footnoteTag = tags.next().captured();
  QVERIFY(footnoteTag.contains(QStringLiteral("id=\"same-1\"")));
  QVERIFY(footnoteTag.contains(QStringLiteral("data-source-line=\"0\"")));
  QVERIFY(!tags.hasNext());
}

void TestMarkdownHtmlRenderer::unusedFootnoteHeadingsDoNotReserveIds_data() {
  QTest::addColumn<QByteArray>("source");
  QTest::addColumn<QVector<int>>("lines");
  QTest::addColumn<QStringList>("anchors");
  QTest::newRow("hidden-before-body") << QByteArrayLiteral("[^unused]: # Same\n\n# Same\n")
                                      << QVector<int>{2} << QStringList{QStringLiteral("same")};
  QTest::newRow("hidden-between-body-headings")
      << QByteArrayLiteral("# Same\n\n[^unused]: # Same\n\n# Same\n") << QVector<int>{0, 4}
      << QStringList{QStringLiteral("same"), QStringLiteral("same-1")};
}

void TestMarkdownHtmlRenderer::unusedFootnoteHeadingsDoNotReserveIds() {
  QFETCH(QByteArray, source);
  QFETCH(QVector<int>, lines);
  QFETCH(QStringList, anchors);
  const auto direct = MarkdownHtmlRenderer::render(source, {});
  QVERIFY2(direct.error.isEmpty(), qPrintable(direct.error));
  QVERIFY(direct.document);
  QCOMPARE(direct.headings.size(), lines.size());
  const auto &sourceHeadings = direct.document->headings();
  QCOMPARE(sourceHeadings.size(), lines.size() + 1);
  const auto editorHeadings = MarkdownHtmlRenderer::buildHeadings(sourceHeadings);
  QCOMPARE(editorHeadings.size(), sourceHeadings.size());
  for (int i = 0; i < sourceHeadings.size(); ++i)
    if (sourceHeadings[i].m_renderOrder < 0)
      QVERIFY(editorHeadings[i].anchor.isEmpty());

  const auto reused = MarkdownHtmlRenderer::render(source, {}, direct.document);
  QVERIFY(reused.error.isEmpty());
  QCOMPARE(reused.html, direct.html);
  const auto headingOnly = MarkdownHtmlRenderer::render(source, {}, direct.document, false);
  QVERIFY(headingOnly.error.isEmpty());
  QVERIFY(headingOnly.html.isNull());
  QCOMPARE(headingOnly.headings.size(), lines.size());
  const QRegularExpression headingTag(QStringLiteral("<h1\\b[^>]*>"));
  auto tags = headingTag.globalMatch(direct.html);
  for (int i = 0; i < lines.size(); ++i) {
    QCOMPARE(direct.headings[i].line, lines[i]);
    QCOMPARE(direct.headings[i].anchor, anchors[i]);
    QCOMPARE(headingOnly.headings[i].line, lines[i]);
    QCOMPARE(headingOnly.headings[i].anchor, anchors[i]);
    QVERIFY(tags.hasNext());
    const auto tag = tags.next().captured();
    QVERIFY(tag.contains(QStringLiteral("id=\"") + anchors[i] + QLatin1Char('"')));
    QVERIFY(tag.contains(QStringLiteral("data-source-line=\"") + QString::number(lines[i]) +
                         QLatin1Char('"')));
  }
  QVERIFY(!tags.hasNext());
}

void TestMarkdownHtmlRenderer::sourceLinesAndViewerMarkers() {
  const auto source =
      QByteArrayLiteral("# Heading\r\n\r\n- [x] Done\r\n\r\n```cpp\r\nint x;\r\n```\r\n");
  const auto result = MarkdownHtmlRenderer::render(source, {});
  QVERIFY(result.error.isEmpty());
  QCOMPARE(result.headings.size(), 1);
  QCOMPARE(result.headings.first().line, 0);
  QCOMPARE(result.headings.first().level, 1);
  QVERIFY(result.html.contains(QStringLiteral("vx-header-anchor")));
  QVERIFY(result.html.contains(QStringLiteral("data-source-line=\"0\"")));
  QVERIFY(result.html.contains(QStringLiteral("task-list-item-checkbox")));
  QVERIFY(result.html.contains(QStringLiteral("checked")));
  QVERIFY(result.html.contains(QStringLiteral("lang-cpp")));
}

void TestMarkdownHtmlRenderer::nativeLinksShareUrlPolicy() {
  const auto source =
      QByteArrayLiteral("[[notes/target.md|native]] [ordinary](notes/target.md)\n\n"
                        "[[javascript:alert(1)|unsafe-wiki]] [unsafe-link](javascript:alert(1))\n\n"
                        "[[file:///tmp/note.md|file-wiki]] [file-link](file:///tmp/note.md)\n");
  const auto result = MarkdownHtmlRenderer::render(source, {});
  QVERIFY(result.error.isEmpty());
  QCOMPARE(result.html.count(QStringLiteral("href=\"notes/target.md\"")), 2);
  QCOMPARE(result.html.count(QStringLiteral("href=\"file:///tmp/note.md\"")), 2);
  QVERIFY(!result.html.contains(QStringLiteral("href=\"javascript:")));
  QVERIFY(result.html.contains(QStringLiteral("unsafe-wiki")));
  QVERIFY(result.html.contains(QStringLiteral("unsafe-link")));
}

void TestMarkdownHtmlRenderer::nativeFeatureCorpus_data() {
  QTest::addColumn<QJsonObject>("testCase");
  QFile fixture(QString::fromUtf8(VNOTE_TEST_DATA_DIR) +
                QStringLiteral("/native_markdown_render_cases.json"));
  QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(fixture.readAll(), &error);
  QCOMPARE(error.error, QJsonParseError::NoError);
  QVERIFY(document.isObject());
  const auto cases = document.object().value(QStringLiteral("cases")).toArray();
  QVERIFY(!cases.isEmpty());
  for (const auto &entry : cases) {
    QVERIFY(entry.isObject());
    const auto testCase = entry.toObject();
    const auto name = testCase.value(QStringLiteral("name")).toString().toUtf8();
    QVERIFY(!name.isEmpty());
    QVERIFY(testCase.value(QStringLiteral("source")).isString());
    QVERIFY(testCase.value(QStringLiteral("contains")).isArray());
    QVERIFY(testCase.value(QStringLiteral("excludes")).isArray());
    QTest::newRow(name.constData()) << testCase;
  }
}

void TestMarkdownHtmlRenderer::nativeFeatureCorpus() {
  QFETCH(QJsonObject, testCase);
  MarkdownHtmlOptions options;
  const auto configured = testCase.value(QStringLiteral("options")).toObject();
  options.htmlEnabled = configured.value(QStringLiteral("htmlEnabled")).toBool(options.htmlEnabled);
  options.linkify = configured.value(QStringLiteral("linkify")).toBool(options.linkify);
  options.autoBreaks = configured.value(QStringLiteral("autoBreaks")).toBool(options.autoBreaks);
  const auto source = testCase.value(QStringLiteral("source")).toString().toUtf8();
  const auto direct = MarkdownHtmlRenderer::render(source, options);
  QVERIFY2(direct.error.isEmpty(), qPrintable(direct.error));
  QVERIFY(direct.document);
  const auto reused = MarkdownHtmlRenderer::render(source, options, direct.document);
  QVERIFY2(reused.error.isEmpty(), qPrintable(reused.error));
  QCOMPARE(reused.document.data(), direct.document.data());
  QCOMPARE(reused.html, direct.html);
  for (const auto &fragment : testCase.value(QStringLiteral("contains")).toArray()) {
    QVERIFY(fragment.isString());
    QVERIFY2(direct.html.contains(fragment.toString()),
             qPrintable(QStringLiteral("Missing HTML fragment: ") + fragment.toString() +
                        QStringLiteral("\nHTML: ") + direct.html));
  }
  for (const auto &fragment : testCase.value(QStringLiteral("excludes")).toArray()) {
    QVERIFY(fragment.isString());
    QVERIFY2(!direct.html.contains(fragment.toString()),
             qPrintable(QStringLiteral("Forbidden HTML fragment: ") + fragment.toString()));
  }
  const auto counts = testCase.value(QStringLiteral("counts")).toObject();
  for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
    QCOMPARE(direct.html.count(it.key()), it.value().toInt());
  }
  if (testCase.contains(QStringLiteral("headings"))) {
    QVERIFY(testCase.value(QStringLiteral("headings")).isArray());
    const auto expected = testCase.value(QStringLiteral("headings")).toArray();
    QCOMPARE(direct.headings.size(), expected.size());
    QCOMPARE(reused.headings.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
      const auto heading = expected[i].toObject();
      QCOMPARE(direct.headings[i].line, heading.value(QStringLiteral("line")).toInt(-1));
      QCOMPARE(direct.headings[i].anchor, heading.value(QStringLiteral("anchor")).toString());
      QCOMPARE(reused.headings[i].line, direct.headings[i].line);
      QCOMPARE(reused.headings[i].anchor, direct.headings[i].anchor);
    }
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownHtmlRenderer)
#include "test_markdownhtmlrenderer.moc"
