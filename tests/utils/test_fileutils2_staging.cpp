// test_fileutils2_staging.cpp - Tests for staging-dir helpers in vnotex::FileUtils2
#include <QtTest>

#include <QDir>
#include <QFile>

#include <temp_dir_fixture.h>
#include <utils/fileutils2.h>

using namespace vnotex;

namespace tests {

class TestFileUtils2Staging : public QObject {
  Q_OBJECT

private slots:
  void testRemoveStagingDir();
};

void TestFileUtils2Staging::testRemoveStagingDir() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create staging dir with nested content
  const QString stagingDir = QDir(tmp.path()).filePath("to-remove");
  QVERIFY(QDir().mkpath(stagingDir));

  // Create nested file to test recursive delete
  QString subDir = QDir(stagingDir).filePath("subdir");
  QVERIFY(QDir().mkpath(subDir));
  QFile nested(QDir(subDir).filePath("nested.txt"));
  QVERIFY(nested.open(QIODevice::WriteOnly));
  nested.write("nested content");
  nested.close();
  QVERIFY(QFile::exists(QDir(subDir).filePath("nested.txt")));

  // Remove the staging dir
  QString errorMsg;
  bool success = FileUtils2::removeStagingDir(stagingDir, &errorMsg);

  QVERIFY2(success, qPrintable(errorMsg));

  // Verify the entire staging dir is gone
  QVERIFY(!QDir(stagingDir).exists());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileUtils2Staging)
#include "test_fileutils2_staging.moc"
