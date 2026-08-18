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
//   * an HTML `<img>` is an image reference too. Its whole `src` ATTRIBUTE is
//     replaced (not just the value), and the renamed url is cached
//     source-neutral so the same asset referenced from both syntaxes is spelled
//     correctly at each occurrence.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <vtextedit/htmlimgscanner.h>
#include <vtextedit/markdownutils.h>

#include <utils/contentmediautils.h>

namespace tests {

class TestContentMediaUtils : public QObject {
  Q_OBJECT

private slots:
  void referenceStyleImageIsCopied();
  void inlineAndReferenceImagesAreBothCopied();
  void renameRewritesUsingTheRawSpan();
  void escapedDestinationSurvivesACopy();
  void htmlImageIsCopied();
  void htmlRenameReplacesTheWholeSrcAttribute();
  void oneAssetReferencedFromBothSyntaxesIsSpelledPerOccurrence();

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

// An HTML `<img>` is an image reference like any other: its asset belongs in
// the bundle, and with no rename in play the note must not be touched.
void TestContentMediaUtils::htmlImageIsCopied() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/html.png"));

  const QString content =
      QStringLiteral("<img src=\"vx_images/html.png\" width=\"500\" class=\"x\" />\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  QVERIFY2(QFileInfo::exists(QDir(dst.path()).filePath(QStringLiteral("vx_images/html.png"))),
           "an HTML-referenced image must be copied into the bundle");
  QCOMPARE(readAll(destFile), content);
}

// A rename must replace the WHOLE `src` attribute, never just its value: an
// unquoted `src=old.png` renamed to a name containing a space would otherwise
// split into two attributes. Every other attribute must survive.
void TestContentMediaUtils::htmlRenameReplacesTheWholeSrcAttribute() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/Pic.png"));
  makeFile(dst.path(), QStringLiteral("vx_images/pic.png"));

  const QString content =
      QStringLiteral("before <img src=vx_images/Pic.png width=\"500\" class=\"x\"> after\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  const QString rewritten = readAll(destFile);
  QVERIFY2(!rewritten.isEmpty(), "the note should have been rewritten after a rename");
  QVERIFY2(rewritten.startsWith(QStringLiteral("before <img src=\"")), qPrintable(rewritten));
  QVERIFY2(rewritten.endsWith(QStringLiteral("> after\n")), qPrintable(rewritten));
  // Attributes VNote did not author survive.
  QVERIFY2(rewritten.contains(QStringLiteral("width=\"500\"")), qPrintable(rewritten));
  QVERIFY2(rewritten.contains(QStringLiteral("class=\"x\"")), qPrintable(rewritten));

  // Exactly one tag, still parseable, pointing at something in the bundle.
  vte::RawTextState state;
  const auto tags = vte::scanHtmlImgTags(rewritten, 0, &state);
  QCOMPARE(tags.size(), 1);
  QVERIFY2(QFileInfo::exists(QDir(dst.path()).filePath(tags.first().src())),
           qPrintable(tags.first().src()));
}

// The renamed url is cached SOURCE-NEUTRAL and spelled per occurrence. Caching
// an already-spelled string breaks the moment the same asset is referenced from
// both syntaxes.
void TestContentMediaUtils::oneAssetReferencedFromBothSyntaxesIsSpelledPerOccurrence() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());

  makeFile(src.path(), QStringLiteral("vx_images/Pic.png"));
  makeFile(dst.path(), QStringLiteral("vx_images/pic.png"));

  const QString content = QStringLiteral("![a](vx_images/Pic.png)\n\n"
                                         "<img src=\"vx_images/Pic.png\">\n");
  const QString destFile = QDir(dst.path()).filePath(QStringLiteral("note.md"));
  QFile out(destFile);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(content.toUtf8());
  out.close();

  vnotex::ContentMediaUtils::copyMarkdownMediaFiles(content, src.path(), nullptr, destFile);

  const QString rewritten = readAll(destFile);
  QVERIFY2(!rewritten.isEmpty(), "the note should have been rewritten after a rename");

  // Both point at the SAME renamed asset, each spelled its own way.
  const auto links = vte::MarkdownUtils::fetchImageLinks(
      rewritten, dst.path(), vte::MarkdownLink::TypeFlag::LocalRelativeInternal);
  QCOMPARE(links.size(), 2);
  QCOMPARE(links.at(0).m_urlInLink, links.at(1).m_urlInLink);
  QVERIFY2(QFileInfo::exists(QDir(dst.path()).filePath(links.first().m_urlInLink)),
           qPrintable(links.first().m_urlInLink));
  // An HTML src is never spelled with Markdown's angle brackets.
  QVERIFY2(!rewritten.contains(QStringLiteral("src=\"<")), qPrintable(rewritten));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestContentMediaUtils)
#include "test_contentmediautils.moc"
