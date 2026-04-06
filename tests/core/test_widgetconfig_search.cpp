#include <QtTest>

#include <core/widgetconfig.h>

using namespace vnotex;

namespace tests {

class TestWidgetConfigSearch : public QObject {
  Q_OBJECT

private slots:
  void testRoundTrip();
  void testDefaults();
  void testInvalidScopeHigh();
  void testInvalidScopeNegative();
  void testInvalidModeHigh();
  void testMissingKeys();
};

void TestWidgetConfigSearch::testRoundTrip() {
  // Create config and set values via setters
  WidgetConfig wc1(nullptr, nullptr);
  wc1.setSearchScope(2);
  wc1.setSearchMode(1);
  wc1.setSearchCaseSensitive(true);
  wc1.setSearchRegex(true);
  wc1.setSearchFilePattern(QStringLiteral("*.md"));

  // Serialize to JSON
  QJsonObject json = wc1.toJson();

  // Deserialize into a new instance
  WidgetConfig wc2(nullptr, nullptr);
  wc2.fromJson(json);

  QCOMPARE(wc2.getSearchScope(), 2);
  QCOMPARE(wc2.getSearchMode(), 1);
  QCOMPARE(wc2.getSearchCaseSensitive(), true);
  QCOMPARE(wc2.getSearchRegex(), true);
  QCOMPARE(wc2.getSearchFilePattern(), QStringLiteral("*.md"));
}

void TestWidgetConfigSearch::testDefaults() {
  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(QJsonObject()); // empty JSON

  QCOMPARE(wc.getSearchScope(), 0);
  QCOMPARE(wc.getSearchMode(), 0);
  QCOMPARE(wc.getSearchCaseSensitive(), false);
  QCOMPARE(wc.getSearchRegex(), false);
  QCOMPARE(wc.getSearchFilePattern(), QString());
}

void TestWidgetConfigSearch::testInvalidScopeHigh() {
  QJsonObject json;
  json[QStringLiteral("searchScope")] = 99;

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);

  QCOMPARE(wc.getSearchScope(), 0); // Falls back to default
}

void TestWidgetConfigSearch::testInvalidScopeNegative() {
  QJsonObject json;
  json[QStringLiteral("searchScope")] = -1;

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);

  QCOMPARE(wc.getSearchScope(), 0); // Falls back to default
}

void TestWidgetConfigSearch::testInvalidModeHigh() {
  QJsonObject json;
  json[QStringLiteral("searchMode")] = 5;

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);

  QCOMPARE(wc.getSearchMode(), 0); // Falls back to default
}

void TestWidgetConfigSearch::testMissingKeys() {
  // JSON with some keys present, search keys missing
  QJsonObject json;
  json[QStringLiteral("outlineAutoExpandedLevel")] = 3;

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);

  // Search fields should retain defaults
  QCOMPARE(wc.getSearchScope(), 0);
  QCOMPARE(wc.getSearchMode(), 0);
  QCOMPARE(wc.getSearchCaseSensitive(), false);
  QCOMPARE(wc.getSearchRegex(), false);
  QCOMPARE(wc.getSearchFilePattern(), QString());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestWidgetConfigSearch)
#include "test_widgetconfig_search.moc"
