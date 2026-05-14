#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QTemporaryDir>

#include <core/theme.h>

namespace tests {

class TestTheme : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testStub();
  void testTranslateStyleByPalette_jsonQuotedValue();
  void testTranslateStyleByPalette_qssStillWorks();
  void testTranslateStyleByPalette_colonPrefixStillWorks();
  void testFetchWebStyleSheet_resolvesTokens();
  void testFetchTextEditorStyle_resolvesTokensInJson();
  void testFetchWebStyleSheet_emptyPathReturnsEmpty();
  void testFetchWebStyleSheet_noTokensUnchanged();
  void testFetchTextEditorStyle_noTokensUnchanged();
  void testThemeFullyResolved_pure();
  void testThemeFullyResolved_everforestDark();
  void testThemeFullyResolved_moonlight();
  void testEditorWebConceptParity_pure();
  void testEditorWebConceptParity_everforestDark();
  void testEditorWebConceptParity_moonlight();
};

void TestTheme::initTestCase() {}

void TestTheme::cleanupTestCase() {}

void TestTheme::testStub() {
  QVERIFY(true);
}

// T1 acceptance: regex must match @-tokens preceded by " (JSON-quoted values).
void TestTheme::testTranslateStyleByPalette_jsonQuotedValue() {
  QJsonObject palette;
  QJsonObject baseObj;
  baseObj["fg"] = QStringLiteral("#222222");
  palette["base"] = baseObj;

  QString style = QStringLiteral("\"text-color\": \"@base#fg\"");
  vnotex::Theme::translateStyleByPalette(palette, style);

  QVERIFY2(style.contains(QStringLiteral("\"#222222\"")),
           qPrintable(QStringLiteral("Expected resolved hex inside quotes; got: %1").arg(style)));
  QVERIFY2(!style.contains(QStringLiteral("@base#fg")),
           qPrintable(QStringLiteral("Token should be replaced; got: %1").arg(style)));
}

// Regression: whitespace-prefixed tokens (existing QSS pattern) still resolve.
void TestTheme::testTranslateStyleByPalette_qssStillWorks() {
  QJsonObject palette;
  QJsonObject baseObj;
  QJsonObject infoObj;
  infoObj["fg"] = QStringLiteral("#1976d2");
  baseObj["info"] = infoObj;
  palette["base"] = baseObj;

  QString style = QStringLiteral("border: 1px solid @base#info#fg;");
  vnotex::Theme::translateStyleByPalette(palette, style);

  QCOMPARE(style, QStringLiteral("border: 1px solid #1976d2;"));
}

// Regression: colon-prefixed tokens (existing QSS pattern, no whitespace) still resolve.
void TestTheme::testTranslateStyleByPalette_colonPrefixStillWorks() {
  QJsonObject palette;
  QJsonObject baseObj;
  baseObj["fg"] = QStringLiteral("#222222");
  palette["base"] = baseObj;

  QString style = QStringLiteral("color:@base#fg;");
  vnotex::Theme::translateStyleByPalette(palette, style);

  QCOMPARE(style, QStringLiteral("color:#222222;"));
}

namespace {
// Helper: clone the pure theme into a temp dir, optionally overriding files.
// Returns the temp dir path. Caller must keep the QTemporaryDir alive.
QString findPureThemePath() {
  // Try test source directory relative path first.
  QString p = QFINDTESTDATA("../../src/data/extra/themes/pure");
  if (p.isEmpty()) {
    p = QFINDTESTDATA("src/data/extra/themes/pure");
  }
  return p;
}

// Helper: locate the no-token fixture directory.
// This fixture is guaranteed to never have tokens, suitable for byte-identity tests.
QString findNoTokenFixturePath() {
  QString p = QFINDTESTDATA("../data/themes/no-tokens");
  if (p.isEmpty()) {
    p = QFINDTESTDATA("tests/data/themes/no-tokens");
  }
  return p;
}

// Helper: locate a packaged theme directory by name (e.g. "pure", "everforest-dark", "moonlight").
QString findThemePath(const QString &p_themeName) {
  QString p = QFINDTESTDATA(QStringLiteral("../../src/data/extra/themes/%1").arg(p_themeName));
  if (p.isEmpty()) {
    p = QFINDTESTDATA(QStringLiteral("src/data/extra/themes/%1").arg(p_themeName));
  }
  return p;
}

// Helper: returns the value of `p_property` in the FIRST CSS block whose selector
// matches `p_selector` exactly (whitespace-tolerant). Trims whitespace. Returns
// an empty string when no match is found. Case-sensitive property name.
QString extractCssColor(const QString &p_css, const QString &p_selector, const QString &p_property) {
  // Match the selector text exactly, anchored either at start-of-content or after `}`.
  // Capture the block body up to the next `}`.
  QRegularExpression re(QStringLiteral("(?:^|\\})\\s*%1\\s*\\{([^}]*)\\}")
                            .arg(QRegularExpression::escape(p_selector)));
  auto match = re.match(p_css);
  if (!match.hasMatch()) {
    return QString();
  }
  const QString block = match.captured(1);
  // Use a negative lookbehind so `color` does not accidentally match the
  // `color` segment inside `background-color`.
  QRegularExpression propRe(QStringLiteral("(?<![-\\w])%1\\s*:\\s*([^;}]+)")
                                .arg(QRegularExpression::escape(p_property)));
  auto pmatch = propRe.match(block);
  if (!pmatch.hasMatch()) {
    return QString();
  }
  return pmatch.captured(1).trimmed();
}

bool copyDir(const QString &src, const QString &dst) {
  QDir().mkpath(dst);
  QDir s(src);
  for (const auto &entry : s.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
    if (!QFile::copy(entry.absoluteFilePath(), QDir(dst).filePath(entry.fileName()))) {
      return false;
    }
  }
  for (const auto &entry : s.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!copyDir(entry.absoluteFilePath(), QDir(dst).filePath(entry.fileName()))) {
      return false;
    }
  }
  return true;
}

bool writeUtf8(const QString &path, const QString &content) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }
  f.write(content.toUtf8());
  return true;
}
} // anonymous namespace

void TestTheme::testFetchWebStyleSheet_resolvesTokens() {
  QString src = findPureThemePath();
  QVERIFY2(!src.isEmpty(), "pure theme fixture not found");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QString themeDir = tmp.filePath("pure-tokenized");
  QVERIFY(copyDir(src, themeDir));

  // Override web.css with a tokenized version. The pure palette has fg3_5 = #222222.
  QVERIFY(writeUtf8(QDir(themeDir).filePath("web.css"),
                    QStringLiteral("body { color: @palette#fg3_5; }")));

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(themeDir));
  QVERIFY(theme);
  QString out = theme->fetchWebStyleSheet();
  QVERIFY2(out.contains(QStringLiteral("#222222")),
           qPrintable(QStringLiteral("expected resolved hex; got: %1").arg(out)));
  QVERIFY2(!out.contains(QStringLiteral("@palette")),
           qPrintable(QStringLiteral("token should be replaced; got: %1").arg(out)));
}

void TestTheme::testFetchTextEditorStyle_resolvesTokensInJson() {
  QString src = findPureThemePath();
  QVERIFY2(!src.isEmpty(), "pure theme fixture not found");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QString themeDir = tmp.filePath("pure-tokenized");
  QVERIFY(copyDir(src, themeDir));

  // Override text-editor.theme: a minimal valid JSON with a tokenized color.
  // Note: this stub does NOT need to be a complete vtextedit theme; we only
  // exercise the token resolution pass, not the JSON-into-vtextedit load.
  QVERIFY(writeUtf8(QDir(themeDir).filePath("text-editor.theme"),
                    QStringLiteral("{ \"editor-styles\": { \"Text\": { \"text-color\": \"@palette#fg3_5\" } } }")));

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(themeDir));
  QVERIFY(theme);
  QString out = theme->fetchTextEditorStyle();
  QVERIFY2(out.contains(QStringLiteral("\"#222222\"")),
           qPrintable(QStringLiteral("expected resolved hex inside quotes; got: %1").arg(out)));
  QVERIFY2(!out.contains(QStringLiteral("@palette#fg3_5")),
           qPrintable(QStringLiteral("token should be replaced; got: %1").arg(out)));
}

void TestTheme::testFetchWebStyleSheet_emptyPathReturnsEmpty() {
  // Build a theme folder without web.css
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QString themeDir = tmp.filePath("empty-theme");
  QDir().mkpath(themeDir);
  // palette.json is required for Theme::fromFolder() to succeed
  QVERIFY(writeUtf8(QDir(themeDir).filePath("palette.json"),
                    QStringLiteral("{ \"metadata\": { \"revision\": 1 }, \"palette\": {} }")));

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(themeDir));
  QVERIFY(theme);
  QString out = theme->fetchWebStyleSheet();
  QVERIFY2(out.isEmpty(), "missing web.css should yield empty string");
}

void TestTheme::testFetchWebStyleSheet_noTokensUnchanged() {
  QString src = findNoTokenFixturePath();
  QVERIFY2(!src.isEmpty(), "no-token fixture not found");

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(src));
  QVERIFY(theme);
  QString out = theme->fetchWebStyleSheet();

  // Read original file content for byte-for-byte comparison.
  QFile rawFile(QDir(src).filePath("web.css"));
  QVERIFY(rawFile.open(QIODevice::ReadOnly | QIODevice::Text));
  QString raw = QString::fromUtf8(rawFile.readAll());

  QCOMPARE(out, raw);
}

void TestTheme::testFetchTextEditorStyle_noTokensUnchanged() {
  QString src = findNoTokenFixturePath();
  QVERIFY2(!src.isEmpty(), "no-token fixture not found");

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(src));
  QVERIFY(theme);
  QString out = theme->fetchTextEditorStyle();

  QFile rawFile(QDir(src).filePath("text-editor.theme"));
  QVERIFY(rawFile.open(QIODevice::ReadOnly | QIODevice::Text));
  QString raw = QString::fromUtf8(rawFile.readAll());

  QCOMPARE(out, raw);
}

// -------- Cross-theme regression: full token resolution --------

namespace {
// Common assertion body for "fully resolved" tests.
void assertThemeFullyResolved(const QString &p_themeName) {
  QString src = findThemePath(p_themeName);
  QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("theme '%1' not found").arg(p_themeName)));
  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(src));
  QVERIFY(theme);

  QString css = theme->fetchWebStyleSheet();
  QString json = theme->fetchTextEditorStyle();

  QVERIFY2(!css.contains(QStringLiteral("@palette#")),
           qPrintable(QStringLiteral("%1 web.css has unresolved @palette# tokens:\n")
                          .arg(p_themeName) +
                      css.left(500)));
  QVERIFY2(!css.contains(QStringLiteral("@base#")),
           qPrintable(QStringLiteral("%1 web.css has unresolved @base# tokens:\n")
                          .arg(p_themeName) +
                      css.left(500)));
  QVERIFY2(!json.contains(QStringLiteral("@palette#")),
           qPrintable(QStringLiteral("%1 text-editor.theme has unresolved @palette# tokens:\n")
                          .arg(p_themeName) +
                      json.left(500)));
  QVERIFY2(!json.contains(QStringLiteral("@base#")),
           qPrintable(
               QStringLiteral("%1 text-editor.theme has unresolved @base# tokens").arg(p_themeName)));

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  QVERIFY2(err.error == QJsonParseError::NoError,
           qPrintable(QStringLiteral("%1 text-editor.theme failed to parse after resolution: ")
                          .arg(p_themeName) +
                      err.errorString()));
  QVERIFY(doc.isObject());
}

// Common assertion body for editor/web concept-color parity tests.
struct ParityExpected {
  QString heading;
  QString link;
  QString inlineCode;
  QString blockquote;
  QString searchBg;
  QString currentSearchBg;
};

void assertEditorWebConceptParity(const QString &p_themeName, const ParityExpected &p_exp) {
  QString src = findThemePath(p_themeName);
  QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("theme '%1' not found").arg(p_themeName)));
  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(src));
  QVERIFY(theme);

  QString css = theme->fetchWebStyleSheet();
  QString json = theme->fetchTextEditorStyle();

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
  QJsonObject root = doc.object();
  QJsonObject syntax = root[QStringLiteral("markdown-syntax-styles")].toObject();
  QJsonObject editorStyles = root[QStringLiteral("editor-styles")].toObject();

  // Heading: h1..h6 combined rule on the web side, H1 syntax style on the editor side.
  QString webHeading = extractCssColor(
      css, QStringLiteral("h1, h2, h3, h4, h5, h6"), QStringLiteral("color"));
  QString editorHeading =
      syntax[QStringLiteral("H1")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorHeading, webHeading);
  QCOMPARE(editorHeading, p_exp.heading);

  // Link
  QString webLink = extractCssColor(css, QStringLiteral("a"), QStringLiteral("color"));
  QString editorLink =
      syntax[QStringLiteral("LINK")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorLink, webLink);
  QCOMPARE(editorLink, p_exp.link);

  // Inline code (first occurrence of `code { ... }` -- not `pre code`).
  QString webInlineCode = extractCssColor(css, QStringLiteral("code"), QStringLiteral("color"));
  QString editorInlineCode =
      syntax[QStringLiteral("CODE")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorInlineCode, webInlineCode);
  QCOMPARE(editorInlineCode, p_exp.inlineCode);

  // Blockquote
  QString webBlockquote =
      extractCssColor(css, QStringLiteral("blockquote"), QStringLiteral("color"));
  QString editorBlockquote =
      syntax[QStringLiteral("BLOCKQUOTE")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorBlockquote, webBlockquote);
  QCOMPARE(editorBlockquote, p_exp.blockquote);

  // Search match bg
  QString webSearchBg = extractCssColor(css,
                                        QStringLiteral("#vx-content span.vx-search-match"),
                                        QStringLiteral("background-color"));
  QString editorSearchBg = editorStyles[QStringLiteral("Search")]
                               .toObject()[QStringLiteral("background-color")]
                               .toString();
  QCOMPARE(editorSearchBg, webSearchBg);
  QCOMPARE(editorSearchBg, p_exp.searchBg);

  // Current search match bg
  QString webCurrentBg = extractCssColor(css,
                                         QStringLiteral("#vx-content span.vx-current-search-match"),
                                         QStringLiteral("background-color"));
  QString editorCurrentBg = editorStyles[QStringLiteral("SearchUnderCursor")]
                                .toObject()[QStringLiteral("background-color")]
                                .toString();
  QCOMPARE(editorCurrentBg, webCurrentBg);
  QCOMPARE(editorCurrentBg, p_exp.currentSearchBg);
}
} // anonymous namespace

void TestTheme::testThemeFullyResolved_pure() {
  assertThemeFullyResolved(QStringLiteral("pure"));
}

void TestTheme::testThemeFullyResolved_everforestDark() {
  assertThemeFullyResolved(QStringLiteral("everforest-dark"));
}

void TestTheme::testThemeFullyResolved_moonlight() {
  assertThemeFullyResolved(QStringLiteral("moonlight"));
}

void TestTheme::testEditorWebConceptParity_pure() {
  ParityExpected exp;
  exp.heading = QStringLiteral("#222222");
  exp.link = QStringLiteral("#0099ff");
  exp.inlineCode = QStringLiteral("#8e24aa");
  exp.blockquote = QStringLiteral("#666666");
  exp.searchBg = QStringLiteral("#4db6ac");
  exp.currentSearchBg = QStringLiteral("#66bb6a");
  assertEditorWebConceptParity(QStringLiteral("pure"), exp);
}

void TestTheme::testEditorWebConceptParity_everforestDark() {
  ParityExpected exp;
  exp.heading = QStringLiteral("#E67E80");
  exp.link = QStringLiteral("#7FBBB3");
  exp.inlineCode = QStringLiteral("#D3C6AA");
  exp.blockquote = QStringLiteral("#859289");
  exp.searchBg = QStringLiteral("#83C092");
  exp.currentSearchBg = QStringLiteral("#A7C080");
  assertEditorWebConceptParity(QStringLiteral("everforest-dark"), exp);
}

void TestTheme::testEditorWebConceptParity_moonlight() {
  ParityExpected exp;
  exp.heading = QStringLiteral("#e06c75");
  exp.link = QStringLiteral("#61afef");
  exp.inlineCode = QStringLiteral("#98c379");
  exp.blockquote = QStringLiteral("#abb2bf");
  exp.searchBg = QStringLiteral("#4db6ac");
  exp.currentSearchBg = QStringLiteral("#66bb6a");
  assertEditorWebConceptParity(QStringLiteral("moonlight"), exp);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTheme)
#include "test_theme.moc"
