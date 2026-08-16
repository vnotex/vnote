// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_extra_qrc_coverage.cpp
//
// Coverage gate between the vendored pdf.js tree on disk and the pdf.js block
// of src/data/extra/extra.qrc.
//
// === Why this test exists ===
// The two failure modes of a hand-maintained qrc block are asymmetric:
//
//   * A <file> entry with NO file on disk fails the build loudly (rcc errors),
//     so it is self-catching and needs no test.
//   * A file on disk with NO <file> entry fails SILENTLY at runtime, as a 404
//     inside the WebEngine page. Nothing in the build, and nothing in any other
//     test, notices.
//
// The pdf.js block is ~267 entries and is regenerated wholesale on every pdf.js
// upgrade, which is exactly the situation where a hand edit drops a file. This
// test asserts both directions anyway, because asserting only the silent one
// would let a stale entry survive a `git rm` until the next full rebuild.
//
// === Scope ===
// Two blocks are gated:
//
//   * src/data/extra/web/pdf.js/ — see above.
//   * src/data/extra/themes/latex-light/ and .../latex-dark/ — a bundled theme
//     is ~33 hand-added <file> lines (palette.json, interface.qss, web.css,
//     highlight.css, text-editor.theme, README.md and 27 SVGs). A missed SVG
//     does not fail anything: interface.qss url()s resolve to a missing qrc
//     path and the affected control simply renders without its indicator.
//     The expected set is derived from the directory listing, never a count,
//     so adding an asset to the folder is what makes the gate demand an entry.
//
// The rest of extra.qrc (the older themes, docs, syntax highlighting, the
// default notebook) is hand-curated and long-stable, so the same risk does not
// apply.
//
// === The runtime subset ===
// The vendored tree deliberately contains files that are NOT shipped inside the
// .rcc. Every exclusion below carries its reason; keep that discipline when
// adding to the list, or the gate degrades into a rubber stamp.
//
// === The locale list ===
// The vendored locale set exists in THREE places: the folders under
// web/pdf.js/web/locale/, the <file> entries in extra.qrc, and the
// "[tag] / @import url(tag/viewer.properties)" pairs in the VNote-generated
// web/locale/locale.properties (which pdf-viewer-template.html loads via
// <link rel="resource" type="application/l10n">). The disk<->qrc slots below
// cover the first two; localePropertiesMatchesVendoredLocales() covers the
// third, because a folder shipped but not imported is silently never loaded and
// an import with no folder 404s inside the page — the same silent-404 class this
// file exists to eliminate.

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QtTest>

namespace tests {

class TestExtraQrcCoverage : public QObject {
  Q_OBJECT

private slots:
  void everyPdfJsFileIsInQrc();
  void everyPdfJsQrcEntryExistsOnDisk();
  void localePropertiesMatchesVendoredLocales();
  void latexThemeFilesMatchQrc_data();
  void latexThemeFilesMatchQrc();

private:
  static QString dataRoot();
  static QString qrcPath();
  static bool isRuntimeFile(const QString &p_relPath);
  static QSet<QString> readPdfJsQrcEntries(QString *p_error);
  static QSet<QString> listPdfJsRuntimeFiles(QString *p_error);
  static QSet<QString> readQrcEntriesUnder(const QString &p_prefix, QString *p_error);
};

// Path prefix (relative to src/data/extra/) of the vendored pdf.js tree. This
// is also the shape of the qrc entries, which are all relative to the
// "/vnotex/data/extra" qresource prefix.
static const char *const c_pdfJsPrefix = "web/pdf.js/";

QString TestExtraQrcCoverage::dataRoot() {
  // VNOTE_SRC_DIR is injected by CMake via target_compile_definitions; see
  // tests/utils/CMakeLists.txt for the registration of this test target.
#ifdef VNOTE_SRC_DIR
  return QStringLiteral(VNOTE_SRC_DIR) + QStringLiteral("/data/extra");
#else
  // Fallback for IDE / out-of-build invocation.
  return QDir::currentPath() + QStringLiteral("/../../../src/data/extra");
#endif
}

QString TestExtraQrcCoverage::qrcPath() { return dataRoot() + QStringLiteral("/extra.qrc"); }

// Returns false for files that are present in the vendored tree but must NOT
// appear in extra.qrc. Every entry needs a reason.
bool TestExtraQrcCoverage::isRuntimeFile(const QString &p_relPath) {
  // web/pdf.js/README.md ⇒ VNote's own provenance note for the vendored tree
  // (upstream URL, version, which files are VNote-owned). Documentation only;
  // no code path reads it. Pre-existing exclusion, predates this test.
  if (p_relPath == QLatin1String("web/pdf.js/README.md")) {
    return false;
  }

  // *.map ⇒ source maps for build/pdf*.js. Only a browser devtools session
  // would fetch them, and they roughly double the size of the .rcc. Not
  // vendored at all today; the check is here so a future re-vendor that copies
  // the release ZIP verbatim does not trip the gate.
  if (p_relPath.endsWith(QLatin1String(".map"))) {
    return false;
  }

  // *.min.js ⇒ VNote loads the non-minified builds (see
  // PdfViewerConfig::defaultViewerResource), so a minified variant shipped
  // alongside would be dead weight in the .rcc.
  if (p_relPath.endsWith(QLatin1String(".min.js"))) {
    return false;
  }

  // Everything else in the tree is fetched by the viewer at runtime, including
  // the LICENSE files under web/cmaps/ and web/standard_fonts/ — those sit
  // beside the assets they cover and are cheap, so they ship with them.
  return true;
}

// Parses the <file>...</file> entries of extra.qrc and returns the subset under
// web/pdf.js/. Deliberately a dumb line scan rather than an XML parse: the file
// is generated one-entry-per-line and a parser would only add a dependency.
QSet<QString> TestExtraQrcCoverage::readPdfJsQrcEntries(QString *p_error) {
  QSet<QString> entries;

  QFile f(qrcPath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *p_error = QStringLiteral("cannot open %1").arg(qrcPath());
    return entries;
  }

  static const QRegularExpression re(QStringLiteral("<file[^>]*>([^<]+)</file>"));

  QTextStream ts(&f);
  while (!ts.atEnd()) {
    const QString line = ts.readLine();
    const auto m = re.match(line);
    if (!m.hasMatch()) {
      continue;
    }
    const QString rel = m.captured(1).trimmed();
    if (rel.startsWith(QLatin1String(c_pdfJsPrefix))) {
      entries.insert(rel);
    }
  }

  return entries;
}

// Walks src/data/extra/web/pdf.js/ and returns every runtime file, as a path
// relative to src/data/extra/ with forward slashes (i.e. qrc entry shape).
QSet<QString> TestExtraQrcCoverage::listPdfJsRuntimeFiles(QString *p_error) {
  QSet<QString> files;

  const QDir extraDir(dataRoot());
  const QString treeRoot = dataRoot() + QLatin1Char('/') + QLatin1String(c_pdfJsPrefix);
  if (!QDir(treeRoot).exists()) {
    *p_error = QStringLiteral("vendored pdf.js tree not found: %1").arg(treeRoot);
    return files;
  }

  QDirIterator it(treeRoot, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString abs = it.next();
    const QString rel = extraDir.relativeFilePath(abs);
    if (isRuntimeFile(rel)) {
      files.insert(rel);
    }
  }

  return files;
}

// The silent failure mode: a vendored file that no qrc entry ships.
void TestExtraQrcCoverage::everyPdfJsFileIsInQrc() {
  QString error;
  const QSet<QString> onDisk = listPdfJsRuntimeFiles(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY2(!onDisk.isEmpty(), "vendored pdf.js tree is empty; the gate would be vacuous");

  const QSet<QString> inQrc = readPdfJsQrcEntries(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  QStringList missing = onDisk.values();
  missing.erase(std::remove_if(missing.begin(), missing.end(),
                               [&inQrc](const QString &p) { return inQrc.contains(p); }),
                missing.end());
  missing.sort();

  if (!missing.isEmpty()) {
    qWarning() << "Vendored pdf.js file(s) missing from extra.qrc:";
    for (const QString &m : missing) {
      qWarning().noquote() << "  " << m;
    }
  }

  QVERIFY2(
      missing.isEmpty(),
      qPrintable(QStringLiteral("%1 vendored pdf.js file(s) have no <file> entry in "
                                "src/data/extra/extra.qrc. They would 404 at runtime inside "
                                "the WebEngine page with no build-time error. Add the entries, "
                                "or exclude them in isRuntimeFile() with a reason.")
                     .arg(missing.size())));
}

// The loud failure mode is rcc's job, but a stale entry only errors once the
// .rcc is regenerated; catch it here too so the two sides cannot drift.
void TestExtraQrcCoverage::everyPdfJsQrcEntryExistsOnDisk() {
  QString error;
  const QSet<QString> inQrc = readPdfJsQrcEntries(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY2(!inQrc.isEmpty(), "no pdf.js entries found in extra.qrc; the gate would be vacuous");

  const QDir extraDir(dataRoot());
  QStringList dangling;
  for (const QString &rel : inQrc) {
    if (!QFile::exists(extraDir.filePath(rel))) {
      dangling.append(rel);
    } else if (!isRuntimeFile(rel)) {
      dangling.append(rel + QStringLiteral(" (excluded by isRuntimeFile())"));
    }
  }
  dangling.sort();

  if (!dangling.isEmpty()) {
    qWarning() << "extra.qrc pdf.js entr(ies) with no matching runtime file:";
    for (const QString &d : dangling) {
      qWarning().noquote() << "  " << d;
    }
  }

  QVERIFY2(
      dangling.isEmpty(),
      qPrintable(QStringLiteral("%1 pdf.js <file> entr(ies) in src/data/extra/extra.qrc do not "
                                "resolve to a runtime file on disk.")
                     .arg(dangling.size())));
}

// The third copy of the locale list. Shipping a locale folder that
// locale.properties does not import means it is silently never loaded; importing
// one that does not exist 404s inside the WebEngine page. Neither shows up in the
// build or in the two slots above.
void TestExtraQrcCoverage::localePropertiesMatchesVendoredLocales() {
  const QString localeDir =
      dataRoot() + QLatin1Char('/') + QLatin1String(c_pdfJsPrefix) + QStringLiteral("web/locale");
  const QDir dir(localeDir);
  QVERIFY2(dir.exists(), qPrintable(QStringLiteral("locale dir not found: %1").arg(localeDir)));

  QSet<QString> onDisk;
  for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    onDisk.insert(name);
  }
  QVERIFY2(!onDisk.isEmpty(), "no vendored locale folders; the gate would be vacuous");

  QFile f(dir.filePath(QStringLiteral("locale.properties")));
  QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
           qPrintable(QStringLiteral("cannot open %1").arg(f.fileName())));

  // e.g.  [zh-CN]
  static const QRegularExpression tagRe(QStringLiteral("^\\s*\\[([^\\]]+)\\]\\s*$"));
  // e.g.  @import url(zh-CN/viewer.properties)
  static const QRegularExpression importRe(QStringLiteral("^\\s*@import\\s+url\\(([^)]+)\\)\\s*$"));

  QSet<QString> declared;
  QStringList badImports;
  QTextStream ts(&f);
  while (!ts.atEnd()) {
    const QString line = ts.readLine();

    const auto tagMatch = tagRe.match(line);
    if (tagMatch.hasMatch()) {
      declared.insert(tagMatch.captured(1).trimmed());
      continue;
    }

    const auto importMatch = importRe.match(line);
    if (importMatch.hasMatch()) {
      const QString rel = importMatch.captured(1).trimmed();
      if (!QFile::exists(dir.filePath(rel))) {
        badImports.append(rel);
      }
    }
  }

  badImports.sort();
  if (!badImports.isEmpty()) {
    qWarning() << "locale.properties imports that do not resolve on disk:";
    for (const QString &b : badImports) {
      qWarning().noquote() << "  " << b;
    }
  }
  QVERIFY2(badImports.isEmpty(),
           qPrintable(QStringLiteral("%1 @import(s) in web/pdf.js/web/locale/locale.properties do "
                                     "not exist on disk; they 404 inside the viewer page.")
                          .arg(badImports.size())));

  QStringList notDeclared = (onDisk - declared).values();
  QStringList notVendored = (declared - onDisk).values();
  notDeclared.sort();
  notVendored.sort();

  QVERIFY2(notDeclared.isEmpty(),
           qPrintable(QStringLiteral("locale folder(s) vendored but not imported by "
                                     "locale.properties (silently never loaded): %1")
                          .arg(notDeclared.join(QStringLiteral(", ")))));
  QVERIFY2(notVendored.isEmpty(),
           qPrintable(QStringLiteral("locale.properties declares locale(s) with no vendored "
                                     "folder: %1")
                          .arg(notVendored.join(QStringLiteral(", ")))));
}

// Generic <file> scan restricted to a path prefix, e.g. "themes/latex-light/".
QSet<QString> TestExtraQrcCoverage::readQrcEntriesUnder(const QString &p_prefix, QString *p_error) {
  QSet<QString> entries;

  QFile f(qrcPath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *p_error = QStringLiteral("cannot open %1").arg(qrcPath());
    return entries;
  }

  static const QRegularExpression re(QStringLiteral("<file[^>]*>([^<]+)</file>"));

  QTextStream ts(&f);
  while (!ts.atEnd()) {
    const auto m = re.match(ts.readLine());
    if (!m.hasMatch()) {
      continue;
    }
    const QString rel = m.captured(1).trimmed();
    if (rel.startsWith(p_prefix)) {
      entries.insert(rel);
    }
  }

  return entries;
}

// Two-way disk<->qrc comparison for the two LaTeX themes. Unlike the pdf.js
// slots there is no "runtime subset": every file in a theme folder ships, so
// the expected set is simply the directory listing. Deriving it that way (and
// not from a count) is what makes a newly added asset fail the gate.
void TestExtraQrcCoverage::latexThemeFilesMatchQrc_data() {
  QTest::addColumn<QString>("themeName");
  QTest::newRow("latex-light") << QStringLiteral("latex-light");
  QTest::newRow("latex-dark") << QStringLiteral("latex-dark");
}

void TestExtraQrcCoverage::latexThemeFilesMatchQrc() {
  QFETCH(QString, themeName);

  const QString prefix = QStringLiteral("themes/") + themeName + QLatin1Char('/');
  const QDir extraDir(dataRoot());
  const QString themeDir = extraDir.filePath(prefix);
  QVERIFY2(QDir(themeDir).exists(),
           qPrintable(QStringLiteral("theme folder not found: %1").arg(themeDir)));

  QSet<QString> onDisk;
  QDirIterator it(themeDir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    onDisk.insert(extraDir.relativeFilePath(it.next()));
  }
  QVERIFY2(!onDisk.isEmpty(), "theme folder is empty; the gate would be vacuous");

  QString error;
  const QSet<QString> inQrc = readQrcEntriesUnder(prefix, &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  QStringList missing = (onDisk - inQrc).values();
  QStringList dangling = (inQrc - onDisk).values();
  missing.sort();
  dangling.sort();

  QVERIFY2(missing.isEmpty(),
           qPrintable(QStringLiteral("%1 file(s) in %2 have no <file> entry in extra.qrc and "
                                     "would 404 at runtime: %3")
                          .arg(missing.size())
                          .arg(prefix, missing.join(QStringLiteral(", ")))));
  QVERIFY2(dangling.isEmpty(),
           qPrintable(QStringLiteral("%1 extra.qrc entr(ies) under %2 do not exist on disk: %3")
                          .arg(dangling.size())
                          .arg(prefix, dangling.join(QStringLiteral(", ")))));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExtraQrcCoverage)
#include "test_extra_qrc_coverage.moc"
