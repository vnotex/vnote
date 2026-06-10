// test_fileutils2_staging.cpp - Tests for staging-dir helpers in vnotex::FileUtils2
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <temp_dir_fixture.h>
#include <utils/fileutils2.h>

using namespace vnotex;

namespace tests {

class TestFileUtils2Staging : public QObject {
  Q_OBJECT

private slots:
  void testGenerateCloneStagingDir();
  void testRenameStagingToFinal_success();
  void testRenameStagingToFinal_failsWhenFinalExists();
  void testRemoveStagingDir();
  void testSweepOrphanStagingDirs();
};

// =============================================================================
// Subtest 1: generateCloneStagingDir
// =============================================================================
void TestFileUtils2Staging::testGenerateCloneStagingDir() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  QString parentDir = tmp.path();
  QString leafName = "test-notebook";

  QString stagingDir = FileUtils2::generateCloneStagingDir(parentDir, leafName, nullptr);

  // Verify the staging dir path is not empty
  QVERIFY(!stagingDir.isEmpty());

  // Verify the staging dir exists
  QVERIFY(QDir(stagingDir).exists());

  // Verify it's in the parent directory
  QVERIFY(stagingDir.startsWith(parentDir));

  // Verify the directory name follows pattern: .<leafName>.vnote-clone-pending-<timestamp>
  QString dirName = QFileInfo(stagingDir).fileName();
  QVERIFY(dirName.startsWith(".test-notebook.vnote-clone-pending-"));

  // Verify marker file exists
  QString markerPath = QDir(stagingDir).filePath("staging-marker.json");
  QVERIFY(QFile::exists(markerPath));

  // Verify marker file is valid JSON with required fields
  QJsonObject markerJson;
  QFile markerFile(markerPath);
  QVERIFY(markerFile.open(QIODevice::ReadOnly));
  markerJson = QJsonDocument::fromJson(markerFile.readAll()).object();
  markerFile.close();

  QVERIFY(markerJson.contains("createdUtc"));
  QVERIFY(markerJson.contains("finalDir"));
  qint64 createdUtc = markerJson.value("createdUtc").toVariant().toLongLong();
  QVERIFY(createdUtc > 0);

  // Verify finalDir in marker is absolute path of intended destination
  QString expectedFinalDir = QDir(parentDir).filePath(leafName);
  QCOMPARE(markerJson.value("finalDir").toString(), expectedFinalDir);
}

// =============================================================================
// Subtest 2: renameStagingToFinal (success case)
// =============================================================================
void TestFileUtils2Staging::testRenameStagingToFinal_success() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create staging dir with a marker file
  QString stagingDir = FileUtils2::generateCloneStagingDir(tmp.path(), "mynotebook", nullptr);
  QVERIFY(!stagingDir.isEmpty());
  QVERIFY(QDir(stagingDir).exists());

  // Create a dummy file in the staging dir to verify it gets moved
  QString dummyFile = QDir(stagingDir).filePath("content.txt");
  QFile f(dummyFile);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("test content");
  f.close();
  QVERIFY(QFile::exists(dummyFile));

  // Target final dir (should not exist yet)
  QString finalDir = QDir(tmp.path()).filePath("mynotebook");
  QVERIFY(!QDir(finalDir).exists());

  // Rename staging to final
  QString errorMsg;
  bool success = FileUtils2::renameStagingToFinal(stagingDir, finalDir, &errorMsg);

  QVERIFY2(success, qPrintable(errorMsg));

  // Verify final dir exists
  QVERIFY(QDir(finalDir).exists());

  // Verify content was moved
  QVERIFY(QFile::exists(QDir(finalDir).filePath("content.txt")));

  // Verify staging dir no longer exists
  QVERIFY(!QDir(stagingDir).exists());

  // Regression: staging-marker.json must not leak into the final dir after a
  // successful rename. Without this gate, the marker surfaces as an "external
  // file" in the notebook explorer (BundledFolderManager::ListExternalNodes
  // has no exclusion for it).
  QVERIFY(!QFile::exists(QDir(finalDir).filePath("staging-marker.json")));
}

// =============================================================================
// Subtest 3: renameStagingToFinal (collision case)
// =============================================================================
void TestFileUtils2Staging::testRenameStagingToFinal_failsWhenFinalExists() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create staging dir
  QString stagingDir =
      FileUtils2::generateCloneStagingDir(tmp.path(), "collision-notebook", nullptr);
  QVERIFY(!stagingDir.isEmpty());
  QVERIFY(QDir(stagingDir).exists());

  // Create a marker file in staging
  QString markerPath = QDir(stagingDir).filePath("staging-marker.json");
  QVERIFY(QFile::exists(markerPath));

  // Pre-create the final destination dir (collision scenario)
  QString finalDir = QDir(tmp.path()).filePath("collision-notebook");
  QVERIFY(QDir().mkpath(finalDir));
  QVERIFY(QDir(finalDir).exists());

  // Create a file in final dir so we can verify it's untouched
  QString finalMarker = QDir(finalDir).filePath("original.txt");
  QFile f(finalMarker);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("original");
  f.close();
  QVERIFY(QFile::exists(finalMarker));

  // Attempt rename (should fail)
  QString errorMsg;
  bool success = FileUtils2::renameStagingToFinal(stagingDir, finalDir, &errorMsg);

  QVERIFY(!success); // Should return false

  // Verify staging dir is UNTOUCHED on disk
  QVERIFY(QDir(stagingDir).exists());
  QVERIFY(QFile::exists(markerPath));

  // Verify final dir is UNTOUCHED
  QVERIFY(QDir(finalDir).exists());
  QVERIFY(QFile::exists(finalMarker));
  QFile check(finalMarker);
  QVERIFY(check.open(QIODevice::ReadOnly));
  QCOMPARE(QString(check.readAll()), QString("original"));
  check.close();
}

// =============================================================================
// Subtest 4: removeStagingDir
// =============================================================================
void TestFileUtils2Staging::testRemoveStagingDir() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  // Create staging dir with nested content
  QString stagingDir = FileUtils2::generateCloneStagingDir(tmp.path(), "to-remove", nullptr);
  QVERIFY(!stagingDir.isEmpty());
  QVERIFY(QDir(stagingDir).exists());

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

// =============================================================================
// Subtest 5: sweepOrphanStagingDirs
// =============================================================================
void TestFileUtils2Staging::testSweepOrphanStagingDirs() {
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());

  qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 oldThresholdMs = 5000; // 5 second threshold

  // Create an OLD staging dir (older than threshold)
  QString oldStagingDir = FileUtils2::generateCloneStagingDir(tmp.path(), "old-notebook", nullptr);
  QVERIFY(!oldStagingDir.isEmpty());

  // Manually patch the marker to make it old
  QString oldMarkerPath = QDir(oldStagingDir).filePath("staging-marker.json");
  QVERIFY(QFile::exists(oldMarkerPath));
  {
    QJsonObject oldMarker;
    oldMarker.insert("createdUtc", QJsonValue(static_cast<double>(now - 10000))); // 10 seconds ago
    oldMarker.insert("finalDir", QJsonValue(QDir(tmp.path()).filePath("old-notebook")));
    QFile markerFile(oldMarkerPath);
    QVERIFY(markerFile.open(QIODevice::WriteOnly));
    markerFile.write(QJsonDocument(oldMarker).toJson());
    markerFile.close();
  }

  // Create a RECENT staging dir (newer than threshold)
  QString recentStagingDir =
      FileUtils2::generateCloneStagingDir(tmp.path(), "recent-notebook", nullptr);
  QVERIFY(!recentStagingDir.isEmpty());
  // This one will have the current timestamp in its marker, so it's recent

  // Sweep for orphans older than threshold
  QStringList orphans = FileUtils2::sweepOrphanStagingDirs(tmp.path(), oldThresholdMs);

  // Verify the old dir is in the orphan list
  QVERIFY(orphans.contains(oldStagingDir));

  // Verify the recent dir is NOT in the orphan list
  QVERIFY(!orphans.contains(recentStagingDir));

  // Verify both dirs still exist (sweep should NOT delete)
  QVERIFY(QDir(oldStagingDir).exists());
  QVERIFY(QDir(recentStagingDir).exists());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileUtils2Staging)
#include "test_fileutils2_staging.moc"
