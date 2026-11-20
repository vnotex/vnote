#include <QtTest>

#include <core/searchresulttypes.h>

using namespace vnotex;

namespace tests {

namespace {
VxCoreUtf8View utf8View(const QByteArray &p_bytes) {
  return {p_bytes.constData(), static_cast<size_t>(p_bytes.size())};
}
} // namespace

class TestSearchResultTypes : public QObject {
  Q_OBJECT

private slots:
  void testContentViewOwnsUngroupedUnicode();
  void testGroupsMatchesByLine();
  void testGroupsNonAdjacentOccurrences();
  void testFileViewPreservesAllFields();
};

void TestSearchResultTypes::testContentViewOwnsUngroupedUnicode() {
  SearchContentResult content;
  QString text = QStringLiteral("\u4f60\u597d \U0001f600 foo foo");
  text.append(QChar(0));
  text.append(QStringLiteral("suffix"));
  {
    QByteArray path = QByteArrayLiteral("notes/a.md");
    QByteArray id = QByteArrayLiteral("file-id");
    QByteArray line = text.toUtf8();
    const VxCoreSearchOccurrenceView matches[] = {{7, 6, 9, utf8View(line)},
                                                  {7, 10, 13, utf8View(line)}};
    const VxCoreContentSearchFileView file{utf8View(path), utf8View(id), matches, 2};
    const VxCoreContentSearchView view{&file, 1, 3, 1};
    content = SearchContentResult::fromView(view);
    // Mutation and destruction of every borrowed buffer must not affect the Qt snapshot.
    path.fill('x');
    id.fill('x');
    line.fill('x');
  }
  QCOMPARE(content.m_files.size(), 1);
  QCOMPARE(content.m_files.first().m_path, QStringLiteral("notes/a.md"));
  QCOMPARE(content.m_files.first().m_id, QStringLiteral("file-id"));
  QCOMPARE(content.m_files.first().m_matches.size(), 2);
  QCOMPARE(content.m_files.first().m_matches.first().m_lineText, text);
  QCOMPARE(content.m_files.first().m_matches.first().m_columnStart, 6);
  QCOMPARE(content.m_files.first().m_matches.first().m_columnEnd, 9);
  QCOMPARE(content.m_encryptedSkippedCount, 3);
  QVERIFY(content.m_truncated);

  const auto grouped = SearchResult::fromContentSearchResult(content, QStringLiteral("nb-1"));
  QCOMPARE(grouped.m_matchCount, 1); // Matched files, not occurrences.
  QCOMPARE(grouped.m_encryptedSkippedCount, 3);
  QVERIFY(grouped.m_truncated);
  const auto &file = grouped.m_fileResults.first();
  QCOMPARE(file.m_notebookId, QStringLiteral("nb-1"));
  QCOMPARE(file.m_matchCount, 2);
  QCOMPARE(file.m_lineMatches.size(), 1);
  QCOMPARE(file.m_lineMatches.first().m_lineText, text);
  QCOMPARE(file.m_lineMatches.first().m_segments.size(), 2);
  QVERIFY(!file.m_replacementSupported); // Conversion never grants replacement authority.
  QCOMPARE(content.m_files.first().m_matches.size(), 2); // Grouping is a separate projection.
}

void TestSearchResultTypes::testGroupsMatchesByLine() {
  SearchContentResult content;
  content.m_files.append({QStringLiteral("notes/a.md"),
                          QStringLiteral("file-guid-1"),
                          {{7, 2, 5, QStringLiteral("foo foo foo bar")},
                           {7, 6, 9, QStringLiteral("foo foo foo bar")},
                           {7, 10, 13, QStringLiteral("foo foo foo bar")},
                           {12, 0, 3, QStringLiteral("baz baz")},
                           {12, 4, 7, QStringLiteral("baz baz")}}});
  const auto result = SearchResult::fromContentSearchResult(content, QStringLiteral("nb-1"));
  QCOMPARE(result.m_matchCount, 1);
  QCOMPARE(result.m_fileResults.size(), 1);
  const auto &file = result.m_fileResults.first();
  QCOMPARE(file.m_lineMatches.size(), 2);
  QCOMPARE(file.m_matchCount, 5);
  const auto &line7 = file.m_lineMatches[0];
  QCOMPARE(line7.m_lineNumber, 7);
  QCOMPARE(line7.m_lineText, QStringLiteral("foo foo foo bar"));
  QCOMPARE(line7.m_segments.size(), 3);
  QCOMPARE(line7.m_segments[0].m_columnStart, 2);
  QCOMPARE(line7.m_segments[0].m_columnEnd, 5);
  QCOMPARE(line7.m_segments[1].m_columnStart, 6);
  QCOMPARE(line7.m_segments[1].m_columnEnd, 9);
  QCOMPARE(line7.m_segments[2].m_columnStart, 10);
  QCOMPARE(line7.m_segments[2].m_columnEnd, 13);
  const auto &line12 = file.m_lineMatches[1];
  QCOMPARE(line12.m_lineNumber, 12);
  QCOMPARE(line12.m_lineText, QStringLiteral("baz baz"));
  QCOMPARE(line12.m_segments.size(), 2);
  QCOMPARE(line12.m_segments[0].m_columnStart, 0);
  QCOMPARE(line12.m_segments[0].m_columnEnd, 3);
  QCOMPARE(line12.m_segments[1].m_columnStart, 4);
  QCOMPARE(line12.m_segments[1].m_columnEnd, 7);
}

void TestSearchResultTypes::testGroupsNonAdjacentOccurrences() {
  SearchContentResult content;
  content.m_files.append({QStringLiteral("notes/a.md"),
                          QStringLiteral("file-guid-1"),
                          {{7, 2, 5, QStringLiteral("text")},
                           {12, 0, 3, QStringLiteral("text")},
                           {7, 6, 9, QStringLiteral("text")}}});
  const auto result = SearchResult::fromContentSearchResult(content, QStringLiteral("nb-1"));
  const auto &file = result.m_fileResults.first();
  QCOMPARE(file.m_matchCount, 3);
  QCOMPARE(file.m_lineMatches.size(), 2);
  QCOMPARE(file.m_lineMatches[0].m_lineNumber, 7);
  QCOMPARE(file.m_lineMatches[0].m_segments.size(), 2);
  QCOMPARE(file.m_lineMatches[0].m_segments[0].m_columnStart, 2);
  QCOMPARE(file.m_lineMatches[0].m_segments[1].m_columnStart, 6);
  QCOMPARE(file.m_lineMatches[1].m_lineNumber, 12);
  QCOMPARE(file.m_lineMatches[1].m_segments.size(), 1);
  QCOMPARE(file.m_lineMatches[1].m_segments[0].m_columnStart, 0);
}

void TestSearchResultTypes::testFileViewPreservesAllFields() {
  SearchResult result;
  {
    const QByteArray path = QByteArrayLiteral("notes/a.md");
    const QByteArray absolute = QByteArrayLiteral("/abs/notes/a.md");
    const QByteArray id = QByteArrayLiteral("file-guid-1");
    QByteArray tag = QStringLiteral("\u6807\u7b7e").toUtf8();
    const VxCoreUtf8View tags[] = {utf8View(tag)};
    const VxCoreFileSearchEntryView files[] = {{utf8View(path), utf8View(absolute), utf8View(id),
                                                1234567890123LL, 2345678901234LL, tags, 1, 0},
                                               {{"notes/sub", 9},
                                                {"/abs/notes/sub", sizeof("/abs/notes/sub") - 1},
                                                {"folder-guid", 11},
                                                10,
                                                20,
                                                nullptr,
                                                0,
                                                1}};
    result = SearchResult::fromFileSearchView({files, 2, 1}, QStringLiteral("notebook-guid"));
    tag.fill('x');
  }
  QCOMPARE(result.m_matchCount, 2);
  QVERIFY(result.m_truncated);
  QCOMPARE(result.m_fileResults.size(), 2);
  const auto &file = result.m_fileResults[0];
  QCOMPARE(file.m_type, SearchResultType::File);
  QCOMPARE(file.m_path, QStringLiteral("notes/a.md"));
  QCOMPARE(file.m_absolutePath, QStringLiteral("/abs/notes/a.md"));
  QCOMPARE(file.m_id, QStringLiteral("file-guid-1"));
  QCOMPARE(file.m_notebookId, QStringLiteral("notebook-guid"));
  QCOMPARE(file.m_tags, QStringList{QStringLiteral("\u6807\u7b7e")});
  QCOMPARE(file.m_createdUtc, 1234567890123LL);
  QCOMPARE(file.m_modifiedUtc, 2345678901234LL);
  QVERIFY(file.m_lineMatches.isEmpty());
  QVERIFY(!file.m_replacementSupported);
  const auto &folder = result.m_fileResults[1];
  QCOMPARE(folder.m_type, SearchResultType::Folder);
  QCOMPARE(folder.m_path, QStringLiteral("notes/sub"));
  QCOMPARE(folder.m_absolutePath, QStringLiteral("/abs/notes/sub"));
  QCOMPARE(folder.m_id, QStringLiteral("folder-guid"));
  QCOMPARE(folder.m_notebookId, QStringLiteral("notebook-guid"));
  QVERIFY(folder.m_tags.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSearchResultTypes)
#include "test_searchresulttypes.moc"
