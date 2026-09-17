#include <QtTest>

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

#include <core/theme.h>

#include "../helpers/source_literal_scanner.h"

namespace tests {

class TestTheme : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testEnabledContrast_data();
  void testEnabledContrast();
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
  void testThemeFullyResolved_latexLight();
  void testThemeFullyResolved_latexDark();
  void testThemeFullyResolved_additionalThemes_data();
  void testThemeFullyResolved_additionalThemes();
  void testEditorWebConceptParity_pure();
  void testEditorWebConceptParity_everforestDark();
  void testEditorWebConceptParity_moonlight();
  void testEditorWebConceptParity_latexLight();
  void testEditorWebConceptParity_latexDark();
  void testEditorWebConceptParity_additionalThemes_data();
  void testEditorWebConceptParity_additionalThemes();
  void testInterfaceQssFullyResolved_data();
  void testInterfaceQssFullyResolved();
  void testCppPaletteTokenExtractor();
  void testBackfilledRolesMatchThemeUtils();
  void testCppPaletteTokensDefined_data();
  void testCppPaletteTokensDefined();
};

void TestTheme::initTestCase() {}

void TestTheme::cleanupTestCase() {}

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
QString extractCssColor(const QString &p_css, const QString &p_selector,
                        const QString &p_property) {
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
  QRegularExpression propRe(
      QStringLiteral("(?<![-\\w])%1\\s*:\\s*([^;}]+)").arg(QRegularExpression::escape(p_property)));
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
  QVERIFY(writeUtf8(
      QDir(themeDir).filePath("text-editor.theme"),
      QStringLiteral(
          "{ \"editor-styles\": { \"Text\": { \"text-color\": \"@palette#fg3_5\" } } }")));

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

  QVERIFY2(
      !css.contains(QStringLiteral("@palette#")),
      qPrintable(QStringLiteral("%1 web.css has unresolved @palette# tokens:\n").arg(p_themeName) +
                 css.left(500)));
  QVERIFY2(
      !css.contains(QStringLiteral("@base#")),
      qPrintable(QStringLiteral("%1 web.css has unresolved @base# tokens:\n").arg(p_themeName) +
                 css.left(500)));
  QVERIFY2(!json.contains(QStringLiteral("@palette#")),
           qPrintable(QStringLiteral("%1 text-editor.theme has unresolved @palette# tokens:\n")
                          .arg(p_themeName) +
                      json.left(500)));
  QVERIFY2(
      !json.contains(QStringLiteral("@base#")),
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
void assertEditorWebConceptParity(const QString &p_themeName) {
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
  QString webHeading =
      extractCssColor(css, QStringLiteral("h1, h2, h3, h4, h5, h6"), QStringLiteral("color"));
  QString editorHeading =
      syntax[QStringLiteral("H1")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorHeading, webHeading);

  // Link
  QString webLink = extractCssColor(css, QStringLiteral("a"), QStringLiteral("color"));
  QString editorLink =
      syntax[QStringLiteral("LINK")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorLink, webLink);

  // Inline code (first occurrence of `code { ... }` -- not `pre code`).
  QString webInlineCode = extractCssColor(css, QStringLiteral("code"), QStringLiteral("color"));
  QString editorInlineCode =
      syntax[QStringLiteral("CODE")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorInlineCode, webInlineCode);

  // Blockquote
  QString webBlockquote =
      extractCssColor(css, QStringLiteral("blockquote"), QStringLiteral("color"));
  QString editorBlockquote =
      syntax[QStringLiteral("BLOCKQUOTE")].toObject()[QStringLiteral("text-color")].toString();
  QCOMPARE(editorBlockquote, webBlockquote);

  // Search match bg
  QString webSearchBg = extractCssColor(css, QStringLiteral("#vx-content span.vx-search-match"),
                                        QStringLiteral("background-color"));
  QString editorSearchBg = editorStyles[QStringLiteral("Search")]
                               .toObject()[QStringLiteral("background-color")]
                               .toString();
  QCOMPARE(editorSearchBg, webSearchBg);

  // Current search match bg
  QString webCurrentBg =
      extractCssColor(css, QStringLiteral("#vx-content span.vx-current-search-match"),
                      QStringLiteral("background-color"));
  QString editorCurrentBg = editorStyles[QStringLiteral("SearchUnderCursor")]
                                .toObject()[QStringLiteral("background-color")]
                                .toString();
  QCOMPARE(editorCurrentBg, webCurrentBg);
}
} // anonymous namespace

void TestTheme::testThemeFullyResolved_pure() { assertThemeFullyResolved(QStringLiteral("pure")); }

void TestTheme::testThemeFullyResolved_everforestDark() {
  assertThemeFullyResolved(QStringLiteral("everforest-dark"));
}

void TestTheme::testThemeFullyResolved_moonlight() {
  assertThemeFullyResolved(QStringLiteral("moonlight"));
}

void TestTheme::testThemeFullyResolved_latexLight() {
  assertThemeFullyResolved(QStringLiteral("latex-light"));
}

void TestTheme::testThemeFullyResolved_latexDark() {
  assertThemeFullyResolved(QStringLiteral("latex-dark"));
}

void TestTheme::testEditorWebConceptParity_pure() {
  assertEditorWebConceptParity(QStringLiteral("pure"));
}

void TestTheme::testEditorWebConceptParity_everforestDark() {
  assertEditorWebConceptParity(QStringLiteral("everforest-dark"));
}

void TestTheme::testEditorWebConceptParity_moonlight() {
  assertEditorWebConceptParity(QStringLiteral("moonlight"));
}

void TestTheme::testEditorWebConceptParity_latexLight() {
  assertEditorWebConceptParity(QStringLiteral("latex-light"));
}

void TestTheme::testEditorWebConceptParity_latexDark() {
  assertEditorWebConceptParity(QStringLiteral("latex-dark"));
}

void TestTheme::testThemeFullyResolved_additionalThemes_data() {
  QTest::addColumn<QString>("themeName");
  for (const auto &name :
       {"solarized-dark", "solarized-light", "vscode-dark", "vue-dark", "vue-light", "vx-idea"}) {
    QTest::newRow(name) << QString::fromLatin1(name);
  }
}

void TestTheme::testThemeFullyResolved_additionalThemes() {
  QFETCH(QString, themeName);
  assertThemeFullyResolved(themeName);
}

void TestTheme::testEditorWebConceptParity_additionalThemes_data() {
  QTest::addColumn<QString>("themeName");
  QTest::addColumn<QString>("cssSelector");
  QTest::addColumn<QString>("cssProperty");
  QTest::addColumn<QString>("editorSection");
  QTest::addColumn<QString>("editorStyle");
  QTest::addColumn<QString>("editorProperty");
  for (const auto &name :
       {"solarized-dark", "solarized-light", "vscode-dark", "vue-dark", "vue-light", "vx-idea"}) {
    const QString theme = QString::fromLatin1(name);
    const auto add = [&](const QString &concept, const QString &selector, const QString &cssProp,
                         const QString &section, const QString &style, const QString &editorProp) {
      const QByteArray row = (theme + QLatin1Char('/') + concept).toUtf8();
      QTest::newRow(row.constData())
          << theme << selector << cssProp << section << style << editorProp;
    };
    // These states intentionally mean the same thing in both surfaces. Other
    // prose colors retain per-theme source/reader distinctions, not forced parity.
    add("search", "#vx-content span.vx-search-match", "background-color", "editor-styles", "Search",
        "background-color");
    add("current-search", "#vx-content span.vx-current-search-match", "background-color",
        "editor-styles", "SearchUnderCursor", "background-color");
    add("selection-text", "::selection", "color", "editor-styles", "Text", "selected-text-color");
    add("selection-background", "::selection", "background-color", "editor-styles", "Text",
        "selected-background-color");
    if (theme == QLatin1String("vscode-dark") || theme == QLatin1String("vx-idea")) {
      add("heading", "h1, h2, h3, h4, h5, h6", "color", "markdown-syntax-styles", "H1",
          "text-color");
    }
    if (theme == QLatin1String("solarized-dark")) {
      add("inline-code", "code", "color", "markdown-syntax-styles", "CODE", "text-color");
    }
  }
}

void TestTheme::testEditorWebConceptParity_additionalThemes() {
  QFETCH(QString, themeName);
  QFETCH(QString, cssSelector);
  QFETCH(QString, cssProperty);
  QFETCH(QString, editorSection);
  QFETCH(QString, editorStyle);
  QFETCH(QString, editorProperty);
  const QString path = findThemePath(themeName);
  QVERIFY2(!path.isEmpty(), qPrintable(themeName));
  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(path));
  QVERIFY(theme);
  QJsonParseError error;
  const auto editor = QJsonDocument::fromJson(theme->fetchTextEditorStyle().toUtf8(), &error);
  QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(error.errorString()));
  const QColor sourceColor(
      editor.object()[editorSection].toObject()[editorStyle].toObject()[editorProperty].toString());
  const QColor readerColor(extractCssColor(theme->fetchWebStyleSheet(), cssSelector, cssProperty));
  QVERIFY2(sourceColor.isValid(),
           qPrintable(editorSection + "/" + editorStyle + "/" + editorProperty));
  QVERIFY2(readerColor.isValid(), qPrintable(cssSelector + "/" + cssProperty));
  QCOMPARE(sourceColor, readerColor);
}

// Enabled text must remain readable in its actual role/state, not match an aesthetic snapshot.
void TestTheme::testEnabledContrast_data() {
  QTest::addColumn<QString>("themeName");
  QTest::addColumn<QString>("foregroundRole");
  QTest::addColumn<QString>("backgroundRole");
  QTest::addColumn<QString>("editorLayer");
  const QStringList themes = {"everforest-dark", "latex-dark",      "latex-light", "moonlight",
                              "solarized-dark",  "solarized-light", "vscode-dark", "vue-dark",
                              "vue-light",       "vx-idea"};
  for (const auto &theme : themes) {
    const auto add = [&](const QString &fg, const QString &bg, const QString &layer = QString()) {
      const QByteArray label = (theme + "/" + (layer.isEmpty() ? fg + "/" + bg : layer)).toUtf8();
      QTest::newRow(label.constData()) << theme << fg << bg << layer;
    };
    for (const auto &role :
         {"normal", "master", "pressed", "content", "content#selected", "content#selected#active",
          "content#selected#inactive", "content#selection"}) {
      const QString prefix = QStringLiteral("base#") + QLatin1String(role);
      add(prefix + "#fg", prefix + "#bg");
    }
    add("widgets#statusbarcolumn#fg", "widgets#statusbarcolumn#mode#bg");
    for (const auto &role : {"muted", "info", "warning", "error", "success"}) {
      for (const auto &surface : {"normal", "content"}) {
        add(QStringLiteral("base#") + QLatin1String(role) + "#fg",
            QStringLiteral("base#") + QLatin1String(surface) + "#bg");
      }
    }
    add("base#content#selection#fg", "base#content#selection#bg", "editor-styles");
    add("base#content#selection#fg", "base#content#selection#bg", "markdown-editor-styles");
  }
}

void TestTheme::testEnabledContrast() {
  QFETCH(QString, themeName);
  QFETCH(QString, foregroundRole);
  QFETCH(QString, backgroundRole);
  QFETCH(QString, editorLayer);
  const QString path = findThemePath(themeName);
  QVERIFY2(!path.isEmpty(), qPrintable(themeName));
  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(path));
  QVERIFY(theme);
  QColor foreground(theme->paletteColor(foregroundRole));
  QColor background(theme->paletteColor(backgroundRole));
  const QColor surface(theme->paletteColor(QStringLiteral("base#normal#bg")));
  QVERIFY(foreground.isValid());
  QVERIFY(background.isValid());
  QVERIFY(surface.isValid());
  if (!editorLayer.isEmpty()) {
    QJsonParseError error;
    const QJsonDocument editor =
        QJsonDocument::fromJson(theme->fetchTextEditorStyle().toUtf8(), &error);
    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(error.errorString()));
    const QJsonObject text = editor.object()[editorLayer].toObject()["Text"].toObject();
    const QColor selectedText(text["selected-text-color"].toString());
    const QColor selectedBackground(text["selected-background-color"].toString());
    QVERIFY(selectedText.isValid());
    QVERIFY(selectedBackground.isValid());
    // The plain and Markdown editors expose the same true-selection semantics.
    QCOMPARE(selectedText, foreground);
    QCOMPARE(selectedBackground, background);
    foreground = selectedText;
    background = selectedBackground;
  }
  const auto composite = [](const QColor &fg, const QColor &bg) {
    const double alpha = fg.alphaF();
    return QColor::fromRgbF(fg.redF() * alpha + bg.redF() * (1.0 - alpha),
                            fg.greenF() * alpha + bg.greenF() * (1.0 - alpha),
                            fg.blueF() * alpha + bg.blueF() * (1.0 - alpha));
  };
  background = composite(background, surface);
  foreground = composite(foreground, background);
  const auto luminance = [](const QColor &color) {
    const auto linear = [](double channel) {
      return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
  };
  const double fg = luminance(foreground);
  const double bg = luminance(background);
  const double ratio = (std::max(fg, bg) + 0.05) / (std::min(fg, bg) + 0.05);
  QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 on %2: %3:1 (requires 4.5:1)")
                                        .arg(foreground.name(), background.name())
                                        .arg(ratio, 0, 'f', 6)));
}

// -------- Cross-theme regression: interface.qss token resolution --------
//
// Every bundled theme must define every palette token its interface.qss
// references. This is the net for per-theme palette drift: adding a rule (e.g.
// *[State="success"]) that names a token a theme does not define leaves an
// unresolved "@..." in the stylesheet, and Qt SILENTLY DROPS the malformed
// declaration rather than failing. Without this test, a missing role shows up
// only as one unstyled widget on one theme out of twelve.
//
// It deliberately resolves tokens against palette.json directly instead of
// calling Theme::fetchQtStyleSheet(): that path also resolves font families via
// QFontDatabase, which qFatal()s in a GUILESS test (exit code 0xC0000409). The
// direct walk is also a better failure message -- it names the exact token.
namespace {

// Walk a '#'-separated path (e.g. "base#success#fg") from the palette root.
//
// Empty segments are NOT skipped: Theme::findValueByKeyPath (src/core/theme.cpp:349)
// splits without Qt::SkipEmptyParts, so "base##normal#fg" genuinely fails at
// runtime and must fail here too.
QJsonValue resolveTokenPath(const QJsonObject &p_root, const QString &p_path) {
  const QStringList parts = p_path.split(QLatin1Char('#'));
  QJsonValue cur = p_root;
  for (const QString &part : parts) {
    if (!cur.isObject()) {
      return QJsonValue();
    }
    const QJsonObject obj = cur.toObject();
    if (!obj.contains(part)) {
      return QJsonValue();
    }
    cur = obj.value(part);
  }
  return cur;
}

// The roles ThemeUtils::backfillSystemPalette() actually creates at runtime.
//
// PARITY NOTE: this set is transcribed from src/gui/utils/themeutils.cpp
// (lines 14-28 for "active", 42-56 for "disabled"). It is duplicated rather
// than linked because that function needs a QApplication and this target is
// GUILESS. When a role is added there, add it here too. The "inactive" group is
// created EMPTY (themeutils.cpp:33-37), so "@palette#inactive#..." must never
// be accepted.
const QSet<QString> &backfilledRoles() {
  static const QSet<QString> roles = {QStringLiteral("window"),
                                      QStringLiteral("window_text"),
                                      QStringLiteral("base"),
                                      QStringLiteral("alternate_base"),
                                      QStringLiteral("text"),
                                      QStringLiteral("button"),
                                      QStringLiteral("button_text"),
                                      QStringLiteral("bright_text"),
                                      QStringLiteral("light"),
                                      QStringLiteral("midlight"),
                                      QStringLiteral("dark"),
                                      QStringLiteral("highlight"),
                                      QStringLiteral("highlighted_text"),
                                      QStringLiteral("link"),
                                      QStringLiteral("link_visited")};
  return roles;
}

// True when p_path is exactly a leaf the runtime backfill will supply.
bool isBackfilledPalettePath(const QString &p_path) {
  const QStringList parts = p_path.split(QLatin1Char('#'));
  if (parts.size() != 3 || parts.at(0) != QStringLiteral("palette")) {
    return false;
  }
  if (parts.at(1) != QStringLiteral("active") && parts.at(1) != QStringLiteral("disabled")) {
    return false;
  }
  return backfilledRoles().contains(parts.at(2));
}

// The SHARED resolver for both palette gates (interface.qss tokens and C++
// tokens), so the two cannot drift. Chases '@' references with a hop bound so a
// cyclic palette fails loudly instead of hanging.
//
// Returns an empty string when p_path resolves to a literal color; otherwise a
// human-readable reason naming the hop it stopped at.
QString paletteTokenError(const QJsonObject &p_palette, const QString &p_path,
                          bool p_backfillsSystemPalette) {
  QString current = p_path;
  for (int hop = 0; hop < 16; ++hop) {
    // For a backfilling theme the ENTIRE "active" / "inactive" / "disabled"
    // object is replaced wholesale at load time (themeutils.cpp:30,36,58 via
    // theme.cpp:96-100), so whatever the file says under palette#<group> is
    // discarded. Decide from the schema BEFORE looking at the file, or the gate
    // would accept an author-written key that production drops.
    if (p_backfillsSystemPalette && current.startsWith(QStringLiteral("palette#"))) {
      return isBackfilledPalettePath(current)
                 ? QString()
                 : QStringLiteral("'%1' is not a role the runtime backfill creates").arg(current);
    }
    const QJsonValue value = resolveTokenPath(p_palette, current);
    if (value.isUndefined() || value.isNull()) {
      return QStringLiteral("unresolved at '%1'").arg(current);
    }
    if (!value.isString()) {
      return QStringLiteral("'%1' is an object, not a color").arg(current);
    }
    const QString str = value.toString();
    if (!str.startsWith(QLatin1Char('@'))) {
      return str.isEmpty() ? QStringLiteral("'%1' is empty").arg(current) : QString();
    }
    current = str.mid(1);
  }
  return QStringLiteral("does not terminate (cycle?), stopped at '%1'").arg(current);
}

bool themeBackfillsSystemPalette(const QJsonObject &p_palette) {
  return p_palette.value(QStringLiteral("metadata"))
      .toObject()
      .value(QStringLiteral("backfill-system-palette"))
      .toBool(false);
}

QString themesRootPath() {
  QString p = QFINDTESTDATA("../../src/data/extra/themes");
  if (p.isEmpty()) {
    p = QFINDTESTDATA("src/data/extra/themes");
  }
  return p;
}

// ONE enumeration for BOTH palette gates: every subdirectory of
// src/data/extra/themes that has a palette.json, so a newly added theme is
// covered by both automatically. Returns false (after failing the caller) when
// the theme set looks wrong.
bool addBundledThemeRows() {
  QTest::addColumn<QString>("themeName");

  const QString root = themesRootPath();
  if (root.isEmpty()) {
    QTest::qFail("themes root not found", __FILE__, __LINE__);
    return false;
  }
  const QStringList themeDirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  int rows = 0;
  for (const QString &name : themeDirs) {
    if (!QFileInfo::exists(QDir(root).filePath(name + QStringLiteral("/palette.json")))) {
      continue;
    }
    QTest::newRow(qPrintable(name)) << name;
    ++rows;
  }
  if (rows < 12) {
    QTest::qFail(
        qPrintable(QStringLiteral("only %1 bundled theme(s) found under %2").arg(rows).arg(root)),
        __FILE__, __LINE__);
    return false;
  }
  return true;
}

} // namespace

void TestTheme::testInterfaceQssFullyResolved_data() { addBundledThemeRows(); }

void TestTheme::testInterfaceQssFullyResolved() {
  QFETCH(QString, themeName);

  const QString src = findThemePath(themeName);
  QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("theme '%1' not found").arg(themeName)));

  QFile qssFile(QDir(src).filePath(QStringLiteral("interface.qss")));
  QVERIFY2(qssFile.open(QIODevice::ReadOnly | QIODevice::Text),
           qPrintable(QStringLiteral("%1 has no interface.qss").arg(themeName)));
  const QString qss = QString::fromUtf8(qssFile.readAll());
  QVERIFY(!qss.isEmpty());

  QFile paletteFile(QDir(src).filePath(QStringLiteral("palette.json")));
  QVERIFY2(paletteFile.open(QIODevice::ReadOnly),
           qPrintable(QStringLiteral("%1 has no palette.json").arg(themeName)));
  QJsonParseError perr;
  const QJsonDocument pdoc = QJsonDocument::fromJson(paletteFile.readAll(), &perr);
  QVERIFY2(
      perr.error == QJsonParseError::NoError,
      qPrintable(
          QStringLiteral("%1 palette.json is invalid: %2").arg(themeName, perr.errorString())));
  const QJsonObject palette = pdoc.object();

  // The severity roles the notification surfaces depend on. `success` is the
  // newest and the one most likely to be forgotten when adding a theme.
  for (const QString &state : {QStringLiteral("info"), QStringLiteral("warning"),
                               QStringLiteral("error"), QStringLiteral("success")}) {
    QVERIFY2(
        qss.contains(QStringLiteral("*[State=\"%1\"]").arg(state)),
        qPrintable(
            QStringLiteral("%1 interface.qss lacks a *[State=\"%2\"] rule").arg(themeName, state)));
  }
  QVERIFY2(qss.contains(QStringLiteral("vnotex--NotificationToast")),
           qPrintable(
               QStringLiteral("%1 interface.qss lacks a NotificationToast block").arg(themeName)));

  // `native` declares "backfill-system-palette": true, so part of its palette
  // section is filled in at runtime from the OS QPalette. That exception is
  // SCHEMA-CHECKED inside paletteTokenError(): only the leaves
  // ThemeUtils::backfillSystemPalette() really creates are accepted, so a
  // misspelled role (or an "@palette#inactive#..." reference) still fails.
  const bool backfillsSystemPalette = themeBackfillsSystemPalette(palette);

  // Tokens this theme must define outright, backfill or not.
  for (const QString &path : {QStringLiteral("base#success#fg"), QStringLiteral("base#info#fg"),
                              QStringLiteral("base#warning#fg"), QStringLiteral("base#error#fg"),
                              QStringLiteral("widgets#unitedentry#popup#border")}) {
    const QJsonValue v = resolveTokenPath(palette, path);
    QVERIFY2(
        v.isString() && !v.toString().isEmpty(),
        qPrintable(QStringLiteral("%1 palette.json does not define @%2").arg(themeName, path)));
  }

  // Same prefix set the production resolver accepts (whitespace, ':' or '"').
  static const QRegularExpression tokenRe(
      QStringLiteral("(?<=[\\s:\"])@([A-Za-z0-9_]+(?:#[A-Za-z0-9_]+)+)"));

  auto it = tokenRe.globalMatch(qss);
  int checked = 0;
  while (it.hasNext()) {
    const QString path = it.next().captured(1);
    ++checked;

    const QString err = paletteTokenError(palette, path, backfillsSystemPalette);
    QVERIFY2(err.isEmpty(),
             qPrintable(QStringLiteral("%1 interface.qss references @%2, which palette.json does "
                                       "not resolve: %3")
                            .arg(themeName, path, err)));
  }

  QVERIFY2(checked > 0,
           qPrintable(QStringLiteral("%1 interface.qss had no tokens at all -- the regex is "
                                     "probably wrong, making this test vacuous")
                          .arg(themeName)));
}

// -------- Cross-theme regression: palette tokens referenced from C++ --------
//
// Theme::paletteColor() only qWarning()s and returns an EMPTY string for a
// token no theme defines. Interpolated into a stylesheet, Qt then silently
// drops the declaration; handed to QColor, the color is invalid. The
// interface.qss gate above covers tokens named in QSS; this one covers the
// tokens named in C++, which is the hole "base#border" fell through.
//
// The scan is SHAPE-based (any string literal that looks like a palette path)
// rather than call-shape-based, because the consumers are not uniform: bare
// "..." literals, QStringLiteral(...), and file-scope c_* constants all occur.
namespace {

// Extract every palette-path-shaped string literal from one translation unit.
// Comment-aware and raw-string aware via the shared scanner, so commented-out
// code cannot create a false positive. Literals carrying the
// `palette-token-optional` marker (an optionalPaletteColor() probe with its own
// fallback) are skipped and reported through p_skipped.
QStringList extractPaletteTokens(const QString &p_source, QStringList *p_skipped = nullptr) {
  static const QRegularExpression shapeRe(
      QStringLiteral("^(base|widgets|palette)#[A-Za-z0-9_#]*[A-Za-z0-9_]$"));
  static const QStringList markers = {QStringLiteral("palette-token-optional")};

  const QStringList lines = p_source.split(QLatin1Char('\n'));
  QStringList out;
  for (const auto &lit : literalscan::extractLiterals(p_source)) {
    if (!shapeRe.match(lit.text).hasMatch()) {
      continue;
    }
    if (literalscan::hasEscapeHatch(lines, lit, markers)) {
      if (p_skipped) {
        p_skipped->append(lit.text);
      }
      continue;
    }
    out.append(lit.text);
  }
  return out;
}

QString srcRootPath() {
#ifdef VNOTE_SRC_DIR
  return QStringLiteral(VNOTE_SRC_DIR);
#else
  return QDir::currentPath() + QStringLiteral("/../../../src");
#endif
}

// Result of the one-shot src/ scan.
struct CppTokenScan {
  QStringList tokens;     // distinct, sorted, enforced tokens
  QStringList skipped;    // distinct tokens carrying the escape hatch
  QStringList unreadable; // files the scan could not open -- a fail-open risk
  QSet<QString> scanned;  // relative paths actually read
};

// Scan src/ once.
const CppTokenScan &scannedCppPaletteTokens() {
  static CppTokenScan result = []() {
    const QString root = srcRootPath();
    const QDir srcDir(root);
    CppTokenScan scan;
    QSet<QString> tokens;
    QSet<QString> skipped;

    QDirIterator it(root, {QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString filePath = it.next();
      const QString relPath =
          srcDir.relativeFilePath(filePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
      // src/data/ is theme/web asset source, not application code.
      if (relPath.startsWith(QStringLiteral("data/"))) {
        continue;
      }
      QFile f(filePath);
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // A silently skipped file is exactly how this gate would fail open.
        scan.unreadable.append(relPath);
        continue;
      }
      const QString source = QString::fromUtf8(f.readAll());
      scan.scanned.insert(relPath);
      QStringList fileSkipped;
      for (const QString &tok : extractPaletteTokens(source, &fileSkipped)) {
        tokens.insert(tok);
      }
      for (const QString &tok : fileSkipped) {
        skipped.insert(tok);
      }
    }

    scan.tokens = tokens.values();
    scan.tokens.sort();
    scan.skipped = skipped.values();
    scan.skipped.sort();
    scan.unreadable.sort();
    return scan;
  }();
  return result;
}

} // namespace

// Self-test of the extractor: a shape-based scan is easy to write vacuously.
void TestTheme::testCppPaletteTokenExtractor() {
  // Positive: all three real consumer shapes.
  QCOMPARE(extractPaletteTokens(QStringLiteral(R"(paletteColor("base#normal#fg");)")),
           QStringList{QStringLiteral("base#normal#fg")});
  QCOMPARE(
      extractPaletteTokens(QStringLiteral(R"(paletteColor(QStringLiteral("base#normal#fg"));)")),
      QStringList{QStringLiteral("base#normal#fg")});
  QCOMPARE(
      extractPaletteTokens(QStringLiteral(R"(static const QString c_x = "widgets#qtreeview#fg";)")),
      QStringList{QStringLiteral("widgets#qtreeview#fg")});

  // Negative: comments (line and block).
  QVERIFY(extractPaletteTokens(QStringLiteral(R"(// paletteColor("base#normal#fg");)")).isEmpty());
  QVERIFY(
      extractPaletteTokens(QStringLiteral("/* paletteColor(\"base#normal#fg\"); */")).isEmpty());
  // Negative: a runtime variable argument leaves no literal to scan.
  QVERIFY(extractPaletteTokens(QStringLiteral(R"(paletteColor(name);)")).isEmpty());
  // Negative: a PREFIX literal concatenated at runtime (themeservice.cpp).
  QVERIFY(extractPaletteTokens(
              QStringLiteral(R"(optionalPaletteColor(QStringLiteral("widgets#pdfcomment#") + t);)"))
              .isEmpty());
  // Negative: the escape hatch, and it is reported as skipped.
  QStringList skipped;
  QVERIFY(extractPaletteTokens(
              QStringLiteral("optionalPaletteColor(\"widgets#a#b\"); // palette-token-optional: x"),
              &skipped)
              .isEmpty());
  QCOMPARE(skipped, QStringList{QStringLiteral("widgets#a#b")});

  // A MALFORMED path (an empty segment) must still be extracted, because
  // Theme::findValueByKeyPath does not skip empty segments either: the token
  // fails at runtime, so the gate has to see it and report it.
  QCOMPARE(extractPaletteTokens(QStringLiteral(R"(paletteColor("base##normal#fg");)")),
           QStringList{QStringLiteral("base##normal#fg")});
  QJsonObject palette;
  QJsonObject baseObj;
  QJsonObject normalObj;
  normalObj[QStringLiteral("fg")] = QStringLiteral("#222222");
  baseObj[QStringLiteral("normal")] = normalObj;
  palette[QStringLiteral("base")] = baseObj;
  QVERIFY(paletteTokenError(palette, QStringLiteral("base#normal#fg"), false).isEmpty());
  QVERIFY(!paletteTokenError(palette, QStringLiteral("base##normal#fg"), false).isEmpty());
  // The backfill exception must not launder a malformed reference either.
  QVERIFY(!isBackfilledPalettePath(QStringLiteral("palette##active#window")));
  QVERIFY(!isBackfilledPalettePath(QStringLiteral("palette#inactive#window")));
  QVERIFY(!isBackfilledPalettePath(QStringLiteral("palette#active#alternate_bsae")));
  QVERIFY(isBackfilledPalettePath(QStringLiteral("palette#active#alternate_base")));

  // backfillSystemPalette REPLACES the whole active/inactive/disabled object,
  // so an author-written key under it is dead at runtime and must fail here
  // even though it is present in the file.
  QJsonObject backfillPalette;
  QJsonObject paletteSection;
  QJsonObject activeGroup;
  activeGroup[QStringLiteral("accent")] = QStringLiteral("#ff0000");
  paletteSection[QStringLiteral("active")] = activeGroup;
  backfillPalette[QStringLiteral("palette")] = paletteSection;
  QVERIFY(
      !paletteTokenError(backfillPalette, QStringLiteral("palette#active#accent"), true).isEmpty());
  // ... and is fine for a theme that does NOT backfill.
  QVERIFY(
      paletteTokenError(backfillPalette, QStringLiteral("palette#active#accent"), false).isEmpty());
}

// The set above is a hand transcription across a test/production boundary, and
// the dangerous direction is silent: if a role is RENAMED or REMOVED in
// themeutils.cpp, the stale name here keeps waiving a token the runtime no
// longer creates -- exactly the empty-string bug these gates exist to catch.
// So check the parity by grepping the real source (same style as the repo's
// other drift gates); ThemeUtils itself cannot be linked into this GUILESS
// target because it needs a QApplication.
void TestTheme::testBackfilledRolesMatchThemeUtils() {
  const QString path = srcRootPath() + QStringLiteral("/gui/utils/themeutils.cpp");
  QFile f(path);
  QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
           qPrintable(QStringLiteral("cannot read %1").arg(path)));
  const QString source = QString::fromUtf8(f.readAll());

  // Every role the function assigns, in either group.
  static const QRegularExpression assignRe(
      QStringLiteral("obj\\[\"([A-Za-z0-9_]+)\"\\]\\s*=\\s*qpalette\\.color\\("));
  QSet<QString> found;
  auto it = assignRe.globalMatch(source);
  while (it.hasNext()) {
    found.insert(it.next().captured(1));
  }

  QVERIFY2(!found.isEmpty(),
           "no roles extracted from themeutils.cpp -- the parity regex is probably wrong");
  QStringList foundList = found.values();
  foundList.sort();
  QStringList expectedList = backfilledRoles().values();
  expectedList.sort();
  QVERIFY2(foundList == expectedList,
           qPrintable(QStringLiteral("backfilledRoles() has drifted from themeutils.cpp:\n  "
                                     "themeutils.cpp: %1\n  test: %2")
                          .arg(foundList.join(QStringLiteral(", ")),
                               expectedList.join(QStringLiteral(", ")))));

  // The "inactive" group must still be created EMPTY, which is what makes
  // "@palette#inactive#..." a failure rather than a waiver.
  QVERIFY2(source.contains(QStringLiteral("p_obj[\"inactive\"]")),
           "themeutils.cpp no longer assigns an 'inactive' group");
  static const QRegularExpression inactiveRe(
      QStringLiteral("QJsonObject obj;\\s*p_obj\\[\"inactive\"\\] = obj;"));
  QVERIFY2(inactiveRe.match(source).hasMatch(),
           "themeutils.cpp's 'inactive' group is no longer created empty -- "
           "isBackfilledPalettePath() must be updated");
}

void TestTheme::testCppPaletteTokensDefined_data() { addBundledThemeRows(); }

void TestTheme::testCppPaletteTokensDefined() {
  QFETCH(QString, themeName);

  const auto &scanned = scannedCppPaletteTokens();
  const QStringList &tokens = scanned.tokens;
  const QStringList &skipped = scanned.skipped;

  // An unreadable file would be silently dropped from the scan -- the exact
  // fail-open mode this gate exists to prevent (cf. test_hardcoded_color_drift).
  QVERIFY2(scanned.unreadable.isEmpty(),
           qPrintable(QStringLiteral("unreadable source file(s): %1")
                          .arg(scanned.unreadable.join(QStringLiteral(", ")))));
  QVERIFY2(scanned.scanned.size() > 100,
           qPrintable(QStringLiteral("only %1 file(s) read - the scan root looks wrong")
                          .arg(scanned.scanned.size())));
  for (const auto &anchor : {QStringLiteral("widgets/dockwidgethelper.cpp"),
                             QStringLiteral("views/filenodedelegate.cpp"),
                             QStringLiteral("gui/services/themeservice.cpp")}) {
    QVERIFY2(scanned.scanned.contains(anchor),
             qPrintable(QStringLiteral("anchor file was not scanned: %1").arg(anchor)));
  }

  // A broken scanner would find nothing and make this test permanently green.
  QVERIFY2(tokens.size() >= 35,
           qPrintable(QStringLiteral("only %1 palette token(s) found under %2 -- the scan looks "
                                     "broken, making this test vacuous")
                          .arg(tokens.size())
                          .arg(srcRootPath())));
  // The escape hatch must stay an exception, not the norm.
  QVERIFY2(skipped.size() <= 4,
           qPrintable(QStringLiteral("%1 token(s) carry palette-token-optional: %2")
                          .arg(skipped.size())
                          .arg(skipped.join(QStringLiteral(", ")))));

  const QString src = findThemePath(themeName);
  QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("theme '%1' not found").arg(themeName)));

  QFile paletteFile(QDir(src).filePath(QStringLiteral("palette.json")));
  QVERIFY2(paletteFile.open(QIODevice::ReadOnly),
           qPrintable(QStringLiteral("%1 has no palette.json").arg(themeName)));
  QJsonParseError perr;
  const QJsonDocument pdoc = QJsonDocument::fromJson(paletteFile.readAll(), &perr);
  QVERIFY2(
      perr.error == QJsonParseError::NoError,
      qPrintable(
          QStringLiteral("%1 palette.json is invalid: %2").arg(themeName, perr.errorString())));
  const QJsonObject palette = pdoc.object();
  const bool backfills = themeBackfillsSystemPalette(palette);

  QStringList failures;
  for (const QString &token : tokens) {
    const QString err = paletteTokenError(palette, token, backfills);
    if (!err.isEmpty()) {
      failures.append(QStringLiteral("@%1 (%2)").arg(token, err));
    }
  }

  QVERIFY2(failures.isEmpty(),
           qPrintable(QStringLiteral("theme '%1' does not define %2 palette token(s) referenced "
                                     "from src/:\n  %3")
                          .arg(themeName)
                          .arg(failures.size())
                          .arg(failures.join(QStringLiteral("\n  ")))));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTheme)
#include "test_theme.moc"
