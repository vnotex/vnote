#include <QtTest>

#include <core/fileopensettings.h>
#include <core/hookevents.h>

using namespace vnotex;

namespace tests {

class TestSearchHighlightContext : public QObject {
  Q_OBJECT

private slots:
  // SearchHighlightContext tests
  void testDefaultConstruction();
  void testRoundTrip();
  void testEmptyPatternsValid();
  void testNegativeCurrentMatchLine();

  // FileOpenSettings with SearchHighlightContext
  void testFileOpenSettingsRoundTrip();
  void testFileOpenSettingsDefaultSearchHighlight();

  // FileOpenEvent search fields
  void testFileOpenEventRoundTrip();
  void testFileOpenEventDefaultSearchFields();
};

void TestSearchHighlightContext::testDefaultConstruction() {
  SearchHighlightContext context;
  QVERIFY(!context.m_isValid);
  QVERIFY(context.m_patterns.isEmpty());
  QCOMPARE(context.m_options, FindOption::FindNone);
  QCOMPARE(context.m_currentMatchLine, -1);
}

void TestSearchHighlightContext::testRoundTrip() {
  SearchHighlightContext orig;
  orig.m_patterns = {QStringLiteral("keyword1"), QStringLiteral("keyword2")};
  orig.m_options = FindOption::CaseSensitive;
  orig.m_currentMatchLine = 42;
  orig.m_isValid = true;

  const QVariantMap map = orig.toVariantMap();
  const SearchHighlightContext restored = SearchHighlightContext::fromVariantMap(map);

  QCOMPARE(restored.m_patterns, orig.m_patterns);
  QCOMPARE(restored.m_options, orig.m_options);
  QCOMPARE(restored.m_currentMatchLine, orig.m_currentMatchLine);
  QCOMPARE(restored.m_isValid, orig.m_isValid);
}

void TestSearchHighlightContext::testEmptyPatternsValid() {
  SearchHighlightContext orig;
  orig.m_isValid = true;

  const QVariantMap map = orig.toVariantMap();
  const SearchHighlightContext restored = SearchHighlightContext::fromVariantMap(map);

  QVERIFY(restored.m_patterns.isEmpty());
  QVERIFY(restored.m_isValid);
}

void TestSearchHighlightContext::testNegativeCurrentMatchLine() {
  SearchHighlightContext orig;
  orig.m_patterns = {QStringLiteral("keyword")};
  orig.m_options = FindOption::FindNone;
  orig.m_currentMatchLine = -1;
  orig.m_isValid = true;

  const QVariantMap map = orig.toVariantMap();
  const SearchHighlightContext restored = SearchHighlightContext::fromVariantMap(map);

  QCOMPARE(restored.m_currentMatchLine, -1);
}

void TestSearchHighlightContext::testFileOpenSettingsRoundTrip() {
  FileOpenSettings orig;
  orig.m_mode = ViewWindowMode::Edit;
  orig.m_forceMode = true;
  orig.m_focus = false;
  orig.m_newFile = true;
  orig.m_readOnly = true;
  orig.m_lineNumber = 12;
  orig.m_alwaysNewWindow = true;
  orig.m_searchHighlight.m_patterns = {QStringLiteral("k1"), QStringLiteral("k2")};
  orig.m_searchHighlight.m_options = FindOption::CaseSensitive;
  orig.m_searchHighlight.m_currentMatchLine = 21;
  orig.m_searchHighlight.m_isValid = true;

  const QVariantMap map = orig.toVariantMap();
  const FileOpenSettings restored = FileOpenSettings::fromVariantMap(map);

  QCOMPARE(restored.m_searchHighlight.m_patterns, orig.m_searchHighlight.m_patterns);
  QCOMPARE(restored.m_searchHighlight.m_options, orig.m_searchHighlight.m_options);
  QCOMPARE(restored.m_searchHighlight.m_currentMatchLine,
           orig.m_searchHighlight.m_currentMatchLine);
  QCOMPARE(restored.m_searchHighlight.m_isValid, orig.m_searchHighlight.m_isValid);
}

void TestSearchHighlightContext::testFileOpenSettingsDefaultSearchHighlight() {
  FileOpenSettings settings;
  QVERIFY(!settings.m_searchHighlight.m_isValid);
}

void TestSearchHighlightContext::testFileOpenEventRoundTrip() {
  FileOpenEvent orig;
  orig.notebookId = QStringLiteral("nb-1");
  orig.filePath = QStringLiteral("notes/a.md");
  orig.bufferId = QStringLiteral("buf-1");
  orig.searchPatterns = {QStringLiteral("pat1"), QStringLiteral("pat2")};
  orig.searchOptions = static_cast<int>(FindOption::CaseSensitive);
  orig.searchCurrentMatchLine = 7;

  const QVariantMap map = orig.toVariantMap();
  const FileOpenEvent restored = FileOpenEvent::fromVariantMap(map);

  QCOMPARE(restored.searchPatterns, orig.searchPatterns);
  QCOMPARE(restored.searchOptions, orig.searchOptions);
  QCOMPARE(restored.searchCurrentMatchLine, orig.searchCurrentMatchLine);
}

void TestSearchHighlightContext::testFileOpenEventDefaultSearchFields() {
  FileOpenEvent event;
  QVERIFY(event.searchPatterns.isEmpty());
  QCOMPARE(event.searchOptions, 0);
  QCOMPARE(event.searchCurrentMatchLine, -1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchHighlightContext)
#include "test_searchhighlightcontext.moc"
