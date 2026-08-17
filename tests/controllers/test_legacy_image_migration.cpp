#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVector>

#include <controllers/legacyimagemigrationcontroller.h>

#include "../helpers/temp_dir_fixture.h"

using vnotex::LegacyImageMigrationController;
using vnotex::LegacyImageRef;
using vnotex::LegacyImageRewrite;

namespace tests {

class TestLegacyImageMigration : public QObject {
  Q_OBJECT

private slots:
  // isLegacyFolderName / containsPercentEscape.
  void testIsLegacyFolderName();
  void testContainsPercentEscape();

  // detect().
  void testDetectsVxImages();
  void testDetectsVImages();
  void testDetectsCaseInsensitiveFolder();
  void testDetectsDotSlashAndNestedParent();
  void testRejectsParentRelative();
  void testRejectsPercentEncoded();
  void testRejectsNonLegacyFolders();
  void testRejectsRemoteAndAbsolute();
  void testRejectsMissingFile();
  void testRejectsAttachmentsFolder();
  void testMultipleHitsAreDescending();
  void testNestedLegacyFolderResolvesToMatchedSegment();
  void testAssetsFolderExclusion();
  void testRecordedPositionMatchesSourceSubstring();

  // stageAssets().
  void testStageDeduplicatesBySource();
  void testStageRollsBackOnFailure();
  void testStageRollsBackOrphanFromPostCopyFailure();
  void testStageRejectsDegenerateDestination();
  void testStageRejectsEmptyLink();

  // Canonical containment.
  void testPathContainment();
  void testPathContainmentResolvesJunctions();

  // finalize()'s gate.
  void testDiskStateRequiresEveryNewUrl();
  void testDiskStateRejectsSurvivingOldUrl();
  void testDiskStateSatisfied();
  void testFinalizeGateRequiresCleanAndIdle();
  void testReferencedSourceKeysCollapseAliases();

  // The descending QTextCursor rewrite loop.
  void testDescendingRewriteLoopProducesExpectedText();

private:
  // Writes a 1x1-ish placeholder image file (content is irrelevant; only
  // existence matters to the detector).
  static void writeStub(const QString &p_absPath) {
    QDir().mkpath(QFileInfo(p_absPath).absolutePath());
    QFile f(p_absPath);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(p_absPath));
    f.write("stub");
    f.close();
  }

  static QString noteWith(const QString &p_url) {
    return QStringLiteral("# Title\n\nSome prose.\n\n![alt](%1)\n\nTrailing.\n").arg(p_url);
  }
};

// ============ isLegacyFolderName / containsPercentEscape ============

void TestLegacyImageMigration::testIsLegacyFolderName() {
  QVERIFY(LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("vx_images")));
  QVERIFY(LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("_v_images")));
  QVERIFY(LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("VX_Images")));
  QVERIFY(LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("_V_IMAGES")));

  QVERIFY(!LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("vx_attachments")));
  QVERIFY(!LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("_v_attachments")));
  QVERIFY(!LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("vx_assets")));
  QVERIFY(!LegacyImageMigrationController::isLegacyFolderName(QStringLiteral("images")));
  QVERIFY(!LegacyImageMigrationController::isLegacyFolderName(QString()));
}

void TestLegacyImageMigration::testContainsPercentEscape() {
  QVERIFY(LegacyImageMigrationController::containsPercentEscape(
      QStringLiteral("vx_images/my%20pic.png")));
  // %2F is exactly what TextUtils::decodeUrl() would have normalized away.
  QVERIFY(
      LegacyImageMigrationController::containsPercentEscape(QStringLiteral("vx_images/a%2Fb.png")));
  QVERIFY(LegacyImageMigrationController::containsPercentEscape(QStringLiteral("%e4%b8%ad.png")));

  QVERIFY(
      !LegacyImageMigrationController::containsPercentEscape(QStringLiteral("vx_images/pic.png")));
  // A bare % that is not an escape.
  QVERIFY(
      !LegacyImageMigrationController::containsPercentEscape(QStringLiteral("vx_images/100%.png")));
}

// ============ detect() ============

void TestLegacyImageMigration::testDetectsVxImages() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/pic.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/pic.png")), base, QString());
  QCOMPARE(refs.size(), 1);
  QCOMPARE(refs.at(0).urlInLink, QStringLiteral("vx_images/pic.png"));
  QVERIFY(refs.at(0).urlStart >= 0);
  QVERIFY(refs.at(0).urlEnd > refs.at(0).urlStart);
  QCOMPARE(QFileInfo(refs.at(0).srcAbsolutePath).fileName(), QStringLiteral("pic.png"));
  QVERIFY(refs.at(0).legacyFolderAbsolutePath.endsWith(QStringLiteral("vx_images")));
  QVERIFY(!refs.at(0).canonicalSrcKey.isEmpty());
}

void TestLegacyImageMigration::testDetectsVImages() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/_v_images/pic.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("_v_images/pic.png")), base, QString());
  QCOMPARE(refs.size(), 1);
  QVERIFY(refs.at(0).legacyFolderAbsolutePath.endsWith(QStringLiteral("_v_images")));
}

void TestLegacyImageMigration::testDetectsCaseInsensitiveFolder() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/VX_Images/pic.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("VX_Images/pic.png")), base, QString());
  QCOMPARE(refs.size(), 1);
}

void TestLegacyImageMigration::testDetectsDotSlashAndNestedParent() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));
  writeStub(base + QStringLiteral("/sub/vx_images/b.png"));

  const auto dotSlash = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("./vx_images/a.png")), base, QString());
  QCOMPARE(dotSlash.size(), 1);
  QCOMPARE(dotSlash.at(0).urlInLink, QStringLiteral("./vx_images/a.png"));

  const auto nested = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("sub/vx_images/b.png")), base, QString());
  QCOMPARE(nested.size(), 1);
  QVERIFY(nested.at(0).legacyFolderAbsolutePath.endsWith(QStringLiteral("sub/vx_images")));
}

void TestLegacyImageMigration::testRejectsParentRelative() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(dir.path() + QStringLiteral("/vx_images/pic.png"));

  // vtextedit classifies "../..." as LocalRelativeExternal and deleteAsset() is
  // notebook-root relative, so this shape is deliberately unsupported.
  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("../vx_images/pic.png")), base, QString());
  QCOMPARE(refs.size(), 0);
}

void TestLegacyImageMigration::testRejectsPercentEncoded() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/my pic.png"));
  // A file whose literal name contains a percent escape: this is the one
  // TextUtils::decodeUrl() would have let through.
  writeStub(base + QStringLiteral("/vx_images/a%2Fb.png"));

  QCOMPARE(LegacyImageMigrationController::detect(
               noteWith(QStringLiteral("vx_images/my%20pic.png")), base, QString())
               .size(),
           0);
  QCOMPARE(LegacyImageMigrationController::detect(noteWith(QStringLiteral("vx_images/a%2Fb.png")),
                                                  base, QString())
               .size(),
           0);
}

void TestLegacyImageMigration::testRejectsNonLegacyFolders() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/images/pic.png"));
  writeStub(base + QStringLiteral("/vx_assets/0123/pic.png"));
  writeStub(base + QStringLiteral("/bare.png"));

  QCOMPARE(LegacyImageMigrationController::detect(noteWith(QStringLiteral("images/pic.png")), base,
                                                  QString())
               .size(),
           0);
  QCOMPARE(LegacyImageMigrationController::detect(
               noteWith(QStringLiteral("vx_assets/0123/pic.png")), base, QString())
               .size(),
           0);
  QCOMPARE(
      LegacyImageMigrationController::detect(noteWith(QStringLiteral("bare.png")), base, QString())
          .size(),
      0);
}

void TestLegacyImageMigration::testRejectsRemoteAndAbsolute() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString abs = base + QStringLiteral("/vx_images/pic.png");
  writeStub(abs);

  QCOMPARE(LegacyImageMigrationController::detect(
               noteWith(QStringLiteral("https://example.com/vx_images/pic.png")), base, QString())
               .size(),
           0);
  QCOMPARE(LegacyImageMigrationController::detect(noteWith(abs), base, QString()).size(), 0);
}

void TestLegacyImageMigration::testRejectsMissingFile() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  QDir().mkpath(base + QStringLiteral("/vx_images"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/missing.png")), base, QString());
  QCOMPARE(refs.size(), 0);
}

void TestLegacyImageMigration::testRejectsAttachmentsFolder() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_attachments/pic.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_attachments/pic.png")), base, QString());
  QCOMPARE(refs.size(), 0);
}

void TestLegacyImageMigration::testMultipleHitsAreDescending() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));
  writeStub(base + QStringLiteral("/vx_images/b.png"));
  writeStub(base + QStringLiteral("/vx_images/c.png"));

  const QString text = QStringLiteral("![a](vx_images/a.png)\n\n![b](vx_images/b.png)\n\n"
                                      "![c](vx_images/c.png)\n");
  const auto refs = LegacyImageMigrationController::detect(text, base, QString());
  QCOMPARE(refs.size(), 3);
  for (int i = 1; i < refs.size(); ++i) {
    QVERIFY2(refs.at(i - 1).urlStart > refs.at(i).urlStart,
             "detect() must preserve the strictly descending urlStart order");
  }
  QCOMPARE(refs.at(0).urlInLink, QStringLiteral("vx_images/c.png"));
  QCOMPARE(refs.at(2).urlInLink, QStringLiteral("vx_images/a.png"));
}

void TestLegacyImageMigration::testNestedLegacyFolderResolvesToMatchedSegment() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/icons/a.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/icons/a.png")), base, QString());
  QCOMPARE(refs.size(), 1);
  // The rmdir target must be vx_images, NOT the immediate parent (icons).
  QVERIFY(refs.at(0).legacyFolderAbsolutePath.endsWith(QStringLiteral("vx_images")));
  QVERIFY(!refs.at(0).legacyFolderAbsolutePath.endsWith(QStringLiteral("icons")));
}

void TestLegacyImageMigration::testAssetsFolderExclusion() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/pic.png"));

  // A notebook whose assetsFolder is itself named vx_images would otherwise be
  // flagged forever.
  const QString assetsFolder = base + QStringLiteral("/vx_images");
  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/pic.png")), base, assetsFolder);
  QCOMPARE(refs.size(), 0);
}

void TestLegacyImageMigration::testRecordedPositionMatchesSourceSubstring() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));
  writeStub(base + QStringLiteral("/vx_images/b.png"));

  // Two links on one line plus a multi-byte character before them, to exercise
  // the QChar-offset contract the QTextCursor rewrite loop depends on.
  const QString text =
      QStringLiteral("Café ![a](vx_images/a.png) and ![b](vx_images/b.png)\n\nMore.\n");
  const auto refs = LegacyImageMigrationController::detect(text, base, QString());
  QCOMPARE(refs.size(), 2);
  for (const auto &ref : refs) {
    // This is the invariant detect() enforces before any offset reaches a
    // QTextCursor; a parser fallback reporting a percent-decoded position would
    // fail it and the entry would have been dropped.
    QCOMPARE(text.mid(ref.urlStart, ref.urlEnd - ref.urlStart), ref.urlInLink);
  }
}

// ============ stageAssets() ============

void TestLegacyImageMigration::testStageDeduplicatesBySource() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString assets = dir.createDir(QStringLiteral("notes/vx_assets/uuid"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));

  const QString text = QStringLiteral("![a](vx_images/a.png)\n\n![again](./vx_images/a.png)\n");
  const auto refs = LegacyImageMigrationController::detect(text, base, assets);
  QCOMPARE(refs.size(), 2);

  int inserterCalls = 0;
  auto inserter = [&](const QString &p_src) -> QString {
    ++inserterCalls;
    const QString dest = assets + QStringLiteral("/") + QFileInfo(p_src).fileName();
    QFile::copy(p_src, dest);
    // Mimic vxcore: return the file name only (relative).
    return QFileInfo(dest).fileName();
  };
  auto linkifier = [](const QString &p_abs) -> QString {
    return QStringLiteral("vx_assets/uuid/") + QFileInfo(p_abs).fileName();
  };

  QString err;
  const auto rewrites =
      LegacyImageMigrationController::stageAssets(refs, inserter, assets, linkifier, &err);
  QVERIFY(err.isEmpty());
  QCOMPARE(rewrites.size(), 2);
  QCOMPARE(inserterCalls, 1);
  QCOMPARE(rewrites.at(0).newUrlInLink, rewrites.at(1).newUrlInLink);
  QCOMPARE(rewrites.at(0).destAbsolutePath, rewrites.at(1).destAbsolutePath);
  QVERIFY(rewrites.at(0).oldUrlInLink != rewrites.at(1).oldUrlInLink);
  // Order must still be descending.
  QVERIFY(rewrites.at(0).urlStart > rewrites.at(1).urlStart);
}

void TestLegacyImageMigration::testStageRollsBackOnFailure() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString assets = dir.createDir(QStringLiteral("notes/vx_assets/uuid"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));
  writeStub(base + QStringLiteral("/vx_images/b.png"));
  writeStub(base + QStringLiteral("/vx_images/c.png"));

  const QString text = QStringLiteral("![a](vx_images/a.png)\n\n![b](vx_images/b.png)\n\n"
                                      "![c](vx_images/c.png)\n");
  const auto refs = LegacyImageMigrationController::detect(text, base, assets);
  QCOMPARE(refs.size(), 3);

  int calls = 0;
  QStringList created;
  auto inserter = [&](const QString &p_src) -> QString {
    ++calls;
    if (calls == 2) {
      return QString(); // Fail on the second DISTINCT source.
    }
    const QString dest = assets + QStringLiteral("/") + QFileInfo(p_src).fileName();
    QFile::copy(p_src, dest);
    created.append(dest);
    return QFileInfo(dest).fileName();
  };
  auto linkifier = [](const QString &p_abs) -> QString {
    return QStringLiteral("vx_assets/uuid/") + QFileInfo(p_abs).fileName();
  };

  QString err;
  const auto rewrites =
      LegacyImageMigrationController::stageAssets(refs, inserter, assets, linkifier, &err);
  QVERIFY(rewrites.isEmpty());
  QVERIFY(!err.isEmpty());
  QCOMPARE(calls, 2);
  // Every destination created by this call must be gone again, exactly once.
  QCOMPARE(created.size(), 1);
  for (const auto &dest : created) {
    QVERIFY2(!QFileInfo::exists(dest), qPrintable(dest));
  }
}

void TestLegacyImageMigration::testStageRollsBackOrphanFromPostCopyFailure() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString assets = dir.createDir(QStringLiteral("notes/vx_assets/uuid"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/a.png")), base, assets);
  QCOMPARE(refs.size(), 1);

  // vxcore's InsertAsset COPIES first and only then computes the
  // notebook-relative path; a post-copy failure returns an empty string while
  // the copy is already on disk. Rollback must not trust the return value.
  QString orphan;
  auto inserter = [&](const QString &p_src) -> QString {
    orphan = assets + QStringLiteral("/") + QFileInfo(p_src).fileName();
    QFile::copy(p_src, orphan);
    return QString();
  };
  auto linkifier = [](const QString &p_abs) -> QString { return QFileInfo(p_abs).fileName(); };

  QString err;
  QVERIFY(LegacyImageMigrationController::stageAssets(refs, inserter, assets, linkifier, &err)
              .isEmpty());
  QVERIFY(!err.isEmpty());
  QVERIFY(!orphan.isEmpty());
  QVERIFY2(!QFileInfo::exists(orphan),
           "a copy created by a failing insertAsset() must still be rolled back");
}

void TestLegacyImageMigration::testStageRejectsDegenerateDestination() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString assets = dir.createDir(QStringLiteral("notes/vx_assets/uuid"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/a.png")), base, assets);
  QCOMPARE(refs.size(), 1);

  auto linkifier = [](const QString &p_abs) -> QString { return QFileInfo(p_abs).fileName(); };

  // vxcore's CleanPath("") returns "." when the post-copy relative-path
  // computation fails (e.g. across Windows volumes); promoting that would yield
  // the assets DIRECTORY, not a file.
  QString err;
  auto dotInserter = [](const QString &) -> QString { return QStringLiteral("."); };
  QVERIFY(LegacyImageMigrationController::stageAssets(refs, dotInserter, assets, linkifier, &err)
              .isEmpty());
  QVERIFY(!err.isEmpty());

  // A destination that resolves to a directory rather than a file.
  err.clear();
  QDir().mkpath(assets + QStringLiteral("/adir"));
  auto dirInserter = [](const QString &) -> QString { return QStringLiteral("adir"); };
  QVERIFY(LegacyImageMigrationController::stageAssets(refs, dirInserter, assets, linkifier, &err)
              .isEmpty());
  QVERIFY(!err.isEmpty());
  // The rollback must NOT have removed the pre-existing directory.
  QVERIFY(QFileInfo(assets + QStringLiteral("/adir")).isDir());
}

void TestLegacyImageMigration::testStageRejectsEmptyLink() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString assets = dir.createDir(QStringLiteral("notes/vx_assets/uuid"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));

  const auto refs = LegacyImageMigrationController::detect(
      noteWith(QStringLiteral("vx_images/a.png")), base, assets);
  QCOMPARE(refs.size(), 1);

  QString destPath;
  auto inserter = [&](const QString &p_src) -> QString {
    destPath = assets + QStringLiteral("/") + QFileInfo(p_src).fileName();
    QFile::copy(p_src, destPath);
    return QFileInfo(destPath).fileName();
  };
  auto linkifier = [](const QString &) -> QString { return QString(); };

  QString err;
  QVERIFY(LegacyImageMigrationController::stageAssets(refs, inserter, assets, linkifier, &err)
              .isEmpty());
  QVERIFY(!err.isEmpty());
  QVERIFY(!destPath.isEmpty());
  QVERIFY2(!QFileInfo::exists(destPath), "the staged copy must be rolled back");
}

// ============ Canonical containment ============

void TestLegacyImageMigration::testPathContainment() {
  TempDirFixture dir;
  const QString root = dir.createDir(QStringLiteral("nb"));
  const QString inside = dir.createDir(QStringLiteral("nb/sub/deep"));
  const QString outside = dir.createDir(QStringLiteral("elsewhere"));

  QVERIFY(LegacyImageMigrationController::isPathContained(root, root));
  QVERIFY(LegacyImageMigrationController::isPathContained(root, inside));
  QVERIFY(!LegacyImageMigrationController::isPathContained(root, outside));
  // A sibling whose name merely starts with the root's name is NOT contained.
  const QString sibling = dir.createDir(QStringLiteral("nb_other"));
  QVERIFY(!LegacyImageMigrationController::isPathContained(root, sibling));
  QVERIFY(!LegacyImageMigrationController::isPathContained(QString(), inside));
  QVERIFY(!LegacyImageMigrationController::isPathContained(root, QString()));
}

void TestLegacyImageMigration::testPathContainmentResolvesJunctions() {
  TempDirFixture dir;
  const QString root = dir.createDir(QStringLiteral("nb"));
  const QString outside = dir.createDir(QStringLiteral("outside"));
  writeStub(outside + QStringLiteral("/secret.png"));

  const QString link = QDir::toNativeSeparators(root + QStringLiteral("/link"));
#if defined(Q_OS_WIN)
  // Directory junctions need no elevation; skip if the filesystem refuses.
  QProcess proc;
  proc.start(QStringLiteral("cmd.exe"),
             {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"), link,
              QDir::toNativeSeparators(outside)});
  proc.waitForFinished(10000);
  if (proc.exitCode() != 0) {
    QSKIP("cannot create a directory junction on this filesystem");
  }
#else
  if (!QFile::link(outside, root + QStringLiteral("/link"))) {
    QSKIP("cannot create a directory symlink on this filesystem");
  }
#endif

  const QString traversed = root + QStringLiteral("/link/secret.png");
  QVERIFY2(QFileInfo::exists(traversed), "the link should be traversable");
  // Lexically the path sits under the notebook root; canonically it does not.
  // Accepting it would let deleteAsset() remove a file outside the notebook.
  QVERIFY2(!LegacyImageMigrationController::isPathContained(root, traversed),
           "containment must be canonical, not lexical");
}

// ============ finalize()'s gate ============

void TestLegacyImageMigration::testDiskStateRequiresEveryNewUrl() {
  LegacyImageRewrite a;
  a.oldUrlInLink = QStringLiteral("vx_images/a.png");
  a.newUrlInLink = QStringLiteral("vx_assets/uuid/a.png");
  LegacyImageRewrite b;
  b.oldUrlInLink = QStringLiteral("vx_images/b.png");
  b.newUrlInLink = QStringLiteral("vx_assets/uuid/b.png");

  const QString text = QStringLiteral("![a](vx_assets/uuid/a.png)\n");
  QVERIFY(!LegacyImageMigrationController::diskStateSatisfies(text, {a, b}));
}

void TestLegacyImageMigration::testDiskStateRejectsSurvivingOldUrl() {
  LegacyImageRewrite a;
  a.oldUrlInLink = QStringLiteral("vx_images/a.png");
  a.newUrlInLink = QStringLiteral("vx_assets/uuid/a.png");

  // Both present: the undo case, or a stale save that has not landed yet.
  const QString text = QStringLiteral("![a](vx_assets/uuid/a.png)\n\n![old](vx_images/a.png)\n");
  QVERIFY(!LegacyImageMigrationController::diskStateSatisfies(text, {a}));
}

void TestLegacyImageMigration::testDiskStateSatisfied() {
  LegacyImageRewrite a;
  a.oldUrlInLink = QStringLiteral("vx_images/a.png");
  a.newUrlInLink = QStringLiteral("vx_assets/uuid/a.png");
  LegacyImageRewrite b;
  b.oldUrlInLink = QStringLiteral("vx_images/b.png");
  b.newUrlInLink = QStringLiteral("vx_assets/uuid/b.png");

  const QString text = QStringLiteral("![a](vx_assets/uuid/a.png)\n\n![b](vx_assets/uuid/b.png)\n");
  QVERIFY(LegacyImageMigrationController::diskStateSatisfies(text, {a, b}));
  QVERIFY(LegacyImageMigrationController::diskStateSatisfies(text, {}));
}

void TestLegacyImageMigration::testFinalizeGateRequiresCleanAndIdle() {
  LegacyImageRewrite a;
  a.oldUrlInLink = QStringLiteral("vx_images/a.png");
  a.newUrlInLink = QStringLiteral("vx_assets/uuid/a.png");
  const QString good = QStringLiteral("![a](vx_assets/uuid/a.png)\n");

  // All three conditions must hold; each one alone blocks the deletion.
  QVERIFY(LegacyImageMigrationController::finalizeGateSatisfied(false, false, good, {a}));
  QVERIFY(!LegacyImageMigrationController::finalizeGateSatisfied(true, false, good, {a}));
  // isDirty() is cleared by syncNow() at ENQUEUE time, so a busy queue must
  // block even when the buffer looks clean.
  QVERIFY(!LegacyImageMigrationController::finalizeGateSatisfied(false, true, good, {a}));
  QVERIFY(!LegacyImageMigrationController::finalizeGateSatisfied(true, true, good, {a}));

  const QString stale = QStringLiteral("![a](vx_images/a.png)\n");
  QVERIFY(!LegacyImageMigrationController::finalizeGateSatisfied(false, false, stale, {a}));
}

void TestLegacyImageMigration::testReferencedSourceKeysCollapseAliases() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  const QString src = base + QStringLiteral("/vx_images/a.png");
  writeStub(src);

  const auto plain = LegacyImageMigrationController::referencedSourceKeys(
      QStringLiteral("![a](vx_images/a.png)\n"), base);
  QCOMPARE(plain.size(), 1);
  QVERIFY(LegacyImageMigrationController::isStillReferenced(src, plain));

  // The exact-spelling gate in diskStateSatisfies() sees none of these, so
  // finalize() consults referencedSourceKeys() before deleting an original.
  // Each of these is a live reference to the SAME file.
  const QStringList aliases = {
      QStringLiteral("vx_images/./a.png"),              // dot segment
      QStringLiteral("./vx_images/../vx_images/a.png"), // parent segment
      QDir::fromNativeSeparators(src),                  // absolute path
      QUrl::fromLocalFile(src).toString(),              // file: URL
  };
  for (const auto &alias : aliases) {
    const QString text =
        QStringLiteral("![new](vx_assets/uuid/a.png)\n\n![alias](%1)\n").arg(alias);
    const auto keys = LegacyImageMigrationController::referencedSourceKeys(text, base);
    QVERIFY2(LegacyImageMigrationController::isStillReferenced(src, keys), qPrintable(alias));
  }

  // Sanity: without any alias the original is NOT referenced, so finalize()
  // is free to delete it.
  const auto none = LegacyImageMigrationController::referencedSourceKeys(
      QStringLiteral("![new](vx_assets/uuid/a.png)\n"), base);
  QVERIFY(!LegacyImageMigrationController::isStillReferenced(src, none));
}

// ============ The descending rewrite loop ============

void TestLegacyImageMigration::testDescendingRewriteLoopProducesExpectedText() {
  TempDirFixture dir;
  const QString base = dir.createDir(QStringLiteral("notes"));
  writeStub(base + QStringLiteral("/vx_images/a.png"));
  writeStub(base + QStringLiteral("/vx_images/b.png"));
  writeStub(base + QStringLiteral("/vx_images/c.png"));

  // Multiple paragraphs, a multi-byte character before a link, and two links on
  // one line.
  const QString text = QStringLiteral("Intro café ![a](vx_images/a.png)\n"
                                      "\n"
                                      "![b](vx_images/b.png) and ![c](vx_images/c.png)\n"
                                      "\n"
                                      "Outro.\n");
  const auto refs = LegacyImageMigrationController::detect(text, base, QString());
  QCOMPARE(refs.size(), 3);

  QTextDocument doc;
  doc.setPlainText(text);

  // Same loop as MarkdownViewWindow2::applyLegacyImageMigration(): the refs are
  // already descending, so every earlier offset stays valid. Sorting ascending
  // would silently corrupt the document.
  QTextCursor cur(&doc);
  cur.beginEditBlock();
  for (const auto &ref : refs) {
    const QString newUrl =
        QStringLiteral("vx_assets/uuid/") + QFileInfo(ref.srcAbsolutePath).fileName();
    cur.setPosition(ref.urlStart);
    cur.setPosition(ref.urlEnd, QTextCursor::KeepAnchor);
    cur.insertText(newUrl);
  }
  cur.endEditBlock();

  const QString expected = QStringLiteral("Intro café ![a](vx_assets/uuid/a.png)\n"
                                          "\n"
                                          "![b](vx_assets/uuid/b.png) and "
                                          "![c](vx_assets/uuid/c.png)\n"
                                          "\n"
                                          "Outro.\n");
  QCOMPARE(doc.toPlainText(), expected);

  // And it is ONE undoable step.
  QVERIFY(doc.isUndoAvailable());
  doc.undo();
  QCOMPARE(doc.toPlainText(), text);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestLegacyImageMigration)
#include "test_legacy_image_migration.moc"
