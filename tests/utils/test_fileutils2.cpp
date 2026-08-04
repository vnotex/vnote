// test_fileutils2.cpp - Tests for vnotex::FileUtils2 class
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

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
  void testCopyFile_overwriteReadOnlyExisting();
  void testCopyFile_readOnlySourceProducesWritableDest();
  void testCopyFile_moveBasic();
  void testCopyFile_moveOverwrite();
  void testCopyFile_samePath();
  void testCopyFile_createsParentDir();

  // copyDir tests
  void testCopyDir_basic();
  void testCopyDir_mergeDirectories();
  void testCopyDir_mergeOverwritesFiles();
  void testCopyDir_mergeOverwritesReadOnlyFiles();
  void testCopyDir_mergeNestedDirs();
  void testCopyDir_moveBasic();
  void testCopyDir_moveMerge();
  void testCopyDir_samePath();

  // copyDirCollectingErrors tests
  void testCopyDirCollectingErrors_continuesPastFailingNode();
  void testCopyDirCollectingErrors_reportsFirstErrorButEveryFailedPath();
  void testCopyDirCollectingErrors_missingSourceIsAnError();
  void testCopyDirCollectingErrors_nonDirectorySourceIsAnError();
  void testCopyDirCollectingErrors_preservesExistingSkippedPath();
  void testCopyDirCollectingErrors_skipMatchIsCaseInsensitive();
  void testCopyDirCollectingErrors_copiesSkippedPathWhenAbsent();

  // installVersionedDir tests
  void testInstallVersionedDir_writesStampOnSuccess();
  void testInstallVersionedDir_matchingStampIsANoOp();
  void testInstallVersionedDir_differentVersionRecopies();
  void testInstallVersionedDir_failureWritesNoStampAndHealsOnRetry();
  void testInstallVersionedDir_missingSourceCreatesNothing();
  void testInstallVersionedDir_missingSourceLeavesACompletedInstallIntact();
  void testInstallVersionedDir_forceRecopiesDespiteMatchingStamp();
  void testInstallVersionedDir_forceFailsBeforeCopyingWhenStampCannotBeRemoved();
  void testInstallVersionedDir_preservesUserOwnedFile();
  void testInstallVersionedDir_incompatiblePreservedNodeWritesNoStamp();

  // generateRandomFileName tests
  void testGenerateRandomFileNameHex();
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

void TestFileUtils2::testCopyFile_overwriteReadOnlyExisting() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcPath = tmp.createTextFile("source.txt", "new content");
  QString destPath = tmp.createTextFile("dest.txt", "old content");

  // Make the destination read-only, mirroring a file previously dumped from a
  // read-only .rcc resource. On Windows QFile::remove fails on such files.
  QVERIFY(QFile::setPermissions(destPath, QFileDevice::ReadOwner | QFileDevice::ReadUser));

  Error err = FileUtils2::copyFile(srcPath, destPath);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  QCOMPARE(readFileContent(destPath), QString("new content"));

  // The overwritten destination must be writable so future overwrites work.
  QVERIFY(QFile::permissions(destPath) & QFileDevice::WriteOwner);
}

void TestFileUtils2::testCopyFile_readOnlySourceProducesWritableDest() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // A read-only source mirrors a file living inside a read-only .rcc resource.
  // QFile::copy propagates the source permissions, so without the post-copy
  // fix the fresh destination would be read-only too.
  QString srcPath = tmp.createTextFile("source.txt", "resource content");
  QVERIFY(QFile::setPermissions(srcPath, QFileDevice::ReadOwner | QFileDevice::ReadUser));

  QString destPath = tmp.filePath("dest.txt");
  QVERIFY(!QFile::exists(destPath));

  Error err = FileUtils2::copyFile(srcPath, destPath);
  QVERIFY(!err);
  QVERIFY(QFile::exists(destPath));
  QCOMPARE(readFileContent(destPath), QString("resource content"));

  // Even though the source was read-only, the new destination must be writable.
  QVERIFY(QFile::permissions(destPath) & QFileDevice::WriteOwner);
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

void TestFileUtils2::testCopyDir_mergeOverwritesReadOnlyFiles() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create source directory with a nested file.
  QString srcDir = tmp.createDir("src");
  tmp.createDir("src/sub");
  tmp.createTextFile("src/sub/common.txt", "new version");

  // Create destination directory with a same-named nested file, set read-only.
  QString destDir = tmp.createDir("dest");
  tmp.createDir("dest/sub");
  QString destFile = tmp.createTextFile("dest/sub/common.txt", "old version");
  QVERIFY(QFile::setPermissions(destFile, QFileDevice::ReadOwner | QFileDevice::ReadUser));

  Error err = FileUtils2::copyDir(srcDir, destDir);
  QVERIFY(!err);

  // The read-only nested file should be overwritten and left writable.
  QCOMPARE(readFileContent(destFile), QString("new version"));
  QVERIFY(QFile::permissions(destFile) & QFileDevice::WriteOwner);
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

// =============================================================================
// copyDirCollectingErrors tests
// =============================================================================

// Portable failure injection: pre-create a DIRECTORY where a FILE must land.
// copyFile() reaches its QFile::remove(p_destPath) branch, and QFile::remove
// uses file-removal semantics, so it fails on a directory on every platform.
static QString blockDestPathWithADirectory(tests::TempDirFixture &p_tmp, const QString &p_relPath) {
  const QString path = p_tmp.createDir(p_relPath);
  return path;
}

void TestFileUtils2::testCopyDirCollectingErrors_continuesPastFailingNode() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/a.txt", "a");
  tmp.createTextFile("src/b.txt", "b");
  tmp.createTextFile("src/c.txt", "c");

  QString destDir = tmp.createDir("dest");
  blockDestPathWithADirectory(tmp, "dest/b.txt");

  QStringList failed;
  Error err = FileUtils2::copyDirCollectingErrors(srcDir, destDir, &failed);

  // The walk reports a failure...
  QVERIFY(err);
  // ...names exactly the bad SOURCE path...
  QCOMPARE(failed.size(), 1);
  QVERIFY2(failed.at(0).endsWith(QStringLiteral("b.txt")), qPrintable(failed.at(0)));
  // ...and still copied every sibling. copyDir() would have aborted at b.txt
  // and left c.txt behind.
  QCOMPARE(readFileContent(destDir + "/a.txt"), QString("a"));
  QCOMPARE(readFileContent(destDir + "/c.txt"), QString("c"));
}

// Two independent failures: the walk must keep going through BOTH and report
// every failed path, while the returned Error stays the FIRST one.
void TestFileUtils2::testCopyDirCollectingErrors_reportsFirstErrorButEveryFailedPath() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/a.txt", "a");
  tmp.createTextFile("src/b.txt", "b");
  tmp.createTextFile("src/c.txt", "c");
  tmp.createTextFile("src/d.txt", "d");

  QString destDir = tmp.createDir("dest");
  blockDestPathWithADirectory(tmp, "dest/b.txt");
  blockDestPathWithADirectory(tmp, "dest/d.txt");

  QStringList failed;
  Error err = FileUtils2::copyDirCollectingErrors(srcDir, destDir, &failed);

  QVERIFY(err);
  // entryInfoList sorts by name, so b.txt is hit first.
  QCOMPARE(err.code(), ErrorCode::FailToRemoveFile);
  QCOMPARE(failed.size(), 2);
  QVERIFY2(failed.at(0).endsWith(QStringLiteral("b.txt")), qPrintable(failed.at(0)));
  QVERIFY2(failed.at(1).endsWith(QStringLiteral("d.txt")), qPrintable(failed.at(1)));
  // The clean siblings still landed.
  QCOMPARE(readFileContent(destDir + "/a.txt"), QString("a"));
  QCOMPARE(readFileContent(destDir + "/c.txt"), QString("c"));
}

void TestFileUtils2::testCopyDirCollectingErrors_missingSourceIsAnError() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  const QString srcDir = tmp.filePath("does-not-exist");
  const QString destDir = tmp.filePath("dest");

  QStringList failed;
  Error err = FileUtils2::copyDirCollectingErrors(srcDir, destDir, &failed);

  QVERIFY(err);
  QCOMPARE(failed.size(), 1);
  // Nothing may be created: copyDir() would mkpath the destination, enumerate
  // nothing and report success.
  QVERIFY2(!QFileInfo::exists(destDir), "a missing source still created the destination");
}

void TestFileUtils2::testCopyDirCollectingErrors_nonDirectorySourceIsAnError() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  const QString srcFile = tmp.createTextFile("src.txt", "not a dir");
  const QString destDir = tmp.filePath("dest");

  Error err = FileUtils2::copyDirCollectingErrors(srcFile, destDir);

  QVERIFY(err);
  QVERIFY(!QFileInfo::exists(destDir));
}

void TestFileUtils2::testCopyDirCollectingErrors_preservesExistingSkippedPath() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createDir("src/css");
  tmp.createTextFile("src/css/user.css", "bundled stub");
  tmp.createTextFile("src/css/other.css", "bundled other");

  QString destDir = tmp.createDir("dest");
  tmp.createDir("dest/css");
  tmp.createTextFile("dest/css/user.css", "MY CUSTOM CSS");

  const QSet<QString> skip{QStringLiteral("css/user.css")};
  QStringList failed;
  Error err = FileUtils2::copyDirCollectingErrors(srcDir, destDir, &failed, &skip);

  // A preserved file is NOT a failure.
  QVERIFY(!err);
  QVERIFY(failed.isEmpty());
  QCOMPARE(readFileContent(destDir + "/css/user.css"), QString("MY CUSTOM CSS"));
  // Everything else still copies.
  QCOMPARE(readFileContent(destDir + "/css/other.css"), QString("bundled other"));
}

// The preserve list is matched case-insensitively (Windows), so a caller's
// spelling must not decide whether a user's file survives.
void TestFileUtils2::testCopyDirCollectingErrors_skipMatchIsCaseInsensitive() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createDir("src/CSS");
  tmp.createTextFile("src/CSS/User.css", "bundled stub");

  QString destDir = tmp.createDir("dest");
  tmp.createDir("dest/CSS");
  tmp.createTextFile("dest/CSS/User.css", "MY CUSTOM CSS");

  // Neither the key's case nor the on-disk case matches exactly.
  const QSet<QString> skip{QStringLiteral("Css/uSeR.CSS")};
  Error err = FileUtils2::copyDirCollectingErrors(srcDir, destDir, nullptr, &skip);

  QVERIFY(!err);
  QCOMPARE(readFileContent(destDir + "/CSS/User.css"), QString("MY CUSTOM CSS"));
}

void TestFileUtils2::testCopyDirCollectingErrors_copiesSkippedPathWhenAbsent() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createDir("src/css");
  tmp.createTextFile("src/css/user.css", "bundled stub");

  QString destDir = tmp.filePath("dest");

  const QSet<QString> skip{QStringLiteral("css/user.css")};
  Error err = FileUtils2::copyDirCollectingErrors(srcDir, destDir, nullptr, &skip);

  QVERIFY(!err);
  // The skip is "do not OVERWRITE", not "never install".
  QCOMPARE(readFileContent(destDir + "/css/user.css"), QString("bundled stub"));
}

// =============================================================================
// installVersionedDir tests
// =============================================================================
static QString stampPathOf(const QString &p_destDir) {
  return QDir(p_destDir).filePath(QLatin1String(FileUtils2::c_versionStampFileName));
}

void TestFileUtils2::testInstallVersionedDir_writesStampOnSuccess() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "v1 content");
  QString destDir = tmp.filePath("dest");

  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"));
  QVERIFY(!err);
  QCOMPARE(readFileContent(destDir + "/file.txt"), QString("v1 content"));
  QCOMPARE(readFileContent(stampPathOf(destDir)).trimmed(), QString("1.0.0"));
}

void TestFileUtils2::testInstallVersionedDir_matchingStampIsANoOp() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "bundled");
  QString destDir = tmp.filePath("dest");

  QVERIFY(!FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0")));

  // Mutate the installed copy; a no-op second call must not overwrite it.
  QFile out(destDir + "/file.txt");
  QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
  out.write("locally edited");
  out.close();

  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"));
  QVERIFY(!err);
  QCOMPARE(readFileContent(destDir + "/file.txt"), QString("locally edited"));
}

void TestFileUtils2::testInstallVersionedDir_differentVersionRecopies() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "bundled");
  QString destDir = tmp.filePath("dest");

  QVERIFY(!FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0")));

  QFile out(destDir + "/file.txt");
  QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
  out.write("stale");
  out.close();

  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("2.0.0"));
  QVERIFY(!err);
  QCOMPARE(readFileContent(destDir + "/file.txt"), QString("bundled"));
  QCOMPARE(readFileContent(stampPathOf(destDir)).trimmed(), QString("2.0.0"));
}

void TestFileUtils2::testInstallVersionedDir_failureWritesNoStampAndHealsOnRetry() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/a.txt", "a");
  tmp.createTextFile("src/b.txt", "b");

  QString destDir = tmp.createDir("dest");
  const QString blocker = blockDestPathWithADirectory(tmp, "dest/b.txt");

  QStringList failed;
  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), &failed);
  QVERIFY(err);
  QVERIFY(!failed.isEmpty());
  // INVARIANT: a reported failure never leaves a stamp holding this version.
  QVERIFY2(!QFileInfo::exists(stampPathOf(destDir)),
           "a partial install was stamped as complete");

  // Remove the blocker; the next call heals the folder and stamps it.
  QVERIFY(QDir(blocker).removeRecursively());

  failed.clear();
  err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), &failed);
  QVERIFY(!err);
  QVERIFY(failed.isEmpty());
  QCOMPARE(readFileContent(destDir + "/b.txt"), QString("b"));
  QCOMPARE(readFileContent(stampPathOf(destDir)).trimmed(), QString("1.0.0"));
}

void TestFileUtils2::testInstallVersionedDir_missingSourceCreatesNothing() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  const QString srcDir = tmp.filePath("missing");
  const QString destDir = tmp.filePath("dest");

  QStringList failed;
  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), &failed);

  QVERIFY(err);
  QCOMPARE(failed.size(), 1);
  QVERIFY2(!QFileInfo::exists(destDir), "a missing source created the destination");
  QVERIFY(!QFileInfo::exists(stampPathOf(destDir)));
}

// The one failure that happens BEFORE the stamp is invalidated. Destroying a
// good install because the bundle went missing would be strictly worse than
// leaving it alone, so the existing stamp deliberately survives.
void TestFileUtils2::testInstallVersionedDir_missingSourceLeavesACompletedInstallIntact() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "bundled");
  QString destDir = tmp.filePath("dest");

  QVERIFY(!FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0")));

  Error err =
      FileUtils2::installVersionedDir(tmp.filePath("gone"), destDir, QStringLiteral("1.0.0"));
  QVERIFY(err);
  QCOMPARE(readFileContent(destDir + "/file.txt"), QString("bundled"));
  QCOMPARE(readFileContent(stampPathOf(destDir)).trimmed(), QString("1.0.0"));
}

void TestFileUtils2::testInstallVersionedDir_forceRecopiesDespiteMatchingStamp() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "bundled");
  QString destDir = tmp.filePath("dest");

  QVERIFY(!FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0")));

  QFile out(destDir + "/file.txt");
  QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
  out.write("stale");
  out.close();

  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), nullptr,
                                              true /* force */);
  QVERIFY(!err);
  QCOMPARE(readFileContent(destDir + "/file.txt"), QString("bundled"));
  QCOMPARE(readFileContent(stampPathOf(destDir)).trimmed(), QString("1.0.0"));
}

// A matching stamp that survives a subsequently-FAILED forced copy would make
// the next normal launch skip a partial tree, so a stamp that cannot be removed
// must abort BEFORE any copying.
void TestFileUtils2::testInstallVersionedDir_forceFailsBeforeCopyingWhenStampCannotBeRemoved() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createTextFile("src/file.txt", "bundled");

  QString destDir = tmp.createDir("dest");
  // A directory at the stamp path cannot be removed by QFile::remove.
  tmp.createDir(QStringLiteral("dest/") + QLatin1String(FileUtils2::c_versionStampFileName));

  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), nullptr,
                                              true /* force */);
  QVERIFY(err);
  QVERIFY2(!QFileInfo::exists(destDir + "/file.txt"), "the copy ran despite the stamp failure");
}

void TestFileUtils2::testInstallVersionedDir_preservesUserOwnedFile() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString srcDir = tmp.createDir("src");
  tmp.createDir("src/css");
  tmp.createTextFile("src/css/user.css", "/* bundled stub */");
  QString destDir = tmp.filePath("dest");

  const QSet<QString> skip{QStringLiteral("css/user.css")};

  // First install seeds the file.
  QVERIFY(!FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), nullptr,
                                           false, &skip));
  QCOMPARE(readFileContent(destDir + "/css/user.css"), QString("/* bundled stub */"));

  QFile out(destDir + "/css/user.css");
  QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
  out.write("body { color: red; }");
  out.close();

  // A later version must NOT clobber the user's copy.
  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("2.0.0"), nullptr,
                                              false, &skip);
  QVERIFY(!err);
  QCOMPARE(readFileContent(destDir + "/css/user.css"), QString("body { color: red; }"));
}

void TestFileUtils2::testInstallVersionedDir_incompatiblePreservedNodeWritesNoStamp() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  const QString srcDir = tmp.createDir("src");
  tmp.createDir("src/css");
  tmp.createTextFile("src/css/user.css", "bundled stub");

  const QString destDir = tmp.createDir("dest");
  tmp.createDir("dest/css/user.css");

  const QSet<QString> skip{QStringLiteral("css/user.css")};
  QStringList failed;
  Error err = FileUtils2::installVersionedDir(srcDir, destDir, QStringLiteral("1.0.0"), &failed,
                                              false, &skip);

  QVERIFY(err);
  QVERIFY(failed.contains(srcDir + QStringLiteral("/css/user.css")));
  QVERIFY(!QFileInfo::exists(stampPathOf(destDir)));
}

// =============================================================================
// generateRandomFileName tests
// =============================================================================
void TestFileUtils2::testGenerateRandomFileNameHex() {
  QRegularExpression hexPattern(QStringLiteral("^[0-9a-f]{1,4}$"));

  // Test format and range: loop 5000 times
  for (int i = 0; i < 5000; ++i) {
    auto name = FileUtils2::generateRandomFileName(QStringLiteral("img"), QStringLiteral("png"));

    // Strip trailing .png to get basename
    QString basename = name;
    if (basename.endsWith(QStringLiteral(".png"))) {
      basename = basename.left(basename.length() - 4);
    }

    // Assert format matches [0-9a-f]{1,4}
    QVERIFY2(hexPattern.match(basename).hasMatch(),
             qPrintable(QStringLiteral("Invalid hex format: %1").arg(basename)));

    // Assert value is in range [0, 0xffff]
    bool ok = false;
    int value = basename.toInt(&ok, 16);
    QVERIFY2(ok, qPrintable(QStringLiteral("Failed to parse hex: %1").arg(basename)));
    QVERIFY2(
        value >= 0 && value <= 0xffff,
        qPrintable(QStringLiteral("Value out of range: %1 (0x%2)").arg(value).arg(value, 0, 16)));
  }

  // Test suffix cases
  auto withPng = FileUtils2::generateRandomFileName(QStringLiteral("x"), QStringLiteral("PNG"));
  QVERIFY2(withPng.endsWith(QStringLiteral(".png")),
           qPrintable(QStringLiteral("PNG suffix not lowercased: %1").arg(withPng)));

  auto noSuffix = FileUtils2::generateRandomFileName(QStringLiteral("x"), QStringLiteral(""));
  QVERIFY2(!noSuffix.contains(QLatin1Char('.')),
           qPrintable(QStringLiteral("Empty suffix should not contain dot: %1").arg(noSuffix)));

  auto withJpeg = FileUtils2::generateRandomFileName(QStringLiteral("x"), QStringLiteral("JPEG"));
  QVERIFY2(withJpeg.endsWith(QStringLiteral(".jpeg")),
           qPrintable(QStringLiteral("JPEG suffix not lowercased: %1").arg(withJpeg)));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileUtils2)
#include "test_fileutils2.moc"
