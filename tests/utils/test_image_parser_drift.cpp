// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_image_parser_drift.cpp
//
// Grep-gate regression test: no regular-expression Markdown image parser may
// come back.
//
// === What this test does ===
// Scans every .cpp/.h under ${CMAKE_SOURCE_DIR}/src/ and
// ${CMAKE_SOURCE_DIR}/libs/vtextedit/src/, extracts the C++ string literals
// (comment-aware, raw-string aware, coalescing adjacent literals the way the
// compiler does), and fails when a literal looks like a regular expression that
// parses the Markdown image syntax: an escaped `![` together with an escaped
// `]` and an escaped `(`.
//
// === Why this test exists ===
// vtextedit 2d07683 + vnote 5f811d45 deleted the optional `(\s*=(\d*)x(\d*))?`
// group from `c_imageLinkRegExp` and inlined what was left at three call sites.
// Removing the group did not make the expression IGNORE a `=500x` token -- it
// made the expression FAIL TO MATCH, because `\s*\)` then had to consume
// ` =500x)`. Every `![](img.png =500x)` in every document silently lost its
// in-place preview and its Image context menu, and stayed broken for three and
// a half months.
//
// That commit looked like a safe refactor. It touched only "dead" regex
// infrastructure, and it passed every test in the tree. Nothing short of a gate
// on the *existence* of a second parser would have stopped it, because the
// defect was not in any single line of the expression -- it was that a hand
// written approximation of the CommonMark link grammar existed at all, next to
// a real parser that already knew the answer.
//
// === What to do instead ===
//   * In the editor, against the live document -> the parse that produced the
//     on-screen highlighting already knows every image:
//     MarkdownHighlighter::getImageLinks() returns region + resolved
//     destination + declared `=WxH` size. Look one up positionally with
//     vnotex::ImageLinkLookup::imageLinkAt().
//   * Against arbitrary content -> vte::MarkdownUtils::fetchImageLinks(),
//     which walks cmark's AST and reports exact spans for both the whole
//     construct and the RAW destination, plus the resolved path and type.
//
// Either way the destination arrives already unescaped and entity-decoded, and
// nothing has to re-derive the grammar of balanced parentheses, angle-bracket
// destinations, three flavours of title delimiter, or the `=WxH` extension.
//
// === Known limits (deliberate) ===
// - Only string literals are inspected. An expression assembled at runtime from
//   non-literal pieces is not detected.
// - A literal that merely mentions `![` (a template, an inserted snippet, a
//   test fixture) is NOT flagged: the signature requires the regex-escaped
//   forms of all three of `![`, `]` and `(`.
//
// === HTML `<img>` too ===
// The same rule applies to the HTML spelling of an image. There is exactly ONE
// note-source `<img>` parser, vte::scanHtmlImgTags()
// (libs/vtextedit/src/include/vtextedit/htmlimgscanner.h), and both the snapshot
// API and the live editor walker call it. A second one would drift on quoting,
// casing, entities, raw-text elements and the single-line rule, exactly as the
// second Markdown parser drifted on `=WxH`.
//
// Scanners over RENDERED or CLIPBOARD html are a different problem and are
// exempt: WebViewExporter rewrites what the web view produced, and the paste
// path inspects what another application put on the clipboard. Neither ever
// sees note source. Both carry a line-local `// image-parser-allow:` hatch
// naming the reason; neither file is allow-listed wholesale.
//
// === Allow-list / escape hatch ===
// Add a file to allowedFiles() with a one-line reason, or append
// `// image-parser-allow: <reason>` (or `// NOLINT`) to any line the literal
// spans.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtTest>

#include "source_literal_scanner.h"

namespace tests {

using literalscan::SourceLiteral;

class TestImageParserDrift : public QObject {
  Q_OBJECT

private slots:
  void scanForRegexImageParsers();
  void detectorFlagsTheRegressionItGuards();
  void detectorIgnoresLegitimatePatterns();
  void scanForHtmlImgParsers();
  void htmlDetectorFlagsAttributeRegexes();
  void htmlDetectorIgnoresLegitimatePatterns();

private:
  static const QStringList &allowedFiles();
  static const QStringList &htmlAllowedFiles();
  static const QStringList &scannedRoots();
  static const QStringList &escapeHatchMarkers();
  static bool isAllowed(const QString &p_relPath);
  static bool isAllowedIn(const QString &p_relPath, const QStringList &p_allowed);
  static bool isImageLinkRegex(const QString &p_literal);
  static bool isHtmlImgRegex(const QString &p_literal);
  static bool sourceHasImageLinkRegex(const QString &p_source);
  static bool sourceHasHtmlImgRegex(const QString &p_source);
};

const QStringList &TestImageParserDrift::htmlAllowedFiles() {
  static const QStringList files = {
      // THE note-source `<img>` parser. Its whole job is to pattern-match one.
      QStringLiteral("libs/vtextedit/src/utils/htmlimgscanner.cpp"),
      QStringLiteral("libs/vtextedit/src/include/vtextedit/htmlimgscanner.h"),
  };
  return files;
}

const QStringList &TestImageParserDrift::allowedFiles() {
  static const QStringList files = {
      // No entries. This file contains image-link regexes in its self-tests,
      // but it lives under tests/ and the scanner only walks the two source
      // roots below, so it never sees itself.
  };
  return files;
}

const QStringList &TestImageParserDrift::scannedRoots() {
  static const QStringList roots = {
      QStringLiteral(VNOTE_SOURCE_DIR) + QStringLiteral("/src"),
      // vtextedit is where the deleted parser lived, and where the preview path
      // that consumed it still lives.
      QStringLiteral(VNOTE_SOURCE_DIR) + QStringLiteral("/libs/vtextedit/src"),
  };
  return roots;
}

bool TestImageParserDrift::isAllowedIn(const QString &p_relPath, const QStringList &p_allowed) {
  const QString normalized = QString(p_relPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
  for (const QString &allowed : p_allowed) {
    if (normalized.endsWith(allowed)) {
      return true;
    }
  }
  return false;
}

bool TestImageParserDrift::isAllowed(const QString &p_relPath) {
  return isAllowedIn(p_relPath, allowedFiles());
}

const QStringList &TestImageParserDrift::escapeHatchMarkers() {
  static const QStringList markers = {QStringLiteral("// image-parser-allow"),
                                      QStringLiteral("// NOLINT")};
  return markers;
}

bool TestImageParserDrift::isImageLinkRegex(const QString &p_literal) {
  // The signature of a Markdown-image regular expression: the escaped image
  // marker, plus the escaped bracket and paren that make it a *link* pattern
  // rather than a passing mention of "![".
  const bool hasBang =
      p_literal.contains(QStringLiteral("\\!\\[")) || p_literal.contains(QStringLiteral("!\\["));
  if (!hasBang) {
    return false;
  }
  return p_literal.contains(QStringLiteral("\\]")) && p_literal.contains(QStringLiteral("\\("));
}

bool TestImageParserDrift::sourceHasImageLinkRegex(const QString &p_source) {
  for (const auto &lit : literalscan::extractLiterals(p_source)) {
    if (isImageLinkRegex(lit.text)) {
      return true;
    }
  }
  return false;
}

// The signature of an HTML `<img>` ATTRIBUTE regular expression: the tag name
// together with an attribute name AND a regex construct that captures or
// skips over its value. A literal that merely spells a tag (a generator, a
// test fixture, a template) has no regex construct and is not flagged.
bool TestImageParserDrift::isHtmlImgRegex(const QString &p_literal) {
  const QString lower = p_literal.toLower();
  if (!lower.contains(QStringLiteral("<img"))) {
    return false;
  }

  static const QStringList attrs = {QStringLiteral("src"), QStringLiteral("width"),
                                    QStringLiteral("height"), QStringLiteral("alt"),
                                    QStringLiteral("title")};
  bool hasAttr = false;
  for (const QString &attr : attrs) {
    if (lower.contains(attr)) {
      hasAttr = true;
      break;
    }
  }
  if (!hasAttr) {
    return false;
  }

  // A regex construct, spelled as it appears in a C++ literal (so the escapes
  // are doubled).
  static const QStringList constructs = {
      QStringLiteral("([^"), QStringLiteral("(.*"), QStringLiteral("(.+"), QStringLiteral("[^>]"),
      QStringLiteral("\\s"), QStringLiteral("\\S"), QStringLiteral("\\w"), QStringLiteral("\\d"),
      QStringLiteral("(?:"), QStringLiteral("(?!")};
  for (const QString &construct : constructs) {
    if (p_literal.contains(construct)) {
      return true;
    }
  }
  return false;
}

bool TestImageParserDrift::sourceHasHtmlImgRegex(const QString &p_source) {
  for (const auto &lit : literalscan::extractLiterals(p_source)) {
    if (isHtmlImgRegex(lit.text)) {
      return true;
    }
  }
  return false;
}

void TestImageParserDrift::scanForRegexImageParsers() {
  QStringList offenders;

  for (const QString &root : scannedRoots()) {
    QVERIFY2(QDir(root).exists(), qPrintable(QStringLiteral("missing root: ") + root));

    int filesInRoot = 0;
    QDirIterator it(root, QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      const QString rel = QDir(QStringLiteral(VNOTE_SOURCE_DIR)).relativeFilePath(path);
      if (isAllowed(rel)) {
        continue;
      }

      QFile f(path);
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        continue;
      }
      const QString source = QString::fromUtf8(f.readAll());
      ++filesInRoot;

      const QStringList lines = source.split(QLatin1Char('\n'));
      for (const auto &lit : literalscan::extractLiterals(source)) {
        if (!isImageLinkRegex(lit.text)) {
          continue;
        }

        if (literalscan::hasEscapeHatch(lines, lit, escapeHatchMarkers())) {
          continue;
        }

        offenders.append(QStringLiteral("%1:%2: %3").arg(rel).arg(lit.firstLine).arg(lit.text));
      }
    }

    // Per-root, not aggregate: the application root alone is large enough to
    // satisfy a combined floor while the vtextedit root silently contributes
    // nothing (a moved directory, a bad relative path).
    QVERIFY2(filesInRoot > 50,
             qPrintable(QStringLiteral("scanned only %1 files under %2; the root looks wrong")
                            .arg(filesInRoot)
                            .arg(root)));
  }

  if (!offenders.isEmpty()) {
    const QString message =
        QStringLiteral("A regular-expression Markdown image parser reappeared:\n  %1\n\n"
                       "Do not re-derive the image grammar. In the editor use "
                       "MarkdownHighlighter::getImageLinks() (with "
                       "vnotex::ImageLinkLookup::imageLinkAt() for a positional query); "
                       "over arbitrary content use vte::MarkdownUtils::fetchImageLinks(). "
                       "Both come from cmark and already handle escaped and angle-bracketed "
                       "destinations, balanced parens, all three title "
                       "delimiters and the `=WxH` size extension.\n\n"
                       "See tests/utils/test_image_parser_drift.cpp for the history.")
            .arg(offenders.join(QStringLiteral("\n  ")));
    QFAIL(qPrintable(message));
  }
}

// The exact expression that was deleted from vtextedit, and the one that
// replaced it. Both must be caught.
void TestImageParserDrift::detectorFlagsTheRegressionItGuards() {
  // image-parser-allow: the regression this gate exists to catch, as data.
  const QString withSize = QStringLiteral(
      "QRegularExpression re(QStringLiteral(\"\\\\!\\\\[([^\\\\[\\\\]]*)\\\\]\""
      "\"\\\\(\\\\s*([^\\\\)\\\"'\\\\s]+)(\\\\s*=(\\\\d*)x(\\\\d*))?\\\\s*\\\\)\"));");
  QVERIFY(sourceHasImageLinkRegex(withSize));

  // image-parser-allow: the post-regression form, which silently stopped
  // matching every sized image.
  const QString withoutSize =
      QStringLiteral("auto r = QStringLiteral(\"\\\\!\\\\[([^\\\\[\\\\]]*)\\\\]\\\\(\\\\s*\""
                     "\"([^\\\\)\\\"'\\\\s]+)\\\\s*\\\\)\");");
  QVERIFY(sourceHasImageLinkRegex(withoutSize));

  // Split across adjacent literals, the way the original was written.
  // image-parser-allow: concatenation case.
  const QString concatenated = QStringLiteral("QStringLiteral(\"\\\\!\\\\[\"\n"
                                              "               \"([^\\\\]]*)\\\\]\"\n"
                                              "               \"\\\\((.*)\\\\)\")");
  QVERIFY(sourceHasImageLinkRegex(concatenated));

  // Preceded by a character literal holding a double quote. Without character
  // literal skipping the phantom string opened by that `"` swallows the real
  // one, and the regex below goes unnoticed.
  // image-parser-allow: character-literal shadowing case.
  const QString afterCharLiteral =
      QStringLiteral("if (c == QLatin1Char('\"')) { return; }\n"
                     "auto r = QStringLiteral(\"\\\\!\\\\[(.*)\\\\]\\\\((.*)\\\\)\");");
  QVERIFY(sourceHasImageLinkRegex(afterCharLiteral));

  // An escaped quote inside a character literal must not desynchronise either.
  // image-parser-allow: escaped character-literal case.
  const QString afterEscapedCharLiteral =
      QStringLiteral("const QChar q = '\\\\';\n"
                     "auto r = QStringLiteral(\"\\\\!\\\\[(.*)\\\\]\\\\((.*)\\\\)\");");
  QVERIFY(sourceHasImageLinkRegex(afterEscapedCharLiteral));
}

void TestImageParserDrift::detectorIgnoresLegitimatePatterns() {
  // A template or snippet that merely contains the image syntax.
  QVERIFY(!sourceHasImageLinkRegex(QStringLiteral("auto s = QStringLiteral(\"![%1](%2)\");")));

  // A test fixture or a generated link.
  QVERIFY(!sourceHasImageLinkRegex(
      QStringLiteral("const QString md = QStringLiteral(\"![alt](a.png =500x)\\n\");")));

  // A regex for something else entirely.
  QVERIFY(!sourceHasImageLinkRegex(
      QStringLiteral("QRegularExpression re(QStringLiteral(\"\\\\[([^\\\\]]*)\\\\]\"));")));

  // Inside a comment, so not a literal at all.
  QVERIFY(!sourceHasImageLinkRegex(
      QStringLiteral("// the old pattern was \"\\\\!\\\\[(.*)\\\\]\\\\((.*)\\\\)\"\nint x = 0;")));
}

void TestImageParserDrift::scanForHtmlImgParsers() {
  QStringList offenders;

  for (const QString &root : scannedRoots()) {
    QVERIFY2(QDir(root).exists(), qPrintable(QStringLiteral("missing root: ") + root));

    int filesInRoot = 0;
    QDirIterator it(root, QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      const QString rel = QDir(QStringLiteral(VNOTE_SOURCE_DIR)).relativeFilePath(path);
      if (isAllowedIn(rel, htmlAllowedFiles())) {
        continue;
      }

      QFile f(path);
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        continue;
      }
      const QString source = QString::fromUtf8(f.readAll());
      ++filesInRoot;

      const QStringList lines = source.split(QLatin1Char('\n'));
      for (const auto &lit : literalscan::extractLiterals(source)) {
        if (!isHtmlImgRegex(lit.text)) {
          continue;
        }
        if (literalscan::hasEscapeHatch(lines, lit, escapeHatchMarkers())) {
          continue;
        }
        offenders.append(QStringLiteral("%1:%2: %3").arg(rel).arg(lit.firstLine).arg(lit.text));
      }
    }

    QVERIFY2(filesInRoot > 50,
             qPrintable(QStringLiteral("scanned only %1 files under %2; the root looks wrong")
                            .arg(filesInRoot)
                            .arg(root)));
  }

  if (!offenders.isEmpty()) {
    const QString message =
        QStringLiteral("A second HTML `<img>` parser appeared:\n  %1\n\n"
                       "Note-source `<img>` parsing has exactly ONE implementation, "
                       "vte::scanHtmlImgTags() in "
                       "libs/vtextedit/src/include/vtextedit/htmlimgscanner.h. It already "
                       "handles case-insensitive names, single/double/unquoted values, "
                       "HTML entities, comments, raw-text elements (script/style/"
                       "textarea/title) and the single-line rule, and it reports "
                       "byte-exact attribute spans.\n\n"
                       "If your scanner operates on RENDERED or CLIPBOARD html rather "
                       "than note source, append `// image-parser-allow: <reason>` to "
                       "the offending line.")
            .arg(offenders.join(QStringLiteral("\n  ")));
    QFAIL(qPrintable(message));
  }
}

void TestImageParserDrift::htmlDetectorFlagsAttributeRegexes() {
  // image-parser-allow: the shapes this gate exists to catch, as data.
  QVERIFY(sourceHasHtmlImgRegex(
      QStringLiteral("QRegularExpression re(\"<img ([^>]*)src=\\\"([^\\\"]+)\\\"([^>]*)>\");")));
  // image-parser-allow: whitespace-based variant.
  QVERIFY(sourceHasHtmlImgRegex(
      QStringLiteral("auto r = QStringLiteral(\"<img\\\\s+width=(\\\\d+)\");")));
  // image-parser-allow: non-capturing variant.
  QVERIFY(sourceHasHtmlImgRegex(QStringLiteral("auto r = QStringLiteral(\"<IMG(?:\\\\s+alt)\");")));
}

void TestImageParserDrift::htmlDetectorIgnoresLegitimatePatterns() {
  // A generator, which spells a tag but parses nothing.
  QVERIFY(!sourceHasHtmlImgRegex(
      QStringLiteral("auto s = QStringLiteral(\"<img src=\\\"%1\\\" width=\\\"%2\\\" />\");")));

  // A test fixture / expected value.
  QVERIFY(!sourceHasHtmlImgRegex(
      QStringLiteral("const QString html = QStringLiteral(\"<img src=\\\"a.png\\\">\");")));

  // A regex for a different tag entirely.
  QVERIFY(!sourceHasHtmlImgRegex(QStringLiteral(
      "QRegularExpression re(QStringLiteral(\"<a ([^>]*)href=\\\"([^\\\"]+)\\\"\"));")));

  // Inside a comment, so not a literal at all.
  QVERIFY(!sourceHasHtmlImgRegex(
      QStringLiteral("// the old pattern was \"<img ([^>]*)src=(.*)>\"\nint x = 0;")));
}

} // namespace tests

QTEST_APPLESS_MAIN(tests::TestImageParserDrift)

#include "test_image_parser_drift.moc"
