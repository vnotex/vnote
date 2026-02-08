// test_pathutils.cpp - Comprehensive tests for vnotex::PathUtils class
#include <QtTest>

#include <utils/pathutils.h>

using namespace vnotex;

namespace tests {

class TestPathUtils : public QObject {
  Q_OBJECT

private slots:
  // cleanPath tests
  void testCleanPath_data();
  void testCleanPath();

  // concatenateFilePath tests
  void testConcatenateFilePath_data();
  void testConcatenateFilePath();

  // fileName tests
  void testFileName_data();
  void testFileName();

  // fileNameCheap tests
  void testFileNameCheap_data();
  void testFileNameCheap();

  // normalizePath tests
  void testNormalizePath();

  // areSamePaths tests
  void testAreSamePaths_data();
  void testAreSamePaths();

  // pathContains tests
  void testPathContains_data();
  void testPathContains();

  // isLegalFileName tests
  void testIsLegalFileName_data();
  void testIsLegalFileName();

  // encodeSpacesInPath tests
  void testEncodeSpacesInPath_data();
  void testEncodeSpacesInPath();

  // removeUrlParameters tests
  void testRemoveUrlParameters_data();
  void testRemoveUrlParameters();

  // isLocalFile tests
  void testIsLocalFile_data();
  void testIsLocalFile();
};

// =============================================================================
// cleanPath tests
// =============================================================================
void TestPathUtils::testCleanPath_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  QTest::newRow("simple") << "a/b/c" << "a/b/c";
  QTest::newRow("trailing_slash") << "a/b/c/" << "a/b/c";
  QTest::newRow("double_slash") << "a//b//c" << "a/b/c";
  QTest::newRow("dot") << "a/./b" << "a/b";
  QTest::newRow("dotdot") << "a/b/../c" << "a/c";
  QTest::newRow("mixed") << "a//b/./c/../d" << "a/b/d";
#if defined(Q_OS_WIN)
  QTest::newRow("win_backslash") << "a\\b\\c" << "a/b/c";
  QTest::newRow("win_mixed") << "a\\b/c" << "a/b/c";
#endif
}

void TestPathUtils::testCleanPath() {
  QFETCH(QString, input);
  QFETCH(QString, expected);

  QCOMPARE(PathUtils::cleanPath(input), expected);
}

// =============================================================================
// concatenateFilePath tests
// =============================================================================
void TestPathUtils::testConcatenateFilePath_data() {
  QTest::addColumn<QString>("dirPath");
  QTest::addColumn<QString>("name");
  QTest::addColumn<QString>("expected");

  QTest::newRow("simple") << "a/b" << "c.txt" << "a/b/c.txt";
  QTest::newRow("trailing_slash") << "a/b/" << "c.txt" << "a/b/c.txt";
  QTest::newRow("empty_name") << "a/b" << "" << "a/b";
  QTest::newRow("empty_dir") << "" << "c.txt" << "c.txt";
  QTest::newRow("both_empty") << "" << "" << "";
  QTest::newRow("nested_name") << "a" << "b/c.txt" << "a/b/c.txt";
}

void TestPathUtils::testConcatenateFilePath() {
  QFETCH(QString, dirPath);
  QFETCH(QString, name);
  QFETCH(QString, expected);

  QCOMPARE(PathUtils::concatenateFilePath(dirPath, name), expected);
}

// =============================================================================
// fileName tests
// =============================================================================
void TestPathUtils::testFileName_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("expected");

  QTest::newRow("simple_file") << "/a/b/c.txt" << "c.txt";
  QTest::newRow("no_extension") << "/a/b/c" << "c";
  QTest::newRow("only_file") << "c.txt" << "c.txt";
  QTest::newRow("hidden_file") << "/a/.hidden" << ".hidden";
  QTest::newRow("trailing_slash") << "/a/b/" << "";
#if defined(Q_OS_WIN)
  QTest::newRow("win_path") << "C:\\Users\\test\\file.txt" << "file.txt";
#endif
}

void TestPathUtils::testFileName() {
  QFETCH(QString, path);
  QFETCH(QString, expected);

  QCOMPARE(PathUtils::fileName(path), expected);
}

// =============================================================================
// fileNameCheap tests
// =============================================================================
void TestPathUtils::testFileNameCheap_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("expected");

  QTest::newRow("unix_path") << "/a/b/c.txt" << "c.txt";
  QTest::newRow("no_slash") << "file.txt" << "file.txt";
  QTest::newRow("win_path") << "C:\\Users\\file.txt" << "file.txt";
  QTest::newRow("mixed_path") << "a/b\\c.txt" << "c.txt";
}

void TestPathUtils::testFileNameCheap() {
  QFETCH(QString, path);
  QFETCH(QString, expected);

  QCOMPARE(PathUtils::fileNameCheap(path), expected);
}

// =============================================================================
// normalizePath tests
// =============================================================================
void TestPathUtils::testNormalizePath() {
  // normalizePath uses absolute path and on Windows lowercases
  // We can only test general behavior
  QString path = QDir::tempPath() + "/TestDir";
  QString normalized = PathUtils::normalizePath(path);

  // Should be an absolute path
  QVERIFY(QFileInfo(normalized).isAbsolute());

#if defined(Q_OS_WIN)
  // On Windows, should be lowercase
  QCOMPARE(normalized, normalized.toLower());
#endif
}

// =============================================================================
// areSamePaths tests
// =============================================================================
void TestPathUtils::testAreSamePaths_data() {
  QTest::addColumn<QString>("pathA");
  QTest::addColumn<QString>("pathB");
  QTest::addColumn<bool>("expected");

  // These tests need to work with temp paths for consistent results
  QString tempDir = QDir::tempPath();

  QTest::newRow("identical") << tempDir << tempDir << true;
  QTest::newRow("with_trailing_slash") << tempDir << (tempDir + "/") << true;
  QTest::newRow("different") << tempDir << (tempDir + "/subdir") << false;

#if defined(Q_OS_WIN)
  // On Windows, paths are case-insensitive
  QTest::newRow("win_case_insensitive") << "C:/temp" << "C:/TEMP" << true;
#endif
}

void TestPathUtils::testAreSamePaths() {
  QFETCH(QString, pathA);
  QFETCH(QString, pathB);
  QFETCH(bool, expected);

  QCOMPARE(PathUtils::areSamePaths(pathA, pathB), expected);
}

// =============================================================================
// pathContains tests
// =============================================================================
void TestPathUtils::testPathContains_data() {
  QTest::addColumn<QString>("dir");
  QTest::addColumn<QString>("path");
  QTest::addColumn<bool>("expected");

  QString tempDir = QDir::tempPath();

  QTest::newRow("direct_child") << tempDir << (tempDir + "/file.txt") << true;
  QTest::newRow("nested_child") << tempDir << (tempDir + "/a/b/file.txt") << true;
  QTest::newRow("same_path") << tempDir << tempDir << true;
  QTest::newRow("outside") << (tempDir + "/a") << (tempDir + "/b") << false;
  QTest::newRow("parent") << (tempDir + "/a/b") << tempDir << false;
}

void TestPathUtils::testPathContains() {
  QFETCH(QString, dir);
  QFETCH(QString, path);
  QFETCH(bool, expected);

  QCOMPARE(PathUtils::pathContains(dir, path), expected);
}

// =============================================================================
// isLegalFileName tests
// =============================================================================
void TestPathUtils::testIsLegalFileName_data() {
  QTest::addColumn<QString>("name");
  QTest::addColumn<bool>("expected");

  // Legal names
  QTest::newRow("simple") << "file.txt" << true;
  QTest::newRow("with_spaces") << "my file.txt" << true;
  QTest::newRow("with_dash") << "my-file.txt" << true;
  QTest::newRow("with_underscore") << "my_file.txt" << true;
  QTest::newRow("unicode") << "文档.txt" << true;

  // Illegal names - contain forbidden characters
  QTest::newRow("slash") << "a/b" << false;
  QTest::newRow("backslash") << "a\\b" << false;
  QTest::newRow("colon") << "a:b" << false;
  QTest::newRow("asterisk") << "a*b" << false;
  QTest::newRow("question") << "a?b" << false;
  QTest::newRow("quote") << "a\"b" << false;
  QTest::newRow("less_than") << "a<b" << false;
  QTest::newRow("greater_than") << "a>b" << false;
  QTest::newRow("pipe") << "a|b" << false;
  QTest::newRow("tab") << "a\tb" << false;
  QTest::newRow("newline") << "a\nb" << false;
}

void TestPathUtils::testIsLegalFileName() {
  QFETCH(QString, name);
  QFETCH(bool, expected);

  QCOMPARE(PathUtils::isLegalFileName(name), expected);
}

// =============================================================================
// encodeSpacesInPath tests
// =============================================================================
void TestPathUtils::testEncodeSpacesInPath_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("expected");

  QTest::newRow("no_spaces") << "a/b/c.txt" << "a/b/c.txt";
  QTest::newRow("single_space") << "a/b c/d.txt" << "a/b%20c/d.txt";
  QTest::newRow("multiple_spaces") << "a b/c d/e f.txt" << "a%20b/c%20d/e%20f.txt";
  QTest::newRow("all_spaces") << "   " << "%20%20%20";
}

void TestPathUtils::testEncodeSpacesInPath() {
  QFETCH(QString, path);
  QFETCH(QString, expected);

  QCOMPARE(PathUtils::encodeSpacesInPath(path), expected);
}

// =============================================================================
// removeUrlParameters tests
// =============================================================================
void TestPathUtils::testRemoveUrlParameters_data() {
  QTest::addColumn<QString>("url");
  QTest::addColumn<QString>("expected");

  QTest::newRow("no_params") << "https://example.com/file.txt" << "https://example.com/file.txt";
  QTest::newRow("with_params") << "https://example.com/file.txt?foo=bar" << "https://example.com/file.txt";
  QTest::newRow("multiple_params") << "https://example.com/file.txt?a=1&b=2" << "https://example.com/file.txt";
  QTest::newRow("local_path") << "/home/user/file.txt" << "/home/user/file.txt";
  QTest::newRow("local_with_query") << "/home/user/file.txt?v=1" << "/home/user/file.txt";
}

void TestPathUtils::testRemoveUrlParameters() {
  QFETCH(QString, url);
  QFETCH(QString, expected);

  QCOMPARE(PathUtils::removeUrlParameters(url), expected);
}

// =============================================================================
// isLocalFile tests
// =============================================================================
void TestPathUtils::testIsLocalFile_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<bool>("expected");

  // Local files
  QTest::newRow("empty") << "" << true;
  QTest::newRow("relative") << "file.txt" << true;
  QTest::newRow("absolute_unix") << "/home/user/file.txt" << true;
#if defined(Q_OS_WIN)
  QTest::newRow("absolute_win") << "C:\\Users\\file.txt" << true;
#endif

  // Remote URLs
  QTest::newRow("http") << "http://example.com/file.txt" << false;
  QTest::newRow("https") << "https://example.com/file.txt" << false;
  QTest::newRow("ftp") << "ftp://example.com/file.txt" << false;
}

void TestPathUtils::testIsLocalFile() {
  QFETCH(QString, path);
  QFETCH(bool, expected);

  QCOMPARE(PathUtils::isLocalFile(path), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPathUtils)
#include "test_pathutils.moc"
