// test_fileutils2.cpp - Tests for vnotex::FileUtils2 class
#include <QtTest>

#include <QDir>
#include <QFile>

#include <temp_dir_fixture.h>
#include <utils/fileutils2.h>

using namespace vnotex;

namespace tests {

class TestFileUtils2 : public QObject {
  Q_OBJECT

private slots:
  // copyFile tests
  void testCopyFile_basic();
  void testCopyFile_overwriteExisting();
  void testCopyFile_moveBasic();
  void testCopyFile_moveOverwrite();
  void testCopyFile_samePath();
  void testCopyFile_createsParentDir();

  // copyDir tests
  void testCopyDir_basic();
  void testCopyDir_mergeDirectories();
  void testCopyDir_mergeOverwritesFiles();
  void testCopyDir_mergeNestedDirs();
  void testCopyDir_moveBasic();
  void testCopyDir_moveMerge();
  void testCopyDir_samePath();
};

// =============================================================================
// Helper to read file content
// =============================================================================
static QString readFileContent(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

// =============================================================================
// copyFile tests
// =============================================================================
void TestFileUtils2::testCopyFile_basic() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("source.txt", "hello world");
  QString destPath = tmp.filePath("dest.txt");

  Error err = FileUtils2::copyFile(srcPath, destPath);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  QCOMPARE(readFileContent(destPath), QString("hello world"));
  // Source should still exist (copy, not move)
  QVERIFY(QFile::exists(srcPath));
}

void TestFileUtils2::testCopyFile_overwriteExisting() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("source.txt", "new content");
  QString destPath = tmp.createTextFile("dest.txt", "old content");

  // Verify dest has old content
  QCOMPARE(readFileContent(destPath), QString("old content"));

  Error err = FileUtils2::copyFile(srcPath, destPath);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  // Dest should now have new content
  QCOMPARE(readFileContent(destPath), QString("new content"));
}

void TestFileUtils2::testCopyFile_moveBasic() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("source.txt", "move me");
  QString destPath = tmp.filePath("dest.txt");

  Error err = FileUtils2::copyFile(srcPath, destPath, true /* move */);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  QCOMPARE(readFileContent(destPath), QString("move me"));
  // Source should be gone after move
  QVERIFY(!QFile::exists(srcPath));
}

void TestFileUtils2::testCopyFile_moveOverwrite() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("source.txt", "new content");
  QString destPath = tmp.createTextFile("dest.txt", "old content");

  Error err = FileUtils2::copyFile(srcPath, destPath, true /* move */);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  QCOMPARE(readFileContent(destPath), QString("new content"));
  // Source should be gone after move
  QVERIFY(!QFile::exists(srcPath));
}

void TestFileUtils2::testCopyFile_samePath() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("file.txt", "content");

  // Copying to same path should succeed (no-op)
  Error err = FileUtils2::copyFile(srcPath, srcPath);
  QVERIFY(!err);
  QVERIFY(QFile::exists(srcPath));
  QCOMPARE(readFileContent(srcPath), QString("content"));
}

void TestFileUtils2::testCopyFile_createsParentDir() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("source.txt", "content");
  QString destPath = tmp.filePath("subdir/nested/dest.txt");

  // Parent directories don't exist yet
  QVERIFY(!QDir(tmp.filePath("subdir")).exists());

  Error err = FileUtils2::copyFile(srcPath, destPath);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  QCOMPARE(readFileContent(destPath), QString("content"));
}

// =============================================================================
// copyDir tests
// =============================================================================
void TestFileUtils2::testCopyDir_basic() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source directory with files
  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file1.txt", "content1");
  tmp.createTextFile("src/file2.txt", "content2");

  QString destDir = tmp.filePath("dest");

  Error err = FileUtils2::copyDir(srcDir, destDir);
  QVERIFY(!err);
  QVERIFY(QDir(destDir).exists());
  QVERIFY(QFile::exists(destDir + "/file1.txt"));
  QVERIFY(QFile::exists(destDir + "/file2.txt"));
  QCOMPARE(readFileContent(destDir + "/file1.txt"), QString("content1"));
  QCOMPARE(readFileContent(destDir + "/file2.txt"), QString("content2"));
  // Source should still exist
  QVERIFY(QDir(srcDir).exists());
}

void TestFileUtils2::testCopyDir_mergeDirectories() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source directory
  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/new_file.txt", "from source");

  // Create destination directory with existing content
  QString destDir = tmp.createDir("dest");
  tmp.createTextFile("dest/existing.txt", "existing content");

  Error err = FileUtils2::copyDir(srcDir, destDir);
  QVERIFY(!err);

  // Both files should exist in dest
  QVERIFY(QFile::exists(destDir + "/existing.txt"));
  QVERIFY(QFile::exists(destDir + "/new_file.txt"));
  QCOMPARE(readFileContent(destDir + "/existing.txt"), QString("existing content"));
  QCOMPARE(readFileContent(destDir + "/new_file.txt"), QString("from source"));
}

void TestFileUtils2::testCopyDir_mergeOverwritesFiles() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source directory
  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/common.txt", "new version");

  // Create destination directory with same filename
  QString destDir = tmp.createDir("dest");
  tmp.createTextFile("dest/common.txt", "old version");

  QCOMPARE(readFileContent(destDir + "/common.txt"), QString("old version"));

  Error err = FileUtils2::copyDir(srcDir, destDir);
  QVERIFY(!err);

  // File should be overwritten
  QCOMPARE(readFileContent(destDir + "/common.txt"), QString("new version"));
}

void TestFileUtils2::testCopyDir_mergeNestedDirs() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source with nested structure
  QString srcDir = tmp.createDir("src");
  tmp.createDir("src/subdir");
  tmp.createTextFile("src/subdir/src_file.txt", "from src");

  // Create destination with overlapping nested structure
  QString destDir = tmp.createDir("dest");
  tmp.createDir("dest/subdir");
  tmp.createTextFile("dest/subdir/dest_file.txt", "from dest");

  Error err = FileUtils2::copyDir(srcDir, destDir);
  QVERIFY(!err);

  // Both files should exist in merged subdir
  QVERIFY(QFile::exists(destDir + "/subdir/src_file.txt"));
  QVERIFY(QFile::exists(destDir + "/subdir/dest_file.txt"));
  QCOMPARE(readFileContent(destDir + "/subdir/src_file.txt"), QString("from src"));
  QCOMPARE(readFileContent(destDir + "/subdir/dest_file.txt"), QString("from dest"));
}

void TestFileUtils2::testCopyDir_moveBasic() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source directory
  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "content");

  QString destDir = tmp.filePath("dest");

  Error err = FileUtils2::copyDir(srcDir, destDir, true /* move */);
  QVERIFY(!err);

  QVERIFY(QDir(destDir).exists());
  QVERIFY(QFile::exists(destDir + "/file.txt"));
  QCOMPARE(readFileContent(destDir + "/file.txt"), QString("content"));
  // Source should be removed after move
  QVERIFY(!QDir(srcDir).exists());
}

void TestFileUtils2::testCopyDir_moveMerge() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source directory
  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/src_file.txt", "from src");

  // Create destination directory with existing content
  QString destDir = tmp.createDir("dest");
  tmp.createTextFile("dest/dest_file.txt", "from dest");

  Error err = FileUtils2::copyDir(srcDir, destDir, true /* move */);
  QVERIFY(!err);

  // Both files should exist
  QVERIFY(QFile::exists(destDir + "/src_file.txt"));
  QVERIFY(QFile::exists(destDir + "/dest_file.txt"));
  // Source should be removed
  QVERIFY(!QDir(srcDir).exists());
}

void TestFileUtils2::testCopyDir_samePath() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("dir");
  tmp.createTextFile("dir/file.txt", "content");

  // Copying to same path should succeed (no-op)
  Error err = FileUtils2::copyDir(srcDir, srcDir);
  QVERIFY(!err);
  QVERIFY(QDir(srcDir).exists());
  QCOMPARE(readFileContent(srcDir + "/file.txt"), QString("content"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileUtils2)
#include "test_fileutils2.moc"
