// GUI-free export option persistence tests, including stable target format values,
// header/footer fields (issue #2524), equality and backward-compatible defaults.
#include <QtTest>

#include <QJsonObject>
#include <QPageLayout>

#include <export/exportdata.h>

namespace tests {

class TestExportData : public QObject {
  Q_OBJECT

private slots:
  void testDefaultFooterRight();
  void testRoundTrip();
  void testFromEmptyKeepsDefaults();
  void testOldConfigKeepsFooterDefault();
  void testEquality();
  void testExportOptionStyleEquality();
  void testExportOptionStyleRoundTrip();
  void testExportFormatPersistence_data();
  void testExportFormatPersistence();
  void testUnknownExportFormat_data();
  void testUnknownExportFormat();
  void testEmptyExportOptionKeepsFormat();
  void testHeadingFoldingRouteMatrix();
  void testIntermediateHtmlMarkerIsRuntimeOnly();
};

void TestExportData::testDefaultFooterRight() {
  vnotex::ExportPdfOption opt;
  // Preserves the historical page-number footer out of the box.
  QCOMPARE(opt.m_footerRight, QStringLiteral("[page]"));
  QVERIFY(opt.m_headerLeft.isEmpty());
  QVERIFY(opt.m_footerLeft.isEmpty());
}

void TestExportData::testRoundTrip() {
  vnotex::ExportPdfOption opt;
  opt.m_headerLeft = QStringLiteral("HL");
  opt.m_headerCenter = QStringLiteral("[title]");
  opt.m_headerRight = QStringLiteral("HR");
  opt.m_footerLeft = QStringLiteral("[date]");
  opt.m_footerCenter = QStringLiteral("中文");
  opt.m_footerRight = QStringLiteral("[page]/[topage]");

  vnotex::ExportPdfOption restored;
  restored.fromJson(opt.toJson());

  QCOMPARE(restored.m_headerLeft, opt.m_headerLeft);
  QCOMPARE(restored.m_headerCenter, opt.m_headerCenter);
  QCOMPARE(restored.m_headerRight, opt.m_headerRight);
  QCOMPARE(restored.m_footerLeft, opt.m_footerLeft);
  QCOMPARE(restored.m_footerCenter, opt.m_footerCenter);
  QCOMPARE(restored.m_footerRight, opt.m_footerRight);
  QVERIFY(restored == opt);
}

void TestExportData::testFromEmptyKeepsDefaults() {
  vnotex::ExportPdfOption opt;
  opt.fromJson(QJsonObject{});
  // Empty-object early return preserves ctor defaults.
  QCOMPARE(opt.m_footerRight, QStringLiteral("[page]"));
}

void TestExportData::testOldConfigKeepsFooterDefault() {
  // Simulate an old config that predates the header/footer keys.
  QJsonObject old;
  old["useWkhtmltopdf"] = true;
  old["addTableOfContents"] = false;

  vnotex::ExportPdfOption opt;
  opt.fromJson(old);

  // Missing footerRight key must NOT clear the default.
  QCOMPARE(opt.m_footerRight, QStringLiteral("[page]"));
  QVERIFY(opt.m_headerLeft.isEmpty());
}

void TestExportData::testEquality() {
  vnotex::ExportPdfOption a;
  vnotex::ExportPdfOption b;
  QVERIFY(a == b);

  b.m_headerCenter = QStringLiteral("x");
  QVERIFY(!(a == b));
}

void TestExportData::testExportOptionStyleEquality() {
  vnotex::ExportOption a;
  vnotex::ExportOption b;
  QVERIFY(a == b);

  b.m_renderingStyleFile = QStringLiteral("custom-render.css");
  QVERIFY(!(a == b));

  vnotex::ExportOption c;
  c.m_syntaxHighlightStyleFile = QStringLiteral("custom-syntax.css");
  QVERIFY(!(a == c));
}

void TestExportData::testExportOptionStyleRoundTrip() {
  vnotex::ExportOption opt;
  opt.m_renderingStyleFile = QStringLiteral("custom-render.css");
  opt.m_syntaxHighlightStyleFile = QStringLiteral("custom-syntax.css");

  vnotex::ExportOption restored;
  restored.fromJson(opt.toJson());

  QCOMPARE(restored.m_renderingStyleFile, opt.m_renderingStyleFile);
  QCOMPARE(restored.m_syntaxHighlightStyleFile, opt.m_syntaxHighlightStyleFile);
  QVERIFY(restored == opt);
}

void TestExportData::testExportFormatPersistence_data() {
  using vnotex::ExportFormat;
  QTest::addColumn<int>("format");
  QTest::addColumn<int>("persistedValue");
  QTest::addColumn<QString>("displayName");
  QTest::newRow("markdown") << int(ExportFormat::Markdown) << 0 << QStringLiteral("Markdown");
  QTest::newRow("html") << int(ExportFormat::HTML) << 1 << QStringLiteral("HTML");
  QTest::newRow("pdf") << int(ExportFormat::PDF) << 2 << QStringLiteral("PDF");
  QTest::newRow("custom") << int(ExportFormat::Custom) << 3 << QStringLiteral("Custom");
  QTest::newRow("typst") << int(ExportFormat::Typst) << 4 << QStringLiteral("Typst");
  QTest::newRow("mindmap") << int(ExportFormat::MindMap) << 5 << QStringLiteral("MindMap");
}

void TestExportData::testExportFormatPersistence() {
  QFETCH(int, format);
  QFETCH(int, persistedValue);
  QFETCH(QString, displayName);
  QCOMPARE(format, persistedValue);

  vnotex::ExportOption option;
  option.m_targetFormat = static_cast<vnotex::ExportFormat>(format);
  option.m_exportAttachments = false;
  option.m_renderingStyleFile = QStringLiteral("retained-style.css");
  const auto json = option.toJson();
  QCOMPARE(json.value(QStringLiteral("targetFormat")).toInt(-1), persistedValue);
  QCOMPARE(vnotex::exportFormatString(option.m_targetFormat), displayName);

  vnotex::ExportOption restored;
  restored.fromJson(json);
  QCOMPARE(restored.m_targetFormat, option.m_targetFormat);
  QVERIFY(restored == option);
  QCOMPARE(restored.toJson(), json);
}

void TestExportData::testUnknownExportFormat_data() {
  QTest::addColumn<int>("format");
  QTest::newRow("negative") << -1;
  QTest::newRow("after-native-formats") << 6;
  QTest::newRow("future-format") << 999;
}

void TestExportData::testUnknownExportFormat() {
  QFETCH(int, format);
  vnotex::ExportOption option;
  option.m_targetFormat = vnotex::ExportFormat::Typst;
  option.fromJson(QJsonObject{{QStringLiteral("targetFormat"), format}});
  QCOMPARE(option.m_targetFormat, vnotex::ExportFormat::HTML);
  QCOMPARE(option.toJson().value(QStringLiteral("targetFormat")).toInt(-1), 1);
  QCOMPARE(vnotex::exportFormatString(static_cast<vnotex::ExportFormat>(format)),
           QStringLiteral("Unknown"));
}

void TestExportData::testEmptyExportOptionKeepsFormat() {
  vnotex::ExportOption option;
  option.fromJson(QJsonObject{});
  QCOMPARE(option.m_targetFormat, vnotex::ExportFormat::HTML);
  for (const auto format : {vnotex::ExportFormat::Typst, vnotex::ExportFormat::MindMap}) {
    option.m_targetFormat = format;
    option.fromJson(QJsonObject{});
    QCOMPARE(option.m_targetFormat, format);
  }
}

void TestExportData::testHeadingFoldingRouteMatrix() {
  vnotex::ExportOption option;
  option.m_targetFormat = vnotex::ExportFormat::HTML;
  QVERIFY(vnotex::isHeadingFoldingEnabledForExport(option, true));
  QVERIFY(!vnotex::isHeadingFoldingEnabledForExport(option, false));

  option.m_targetFormat = vnotex::ExportFormat::PDF;
  QVERIFY(!vnotex::isHeadingFoldingEnabledForExport(option, true));

  option.m_targetFormat = vnotex::ExportFormat::Custom;
  QVERIFY(!vnotex::isHeadingFoldingEnabledForExport(option, true));

  option.m_targetFormat = vnotex::ExportFormat::Markdown;
  QVERIFY(!vnotex::isHeadingFoldingEnabledForExport(option, true));

  option.m_targetFormat = vnotex::ExportFormat::HTML;
  option.m_intermediateHtml = true;
  QVERIFY(!vnotex::isHeadingFoldingEnabledForExport(option, true));
}

void TestExportData::testIntermediateHtmlMarkerIsRuntimeOnly() {
  vnotex::ExportOption marked;
  marked.m_intermediateHtml = true;
  const QJsonObject json = marked.toJson();
  QVERIFY(!json.contains(QStringLiteral("intermediateHtml")));

  vnotex::ExportOption restored;
  restored.fromJson(json);
  QVERIFY(!restored.m_intermediateHtml);
  QVERIFY(marked == restored);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExportData)
#include "test_exportdata.moc"
