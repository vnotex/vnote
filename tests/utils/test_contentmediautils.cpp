// test_contentmediautils.cpp
//
// ContentMediaUtils::copyMarkdownMediaFiles() is what puts a note's images into
// an exported bundle. It is covered here because two of its behaviours are easy
// to lose and impossible to notice until a user opens an export and finds a
// broken image:
//
//   * a reference-style image (`![a][ref]`) must still be COPIED. It has no
//     destination span, so the previous implementation -- which located
//     destinations by searching the text for cmark's cleaned url -- dropped it
//     entirely, and the file was silently omitted from the bundle.
//   * when a name collision forces a rename, the destination must be rewritten
//     using the RAW span, never the length of the cleaned url. `a\_b.png`
//     occupies 8 source characters and cleans to 7, so a cleaned-length
//     replacement eats the character after it and corrupts the document.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <utils/contentmediautils.h>

namespace tests {

class TestContentMediaUtils : public QObject {
  Q_OBJECT

private slots:
  void referenceStyleImageIsCopied();
  void inlineAndReferenceImagesAreBothCopied();
  void renameRewritesUsingTheRawSpan();
  void escapedDestinationSurvivesACopy();

private:
  // Writes @p_relPath under @p_dir with some bytes in it.
  static void makeFile(const QString &p_dir, const QString &p_relPath);
  static QString readAll(const QString &p_path);
};

void TestContentMediaUtils::makeFile(const QString &p_dir, const QString &p_relPath) {
  QDir dir(p_dir);
  const QString full = dir.filePath(p_relPath);
  QVERIFY(dir.mkpath(QFileInfo(full).absolutePath()));
  QFile f(full);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("image-bytes");
  f.close();
}

QString TestContentMediaUtils::readAll(const QString &p_path) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

// The one that was actually broken: a reference-style image is a real, local,
// still-referenced asset and belongs in the bundle.
void TestContentMediaUtils::referenceStyleImageIsCopied() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/ref.png"));

  const QString content = QStringLiteral("![alt][r]\n\n[r]: vx_images/ref.png\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  QVERIFY2(QFileInfo::exists(QDir(dst.path()).filePath(QStringLiteral("vx_images/ref.png"))),
           "a reference-style image must be copied into the bundle");
}

void TestContentMediaUtils::inlineAndReferenceImagesAreBothCopied() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/inline.png"));
  makeFile(src.path(), QStringLiteral("vx_images/ref.png"));

  const QString content = QStringLiteral("![a](vx_images/inline.png)\n\n"
                                         "![b][r]\n\n[r]: vx_images/ref.png\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  QVERIFY(QFileInfo::exists(QDir(dst.path()).filePath(QStringLiteral("vx_images/inline.png"))));
  QVERIFY(QFileInfo::exists(QDir(dst.path()).filePath(QStringLiteral("vx_images/ref.png"))));
  // Nothing was renamed, so the note must not have been rewritten at all.
  QCOMPARE(readAll(destFile), content);
}

// A case-insensitive collision at the destination forces a rename, which is the
// only path that rewrites the note. The replacement must be measured by the
// span, and the text either side of it must survive untouched.
void TestContentMediaUtils::renameRewritesUsingTheRawSpan() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/Pic.png"));
  // Already present at the destination under a different case.
  makeFile(dst.path(), QStringLiteral("vx_images/pic.png"));

  const QString content = QStringLiteral("before ![a](vx_images/Pic.png) after\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  const QString rewritten = readAll(destFile);
  QVERIFY2(!rewritten.isEmpty(), "the note should have been rewritten after a rename");
  // The surrounding text is intact -- an off-by-one in the replacement length
  // shows up here as a missing `)` or a swallowed space.
  QVERIFY2(rewritten.startsWith(QStringLiteral("before ![a](")), qPrintable(rewritten));
  QVERIFY2(rewritten.endsWith(QStringLiteral(") after\n")), qPrintable(rewritten));
  // And it points at something that is actually in the bundle.
  const int open = rewritten.indexOf(QStringLiteral("]("));
  const int close = rewritten.indexOf(QLatin1Char(')'), open);
  QVERIFY(open > 0 && close > open);
  QString url = rewritten.mid(open + 2, close - open - 2);
  if (url.startsWith(QLatin1Char('<')) && url.endsWith(QLatin1Char('>'))) {
    url = url.mid(1, url.size() - 2);
  }
  QVERIFY2(QFileInfo::exists(QDir(dst.path()).filePath(url)), qPrintable(url));
}

// A destination whose source spelling is longer than its resolved value. The
// file must be copied under the RESOLVED name, and with no rename in play the
// note must not be touched.
void TestContentMediaUtils::escapedDestinationSurvivesACopy() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/a_b.png"));

  const QString content = QStringLiteral("x ![a](vx_images/a\\_b.png) y\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  QVERIFY2(QFileInfo::exists(QDir(dst.path()).filePath(QStringLiteral("vx_images/a_b.png"))),
           "an escaped destination resolves to a real file and must be copied");
  QCOMPARE(readAll(destFile), content);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestContentMediaUtils)
#include "test_contentmediautils.moc"
