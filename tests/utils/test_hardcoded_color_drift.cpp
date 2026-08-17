// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_hardcoded_color_drift.cpp
//
// Grep-gate regression test for hardcoded colors in widget styling.
//
// === What this test does ===
// Parses every .cpp/.h under ${CMAKE_SOURCE_DIR}/src/, extracts the C++ string
// literals (skipping comments, honoring raw strings, and COALESCING adjacent
// literals the way the compiler does), and fails when one literal contains
// BOTH a CSS color property (color:, background-color:, border:, ...) AND a
// literal color value. That combination is the signature of a hardcoded,
// un-themed style.
//
// A literal color value is a #hex, a functional rgb()/rgba()/hsl()/hsla()/
// hsv()/hsva() with numeric arguments, or any name in QColor::colorNames()
// except "transparent".
//
// === Why this test exists ===
// VNote ships 12 themes, 6 of them dark. A widget that hardcodes e.g.
// "background-color: #FFF3CD; color: #856404;" looks correct only in the theme
// its author happened to be running, and cannot follow a runtime theme switch.
// That drifted into two independently copy-pasted banners and a whole
// info/warning/error ramp in the sync dialog before it was cleaned up.
//
// === What to do instead ===
//   * A notification strip -> vnotex::InlineBanner (severity is themed).
//   * Severity-colored TEXT -> the SeverityText property
//     (PropertyDefs::c_severityText).
//   * Muted / secondary text -> the MutedText property
//     (PropertyDefs::c_mutedText). Do NOT use setEnabled(false): it lies to
//     accessibility tooling, and the themes style QLabel unconditionally with
//     no :disabled variant, so it would not even change the color.
//   * A color computed at runtime -> ThemeService::paletteColor("...")
//     interpolated with .arg(), as NavigationMode does. A style string whose
//     color comes from a %N placeholder is NOT flagged.
//   * Best of all: put the rule in each theme's interface.qss and select on a
//     class name or a dynamic property, so it re-themes for free.
//
// See src/widgets/AGENTS.md "No Hardcoded Colors in C++".
//
// === Known limits (deliberate) ===
// - Only string literals are inspected. A color built at runtime from
//   non-literal pieces (e.g. a QColor constant defined elsewhere and fed
//   through .arg()) is NOT detected; that is indistinguishable from the
//   legitimate ThemeService::paletteColor() pattern without real type
//   analysis.
// - QColor / QPen / QBrush painting inside a QStyledItemDelegate or
//   paintEvent is OUT OF SCOPE; this gate covers stylesheet strings only.
// - Colors used as DATA rather than chrome (a color picker's swatches) are
//   correctly not flagged, because they never pair with a CSS property.
//
// === Allow-list / escape hatch ===
// Add a file to allowedFiles() with a one-line reason, or append
// `// hardcoded-color-allow: <reason>` (or `// NOLINT`) to any line the
// literal spans.

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtTest>

#include "source_literal_scanner.h"

namespace tests {

using literalscan::SourceLiteral;

class TestHardcodedColorDrift : public QObject {
  Q_OBJECT

private slots:
  void scanSrcForHardcodedStyleColors();
  void detectorFlagsARealOffender();
  void detectorIgnoresLegitimatePatterns();
  void extractorHandlesCommentsRawStringsAndConcatenation();

private:
  static const QStringList &allowedFiles();
  static bool isAllowed(const QString &p_relPath);
  static const QStringList &escapeHatchMarkers();

  // The property+value test applied to a single (coalesced) literal.
  static bool isHardcodedStyleColor(const QString &p_literal);

  // Convenience for the self-tests: extract then classify.
  static bool sourceHasHardcodedStyleColor(const QString &p_source);
};

const QStringList &TestHardcodedColorDrift::allowedFiles() {
  static const QStringList files = {
      // No entries. The two literal-color sites in src/ (marknodedialog2's
      // swatch palette and notebookselector2's avatar colors) offer colors as
      // DATA and never pair them with a CSS property, so the detector already
      // ignores them without an allow-list entry.
  };
  return files;
}

bool TestHardcodedColorDrift::isAllowed(const QString &p_relPath) {
  const QString normalized = QString(p_relPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
  for (const QString &allowed : allowedFiles()) {
    if (normalized.endsWith(allowed)) {
      return true;
    }
  }
  return false;
}

const QStringList &TestHardcodedColorDrift::escapeHatchMarkers() {
  static const QStringList markers = {QStringLiteral("// hardcoded-color-allow"),
                                      QStringLiteral("// NOLINT")};
  return markers;
}

bool TestHardcodedColorDrift::isHardcodedStyleColor(const QString &p_literal) {
  static const QRegularExpression colorProp(
      QStringLiteral("\\b(?:background-color|border-color|border-top-color|border-bottom-color|"
                     "border-left-color|border-right-color|selection-background-color|"
                     "selection-color|alternate-background-color|gridline-color|background|color|"
                     "border|border-top|border-bottom|border-left|border-right|outline)\\s*:"));

  static const QRegularExpression hexOrFunctional(
      QStringLiteral("#[0-9a-fA-F]{3,8}\\b|\\b(?:rgba?|hsla?|hsva?)\\s*\\(\\s*\\d"));

  // Qt's full SVG color-name set, so a name like "darkred" or "coral" cannot
  // slip past a hand-written list. "transparent" is a legitimate, theme-neutral
  // value and is excluded.
  static const QRegularExpression namedColor = [] {
    QStringList names = QColor::colorNames();
    names.removeAll(QStringLiteral("transparent"));
    // Longest-first so alternation does not match a prefix of a longer name.
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
    QStringList escaped;
    escaped.reserve(names.size());
    for (const auto &name : names) {
      escaped.append(QRegularExpression::escape(name));
    }
    // Must be followed by a declaration terminator, so prose like "the red
    // notebook" is not mistaken for a value. `}` counts: the semicolon after
    // the LAST declaration in a QSS block is optional, so
    // "QLabel { color: darkgoldenrod }" is valid and must be caught.
    return QRegularExpression(QStringLiteral("\\b(?:%1)\\s*(?:;|!|\\}|$)").arg(escaped.join('|')),
                              QRegularExpression::CaseInsensitiveOption);
  }();

  if (!colorProp.match(p_literal).hasMatch()) {
    return false;
  }
  return hexOrFunctional.match(p_literal).hasMatch() || namedColor.match(p_literal).hasMatch();
}

bool TestHardcodedColorDrift::sourceHasHardcodedStyleColor(const QString &p_source) {
  for (const auto &lit : literalscan::extractLiterals(p_source)) {
    if (isHardcodedStyleColor(lit.text)) {
      return true;
    }
  }
  return false;
}

void TestHardcodedColorDrift::extractorHandlesCommentsRawStringsAndConcatenation() {
  // Adjacent literals concatenate, including across lines.
  auto lits = literalscan::extractLiterals(QStringLiteral("f(\"color: \"\n   \"#fff;\");"));
  QCOMPARE(lits.size(), 1);
  QCOMPARE(lits.at(0).text, QStringLiteral("color: #fff;"));
  QCOMPARE(lits.at(0).firstLine, 1);
  QCOMPARE(lits.at(0).lastLine, 2);

  // A comment between them does not break concatenation.
  lits = literalscan::extractLiterals(QStringLiteral("f(\"a\" /* c */ \"b\");"));
  QCOMPARE(lits.size(), 1);
  QCOMPARE(lits.at(0).text, QStringLiteral("ab"));

  // An operator between them DOES break it.
  lits = literalscan::extractLiterals(QStringLiteral("s << \"a\" << \"b\";"));
  QCOMPARE(lits.size(), 2);

  // Literals inside comments are not literals.
  QCOMPARE(literalscan::extractLiterals(QStringLiteral("// \"color: red;\"\n")).size(), 0);
  QCOMPARE(literalscan::extractLiterals(QStringLiteral("/* \"color: red;\" */\n")).size(), 0);

  // Raw strings are read whole, quotes and all.
  lits = literalscan::extractLiterals(QStringLiteral("auto s = R\"(color: \"#fff\";)\";"));
  QCOMPARE(lits.size(), 1);
  QVERIFY(lits.at(0).text.contains(QStringLiteral("#fff")));

  // A quote inside a char literal must not open a string.
  lits = literalscan::extractLiterals(QStringLiteral("if (c == '\"') { f(\"ok\"); }"));
  QCOMPARE(lits.size(), 1);
  QCOMPARE(lits.at(0).text, QStringLiteral("ok"));
}

void TestHardcodedColorDrift::detectorFlagsARealOffender() {
  // The exact shapes this gate was written to catch, verbatim from the code it
  // replaced.
  QVERIFY(sourceHasHardcodedStyleColor(QStringLiteral(
      R"(setStyleSheet(QStringLiteral("QWidget { background-color: #FFF3CD; }"));)")));
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(l->setStyleSheet(QStringLiteral("color: #1f6feb;")); // blue)")));
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(l->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));)")));
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(w->setStyleSheet("border-bottom: 1px solid #FFEEBA;");)")));
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(w->setStyleSheet("background-color: rgba(0, 0, 0, 128);");)")));
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(w->setStyleSheet("color: hsl(120, 50%, 50%);");)")));

  // Names outside any hand-written shortlist must still be caught.
  QVERIFY(sourceHasHardcodedStyleColor(QStringLiteral(R"(w->setStyleSheet("color: darkred;");)")));
  QVERIFY(sourceHasHardcodedStyleColor(QStringLiteral(R"(w->setStyleSheet("color: coral;");)")));

  // The semicolon after the LAST declaration in a QSS block is optional, so a
  // closing brace is a valid terminator too.
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(w->setStyleSheet("QLabel { color: darkgoldenrod }");)")));
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral(R"(w->setStyleSheet("QLabel { color: #abc }");)")));

  // Splitting the string across lines must not defeat the gate.
  QVERIFY(sourceHasHardcodedStyleColor(QStringLiteral("w->setStyleSheet(QStringLiteral(\n"
                                                      "    \"color: \"\n"
                                                      "    \"#ffffff;\"));")));
  // Neither must a raw string.
  QVERIFY(sourceHasHardcodedStyleColor(
      QStringLiteral("w->setStyleSheet(QStringLiteral(R\"(\ncolor: #ffffff;\n)\"));")));
}

void TestHardcodedColorDrift::detectorIgnoresLegitimatePatterns() {
  // Colors as DATA, with no CSS property in sight.
  QVERIFY(
      !sourceHasHardcodedStyleColor(QStringLiteral(R"(    QStringLiteral("#e53935"), // Red)")));
  QVERIFY(!sourceHasHardcodedStyleColor(QStringLiteral(R"(  p_fg = "#ffffff";)")));
  // Color supplied at runtime through a placeholder.
  QVERIFY(!sourceHasHardcodedStyleColor(
      QStringLiteral(R"(  QStringLiteral("color: %1; background-color: %2;"))")));
  // Colorless style strings.
  QVERIFY(!sourceHasHardcodedStyleColor(
      QStringLiteral(R"(setStyleSheet(QStringLiteral("QLabel { font-style: italic; }"));)")));
  QVERIFY(!sourceHasHardcodedStyleColor(QStringLiteral(
      R"(setStyleSheet(QStringLiteral("QCheckBox { background: transparent; }"));)")));
  QVERIFY(!sourceHasHardcodedStyleColor(QStringLiteral(
      R"(setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));)")));
  QVERIFY(!sourceHasHardcodedStyleColor(QStringLiteral(R"(  "border: 1px solid @base#info#fg;")")));
  // Prose that merely names a property or a color.
  QVERIFY(
      !sourceHasHardcodedStyleColor(QStringLiteral("// the background-color came from #FFF3CD")));
  QVERIFY(!sourceHasHardcodedStyleColor(
      QStringLiteral(R"(  tr("Pick a border: red, green or blue are fine"))")));
}

void TestHardcodedColorDrift::scanSrcForHardcodedStyleColors() {
#ifdef VNOTE_SRC_DIR
  const QString srcRoot = QStringLiteral(VNOTE_SRC_DIR);
#else
  const QString srcRoot = QDir::currentPath() + QStringLiteral("/../../../src");
#endif
  QVERIFY2(QDir(srcRoot).exists(),
           qPrintable(QStringLiteral("src/ root not found: %1").arg(srcRoot)));

  const QDir srcDir(srcRoot);
  QStringList violations;
  QStringList unreadable;
  QSet<QString> scanned;

  QDirIterator it(srcRoot, {QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString filePath = it.next();
    const QString relPath =
        srcDir.relativeFilePath(filePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (isAllowed(relPath)) {
      continue;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      unreadable.append(relPath);
      continue;
    }
    const QString source = QString::fromUtf8(f.readAll());
    scanned.insert(relPath);

    const QStringList lines = source.split(QLatin1Char('\n'));
    for (const auto &lit : literalscan::extractLiterals(source)) {
      if (!isHardcodedStyleColor(lit.text)) {
        continue;
      }
      // The escape hatch may sit on any line the literal spans.
      if (!literalscan::hasEscapeHatch(lines, lit, escapeHatchMarkers())) {
        violations.append(
            QStringLiteral("%1:%2: %3").arg(relPath).arg(lit.firstLine).arg(lit.text));
      }
    }
  }

  // A silently-empty scan would make this test permanently green. Assert the
  // scan actually READ files, and that specific known files were among them.
  QVERIFY2(unreadable.isEmpty(), qPrintable(QStringLiteral("unreadable source file(s): %1")
                                                .arg(unreadable.join(QStringLiteral(", ")))));
  QVERIFY2(
      scanned.size() > 100,
      qPrintable(
          QStringLiteral("only %1 file(s) read - the scan root looks wrong").arg(scanned.size())));
  for (const auto &anchor : {QStringLiteral("main.cpp"), QStringLiteral("widgets/inlinebanner.cpp"),
                             QStringLiteral("widgets/dialogs/notebooksyncinfodialog2.cpp")}) {
    QVERIFY2(scanned.contains(anchor),
             qPrintable(QStringLiteral("anchor file was not scanned: %1").arg(anchor)));
  }

  if (!violations.isEmpty()) {
    qWarning() << "Found" << violations.size() << "hardcoded style color(s) across"
               << scanned.size() << "scanned file(s):";
    for (const QString &v : violations) {
      qWarning().noquote() << "  " << v;
    }
  }

  QVERIFY2(violations.isEmpty(),
           qPrintable(
               QStringLiteral(
                   "Found %1 hardcoded style color(s) in src/. VNote ships 12 themes, 6 of them "
                   "dark, so a literal color is only correct in whichever theme its author was "
                   "running. Use vnotex::InlineBanner, the SeverityText or MutedText property, or "
                   "ThemeService::paletteColor(). See src/widgets/AGENTS.md 'No Hardcoded Colors "
                   "in C++'. If the literal is genuinely DATA rather than chrome, append "
                   "'// hardcoded-color-allow: <reason>'.")
                   .arg(violations.size())));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHardcodedColorDrift)
#include "test_hardcoded_color_drift.moc"
