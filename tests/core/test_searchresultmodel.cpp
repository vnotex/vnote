#include <QtTest>

#include <QAbstractItemModelTester>

#include <core/nodeidentifier.h>
#include <core/services/searchservice.h>
#include <models/searchresultmodel.h>

namespace tests {

class TestSearchResultModel : public QObject {
  Q_OBJECT

private slots:
  void testEmptyModel();
  void testSetContentSearchResult();
  void testSetFileSearchResult();
  void testClear();
  void testTotalMatchCount();
  void testIsTruncated();
  void testNodeIdRole();
  void testLineNumberRole();
  void testSegmentsRole();
  void testIsFileResultRole();
  void testModelTester();
  void testReplacementParentSubsumesChildren();
  void testReplacementSingleLine();
  void testReplacementRejectsInvalidSelections();
  void testAllReplacementTargetsDeduplicateInModelOrder();
  void testReplacementRejectsConflictingSnapshots();

private:
  static vnotex::SearchResult makeContentResult();
  static vnotex::SearchResult makeFileResult();
  static vnotex::SearchResult makeReplacementResult();
};

vnotex::SearchResult TestSearchResultModel::makeContentResult() {
  vnotex::SearchLineMatch m1;
  m1.m_lineNumber = 1;
  m1.m_segments = {{2, 4}};
  m1.m_lineText = QStringLiteral("first line");

  vnotex::SearchLineMatch m2;
  m2.m_lineNumber = 5;
  m2.m_segments = {{1, 3}};
  m2.m_lineText = QStringLiteral("second line");

  vnotex::SearchFileResult file;
  file.m_path = QStringLiteral("notes/file.md");
  file.m_absolutePath = QStringLiteral("/abs/notes/file.md");
  file.m_id = QStringLiteral("id-file");
  file.m_notebookId = QStringLiteral("nb-1");
  file.m_matchCount = 2;
  file.m_lineMatches = {m1, m2};

  vnotex::SearchResult result;
  result.m_fileResults = {file};
  result.m_matchCount = 2;
  result.m_truncated = false;
  return result;
}

vnotex::SearchResult TestSearchResultModel::makeFileResult() {
  vnotex::SearchFileResult file;
  file.m_type = vnotex::SearchResultType::File;
  file.m_path = QStringLiteral("notes/a.md");
  file.m_absolutePath = QStringLiteral("/abs/notes/a.md");
  file.m_id = QStringLiteral("id-a");
  file.m_notebookId = QStringLiteral("nb-2");

  vnotex::SearchFileResult folder;
  folder.m_type = vnotex::SearchResultType::Folder;
  folder.m_path = QStringLiteral("notes/folder");
  folder.m_absolutePath = QStringLiteral("/abs/notes/folder");
  folder.m_id = QStringLiteral("id-folder");
  folder.m_notebookId = QStringLiteral("nb-2");

  vnotex::SearchResult result;
  result.m_fileResults = {file, folder};
  result.m_matchCount = 2;
  result.m_truncated = true;
  return result;
}

void TestSearchResultModel::testEmptyModel() {
  vnotex::SearchResultModel model;
  QCOMPARE(model.rowCount(), 0);
  QCOMPARE(model.totalMatchCount(), 0);
  QCOMPARE(model.isTruncated(), false);
}

void TestSearchResultModel::testSetContentSearchResult() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeContentResult());

  QCOMPARE(model.rowCount(), 1);

  const QModelIndex fileIndex = model.index(0, 0);
  QVERIFY(fileIndex.isValid());
  QCOMPARE(model.rowCount(fileIndex), 2);
  QCOMPARE(model.data(fileIndex, Qt::DisplayRole).toString(), QStringLiteral("file.md"));

  const QModelIndex lineIndex0 = model.index(0, 0, fileIndex);
  const QModelIndex lineIndex1 = model.index(1, 0, fileIndex);
  QVERIFY(lineIndex0.isValid());
  QVERIFY(lineIndex1.isValid());
  QCOMPARE(model.data(lineIndex0, Qt::DisplayRole).toString(), QStringLiteral("first line"));
  QCOMPARE(model.data(lineIndex1, Qt::DisplayRole).toString(), QStringLiteral("second line"));
}

void TestSearchResultModel::testSetFileSearchResult() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeFileResult());

  QCOMPARE(model.rowCount(), 2);
  const QModelIndex fileIndex = model.index(0, 0);
  const QModelIndex folderIndex = model.index(1, 0);
  QVERIFY(fileIndex.isValid());
  QVERIFY(folderIndex.isValid());
  QCOMPARE(model.rowCount(fileIndex), 0);
  QCOMPARE(model.rowCount(folderIndex), 0);
}

void TestSearchResultModel::testClear() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeContentResult());
  QCOMPARE(model.rowCount(), 1);

  model.clear();
  QCOMPARE(model.rowCount(), 0);
}

void TestSearchResultModel::testTotalMatchCount() {
  vnotex::SearchResultModel model;
  vnotex::SearchResult result = makeContentResult();
  result.m_matchCount = 123;
  model.setSearchResult(result);
  QCOMPARE(model.totalMatchCount(), 123);
}

void TestSearchResultModel::testIsTruncated() {
  vnotex::SearchResultModel model;
  vnotex::SearchResult result = makeContentResult();
  result.m_truncated = true;
  model.setSearchResult(result);
  QCOMPARE(model.isTruncated(), true);
}

void TestSearchResultModel::testNodeIdRole() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeContentResult());

  const QModelIndex fileIndex = model.index(0, 0);
  const vnotex::NodeIdentifier fileId =
      model.data(fileIndex, vnotex::SearchResultModel::NodeIdRole).value<vnotex::NodeIdentifier>();
  QCOMPARE(fileId.notebookId, QStringLiteral("nb-1"));
  QCOMPARE(fileId.relativePath, QStringLiteral("notes/file.md"));

  const QModelIndex lineIndex = model.index(0, 0, fileIndex);
  const vnotex::NodeIdentifier lineId =
      model.data(lineIndex, vnotex::SearchResultModel::NodeIdRole).value<vnotex::NodeIdentifier>();
  QCOMPARE(lineId.notebookId, QStringLiteral("nb-1"));
  QCOMPARE(lineId.relativePath, QStringLiteral("notes/file.md"));
}

void TestSearchResultModel::testLineNumberRole() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeContentResult());

  const QModelIndex fileIndex = model.index(0, 0);
  QCOMPARE(model.data(fileIndex, vnotex::SearchResultModel::LineNumberRole).toInt(), -1);

  const QModelIndex lineIndex = model.index(1, 0, fileIndex);
  QCOMPARE(model.data(lineIndex, vnotex::SearchResultModel::LineNumberRole).toInt(), 5);
}

void TestSearchResultModel::testSegmentsRole() {
  // A line with three matches exposes all three segments via SegmentsRole,
  // while the single-range Column roles report the FIRST segment (for
  // navigation / united-entry consumers).
  vnotex::SearchLineMatch line;
  line.m_lineNumber = 3;
  line.m_lineText = QStringLiteral("foo foo foo");
  line.m_segments = {{0, 3}, {4, 7}, {8, 11}};

  vnotex::SearchFileResult file;
  file.m_path = QStringLiteral("notes/multi.md");
  file.m_id = QStringLiteral("id-multi");
  file.m_notebookId = QStringLiteral("nb-9");
  file.m_matchCount = 3;
  file.m_lineMatches = {line};

  vnotex::SearchResult result;
  result.m_fileResults = {file};
  result.m_matchCount = 3;

  vnotex::SearchResultModel model;
  model.setSearchResult(result);

  const QModelIndex fileIndex = model.index(0, 0);
  // File badge counts occurrences (3), NOT matched lines (1).
  QCOMPARE(model.data(fileIndex, vnotex::SearchResultModel::MatchCountRole).toInt(), 3);
  const QModelIndex lineIndex = model.index(0, 0, fileIndex);
  QVERIFY(lineIndex.isValid());

  QCOMPARE(model.data(lineIndex, vnotex::SearchResultModel::ColumnStartRole).toInt(), 0);
  QCOMPARE(model.data(lineIndex, vnotex::SearchResultModel::ColumnEndRole).toInt(), 3);

  const auto segments = model.data(lineIndex, vnotex::SearchResultModel::SegmentsRole)
                            .value<QVector<vnotex::SearchMatchSegment>>();
  QCOMPARE(segments.size(), 3);
  QCOMPARE(segments[0].m_columnStart, 0);
  QCOMPARE(segments[0].m_columnEnd, 3);
  QCOMPARE(segments[1].m_columnStart, 4);
  QCOMPARE(segments[1].m_columnEnd, 7);
  QCOMPARE(segments[2].m_columnStart, 8);
  QCOMPARE(segments[2].m_columnEnd, 11);
}

void TestSearchResultModel::testIsFileResultRole() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeContentResult());

  const QModelIndex fileIndex = model.index(0, 0);
  QCOMPARE(model.data(fileIndex, vnotex::SearchResultModel::IsFileResultRole).toBool(), true);

  const QModelIndex lineIndex = model.index(0, 0, fileIndex);
  QCOMPARE(model.data(lineIndex, vnotex::SearchResultModel::IsFileResultRole).toBool(), false);
}

void TestSearchResultModel::testModelTester() {
  vnotex::SearchResultModel model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.setSearchResult(makeContentResult());
  QCOMPARE(model.rowCount(), 1);
}

vnotex::SearchResult TestSearchResultModel::makeReplacementResult() {
  auto result = makeContentResult();
  auto &file = result.m_fileResults[0];
  file.m_lineMatches = {{1, QStringLiteral("foo foo"), {{0, 3}, {4, 7}}},
                        {3, QStringLiteral("foo"), {{0, 3}}}};
  file.m_replacementSupported = true;
  // Extraction must count available segments, not trust an aggregate count.
  file.m_matchCount = 99;
  result.m_matchCount = 99;
  return result;
}

void TestSearchResultModel::testReplacementParentSubsumesChildren() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeReplacementResult());
  const auto parent = model.index(0, 0);
  const auto targets = model.replacementTargets(
      {model.index(1, 0, parent), model.index(0, 0, parent), parent, parent});
  QCOMPARE(targets.size(), 1);
  QCOMPARE(targets[0].m_matchCount, 3);
  QCOMPARE(targets[0].m_lineMatches.size(), 2);

  // The extracted value remains usable after the original model has been reset.
  model.clear();
  QString text;
  QString error;
  int count = 0;
  QVERIFY(vnotex::SearchService::buildReplacement(QStringLiteral("foo foo\nkeep\nfoo"),
                                                  targets[0].m_lineMatches, QStringLiteral("bar"),
                                                  &text, &count, &error));
  QCOMPARE(text, QStringLiteral("bar bar\nkeep\nbar"));
  QCOMPARE(count, 3);
}

void TestSearchResultModel::testReplacementSingleLine() {
  vnotex::SearchResultModel model;
  model.setSearchResult(makeReplacementResult());
  const auto parent = model.index(0, 0);
  const auto targets = model.replacementTargets({model.index(1, 0, parent)});
  QCOMPARE(targets.size(), 1);
  QCOMPARE(targets[0].m_matchCount, 1);

  QString text;
  QString error;
  int count = 0;
  QVERIFY(vnotex::SearchService::buildReplacement(QStringLiteral("foo foo\nkeep\nfoo"),
                                                  targets[0].m_lineMatches, QStringLiteral("bar"),
                                                  &text, &count, &error));
  QCOMPARE(text, QStringLiteral("foo foo\nkeep\nbar"));
  QCOMPARE(count, 1);
}

void TestSearchResultModel::testReplacementRejectsInvalidSelections() {
  class IndexProbeModel : public vnotex::SearchResultModel {
  public:
    QModelIndex nonzeroColumnIndex() const { return createIndex(0, 1, static_cast<quintptr>(0)); }
  };
  IndexProbeModel model;
  model.setSearchResult(makeReplacementResult());
  vnotex::SearchResultModel foreign;
  foreign.setSearchResult(makeReplacementResult());
  const QVector<QModelIndexList> selections = {
      {}, {QModelIndex()}, {foreign.index(0, 0)}, {model.nonzeroColumnIndex()}};
  const QString source = QStringLiteral("foo foo\nkeep\nfoo");
  for (const auto &selection : selections) {
    const auto targets = model.replacementTargets(selection);
    QVector<vnotex::SearchLineMatch> lines;
    for (const auto &target : targets) {
      lines += target.m_lineMatches;
    }
    QString text;
    QString error;
    int count = 0;
    QVERIFY(vnotex::SearchService::buildReplacement(source, lines, QStringLiteral("bar"), &text,
                                                    &count, &error));
    QCOMPARE(text, source);
    QCOMPARE(count, 0);
    QVERIFY(targets.isEmpty());
  }

  // Rejected rows neither widen a valid selection nor prevent its selected line from contributing.
  const auto targets =
      model.replacementTargets({foreign.index(0, 0), QModelIndex(), model.nonzeroColumnIndex(),
                                model.index(1, 0, model.index(0, 0))});
  QCOMPARE(targets.size(), 1);
  QString text;
  QString error;
  int count = 0;
  QVERIFY(vnotex::SearchService::buildReplacement(source, targets[0].m_lineMatches,
                                                  QStringLiteral("bar"), &text, &count, &error));
  QCOMPARE(text, QStringLiteral("foo foo\nkeep\nbar"));
  QCOMPARE(count, 1);
}

void TestSearchResultModel::testAllReplacementTargetsDeduplicateInModelOrder() {
  auto result = makeReplacementResult();
  auto &first = result.m_fileResults[0];
  // Deliberately nonnumeric line order: extraction follows rows, not sorted addresses.
  first.m_lineMatches.swapItemsAt(0, 1);
  auto otherNotebook = first;
  otherNotebook.m_notebookId = QStringLiteral("nb-2");
  otherNotebook.m_lineMatches = {{2, QStringLiteral("foo"), {{0, 3}}}};
  otherNotebook.m_replacementSupported = false;
  auto duplicate = first;
  duplicate.m_lineMatches.swapItemsAt(0, 1);
  duplicate.m_lineMatches[0].m_segments.append({0, 3});
  result.m_fileResults += {otherNotebook, duplicate};
  result.m_fileResults += makeFileResult().m_fileResults;

  vnotex::SearchResultModel model;
  model.setSearchResult(result);
  const auto allTargets = model.allReplacementTargets();
  const auto selectedTargets =
      model.replacementTargets({model.index(2, 0), model.index(1, 0), model.index(0, 0)});
  for (const auto &targets : {allTargets, selectedTargets}) {
    QCOMPARE(targets.size(), 2);
    QCOMPARE(targets[0].m_notebookId, QStringLiteral("nb-1"));
    QCOMPARE(targets[1].m_notebookId, QStringLiteral("nb-2"));
    QCOMPARE(targets[0].m_lineMatches[0].m_lineNumber, 3);
    QCOMPARE(targets[0].m_lineMatches[1].m_lineNumber, 1);
    QCOMPARE(targets[0].m_matchCount, 3);
    QCOMPARE(targets[1].m_matchCount, 1);
    QVERIFY(targets[0].m_replacementSupported);
    QVERIFY(!targets[1].m_replacementSupported);

    QString text;
    QString error;
    int count = 0;
    QVERIFY(vnotex::SearchService::buildReplacement(QStringLiteral("foo foo\nkeep\nfoo"),
                                                    targets[0].m_lineMatches, QStringLiteral("bar"),
                                                    &text, &count, &error));
    QCOMPARE(text, QStringLiteral("bar bar\nkeep\nbar"));
    QCOMPARE(count, 3);
    QVERIFY(vnotex::SearchService::buildReplacement(QStringLiteral("keep\nfoo"),
                                                    targets[1].m_lineMatches, QStringLiteral("bar"),
                                                    &text, &count, &error));
    QCOMPARE(text, QStringLiteral("keep\nbar"));
    QCOMPARE(count, 1);
  }

  // Deduplicating a mixed-provenance file must not grant permission to its unsupported rows.
  result.m_fileResults[2].m_replacementSupported = false;
  model.setSearchResult(result);
  const auto mixedTargets = model.allReplacementTargets();
  QCOMPARE(mixedTargets.size(), 2);
  QVERIFY(!mixedTargets[0].m_replacementSupported);
}

void TestSearchResultModel::testReplacementRejectsConflictingSnapshots() {
  auto result = makeReplacementResult();
  auto duplicate = result.m_fileResults[0];
  duplicate.m_lineMatches[0].m_lineText = QStringLiteral("foo old");
  result.m_fileResults.append(duplicate);
  vnotex::SearchResultModel model;
  model.setSearchResult(result);
  QVERIFY(model.allReplacementTargets().isEmpty());

  // Selecting the conflicting snapshot alone retains the conflict for the transformation to reject.
  const auto targets = model.replacementTargets({model.index(0, 0, model.index(1, 0))});
  QCOMPARE(targets.size(), 1);
  QString text;
  QString error;
  int count = 0;
  QVERIFY(!vnotex::SearchService::buildReplacement(QStringLiteral("foo foo\nkeep\nfoo"),
                                                   targets[0].m_lineMatches, QStringLiteral("bar"),
                                                   &text, &count, &error));
  QVERIFY(text.isEmpty());
  QCOMPARE(count, 0);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchResultModel)
#include "test_searchresultmodel.moc"
