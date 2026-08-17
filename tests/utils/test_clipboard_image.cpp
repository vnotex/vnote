// test_clipboard_image.cpp - Tests for ClipboardUtils cloneMimeData and the
// relative-image resolution the "Paste with Linked Images" flow depends on.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>

#include <utils/clipboardutils.h>
#include <vtextedit/markdownutils.h>

namespace tests {

namespace {
// What MarkdownEditor's "Paste with Linked Images" asks for: the two local
// relative flavors, and nothing else. Absolute, Qt-resource and remote
// destinations are excluded by the flags rather than by a hand-written filter.
QVector<vte::MarkdownLink> resolveRelative(const QString &p_markdown, const QString &p_basePath) {
  return vte::MarkdownUtils::fetchImageLinks(
      p_markdown, p_basePath,
      vte::MarkdownLink::TypeFlag::LocalRelativeInternal |
          vte::MarkdownLink::TypeFlag::LocalRelativeExternal);
}
} // namespace

class TestClipboardImage : public QObject {
  Q_OBJECT

private slots:
  // ClipboardUtils::cloneMimeData tests
  void test_cloneMimeData_preservesCustomFormat();
  void test_cloneMimeData_preservesMultipleCustomFormats();
  void test_cloneMimeData_preservesStandardAndCustom();

  // Relative-image resolution tests
  void test_resolveRelativeImages_singleExisting();
  void test_resolveRelativeImages_singleMissing();
  void test_resolveRelativeImages_mixed();
  void test_resolveRelativeImages_duplicateReferences();
  void test_resolveRelativeImages_noImages();
  void test_resolveRelativeImages_absolutePath();
  void test_resolveRelativeImages_networkUrl();
  void test_resolveRelativeImages_descendingOrder();
};

// =============================================================================
// ClipboardUtils::cloneMimeData tests
// =============================================================================

void TestClipboardImage::test_cloneMimeData_preservesCustomFormat() {
  QMimeData source;
  source.setText("hello");
  source.setData("application/x-test-custom", QByteArray("custom-data"));

  auto clone = vnotex::ClipboardUtils::cloneMimeData(&source);
  QVERIFY(clone);
  QCOMPARE(clone->text(), QString("hello"));
  QVERIFY(clone->hasFormat("application/x-test-custom"));
  QCOMPARE(clone->data("application/x-test-custom"), QByteArray("custom-data"));
}

void TestClipboardImage::test_cloneMimeData_preservesMultipleCustomFormats() {
  QMimeData source;
  source.setData("application/x-format-a", QByteArray("data-a"));
  source.setData("application/x-format-b", QByteArray("data-b"));

  auto clone = vnotex::ClipboardUtils::cloneMimeData(&source);
  QVERIFY(clone->hasFormat("application/x-format-a"));
  QVERIFY(clone->hasFormat("application/x-format-b"));
  QCOMPARE(clone->data("application/x-format-a"), QByteArray("data-a"));
  QCOMPARE(clone->data("application/x-format-b"), QByteArray("data-b"));
}

void TestClipboardImage::test_cloneMimeData_preservesStandardAndCustom() {
  QMimeData source;
  source.setText("text-content");
  source.setHtml("<b>html</b>");
  source.setData("application/x-vnotex-content-source", QByteArray("/some/path"));

  auto clone = vnotex::ClipboardUtils::cloneMimeData(&source);
  QCOMPARE(clone->text(), QString("text-content"));
  QCOMPARE(clone->html(), QString("<b>html</b>"));
  QVERIFY(clone->hasFormat("application/x-vnotex-content-source"));
  QCOMPARE(clone->data("application/x-vnotex-content-source"), QByteArray("/some/path"));
}

// =============================================================================
// Relative-image resolution, as MarkdownEditor's "Paste with Linked Images"
// consumes it: local relative destinations only, descending by span so the
// paste handler can rewrite them in place.
// =============================================================================

void TestClipboardImage::test_resolveRelativeImages_singleExisting() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  // Create vx_images/test.png
  QDir dir(tmpDir.path());
  dir.mkdir("vx_images");
  QFile f(dir.filePath("vx_images/test.png"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("fake-png-data");
  f.close();

  QString md = "![alt](vx_images/test.png)";
  auto results = resolveRelative(md, tmpDir.path());

  QCOMPARE(results.size(), 1);
  QCOMPARE(results[0].m_urlInLink, QString("vx_images/test.png"));
  QVERIFY(results[0].m_exists);
  QVERIFY(!results[0].m_path.isEmpty());
  QCOMPARE(results[0].m_alt, QString("alt"));
}

void TestClipboardImage::test_resolveRelativeImages_singleMissing() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QString md = "![](vx_images/missing.png)";
  auto results = resolveRelative(md, tmpDir.path());

  QCOMPARE(results.size(), 1);
  QCOMPARE(results[0].m_urlInLink, QString("vx_images/missing.png"));
  QVERIFY(!results[0].m_exists);
}

void TestClipboardImage::test_resolveRelativeImages_mixed() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QDir dir(tmpDir.path());
  dir.mkdir("vx_images");
  QFile f1(dir.filePath("vx_images/exists1.png"));
  QVERIFY(f1.open(QIODevice::WriteOnly));
  f1.write("data");
  f1.close();
  QFile f2(dir.filePath("vx_images/exists2.png"));
  QVERIFY(f2.open(QIODevice::WriteOnly));
  f2.write("data");
  f2.close();

  QString md = "![](vx_images/exists1.png) ![](vx_images/missing.png) ![](vx_images/exists2.png)";
  auto results = resolveRelative(md, tmpDir.path());

  QCOMPARE(results.size(), 3);
  // Results are in descending order by position
  int existCount = 0;
  int missingCount = 0;
  for (const auto &r : results) {
    if (r.m_exists)
      existCount++;
    else
      missingCount++;
  }
  QCOMPARE(existCount, 2);
  QCOMPARE(missingCount, 1);
}

void TestClipboardImage::test_resolveRelativeImages_duplicateReferences() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QDir dir(tmpDir.path());
  dir.mkdir("vx_images");
  QFile f(dir.filePath("vx_images/img.png"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("data");
  f.close();

  QString md = "![a](vx_images/img.png) ![b](vx_images/img.png)";
  auto results = resolveRelative(md, tmpDir.path());

  QCOMPARE(results.size(), 2);
  QCOMPARE(results[0].m_path, results[1].m_path);
}

void TestClipboardImage::test_resolveRelativeImages_noImages() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QString md = "Hello world, no images here.";
  auto results = resolveRelative(md, tmpDir.path());
  QVERIFY(results.isEmpty());
}

void TestClipboardImage::test_resolveRelativeImages_absolutePath() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  // Absolute paths should be filtered out (not treated as relative). Use a
  // POSIX-absolute path so the assertion holds on both Windows and Linux
  // (QDir::isAbsolutePath("C:/...") returns false on Linux because "C:" is
  // just a relative dirname there).
  QString mdPosix = "![](/absolute/path/img.png)";
  auto resultsPosix = resolveRelative(mdPosix, tmpDir.path());
  QVERIFY(resultsPosix.isEmpty());

#ifdef Q_OS_WIN
  // Windows drive-letter paths are also absolute on Windows; keep the
  // original case covered on its native platform.
  QString mdWin = "![](C:/absolute/path/img.png)";
  auto resultsWin = resolveRelative(mdWin, tmpDir.path());
  QVERIFY(resultsWin.isEmpty());
#endif
}

void TestClipboardImage::test_resolveRelativeImages_networkUrl() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QString md = "![](https://example.com/image.png)";
  auto results = resolveRelative(md, tmpDir.path());
  QVERIFY(results.isEmpty());
}

void TestClipboardImage::test_resolveRelativeImages_descendingOrder() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QDir dir(tmpDir.path());
  dir.mkdir("vx_images");
  QFile f1(dir.filePath("vx_images/a.png"));
  QVERIFY(f1.open(QIODevice::WriteOnly));
  f1.write("data");
  f1.close();
  QFile f2(dir.filePath("vx_images/b.png"));
  QVERIFY(f2.open(QIODevice::WriteOnly));
  f2.write("data");
  f2.close();

  QString md = "![](vx_images/a.png) some text ![](vx_images/b.png)";
  auto results = resolveRelative(md, tmpDir.path());

  QCOMPARE(results.size(), 2);
  // Descending order: second image in text should come first in results
  QVERIFY(results[0].m_urlStart > results[1].m_urlStart);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestClipboardImage)
#include "test_clipboard_image.moc"
