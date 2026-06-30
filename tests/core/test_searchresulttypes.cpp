#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include <core/searchresulttypes.h>

namespace tests {

class TestSearchResultTypes : public QObject {
  Q_OBJECT

private slots:
  void testFromContentSearchJsonEmpty();
  void testFromFileSearchJsonEmpty();
  void testFromContentSearchJsonValid();
  void testFromContentSearchJsonGroupsMatchesByLine();
  void testFromContentSearchJsonGroupsNonAdjacent();
  void testFromFileSearchJsonValid();
  void testNotebookIdStamped();
};

void TestSearchResultTypes::testFromContentSearchJsonEmpty() {
  const vnotex::SearchResult result =
      vnotex::SearchResult::fromContentSearchJson(QJsonObject(), QStringLiteral("nb-1"));

  QVERIFY(result.m_fileResults.isEmpty());
  QCOMPARE(result.m_matchCount, 0);
  QCOMPARE(result.m_truncated, false);
}

void TestSearchResultTypes::testFromFileSearchJsonEmpty() {
  const vnotex::SearchResult result =
      vnotex::SearchResult::fromFileSearchJson(QJsonObject(), QStringLiteral("nb-1"));

  QVERIFY(result.m_fileResults.isEmpty());
  QCOMPARE(result.m_matchCount, 0);
  QCOMPARE(result.m_truncated, false);
}

void TestSearchResultTypes::testFromContentSearchJsonValid() {
  QJsonObject line1;
  line1[QStringLiteral("lineNumber")] = 10;
  line1[QStringLiteral("columnStart")] = 3;
  line1[QStringLiteral("columnEnd")] = 8;
  line1[QStringLiteral("lineText")] = QStringLiteral("hello world");

  QJsonObject line2;
  line2[QStringLiteral("lineNumber")] = 42;
  line2[QStringLiteral("columnStart")] = 1;
  line2[QStringLiteral("columnEnd")] = 6;
  line2[QStringLiteral("lineText")] = QStringLiteral("search match");

  QJsonArray lineMatches;
  lineMatches.append(line1);
  lineMatches.append(line2);

  QJsonObject file1;
  file1[QStringLiteral("path")] = QStringLiteral("notes/a.md");
  file1[QStringLiteral("id")] = QStringLiteral("file-guid-1");
  file1[QStringLiteral("matches")] = lineMatches;

  QJsonObject file2;
  file2[QStringLiteral("path")] = QStringLiteral("notes/b.md");
  file2[QStringLiteral("id")] = QStringLiteral("file-guid-2");
  file2[QStringLiteral("matches")] = QJsonArray();

  QJsonObject json;
  json[QStringLiteral("matchCount")] = 5;
  json[QStringLiteral("truncated")] = true;
  json[QStringLiteral("matches")] = QJsonArray{file1, file2};

  const vnotex::SearchResult result =
      vnotex::SearchResult::fromContentSearchJson(json, QStringLiteral("nb-1"));

  QCOMPARE(result.m_matchCount, 5);
  QCOMPARE(result.m_truncated, true);
  QCOMPARE(result.m_fileResults.size(), 2);

  const vnotex::SearchFileResult &parsedFile1 = result.m_fileResults[0];
  QCOMPARE(parsedFile1.m_type, vnotex::SearchResultType::File);
  QCOMPARE(parsedFile1.m_path, QStringLiteral("notes/a.md"));
  QCOMPARE(parsedFile1.m_id, QStringLiteral("file-guid-1"));
  QCOMPARE(parsedFile1.m_notebookId, QStringLiteral("nb-1"));
  QCOMPARE(parsedFile1.m_lineMatches.size(), 2);

  const vnotex::SearchLineMatch &parsedLine1 = parsedFile1.m_lineMatches[0];
  QCOMPARE(parsedLine1.m_lineNumber, 10);
  QCOMPARE(parsedLine1.m_segments.size(), 1);
  QCOMPARE(parsedLine1.m_segments[0].m_columnStart, 3);
  QCOMPARE(parsedLine1.m_segments[0].m_columnEnd, 8);
  QCOMPARE(parsedLine1.m_lineText, QStringLiteral("hello world"));

  const vnotex::SearchLineMatch &parsedLine2 = parsedFile1.m_lineMatches[1];
  QCOMPARE(parsedLine2.m_lineNumber, 42);
  QCOMPARE(parsedLine2.m_segments.size(), 1);
  QCOMPARE(parsedLine2.m_segments[0].m_columnStart, 1);
  QCOMPARE(parsedLine2.m_segments[0].m_columnEnd, 6);
  QCOMPARE(parsedLine2.m_lineText, QStringLiteral("search match"));

  const vnotex::SearchFileResult &parsedFile2 = result.m_fileResults[1];
  QCOMPARE(parsedFile2.m_path, QStringLiteral("notes/b.md"));
  QCOMPARE(parsedFile2.m_id, QStringLiteral("file-guid-2"));
  QVERIFY(parsedFile2.m_lineMatches.isEmpty());
}

// Regression: multiple vxcore matches on the SAME line must collapse into a
// single SearchLineMatch carrying one segment per occurrence, instead of one
// duplicated line row per occurrence. See root cause in the search refactor:
// vxcore emits one match per occurrence (correct facts layer); the Qt display
// layer is responsible for grouping occurrences by line.
void TestSearchResultTypes::testFromContentSearchJsonGroupsMatchesByLine() {
  // Three occurrences on line 7, then two occurrences on line 12, interleaved
  // exactly as vxcore emits them (sorted by line, then by column).
  auto makeMatch = [](int p_line, int p_colStart, int p_colEnd,
                      const QString &p_text) {
    QJsonObject o;
    o[QStringLiteral("lineNumber")] = p_line;
    o[QStringLiteral("columnStart")] = p_colStart;
    o[QStringLiteral("columnEnd")] = p_colEnd;
    o[QStringLiteral("lineText")] = p_text;
    return o;
  };

  QJsonArray lineMatches;
  lineMatches.append(makeMatch(7, 2, 5, QStringLiteral("foo foo foo bar")));
  lineMatches.append(makeMatch(7, 6, 9, QStringLiteral("foo foo foo bar")));
  lineMatches.append(makeMatch(7, 10, 13, QStringLiteral("foo foo foo bar")));
  lineMatches.append(makeMatch(12, 0, 3, QStringLiteral("baz baz")));
  lineMatches.append(makeMatch(12, 4, 7, QStringLiteral("baz baz")));

  QJsonObject file1;
  file1[QStringLiteral("path")] = QStringLiteral("notes/a.md");
  file1[QStringLiteral("id")] = QStringLiteral("file-guid-1");
  file1[QStringLiteral("matches")] = lineMatches;

  QJsonObject json;
  json[QStringLiteral("matchCount")] = 5;
  json[QStringLiteral("matches")] = QJsonArray{file1};

  const vnotex::SearchResult result =
      vnotex::SearchResult::fromContentSearchJson(json, QStringLiteral("nb-1"));

  QCOMPARE(result.m_fileResults.size(), 1);
  const vnotex::SearchFileResult &parsedFile = result.m_fileResults[0];

  // Two distinct line rows, NOT five.
  QCOMPARE(parsedFile.m_lineMatches.size(), 2);
  // ...but the file-level occurrence count still reflects all five matches, so
  // the result badge does not undercount same-line occurrences.
  QCOMPARE(parsedFile.m_matchCount, 5);

  const vnotex::SearchLineMatch &line7 = parsedFile.m_lineMatches[0];
  QCOMPARE(line7.m_lineNumber, 7);
  QCOMPARE(line7.m_lineText, QStringLiteral("foo foo foo bar"));
  QCOMPARE(line7.m_segments.size(), 3);
  QCOMPARE(line7.m_segments[0].m_columnStart, 2);
  QCOMPARE(line7.m_segments[0].m_columnEnd, 5);
  QCOMPARE(line7.m_segments[1].m_columnStart, 6);
  QCOMPARE(line7.m_segments[1].m_columnEnd, 9);
  QCOMPARE(line7.m_segments[2].m_columnStart, 10);
  QCOMPARE(line7.m_segments[2].m_columnEnd, 13);

  const vnotex::SearchLineMatch &line12 = parsedFile.m_lineMatches[1];
  QCOMPARE(line12.m_lineNumber, 12);
  QCOMPARE(line12.m_lineText, QStringLiteral("baz baz"));
  QCOMPARE(line12.m_segments.size(), 2);
  QCOMPARE(line12.m_segments[0].m_columnStart, 0);
  QCOMPARE(line12.m_segments[0].m_columnEnd, 3);
  QCOMPARE(line12.m_segments[1].m_columnStart, 4);
  QCOMPARE(line12.m_segments[1].m_columnEnd, 7);
}

// Grouping must key on lineNumber and NOT assume same-line matches arrive
// adjacently: interleaved occurrences (line 7, line 12, line 7) must still
// collapse to one row per line with every segment retained, in first-seen
// line order.
void TestSearchResultTypes::testFromContentSearchJsonGroupsNonAdjacent() {
  auto makeMatch = [](int p_line, int p_colStart, int p_colEnd) {
    QJsonObject o;
    o[QStringLiteral("lineNumber")] = p_line;
    o[QStringLiteral("columnStart")] = p_colStart;
    o[QStringLiteral("columnEnd")] = p_colEnd;
    o[QStringLiteral("lineText")] = QStringLiteral("text");
    return o;
  };

  QJsonArray lineMatches;
  lineMatches.append(makeMatch(7, 2, 5));
  lineMatches.append(makeMatch(12, 0, 3));
  lineMatches.append(makeMatch(7, 6, 9));

  QJsonObject file1;
  file1[QStringLiteral("path")] = QStringLiteral("notes/a.md");
  file1[QStringLiteral("id")] = QStringLiteral("file-guid-1");
  file1[QStringLiteral("matches")] = lineMatches;

  QJsonObject json;
  json[QStringLiteral("matches")] = QJsonArray{file1};

  const vnotex::SearchResult result =
      vnotex::SearchResult::fromContentSearchJson(json, QStringLiteral("nb-1"));

  QCOMPARE(result.m_fileResults.size(), 1);
  const vnotex::SearchFileResult &parsedFile = result.m_fileResults[0];
  QCOMPARE(parsedFile.m_lineMatches.size(), 2);

  // First-seen order: line 7 first, then line 12.
  QCOMPARE(parsedFile.m_lineMatches[0].m_lineNumber, 7);
  QCOMPARE(parsedFile.m_lineMatches[0].m_segments.size(), 2);
  QCOMPARE(parsedFile.m_lineMatches[0].m_segments[0].m_columnStart, 2);
  QCOMPARE(parsedFile.m_lineMatches[0].m_segments[1].m_columnStart, 6);

  QCOMPARE(parsedFile.m_lineMatches[1].m_lineNumber, 12);
  QCOMPARE(parsedFile.m_lineMatches[1].m_segments.size(), 1);
  QCOMPARE(parsedFile.m_lineMatches[1].m_segments[0].m_columnStart, 0);
}

void TestSearchResultTypes::testFromFileSearchJsonValid() {
  QJsonObject fileItem;
  fileItem[QStringLiteral("type")] = QStringLiteral("file");
  fileItem[QStringLiteral("path")] = QStringLiteral("notes/a.md");
  fileItem[QStringLiteral("absolute_path")] = QStringLiteral("/abs/notes/a.md");
  fileItem[QStringLiteral("id")] = QStringLiteral("file-guid-1");

  QJsonObject folderItem;
  folderItem[QStringLiteral("type")] = QStringLiteral("folder");
  folderItem[QStringLiteral("path")] = QStringLiteral("notes/sub");
  folderItem[QStringLiteral("absolute_path")] = QStringLiteral("/abs/notes/sub");
  folderItem[QStringLiteral("id")] = QStringLiteral("folder-guid-1");

  QJsonObject json;
  json[QStringLiteral("matchCount")] = 2;
  json[QStringLiteral("truncated")] = false;
  json[QStringLiteral("matches")] = QJsonArray{fileItem, folderItem};

  const vnotex::SearchResult result =
      vnotex::SearchResult::fromFileSearchJson(json, QStringLiteral("nb-1"));

  QCOMPARE(result.m_matchCount, 2);
  QCOMPARE(result.m_truncated, false);
  QCOMPARE(result.m_fileResults.size(), 2);

  const vnotex::SearchFileResult &parsedFile = result.m_fileResults[0];
  QCOMPARE(parsedFile.m_type, vnotex::SearchResultType::File);
  QCOMPARE(parsedFile.m_path, QStringLiteral("notes/a.md"));
  QCOMPARE(parsedFile.m_absolutePath, QStringLiteral("/abs/notes/a.md"));
  QCOMPARE(parsedFile.m_id, QStringLiteral("file-guid-1"));

  const vnotex::SearchFileResult &parsedFolder = result.m_fileResults[1];
  QCOMPARE(parsedFolder.m_type, vnotex::SearchResultType::Folder);
  QCOMPARE(parsedFolder.m_path, QStringLiteral("notes/sub"));
  QCOMPARE(parsedFolder.m_absolutePath, QStringLiteral("/abs/notes/sub"));
  QCOMPARE(parsedFolder.m_id, QStringLiteral("folder-guid-1"));
}

void TestSearchResultTypes::testNotebookIdStamped() {
  QJsonObject fileItem;
  fileItem[QStringLiteral("path")] = QStringLiteral("notes/a.md");
  fileItem[QStringLiteral("id")] = QStringLiteral("file-guid-1");
  fileItem[QStringLiteral("matches")] = QJsonArray();

  QJsonObject json;
  json[QStringLiteral("matches")] = QJsonArray{fileItem};

  const vnotex::SearchResult contentResult =
      vnotex::SearchResult::fromContentSearchJson(json, QStringLiteral("notebook-guid"));
  QCOMPARE(contentResult.m_fileResults.size(), 1);
  QCOMPARE(contentResult.m_fileResults[0].m_notebookId, QStringLiteral("notebook-guid"));

  QJsonObject fileSearchItem;
  fileSearchItem[QStringLiteral("type")] = QStringLiteral("file");
  fileSearchItem[QStringLiteral("path")] = QStringLiteral("notes/b.md");
  fileSearchItem[QStringLiteral("absolute_path")] = QStringLiteral("/abs/notes/b.md");
  fileSearchItem[QStringLiteral("id")] = QStringLiteral("file-guid-2");

  QJsonObject fileSearchJson;
  fileSearchJson[QStringLiteral("matches")] = QJsonArray{fileSearchItem};

  const vnotex::SearchResult fileResult =
      vnotex::SearchResult::fromFileSearchJson(fileSearchJson, QStringLiteral("notebook-guid"));
  QCOMPARE(fileResult.m_fileResults.size(), 1);
  QCOMPARE(fileResult.m_fileResults[0].m_notebookId, QStringLiteral("notebook-guid"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchResultTypes)
#include "test_searchresulttypes.moc"
