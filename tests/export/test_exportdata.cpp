// GUI-free round-trip test for ExportPdfOption JSON serialization + equality,
// covering the header/footer fields (issue #2524) and backward-compat defaults.
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExportData)
#include "test_exportdata.moc"
