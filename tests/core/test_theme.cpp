#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
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
  void testFetchMarkdownEditorStyle_explicitFileReturnsRaw();
  void testFetchMarkdownEditorStyle_fallbackResolvesTokens();
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
  QString src = findPureThemePath();
  QVERIFY2(!src.isEmpty(), "pure theme fixture not found");

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
  QString src = findPureThemePath();
  QVERIFY2(!src.isEmpty(), "pure theme fixture not found");

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(src));
  QVERIFY(theme);
  QString out = theme->fetchTextEditorStyle();

  QFile rawFile(QDir(src).filePath("text-editor.theme"));
  QVERIFY(rawFile.open(QIODevice::ReadOnly | QIODevice::Text));
  QString raw = QString::fromUtf8(rawFile.readAll());

  QCOMPARE(out, raw);
}

void TestTheme::testFetchMarkdownEditorStyle_explicitFileReturnsRaw() {
  QString src = findPureThemePath();
  QVERIFY2(!src.isEmpty(), "pure theme fixture not found");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QString themeDir = tmp.filePath("pure-with-markdown-theme");
  QVERIFY(copyDir(src, themeDir));

  // Add an explicit markdown-text-editor.theme containing a literal token.
  // Per locked scope: explicit markdown theme must NOT have tokens resolved.
  QVERIFY(writeUtf8(QDir(themeDir).filePath("markdown-text-editor.theme"),
                    QStringLiteral("{ \"editor-styles\": { \"Text\": { \"text-color\": \"@palette#fg3_5\" } } }")));

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(themeDir));
  QVERIFY(theme);
  QString out = theme->fetchMarkdownEditorStyle();
  QVERIFY2(out.contains(QStringLiteral("@palette#fg3_5")),
           qPrintable(QStringLiteral("explicit markdown theme should NOT have tokens resolved; got: %1").arg(out)));
}

void TestTheme::testFetchMarkdownEditorStyle_fallbackResolvesTokens() {
  QString src = findPureThemePath();
  QVERIFY2(!src.isEmpty(), "pure theme fixture not found");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QString themeDir = tmp.filePath("pure-fallback");
  QVERIFY(copyDir(src, themeDir));

  // Tokenize text-editor.theme. NO markdown-text-editor.theme present.
  QVERIFY(writeUtf8(QDir(themeDir).filePath("text-editor.theme"),
                    QStringLiteral("{ \"editor-styles\": { \"Text\": { \"text-color\": \"@palette#fg3_5\" } } }")));

  QScopedPointer<vnotex::Theme> theme(vnotex::Theme::fromFolder(themeDir));
  QVERIFY(theme);
  QString out = theme->fetchMarkdownEditorStyle();
  QVERIFY2(out.contains(QStringLiteral("\"#222222\"")),
           qPrintable(QStringLiteral("fallback path should resolve tokens; got: %1").arg(out)));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTheme)
#include "test_theme.moc"
