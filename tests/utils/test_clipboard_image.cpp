// test_clipboard_image.cpp - Tests for ClipboardUtils cloneMimeData and ImageResolutionUtils
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>

#include <utils/clipboardutils.h>
#include <utils/imageresolutionutils.h>

namespace tests {

class TestClipboardImage : public QObject {
  Q_OBJECT

private slots:
  // ClipboardUtils::cloneMimeData tests
  void test_cloneMimeData_preservesCustomFormat();
  void test_cloneMimeData_preservesMultipleCustomFormats();
  void test_cloneMimeData_preservesStandardAndCustom();

  // ImageResolutionUtils::resolveRelativeImages tests
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
// ImageResolutionUtils::resolveRelativeImages tests
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
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());

  QCOMPARE(results.size(), 1);
  QCOMPARE(results[0].urlInLink, QString("vx_images/test.png"));
  QVERIFY(results[0].exists);
  QVERIFY(!results[0].srcAbsolutePath.isEmpty());
  QCOMPARE(results[0].alt, QString("alt"));
}

void TestClipboardImage::test_resolveRelativeImages_singleMissing() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QString md = "![](vx_images/missing.png)";
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());

  QCOMPARE(results.size(), 1);
  QCOMPARE(results[0].urlInLink, QString("vx_images/missing.png"));
  QVERIFY(!results[0].exists);
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
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());

  QCOMPARE(results.size(), 3);
  // Results are in descending order by position
  int existCount = 0;
  int missingCount = 0;
  for (const auto &r : results) {
    if (r.exists)
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
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());

  QCOMPARE(results.size(), 2);
  QCOMPARE(results[0].srcAbsolutePath, results[1].srcAbsolutePath);
}

void TestClipboardImage::test_resolveRelativeImages_noImages() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QString md = "Hello world, no images here.";
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());
  QVERIFY(results.isEmpty());
}

void TestClipboardImage::test_resolveRelativeImages_absolutePath() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  // Absolute paths should be filtered out (not treated as relative)
  QString md = "![](C:/absolute/path/img.png)";
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());
  QVERIFY(results.isEmpty());
}

void TestClipboardImage::test_resolveRelativeImages_networkUrl() {
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  QString md = "![](https://example.com/image.png)";
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());
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
  auto results = vnotex::ImageResolutionUtils::resolveRelativeImages(md, tmpDir.path());

  QCOMPARE(results.size(), 2);
  // Descending order: second image in text should come first in results
  QVERIFY(results[0].urlInLinkPos > results[1].urlInLinkPos);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestClipboardImage)
#include "test_clipboard_image.moc"
