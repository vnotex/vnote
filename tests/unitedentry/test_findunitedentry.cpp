#include <QtTest>

#include <QCommandLineOption>
#include <QCommandLineParser>

#include <controllers/searchcontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <unitedentry/findunitedentry.h>
#include <unitedentry/unitedentrymgr.h>

using namespace vnotex;

namespace tests {

class TestFindUnitedEntry : public QObject {
  Q_OBJECT

private slots:
  void testScopeBuffer();
  void testScopeFolder();
  void testScopeNotebook();
  void testScopeAllNotebook();

  void testObjectContent();
  void testObjectNamePath();
  void testObjectTag();
  void testMultiObjectPriority();

  void testCaseSensitive();
  void testRegex();
  void testFilePattern();
  void testDefaultValues();

  void testContextGetterSetter();

private:
  static void setupParser(QCommandLineParser &p_parser);
};

void TestFindUnitedEntry::setupParser(QCommandLineParser &p_parser) {
  p_parser.addPositionalArgument(QStringLiteral("keywords"),
                                 QStringLiteral("Keywords to search for."));

  QCommandLineOption scopeOpt({QStringLiteral("s"), QStringLiteral("scope")},
                              QStringLiteral("Search scope."), QStringLiteral("search_scope"),
                              QStringLiteral("notebook"));
  p_parser.addOption(scopeOpt);

  QCommandLineOption objectOpt({QStringLiteral("b"), QStringLiteral("object")},
                               QStringLiteral("Search objects."), QStringLiteral("search_objects"));
  objectOpt.setDefaultValues({QStringLiteral("name"), QStringLiteral("content")});
  p_parser.addOption(objectOpt);

  p_parser.addOption(QCommandLineOption({QStringLiteral("p"), QStringLiteral("pattern")},
                                        QStringLiteral("Wildcard pattern of files to search."),
                                        QStringLiteral("file_pattern")));
  p_parser.addOption(QCommandLineOption({QStringLiteral("c"), QStringLiteral("case-sensitive")},
                                        QStringLiteral("Search in case sensitive.")));
  p_parser.addOption(QCommandLineOption({QStringLiteral("r"), QStringLiteral("regular-expression")},
                                        QStringLiteral("Search by regular expression.")));
}

void TestFindUnitedEntry::testScopeBuffer() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-s"), QStringLiteral("buffer"),
                        QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.scope, static_cast<int>(SearchController::Buffers));
  QCOMPARE(params.keyword, QStringLiteral("myquery"));
}

void TestFindUnitedEntry::testScopeFolder() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-s"), QStringLiteral("folder"),
                        QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.scope, static_cast<int>(SearchController::CurrentFolder));
}

void TestFindUnitedEntry::testScopeNotebook() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-s"), QStringLiteral("notebook"),
                        QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.scope, static_cast<int>(SearchController::CurrentNotebook));
}

void TestFindUnitedEntry::testScopeAllNotebook() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-s"),
                        QStringLiteral("all_notebook"), QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.scope, static_cast<int>(SearchController::AllNotebooks));
}

void TestFindUnitedEntry::testObjectContent() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-b"), QStringLiteral("content"),
                        QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.searchMode, static_cast<int>(SearchController::ContentSearch));
}

void TestFindUnitedEntry::testObjectNamePath() {
  {
    QCommandLineParser parser;
    setupParser(parser);
    QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-b"), QStringLiteral("name"),
                          QStringLiteral("myquery")}));

    const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
    QCOMPARE(params.searchMode, static_cast<int>(SearchController::FileNameSearch));
  }

  {
    QCommandLineParser parser;
    setupParser(parser);
    QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-b"), QStringLiteral("path"),
                          QStringLiteral("myquery")}));

    const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
    QCOMPARE(params.searchMode, static_cast<int>(SearchController::FileNameSearch));
  }
}

void TestFindUnitedEntry::testObjectTag() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-b"), QStringLiteral("tag"),
                        QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.searchMode, static_cast<int>(SearchController::TagSearch));
}

void TestFindUnitedEntry::testMultiObjectPriority() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(
      parser.parse({QStringLiteral("find"), QStringLiteral("-b"), QStringLiteral("name"),
                    QStringLiteral("-b"), QStringLiteral("content"), QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.searchMode, static_cast<int>(SearchController::ContentSearch));
}

void TestFindUnitedEntry::testCaseSensitive() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-c"), QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QVERIFY(params.caseSensitive);
}

void TestFindUnitedEntry::testRegex() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-r"), QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QVERIFY(params.useRegex);
}

void TestFindUnitedEntry::testFilePattern() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("-p"), QStringLiteral("*.md"),
                        QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.filePattern, QStringLiteral("*.md"));
}

void TestFindUnitedEntry::testDefaultValues() {
  QCommandLineParser parser;
  setupParser(parser);
  QVERIFY(parser.parse({QStringLiteral("find"), QStringLiteral("myquery")}));

  const auto params = FindUnitedEntry::mapArgsToSearchParams(parser);
  QCOMPARE(params.scope, static_cast<int>(SearchController::CurrentNotebook));
  QCOMPARE(params.searchMode, static_cast<int>(SearchController::ContentSearch));
  QCOMPARE(params.keyword, QStringLiteral("myquery"));
  QVERIFY(!params.caseSensitive);
  QVERIFY(!params.useRegex);
  QVERIFY(params.filePattern.isEmpty());
}

void TestFindUnitedEntry::testContextGetterSetter() {
  ServiceLocator services;
  UnitedEntryMgr mgr(services);

  const QString notebookId = QStringLiteral("nb-1");
  NodeIdentifier folderId;
  folderId.notebookId = notebookId;
  folderId.relativePath = QStringLiteral("docs/sub");

  mgr.setCurrentNotebookId(notebookId);
  mgr.setCurrentFolderId(folderId);

  QCOMPARE(mgr.currentNotebookId(), notebookId);
  QCOMPARE(mgr.currentFolderId(), folderId);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFindUnitedEntry)
#include "test_findunitedentry.moc"
