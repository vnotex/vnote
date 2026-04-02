#include <QtTest>

#include <QAbstractItemModelTester>

#include <core/nodeidentifier.h>
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
  void testIsFileResultRole();
  void testModelTester();

private:
  static vnotex::SearchResult makeContentResult();
  static vnotex::SearchResult makeFileResult();
};

vnotex::SearchResult TestSearchResultModel::makeContentResult() {
  vnotex::SearchLineMatch m1;
  m1.m_lineNumber = 1;
  m1.m_columnStart = 2;
  m1.m_columnEnd = 4;
  m1.m_lineText = QStringLiteral("first line");

  vnotex::SearchLineMatch m2;
  m2.m_lineNumber = 5;
  m2.m_columnStart = 1;
  m2.m_columnEnd = 3;
  m2.m_lineText = QStringLiteral("second line");

  vnotex::SearchFileResult file;
  file.m_path = QStringLiteral("notes/file.md");
  file.m_absolutePath = QStringLiteral("/abs/notes/file.md");
  file.m_id = QStringLiteral("id-file");
  file.m_notebookId = QStringLiteral("nb-1");
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
  QVERIFY(model.data(lineIndex0, Qt::DisplayRole).toString().contains(QStringLiteral("2:")));
  QVERIFY(model.data(lineIndex1, Qt::DisplayRole).toString().contains(QStringLiteral("6:")));
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchResultModel)
#include "test_searchresultmodel.moc"
