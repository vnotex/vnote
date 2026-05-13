#include <QtTest>

#include <QJsonObject>

#include <core/theme.h>

namespace tests {

class TestTheme : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testStub();
  void testTranslateStyleByPalette_jsonQuotedValue();
  void testTranslateStyleByPalette_qssStillWorks();
  void testTranslateStyleByPalette_colonPrefixStillWorks();
};

void TestTheme::initTestCase() {}

void TestTheme::cleanupTestCase() {}

void TestTheme::testStub() {
  QVERIFY(true);
}

// T1 acceptance: regex must match @-tokens preceded by " (JSON-quoted values).
void TestTheme::testTranslateStyleByPalette_jsonQuotedValue() {
  QJsonObject palette;
  QJsonObject baseObj;
  baseObj["fg"] = QStringLiteral("#222222");
  palette["base"] = baseObj;

  QString style = QStringLiteral("\"text-color\": \"@base#fg\"");
  vnotex::Theme::translateStyleByPalette(palette, style);

  QVERIFY2(style.contains(QStringLiteral("\"#222222\"")),
           qPrintable(QStringLiteral("Expected resolved hex inside quotes; got: %1").arg(style)));
  QVERIFY2(!style.contains(QStringLiteral("@base#fg")),
           qPrintable(QStringLiteral("Token should be replaced; got: %1").arg(style)));
}

// Regression: whitespace-prefixed tokens (existing QSS pattern) still resolve.
void TestTheme::testTranslateStyleByPalette_qssStillWorks() {
  QJsonObject palette;
  QJsonObject baseObj;
  QJsonObject infoObj;
  infoObj["fg"] = QStringLiteral("#1976d2");
  baseObj["info"] = infoObj;
  palette["base"] = baseObj;

  QString style = QStringLiteral("border: 1px solid @base#info#fg;");
  vnotex::Theme::translateStyleByPalette(palette, style);

  QCOMPARE(style, QStringLiteral("border: 1px solid #1976d2;"));
}

// Regression: colon-prefixed tokens (existing QSS pattern, no whitespace) still resolve.
void TestTheme::testTranslateStyleByPalette_colonPrefixStillWorks() {
  QJsonObject palette;
  QJsonObject baseObj;
  baseObj["fg"] = QStringLiteral("#222222");
  palette["base"] = baseObj;

  QString style = QStringLiteral("color:@base#fg;");
  vnotex::Theme::translateStyleByPalette(palette, style);

  QCOMPARE(style, QStringLiteral("color:#222222;"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTheme)
#include "test_theme.moc"
