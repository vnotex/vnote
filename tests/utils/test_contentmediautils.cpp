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
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
  void nativeNestedImageBytesAreCopied();
  void nativeDecodedNamesAreNotUrlDecoded_data();
  void nativeDecodedNamesAreNotUrlDecoded();
  void nativeDuplicatesAndInternalTraversalReuseTheCopy();
  void nativeMissingAndOutsidePathsWarnAndContinue();
  void nativeCollisionsFailWithoutOverwriting_data();
  void nativeCollisionsFailWithoutOverwriting();
  void nativeReservedSourcePathFails_data();
  void nativeReservedSourcePathFails();
  void nativeSourceDirectoryEscapeIsSkipped();
  void nativeDestinationDirectoryEscapeIsSkipped();
  void nativeContainedAliasesReuseOnlyMatchingPairs();
  void nativeReservedSourceAliasFails();
  void nativeUnavailableOutputParentFails();
  void nativeCopyFailureIsReported();

private:
  // Writes @p_relPath under @p_dir with some bytes in it.
  static void makeFile(const QString &p_dir, const QString &p_relPath,
                       const QByteArray &p_bytes = QByteArrayLiteral("image-bytes"));
  static QString readAll(const QString &p_path);
  static bool makeDirectoryLink(const QString &p_target, const QString &p_link);
  static void removeDirectoryLink(const QString &p_link);
};

void TestContentMediaUtils::makeFile(const QString &p_dir, const QString &p_relPath,
                                     const QByteArray &p_bytes) {
  QDir dir(p_dir);
  const QString full = dir.filePath(p_relPath);
  QVERIFY(dir.mkpath(QFileInfo(full).absolutePath()));
  QFile f(full);
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write(p_bytes), p_bytes.size());
  f.close();
}

QString TestContentMediaUtils::readAll(const QString &p_path) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

bool TestContentMediaUtils::makeDirectoryLink(const QString &p_target, const QString &p_link) {
#ifdef Q_OS_WIN
  // Junctions exercise reparse containment without requiring symlink privileges.
  QProcess process;
  process.start(QStringLiteral("cmd.exe"),
                {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                 QDir::toNativeSeparators(p_link), QDir::toNativeSeparators(p_target)});
  return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
         process.exitCode() == 0 && QFileInfo(p_link).isJunction();
#else
  return QFile::link(p_target, p_link) && QFileInfo(p_link).isSymbolicLink();
#endif
}

void TestContentMediaUtils::removeDirectoryLink(const QString &p_link) {
#ifdef Q_OS_WIN
  QDir().rmdir(p_link);
#else
  QFile::remove(p_link);
#endif
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

void TestContentMediaUtils::nativeNestedImageBytesAreCopied() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  const auto relative = QStringLiteral("images/nested/pixel.png");
  const auto bytes = QByteArray::fromHex("89504e470d0a1a0a000d0aff007f80");
  makeFile(src.path(), relative, bytes);
  const auto output = dst.filePath(QStringLiteral("unpublished/bundle/note.mmm"));
  QStringList warnings;
  QVERIFY(
      vnotex::ContentMediaUtils::copyRelativeImageFiles({relative}, src.path(), output, &warnings));
  QVERIFY(warnings.isEmpty());
  QVERIFY(!QFileInfo::exists(output));
  QFile copied(dst.filePath(QStringLiteral("unpublished/bundle/") + relative));
  QVERIFY(copied.open(QIODevice::ReadOnly));
  QCOMPARE(copied.readAll(), bytes);
}

void TestContentMediaUtils::nativeDecodedNamesAreNotUrlDecoded_data() {
  QTest::addColumn<QString>("name");
  QTest::newRow("literal-percent-escape") << QStringLiteral("images/a%20b.png");
  QTest::newRow("decoded-space") << QStringLiteral("images/a b.png");
  QTest::newRow("literal-fragment") << QStringLiteral("images/a#fragment.png");
  QTest::newRow("unicode") << QString::fromUtf8("images/雪.png");
#ifndef Q_OS_WIN
  QTest::newRow("literal-query") << QStringLiteral("images/a?query.png");
#endif
}

void TestContentMediaUtils::nativeDecodedNamesAreNotUrlDecoded() {
  QFETCH(QString, name);
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  makeFile(src.path(), name);
  QStringList warnings;
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {name}, src.path(), dst.filePath(QStringLiteral("note.typst")), &warnings));
  QVERIFY(warnings.isEmpty());
  QCOMPARE(readAll(dst.filePath(name)), QStringLiteral("image-bytes"));
}

void TestContentMediaUtils::nativeDuplicatesAndInternalTraversalReuseTheCopy() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  makeFile(src.path(), QStringLiteral("images/pixel.png"));
  const auto output = dst.filePath(QStringLiteral("note.mmm"));
  QStringList warnings;
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {QString(), QStringLiteral("images/pixel.png"), QStringLiteral("./images/pixel.png"),
       QStringLiteral("assets/../images/pixel.png"), QStringLiteral("images/pixel.png")},
      src.path(), output, &warnings));
  QVERIFY(warnings.isEmpty());
  QCOMPARE(readAll(dst.filePath(QStringLiteral("images/pixel.png"))),
           QStringLiteral("image-bytes"));
  QCOMPARE(QDir(dst.filePath(QStringLiteral("images"))).entryList(QDir::Files),
           QStringList{QStringLiteral("pixel.png")});
  // Reuse is operation-local: a subsequent call must not overwrite the old bundle.
  QVERIFY(!vnotex::ContentMediaUtils::copyRelativeImageFiles({QStringLiteral("images/pixel.png")},
                                                             src.path(), output, &warnings));
  QCOMPARE(warnings.size(), 1);
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles({QString()}, QString(), QString()));
}

void TestContentMediaUtils::nativeMissingAndOutsidePathsWarnAndContinue() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const auto source = temp.filePath(QStringLiteral("source"));
  const auto destination = temp.filePath(QStringLiteral("destination"));
  makeFile(source, QStringLiteral("images/kept.png"));
  makeFile(temp.path(), QStringLiteral("outside.png"), QByteArrayLiteral("outside"));
  QStringList warnings;
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {QString(), QStringLiteral("missing.png"), QStringLiteral("images"),
       QStringLiteral("../outside.png"), QStringLiteral("assets/../../outside.png"),
       temp.filePath(QStringLiteral("outside.png")), QStringLiteral("images/kept.png")},
      source, QDir(destination).filePath(QStringLiteral("note.mmm")), &warnings));
  QCOMPARE(warnings.size(), 5);
  QCOMPARE(warnings.filter(QStringLiteral("outside the source directory")).size(), 3);
  QCOMPARE(readAll(QDir(destination).filePath(QStringLiteral("images/kept.png"))),
           QStringLiteral("image-bytes"));
  QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("outside.png"))));
  QCOMPARE(readAll(temp.filePath(QStringLiteral("outside.png"))), QStringLiteral("outside"));
}

void TestContentMediaUtils::nativeCollisionsFailWithoutOverwriting_data() {
  QTest::addColumn<int>("kind");
  QTest::newRow("existing-file") << 0;
  QTest::newRow("existing-directory") << 1;
  QTest::newRow("file-blocks-parent-directory") << 2;
}

void TestContentMediaUtils::nativeCollisionsFailWithoutOverwriting() {
  QFETCH(int, kind);
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  const auto relative = QStringLiteral("images/pixel.png");
  makeFile(src.path(), relative, QByteArrayLiteral("new-image"));
  if (kind == 0) {
    makeFile(dst.path(), relative, QByteArrayLiteral("existing-image"));
  } else if (kind == 1) {
    QVERIFY(QDir().mkpath(dst.filePath(relative)));
  } else {
    makeFile(dst.path(), QStringLiteral("images"), QByteArrayLiteral("not-a-directory"));
  }
  const auto output = dst.filePath(QStringLiteral("note.mmm"));
  QStringList warnings;
  QVERIFY(!vnotex::ContentMediaUtils::copyRelativeImageFiles({relative}, src.path(), output,
                                                             &warnings));
  QCOMPARE(warnings.size(), 1);
  QVERIFY(!QFileInfo::exists(output));
  QVERIFY(!QFileInfo::exists(dst.filePath(QStringLiteral("images/pixel_1.png"))));
  if (kind == 0) {
    QCOMPARE(readAll(dst.filePath(relative)), QStringLiteral("existing-image"));
  } else if (kind == 1) {
    QVERIFY(QFileInfo(dst.filePath(relative)).isDir());
  } else {
    QCOMPARE(readAll(dst.filePath(QStringLiteral("images"))), QStringLiteral("not-a-directory"));
  }
}

void TestContentMediaUtils::nativeReservedSourcePathFails_data() {
  QTest::addColumn<QString>("relative");
  QTest::newRow("reserved-leaf") << QStringLiteral("note.mmm");
  QTest::newRow("normalized-reserved-leaf") << QStringLiteral("assets/../note.mmm");
  QTest::newRow("reserved-parent") << QStringLiteral("note.mmm/image.png");
#ifdef Q_OS_WIN
  QTest::newRow("reserved-case-alias") << QStringLiteral("NOTE.MMM");
#endif
}

void TestContentMediaUtils::nativeReservedSourcePathFails() {
  QFETCH(QString, relative);
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  makeFile(src.path(), QDir::cleanPath(relative));
  const auto output = dst.filePath(QStringLiteral("note.mmm"));
  QStringList warnings;
  QVERIFY(!vnotex::ContentMediaUtils::copyRelativeImageFiles({relative}, src.path(), output,
                                                             &warnings));
  QCOMPARE(warnings.size(), 1);
  QVERIFY(warnings.constFirst().contains(QStringLiteral("conflicts with the source output")));
  QVERIFY(!QFileInfo::exists(output));
  QVERIFY(QDir(dst.path()).isEmpty());
}

void TestContentMediaUtils::nativeSourceDirectoryEscapeIsSkipped() {
  QTemporaryDir temp;
  QTemporaryDir dst;
  QVERIFY(temp.isValid() && dst.isValid());
  const auto source = temp.filePath(QStringLiteral("source"));
  const auto outside = temp.filePath(QStringLiteral("source-other"));
  makeFile(source, QStringLiteral("inside.png"));
  makeFile(outside, QStringLiteral("secret.png"), QByteArrayLiteral("outside"));
  const auto escape = QDir(source).filePath(QStringLiteral("escape"));
  const auto back = QDir(outside).filePath(QStringLiteral("back"));
  const auto cleanup = qScopeGuard([&]() {
    removeDirectoryLink(back);
    removeDirectoryLink(escape);
  });
  QVERIFY(makeDirectoryLink(outside, escape));
  QVERIFY(makeDirectoryLink(source, back));
  QStringList warnings;
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {QStringLiteral("escape/secret.png"), QStringLiteral("escape/back/inside.png"),
       QStringLiteral("escape/missing/image.png"), QStringLiteral("escape/../inside.png")},
      source, dst.filePath(QStringLiteral("note.mmm")), &warnings));
  QCOMPARE(warnings.size(), 4);
  QCOMPARE(warnings.filter(QStringLiteral("outside the source directory")).size(), 4);
  QVERIFY(QDir(dst.path()).isEmpty());
  QCOMPARE(readAll(QDir(outside).filePath(QStringLiteral("secret.png"))),
           QStringLiteral("outside"));
}

void TestContentMediaUtils::nativeDestinationDirectoryEscapeIsSkipped() {
  QTemporaryDir src;
  QTemporaryDir temp;
  QVERIFY(src.isValid() && temp.isValid());
  const auto destination = temp.filePath(QStringLiteral("destination"));
  const auto outside = temp.filePath(QStringLiteral("destination-other"));
  QVERIFY(QDir().mkpath(destination));
  QVERIFY(QDir().mkpath(outside));
  makeFile(src.path(), QStringLiteral("escape/missing/image.png"));
  makeFile(src.path(), QStringLiteral("escape/back/inside.png"));
  makeFile(src.path(), QStringLiteral("inside.png"));
  const auto escape = QDir(destination).filePath(QStringLiteral("escape"));
  const auto back = QDir(outside).filePath(QStringLiteral("back"));
  const auto cleanup = qScopeGuard([&]() {
    removeDirectoryLink(back);
    removeDirectoryLink(escape);
  });
  QVERIFY(makeDirectoryLink(outside, escape));
  QVERIFY(makeDirectoryLink(destination, back));
  QStringList warnings;
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {QStringLiteral("escape/missing/image.png"), QStringLiteral("escape/back/inside.png"),
       QStringLiteral("escape/../inside.png")},
      src.path(), QDir(destination).filePath(QStringLiteral("note.mmm")), &warnings));
  QCOMPARE(warnings.size(), 3);
  QCOMPARE(warnings.filter(QStringLiteral("outside the output directory")).size(), 3);
  QVERIFY(!QFileInfo::exists(QDir(outside).filePath(QStringLiteral("missing"))));
  QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("inside.png"))));
}

void TestContentMediaUtils::nativeContainedAliasesReuseOnlyMatchingPairs() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  makeFile(src.path(), QStringLiteral("images/pixel.png"));
  QVERIFY(QDir().mkpath(dst.filePath(QStringLiteral("images"))));
  const auto sourceAlias = src.filePath(QStringLiteral("alias"));
  const auto destinationAlias = dst.filePath(QStringLiteral("alias"));
  const auto cleanup = qScopeGuard([&]() {
    removeDirectoryLink(destinationAlias);
    removeDirectoryLink(sourceAlias);
  });
  QVERIFY(makeDirectoryLink(src.filePath(QStringLiteral("images")), sourceAlias));
  QVERIFY(makeDirectoryLink(dst.filePath(QStringLiteral("images")), destinationAlias));
  QStringList warnings;
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {QStringLiteral("alias/pixel.png"), QStringLiteral("images/pixel.png")}, src.path(),
      dst.filePath(QStringLiteral("note.mmm")), &warnings));
  QVERIFY(warnings.isEmpty());
  QCOMPARE(readAll(dst.filePath(QStringLiteral("images/pixel.png"))),
           QStringLiteral("image-bytes"));
  // A shared source does not deduplicate two distinct requested destinations.
  QTemporaryDir otherDestination;
  QVERIFY(otherDestination.isValid());
  QVERIFY(vnotex::ContentMediaUtils::copyRelativeImageFiles(
      {QStringLiteral("alias/pixel.png"), QStringLiteral("images/pixel.png")}, src.path(),
      otherDestination.filePath(QStringLiteral("note.mmm")), &warnings));
  QVERIFY(warnings.isEmpty());
  QCOMPARE(readAll(otherDestination.filePath(QStringLiteral("images/pixel.png"))),
           QStringLiteral("image-bytes"));
  QCOMPARE(readAll(otherDestination.filePath(QStringLiteral("alias/pixel.png"))),
           QStringLiteral("image-bytes"));
}

void TestContentMediaUtils::nativeReservedSourceAliasFails() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  makeFile(src.path(), QStringLiteral("alias/note.mmm"));
  const auto alias = dst.filePath(QStringLiteral("alias"));
  const auto cleanup = qScopeGuard([&]() { removeDirectoryLink(alias); });
  QVERIFY(makeDirectoryLink(dst.path(), alias));
  const auto output = dst.filePath(QStringLiteral("note.mmm"));
  QStringList warnings;
  QVERIFY(!vnotex::ContentMediaUtils::copyRelativeImageFiles({QStringLiteral("alias/note.mmm")},
                                                             src.path(), output, &warnings));
  QCOMPARE(warnings.size(), 1);
  QVERIFY(warnings.constFirst().contains(QStringLiteral("conflicts with the source output")));
  QVERIFY(!QFileInfo::exists(output));
}

void TestContentMediaUtils::nativeUnavailableOutputParentFails() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  makeFile(src.path(), QStringLiteral("image.png"));
  makeFile(dst.path(), QStringLiteral("blocked"), QByteArrayLiteral("existing"));
  QStringList warnings;
  const auto output = dst.filePath(QStringLiteral("blocked/bundle/note.mmm"));
  QVERIFY(!vnotex::ContentMediaUtils::copyRelativeImageFiles({QStringLiteral("image.png")},
                                                             src.path(), output, &warnings));
  QCOMPARE(warnings.size(), 1);
  QCOMPARE(readAll(dst.filePath(QStringLiteral("blocked"))), QStringLiteral("existing"));
  QVERIFY(!QFileInfo::exists(output));
}

void TestContentMediaUtils::nativeCopyFailureIsReported() {
  QTemporaryDir src;
  QTemporaryDir dst;
  QVERIFY(src.isValid() && dst.isValid());
  const auto relative = QStringLiteral("image.png");
  makeFile(src.path(), relative);
  const auto source = src.filePath(relative);
#ifdef Q_OS_WIN
  const auto native = QDir::toNativeSeparators(source);
  const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()),
                                      GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  QVERIFY(handle != INVALID_HANDLE_VALUE);
  const auto cleanup = qScopeGuard([&]() { ::CloseHandle(handle); });
#else
  const auto permissions = QFile::permissions(source);
  const auto cleanup = qScopeGuard([&]() { QFile::setPermissions(source, permissions); });
  QVERIFY(QFile::setPermissions(source, QFileDevice::WriteOwner));
  QFile readable(source);
  if (readable.open(QIODevice::ReadOnly)) {
    QSKIP("The current user bypasses filesystem read permissions");
  }
#endif
  const auto output = dst.filePath(QStringLiteral("note.mmm"));
  QStringList warnings;
  QVERIFY(!vnotex::ContentMediaUtils::copyRelativeImageFiles({relative}, src.path(), output,
                                                             &warnings));
  QCOMPARE(warnings.size(), 1);
  QVERIFY(!QFileInfo::exists(output));
  QVERIFY(!QFileInfo::exists(dst.filePath(relative)));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestContentMediaUtils)
#include "test_contentmediautils.moc"
