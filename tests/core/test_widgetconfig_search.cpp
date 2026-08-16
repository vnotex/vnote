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

  // folderShareLastDestination (Share Folder).
  void testFolderShareDestinationRoundTrip();
  void testFolderShareDestinationDefault();
  void testFolderShareDestinationMissingOrWrongType();

  // newNoteDefaultTemplates (per-file-type default note template).
  void testNewNoteDefaultTemplatesSeedsMarkdownOnFirstRun();
  void testNewNoteDefaultTemplatesRoundTripAndPerTypeAccess();
  void testNewNoteDefaultTemplatesEmptyObjectIsRespected();
  void testNewNoteDefaultTemplatesWrongTypeYieldsNoDefaults();
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
  // The folder-share destination is equally unaffected by unrelated keys.
  QCOMPARE(wc.getFolderShareLastDestination(), QString());
}

void TestWidgetConfigSearch::testFolderShareDestinationRoundTrip() {
  WidgetConfig wc1(nullptr, nullptr);
  // Unicode + spaces: the remembered destination is an arbitrary user path.
  const QString path =
      QStringLiteral("C:/Users/me/My Bundles/") + QString::fromUtf8("\xE9\xA1\xB9\xE7\x9B\xAE");
  wc1.setFolderShareLastDestination(path);
  QCOMPARE(wc1.getFolderShareLastDestination(), path);

  WidgetConfig wc2(nullptr, nullptr);
  wc2.fromJson(wc1.toJson());
  QCOMPARE(wc2.getFolderShareLastDestination(), path);
}

void TestWidgetConfigSearch::testFolderShareDestinationDefault() {
  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(QJsonObject()); // empty JSON

  // Empty until the user accepts a destination for the first time; the view
  // then falls back to the documents/home path.
  QCOMPARE(wc.getFolderShareLastDestination(), QString());
  QVERIFY(wc.toJson().contains(QStringLiteral("folderShareLastDestination")));
}

void TestWidgetConfigSearch::testFolderShareDestinationMissingOrWrongType() {
  QJsonObject json;
  json[QStringLiteral("searchScope")] = 2;

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);
  QCOMPARE(wc.getFolderShareLastDestination(), QString());

  // A wrong-typed value must not crash and must not be adopted.
  QJsonObject bad;
  bad[QStringLiteral("folderShareLastDestination")] = 42;
  WidgetConfig wcBad(nullptr, nullptr);
  wcBad.fromJson(bad);
  QCOMPARE(wcBad.getFolderShareLastDestination(), QString());
}

// An absent key means "never configured", so the bundled mapping is seeded.
// Markdown ships title.md; nothing else has a bundled template.
void TestWidgetConfigSearch::testNewNoteDefaultTemplatesSeedsMarkdownOnFirstRun() {
  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(QJsonObject());

  QCOMPARE(wc.getNewNoteDefaultTemplate(QStringLiteral("Markdown")), QStringLiteral("title.md"));
  QCOMPARE(wc.getNewNoteDefaultTemplate(QStringLiteral("Text")), QString());
  QCOMPARE(wc.getNewNoteDefaultTemplate(QString()), QString());
  QVERIFY(wc.toJson().contains(QStringLiteral("newNoteDefaultTemplates")));
}

void TestWidgetConfigSearch::testNewNoteDefaultTemplatesRoundTripAndPerTypeAccess() {
  WidgetConfig wc1(nullptr, nullptr);
  wc1.setNewNoteDefaultTemplate(QStringLiteral("Text"), QStringLiteral("plain.txt"));
  wc1.setNewNoteDefaultTemplate(QStringLiteral("Markdown"), QStringLiteral("mine.md"));
  // An empty type name is not a key and must be ignored rather than stored.
  wc1.setNewNoteDefaultTemplate(QString(), QStringLiteral("orphan.md"));

  QCOMPARE(wc1.getNewNoteDefaultTemplates().size(), 2);

  WidgetConfig wc2(nullptr, nullptr);
  wc2.fromJson(wc1.toJson());
  QCOMPARE(wc2.getNewNoteDefaultTemplate(QStringLiteral("Markdown")), QStringLiteral("mine.md"));
  QCOMPARE(wc2.getNewNoteDefaultTemplate(QStringLiteral("Text")), QStringLiteral("plain.txt"));
  QCOMPARE(wc2.getNewNoteDefaultTemplate(QStringLiteral("PDF")), QString());
}

// A user who wants no default at all writes an explicit mapping; a present key
// is their choice and must NOT be re-seeded with the bundled entry.
void TestWidgetConfigSearch::testNewNoteDefaultTemplatesEmptyObjectIsRespected() {
  QJsonObject json;
  json[QStringLiteral("newNoteDefaultTemplates")] = QJsonObject();

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);
  QCOMPARE(wc.getNewNoteDefaultTemplate(QStringLiteral("Markdown")), QString());

  // The same holds for an explicitly cleared per-type entry.
  QJsonObject cleared;
  QJsonObject mapping;
  mapping[QStringLiteral("Markdown")] = QString();
  cleared[QStringLiteral("newNoteDefaultTemplates")] = mapping;

  WidgetConfig wcCleared(nullptr, nullptr);
  wcCleared.fromJson(cleared);
  QCOMPARE(wcCleared.getNewNoteDefaultTemplate(QStringLiteral("Markdown")), QString());
}

void TestWidgetConfigSearch::testNewNoteDefaultTemplatesWrongTypeYieldsNoDefaults() {
  QJsonObject json;
  json[QStringLiteral("newNoteDefaultTemplates")] = 42;

  WidgetConfig wc(nullptr, nullptr);
  wc.fromJson(json);
  QVERIFY(wc.getNewNoteDefaultTemplates().isEmpty());
  QCOMPARE(wc.getNewNoteDefaultTemplate(QStringLiteral("Markdown")), QString());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestWidgetConfigSearch)
#include "test_widgetconfig_search.moc"
