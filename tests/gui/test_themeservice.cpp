#include <QtTest>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopedPointer>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

#include <core/services/commenttypes.h>
#include <core/theme.h>
#include <gui/services/themeservice.h>
#include <gui/utils/themeutils.h>

namespace tests {

class TestThemeService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testFetchWebStyleSheet_delegatesToCurrentTheme();
  void testFetchTextEditorStyle_delegatesToCurrentTheme();
  void testFetchWebStyleSheet_nullThemeReturnsEmpty();

  void getWebStyles_returnsOnlyWebCss();
  void getSyntaxStyles_returnsOnlyHighlightCss();
  void getWebStyles_and_getSyntaxStyles_areDisjoint();
  void getWebStyles_includesCurrentThemeWebCss();
  void getSyntaxStyles_includesCurrentThemeHighlightCss();
  void styleCounts_matchThemesShippingThatFile();
  void styleEntries_haveDisplayNameAndExistingFile();
  void styleLists_data();
  void styleLists();

  void interfaceQssFullyResolved_data();
  void interfaceQssFullyResolved();
  void interfaceQssStylesInlineBanner_data();
  void interfaceQssStylesInlineBanner();
  void interfaceQssStylesSeverityText_data();
  void interfaceQssStylesSeverityText();
  void interfaceQssStylesMutedText_data();
  void interfaceQssStylesMutedText();
  void mutedTextIsReadable_data();
  void mutedTextIsReadable();

  void commentHighlightColorsAreDefinedForEveryToken_data();
  void commentHighlightColorsAreDefinedForEveryToken();
  void commentHighlightCssVariablesAreFullyResolved_data();
  void commentHighlightCssVariablesAreFullyResolved();
  void commentHighlightColorFallsBackForAnUnknownToken();

private:
  QString m_themeFolder;
  QTemporaryDir m_tmp;

  QString findPureTheme();

  // Resolved interface.qss for a bundled theme, via the exact production path
  // (including the system-palette backfill `native` depends on).
  static QString resolvedInterfaceQss(const QString &p_themeName);

  // Directory holding the bundled themes, or a null string.
  static QString bundledThemesDir();

  // Declaration block of the rule with exactly this selector.
  static QString qssRuleBody(const QString &p_qss, const QString &p_selector);

  // WCAG 2.x relative-luminance contrast ratio, in [1, 21].
  static qreal contrastRatio(const QColor &p_a, const QColor &p_b);

  // One QTest row per bundled theme.
  static void addBundledThemeRows();

  // Build a ThemeService pointing at the bundled themes (pure current theme).
  vnotex::ThemeServiceConfig makeConfig() const;

  // Helper: derive the proper appDataPath (parent of the themes/ directory)
  // from m_themeFolder (which points at .../themes/pure).
  QString appDataPathFromThemeFolder() const;
};

QString TestThemeService::findPureTheme() {
  QString p = QFINDTESTDATA("../../src/data/extra/themes/pure");
  if (p.isEmpty()) {
    p = QFINDTESTDATA("src/data/extra/themes/pure");
  }
  return p;
}

QString TestThemeService::appDataPathFromThemeFolder() const {
  // m_themeFolder = .../src/data/extra/themes/pure
  // themesDir     = .../src/data/extra/themes
  // appDataPath   = .../src/data/extra        (parent of themes/)
  // ThemeService constructor appends "themes" to appDataPath, so we must
  // hand it the parent of the themes/ directory, NOT the themes/ directory.
  QFileInfo themeFolderInfo(m_themeFolder);
  QString themesDir = themeFolderInfo.absolutePath();
  return QFileInfo(themesDir).absolutePath();
}

vnotex::ThemeServiceConfig TestThemeService::makeConfig() const {
  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("pure");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = appDataPathFromThemeFolder();
  return cfg;
}

void TestThemeService::initTestCase() {
  QVERIFY(m_tmp.isValid());
  m_themeFolder = findPureTheme();
  QVERIFY2(!m_themeFolder.isEmpty(), "pure theme fixture not found");
}

void TestThemeService::cleanupTestCase() {}

void TestThemeService::testFetchWebStyleSheet_delegatesToCurrentTheme() {
  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("pure");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = appDataPathFromThemeFolder();

  vnotex::ThemeService svc(cfg);
  // Ensure m_currentTheme is loaded; switchTheme triggers load.
  svc.switchTheme(QStringLiteral("pure"));

  QString out = svc.fetchWebStyleSheet();
  QVERIFY2(!out.isEmpty(), "expected non-empty CSS from pure theme");
}

void TestThemeService::testFetchTextEditorStyle_delegatesToCurrentTheme() {
  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("pure");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = appDataPathFromThemeFolder();

  vnotex::ThemeService svc(cfg);
  svc.switchTheme(QStringLiteral("pure"));

  QString out = svc.fetchTextEditorStyle();
  QVERIFY2(!out.isEmpty(), "expected non-empty JSON from pure text-editor.theme");
}

void TestThemeService::testFetchWebStyleSheet_nullThemeReturnsEmpty() {
  // SKIP: ThemeService guarantees m_currentTheme is never null after construction.
  //
  // Rationale: ThemeService::loadAvailableThemes() throws (Exception::throwOne is
  // [[noreturn]]) if no themes are found in the search path, so the constructor
  // cannot complete without at least one valid theme. After loadCurrentTheme(),
  // ThemeService falls back to the "pure" theme when the requested theme is not
  // found (themeservice.cpp:95-99), so m_currentTheme is always non-null on a
  // successfully constructed instance. There is no public API to force
  // m_currentTheme to null without modifying production code, which the task
  // explicitly forbids. The defensive `if (!m_currentTheme) return QString();`
  // branches in fetch*() remain reachable only by future refactors and are
  // tracked by code review rather than a unit test.
  QSKIP("ThemeService::m_currentTheme is non-null by construction; null branch is unreachable "
        "from public API. See slot comment for full rationale.");
}

void TestThemeService::getWebStyles_returnsOnlyWebCss() {
  vnotex::ThemeService svc(makeConfig());
  const auto styles = svc.getWebStyles();
  QVERIFY2(!styles.isEmpty(), "expected at least one web style");
  for (const auto &s : styles) {
    QCOMPARE(QFileInfo(s.second).fileName(), QStringLiteral("web.css"));
    QVERIFY2(!s.second.endsWith(QStringLiteral("highlight.css")),
             "web list must exclude highlight");
  }
}

void TestThemeService::getSyntaxStyles_returnsOnlyHighlightCss() {
  vnotex::ThemeService svc(makeConfig());
  const auto styles = svc.getSyntaxStyles();
  QVERIFY2(!styles.isEmpty(), "expected at least one syntax style");
  for (const auto &s : styles) {
    QCOMPARE(QFileInfo(s.second).fileName(), QStringLiteral("highlight.css"));
    QVERIFY2(!s.second.endsWith(QStringLiteral("web.css")), "syntax list must exclude web.css");
  }
}

void TestThemeService::getWebStyles_and_getSyntaxStyles_areDisjoint() {
  vnotex::ThemeService svc(makeConfig());
  QSet<QString> webPaths;
  for (const auto &s : svc.getWebStyles()) {
    webPaths.insert(s.second);
  }
  for (const auto &s : svc.getSyntaxStyles()) {
    QVERIFY2(!webPaths.contains(s.second), "web and syntax style paths must not intersect");
  }
}

void TestThemeService::getWebStyles_includesCurrentThemeWebCss() {
  vnotex::ThemeService svc(makeConfig());
  const auto currentWeb = svc.getFile(vnotex::Theme::File::WebStyleSheet);
  QVERIFY(!currentWeb.isEmpty());
  bool found = false;
  for (const auto &s : svc.getWebStyles()) {
    if (s.second == currentWeb) {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "current theme web.css must appear in getWebStyles()");
}

void TestThemeService::getSyntaxStyles_includesCurrentThemeHighlightCss() {
  vnotex::ThemeService svc(makeConfig());
  const auto currentHighlight = svc.getFile(vnotex::Theme::File::HighlightStyleSheet);
  QVERIFY(!currentHighlight.isEmpty());
  bool found = false;
  for (const auto &s : svc.getSyntaxStyles()) {
    if (s.second == currentHighlight) {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "current theme highlight.css must appear in getSyntaxStyles()");
}

void TestThemeService::styleCounts_matchThemesShippingThatFile() {
  vnotex::ThemeService svc(makeConfig());

  int expectedWeb = 0;
  int expectedSyntax = 0;
  for (const auto &th : svc.getAllThemes()) {
    if (!vnotex::Theme::getFile(th.m_folderPath, vnotex::Theme::File::WebStyleSheet).isEmpty()) {
      ++expectedWeb;
    }
    if (!vnotex::Theme::getFile(th.m_folderPath, vnotex::Theme::File::HighlightStyleSheet)
             .isEmpty()) {
      ++expectedSyntax;
    }
  }

  // Fixture has no web_styles/ search dir, so web count == #themes shipping web.css.
  QCOMPARE(svc.getWebStyles().size(), expectedWeb);
  QCOMPARE(svc.getSyntaxStyles().size(), expectedSyntax);
}

void TestThemeService::styleEntries_haveDisplayNameAndExistingFile() {
  vnotex::ThemeService svc(makeConfig());
  auto check = [](const QVector<QPair<QString, QString>> &p_styles) {
    for (const auto &s : p_styles) {
      QVERIFY2(!s.first.isEmpty(), "display name must be non-empty");
      QVERIFY2(QFileInfo::exists(s.second), qPrintable("file must exist: " + s.second));
    }
  };
  check(svc.getWebStyles());
  check(svc.getSyntaxStyles());
}

void TestThemeService::styleLists_data() {
  QTest::addColumn<bool>("web");
  QTest::addColumn<QString>("expectedBasename");
  QTest::addColumn<QString>("forbiddenBasename");
  QTest::newRow("web") << true << QStringLiteral("web.css") << QStringLiteral("highlight.css");
  QTest::newRow("syntax") << false << QStringLiteral("highlight.css") << QStringLiteral("web.css");
}

void TestThemeService::styleLists() {
  QFETCH(bool, web);
  QFETCH(QString, expectedBasename);
  QFETCH(QString, forbiddenBasename);

  vnotex::ThemeService svc(makeConfig());
  const auto styles = web ? svc.getWebStyles() : svc.getSyntaxStyles();
  QVERIFY(!styles.isEmpty());
  for (const auto &s : styles) {
    QCOMPARE(QFileInfo(s.second).fileName(), expectedBasename);
    QVERIFY(QFileInfo(s.second).fileName() != forbiddenBasename);
  }
}

// -------- interface.qss token resolution, across every bundled theme --------

void TestThemeService::addBundledThemeRows() {
  QTest::addColumn<QString>("themeName");

  // Discovered, NOT hardcoded: a hardcoded list silently stops covering a
  // newly added theme, which is exactly the theme most likely to be missing a
  // rule.
  const QString themesDir = bundledThemesDir();
  QVERIFY2(!themesDir.isEmpty(), "could not locate src/data/extra/themes");

  QStringList names = QDir(themesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  names.erase(std::remove_if(names.begin(), names.end(),
                             [&themesDir](const QString &p_name) {
                               return !QFileInfo::exists(QDir(themesDir).filePath(
                                   p_name + QStringLiteral("/interface.qss")));
                             }),
              names.end());

  QVERIFY2(names.size() >= 12,
           qPrintable(QStringLiteral("expected at least the 12 bundled themes, found %1: %2")
                          .arg(names.size())
                          .arg(names.join(QStringLiteral(", ")))));

  for (const auto &t : names) {
    QTest::newRow(qPrintable(t)) << t;
  }
}

QString TestThemeService::bundledThemesDir() {
  const QString rel = QStringLiteral("src/data/extra/themes");
  QString path = QFINDTESTDATA(QStringLiteral("../../") + rel);
  if (path.isEmpty()) {
    path = QFINDTESTDATA(rel);
  }
  return path;
}

// Body of the rule whose selector is exactly @p_selector, or a null string.
// The selector must be followed by '{' so that a mention inside a comment, or
// a longer selector that merely starts with the same text (e.g.
// "vnotex--InlineBanner QLabel"), is not mistaken for the rule itself.
QString TestThemeService::qssRuleBody(const QString &p_qss, const QString &p_selector) {
  int from = 0;
  while (from < p_qss.size()) {
    const int idx = p_qss.indexOf(p_selector, from);
    if (idx < 0) {
      return QString();
    }
    int i = idx + p_selector.size();
    while (i < p_qss.size() && p_qss.at(i).isSpace()) {
      ++i;
    }
    if (i < p_qss.size() && p_qss.at(i) == QLatin1Char('{')) {
      const int end = p_qss.indexOf(QLatin1Char('}'), i);
      if (end < 0) {
        return QString();
      }
      return p_qss.mid(i + 1, end - i - 1);
    }
    from = idx + p_selector.size();
  }
  return QString();
}

QString TestThemeService::resolvedInterfaceQss(const QString &p_themeName) {
  const QString rel = QStringLiteral("src/data/extra/themes/") + p_themeName;
  QString path = QFINDTESTDATA(QStringLiteral("../../") + rel);
  if (path.isEmpty()) {
    path = QFINDTESTDATA(rel);
  }
  if (path.isEmpty()) {
    return QString();
  }

  // `native` leaves its palette section empty and relies on the runtime
  // system-palette backfill, so the production preprocessor is required or its
  // tokens would look unresolvable here.
  QScopedPointer<vnotex::Theme> theme(
      vnotex::Theme::fromFolder(path, vnotex::ThemeUtils::backfillSystemPalette));
  if (!theme) {
    return QString();
  }
  return theme->fetchQtStyleSheet();
}

// WCAG 2.x: (L1 + 0.05) / (L2 + 0.05), lighter luminance first.
qreal TestThemeService::contrastRatio(const QColor &p_a, const QColor &p_b) {
  const auto luminance = [](const QColor &c) {
    const auto channel = [](qreal v) {
      // 0.03928 is the historical WCAG 2.0 threshold; the erratum value is
      // 0.04045. No 8-bit channel lands between the two, so both give
      // identical results for the palette's six-digit colors.
      return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
  };
  qreal la = luminance(p_a);
  qreal lb = luminance(p_b);
  if (la < lb) {
    std::swap(la, lb);
  }
  return (la + 0.05) / (lb + 0.05);
}

// A missing palette key does NOT fail loudly: Theme::translateStyleByPalette
// logs a qWarning and leaves the literal "@base#..." in the stylesheet, after
// which Qt's CSS parser silently drops the whole declaration. This test is the
// only thing standing between a typo and a theme quietly losing a property.
void TestThemeService::interfaceQssFullyResolved_data() { addBundledThemeRows(); }

void TestThemeService::interfaceQssFullyResolved() {
  QFETCH(QString, themeName);

  const QString qss = resolvedInterfaceQss(themeName);
  QVERIFY2(!qss.isEmpty(),
           qPrintable(QStringLiteral("theme '%1' produced no interface.qss").arg(themeName)));

  for (const auto &prefix :
       {QStringLiteral("@palette#"), QStringLiteral("@base#"), QStringLiteral("@widgets#")}) {
    const int idx = qss.indexOf(prefix);
    QVERIFY2(idx < 0,
             qPrintable(QStringLiteral("%1 interface.qss has an unresolved token: ...%2...")
                            .arg(themeName, qss.mid(qMax(0, idx - 60), 160))));
  }
}

// InlineBanner is styled purely through the theme (it sets no inline
// stylesheet), so a theme that forgets the rules renders an unstyled strip.
// Asserting on the rule BODIES, not just the selectors: an empty rule, or a
// mention left behind in a comment, must not pass.
void TestThemeService::interfaceQssStylesInlineBanner_data() { addBundledThemeRows(); }

void TestThemeService::interfaceQssStylesInlineBanner() {
  QFETCH(QString, themeName);

  const QString qss = resolvedInterfaceQss(themeName);
  QVERIFY(!qss.isEmpty());

  const QString base = qssRuleBody(qss, QStringLiteral("vnotex--InlineBanner"));
  QVERIFY2(!base.isEmpty(),
           qPrintable(
               QStringLiteral("%1 interface.qss has no vnotex--InlineBanner rule").arg(themeName)));
  for (const auto &decl : {QStringLiteral("background-color:"), QStringLiteral("color:"),
                           QStringLiteral("border-top:"), QStringLiteral("border-bottom:"),
                           QStringLiteral("border-left:")}) {
    QVERIFY2(base.contains(decl),
             qPrintable(QStringLiteral("%1 InlineBanner rule is missing '%2'; body was:%3")
                            .arg(themeName, decl, base)));
  }

  // Both non-default severities must actually restyle the left rule,
  // otherwise setSeverity() is a no-op in this theme. The values mirror
  // InlineBanner::severityName().
  for (const auto &severity : {QStringLiteral("warning"), QStringLiteral("error")}) {
    const QString body = qssRuleBody(
        qss, QStringLiteral("vnotex--InlineBanner[BannerSeverity=\"%1\"]").arg(severity));
    QVERIFY2(
        !body.isEmpty(),
        qPrintable(QStringLiteral("%1 interface.qss has no InlineBanner rule for severity '%2'")
                       .arg(themeName, severity)));
    QVERIFY2(body.contains(QStringLiteral("border-left:")),
             qPrintable(QStringLiteral("%1 InlineBanner '%2' rule does not set border-left; "
                                       "body was:%3")
                            .arg(themeName, severity, body)));
  }

  // The message label must be transparent, or the banner fill is punched out
  // by the generic QLabel background.
  const QString label = qssRuleBody(qss, QStringLiteral("vnotex--InlineBanner QLabel"));
  QVERIFY2(label.contains(QStringLiteral("background-color: transparent")),
           qPrintable(QStringLiteral("%1 has no transparent InlineBanner QLabel rule; body was:%2")
                          .arg(themeName, label)));
}

// The SeverityText property is the sanctioned replacement for a hardcoded
// color in a C++ setStyleSheet() call (see test_hardcoded_color_drift), so a
// theme that omits it silently leaves that text uncolored.
void TestThemeService::interfaceQssStylesSeverityText_data() { addBundledThemeRows(); }

void TestThemeService::interfaceQssStylesSeverityText() {
  QFETCH(QString, themeName);

  const QString qss = resolvedInterfaceQss(themeName);
  QVERIFY(!qss.isEmpty());

  for (const auto &severity :
       {QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")}) {
    const QString body = qssRuleBody(qss, QStringLiteral("*[SeverityText=\"%1\"]").arg(severity));
    QVERIFY2(!body.isEmpty(),
             qPrintable(QStringLiteral("%1 interface.qss has no SeverityText rule for '%2'")
                            .arg(themeName, severity)));
    QVERIFY2(body.contains(QStringLiteral("color:")),
             qPrintable(QStringLiteral("%1 SeverityText '%2' rule does not set color; body was:%3")
                            .arg(themeName, severity, body)));
  }
}

// MutedText replaces setEnabled(false) for hint/caption text: the themes style
// QLabel unconditionally and define no :disabled variant, so the disabled
// palette role never reaches the label. A theme that omits this rule leaves
// hint text at full contrast.
void TestThemeService::interfaceQssStylesMutedText_data() { addBundledThemeRows(); }

void TestThemeService::interfaceQssStylesMutedText() {
  QFETCH(QString, themeName);

  const QString qss = resolvedInterfaceQss(themeName);
  QVERIFY(!qss.isEmpty());

  const QString body = qssRuleBody(qss, QStringLiteral("*[MutedText=\"true\"]"));
  QVERIFY2(!body.isEmpty(),
           qPrintable(QStringLiteral("%1 interface.qss has no MutedText rule").arg(themeName)));
  QVERIFY2(body.contains(QStringLiteral("color:")),
           qPrintable(QStringLiteral("%1 MutedText rule does not set color; body was:%2")
                          .arg(themeName, body)));
}

// MutedText must stay READABLE. The obvious token, base.disabled.fg, is chosen
// for disabled CONTROLS and is far too faint for enabled instructional text --
// on solarized-light it resolves to #DAD3C2 over #FDF6E3, about 1.38:1. That
// is why every hand-written theme carries a dedicated base.muted.fg instead,
// and why this test asserts the resolved contrast rather than merely the
// presence of a "color:" declaration.
//
// Floor: WCAG AA body text is 4.5:1. A theme whose own NORMAL foreground is
// already below that (solarized-light is 4.13:1) cannot beat it, so the bar is
// min(4.5, contrast(normal.fg, normal.bg)) -- muted may be as faint as the
// theme's ordinary text, never fainter.
void TestThemeService::mutedTextIsReadable_data() { addBundledThemeRows(); }

void TestThemeService::mutedTextIsReadable() {
  QFETCH(QString, themeName);

  if (themeName == QStringLiteral("native")) {
    // native resolves its colors from the live system palette, so the value
    // is the OS's own muted-text choice and varies per machine and per
    // light/dark setting. Nothing stable to assert.
    QSKIP("native takes its colors from the system palette at runtime");
  }

  const QString themesDir = bundledThemesDir();
  QVERIFY(!themesDir.isEmpty());
  QScopedPointer<vnotex::Theme> theme(
      vnotex::Theme::fromFolder(QDir(themesDir).filePath(themeName)));
  QVERIFY(theme);

  const QColor muted(theme->paletteColor(QStringLiteral("base#muted#fg")));
  const QColor bg(theme->paletteColor(QStringLiteral("base#normal#bg")));
  const QColor fg(theme->paletteColor(QStringLiteral("base#normal#fg")));
  QVERIFY2(muted.isValid(), qPrintable(QStringLiteral("%1 has no base.muted.fg").arg(themeName)));
  QVERIFY(bg.isValid() && fg.isValid());

  const qreal normalRatio = contrastRatio(fg, bg);
  const qreal mutedRatio = contrastRatio(muted, bg);
  const qreal floor = qMin(qreal(4.5), normalRatio);

  QVERIFY2(mutedRatio + 0.01 >= floor,
           qPrintable(QStringLiteral("%1: muted %2 on %3 is only %4:1, below the %5:1 floor")
                          .arg(themeName, muted.name(), bg.name())
                          .arg(mutedRatio, 0, 'f', 2)
                          .arg(floor, 0, 'f', 2)));

  // It must also actually BE muted, or the property is pointless.
  QVERIFY2(mutedRatio <= normalRatio + 0.01,
           qPrintable(QStringLiteral("%1: muted text (%2:1) must not out-contrast normal text "
                                     "(%3:1)")
                          .arg(themeName)
                          .arg(mutedRatio, 0, 'f', 2)
                          .arg(normalRatio, 0, 'f', 2)));
}

// ============ Comment highlight colors ============
//
// pdfviewer.css is linked VERBATIM into the PDF viewer template and is NOT
// processed by the theme token resolver, so it can only reference CSS custom
// properties. These cases pin the two halves of that contract: every schema
// token gets a mapping, and what is injected is a RESOLVED color rather than an
// unresolved `@palette#` / `@base#` token (which the CSS parser would silently
// drop, leaving the highlight invisible).

void TestThemeService::commentHighlightColorsAreDefinedForEveryToken_data() {
  addBundledThemeRows();
}

void TestThemeService::commentHighlightColorsAreDefinedForEveryToken() {
  QFETCH(QString, themeName);

  auto cfg = makeConfig();
  cfg.themeName = themeName;
  vnotex::ThemeService svc(cfg);

  const auto tokens = vnotex::CommentColor::all();
  QVERIFY2(!tokens.isEmpty(), "the color schema is empty; the gate would be vacuous");

  for (const auto &token : tokens) {
    const auto color = svc.commentHighlightColor(token);
    QVERIFY2(
        !color.isEmpty(),
        qPrintable(QStringLiteral("theme %1 has no color for token %2").arg(themeName, token)));
    QVERIFY2(!color.contains(QLatin1Char('@')),
             qPrintable(QStringLiteral("theme %1 leaves token %2 unresolved: %3")
                            .arg(themeName, token, color)));
    QVERIFY2(QColor::isValidColorName(color) || color.startsWith(QStringLiteral("rgba(")) ||
                 color.startsWith(QStringLiteral("rgb(")) || color.startsWith(QLatin1Char('#')),
             qPrintable(QStringLiteral("theme %1 token %2 is not a CSS color: %3")
                            .arg(themeName, token, color)));
  }
}

void TestThemeService::commentHighlightCssVariablesAreFullyResolved_data() {
  addBundledThemeRows();
}

void TestThemeService::commentHighlightCssVariablesAreFullyResolved() {
  QFETCH(QString, themeName);

  auto cfg = makeConfig();
  cfg.themeName = themeName;
  vnotex::ThemeService svc(cfg);

  const auto css = svc.commentHighlightCssVariables();
  QVERIFY(css.startsWith(QStringLiteral(":root {")));
  QVERIFY2(
      !css.contains(QLatin1Char('@')),
      qPrintable(QStringLiteral("theme %1 injects unresolved tokens:\n%2").arg(themeName, css)));
  // Would let a palette value inject markup into the template.
  QVERIFY(!css.contains(QStringLiteral("</style>"), Qt::CaseInsensitive));

  for (const auto &token : vnotex::CommentColor::all()) {
    QVERIFY2(
        css.contains(QStringLiteral("--vx-comment-%1:").arg(token)),
        qPrintable(QStringLiteral("theme %1 is missing --vx-comment-%2").arg(themeName, token)));
  }
  // The selection ring is VNote chrome and follows the palette, so it is part
  // of the same block.
  QVERIFY(css.contains(QStringLiteral("--vx-comment-selected-outline:")));
}

void TestThemeService::commentHighlightColorFallsBackForAnUnknownToken() {
  vnotex::ThemeService svc(makeConfig());

  // A hand-edited or newer-schema comments.json must never render unstyled.
  const auto fallback = svc.commentHighlightColor(vnotex::CommentColor::defaultToken());
  QCOMPARE(svc.commentHighlightColor(QStringLiteral("chartreuse")), fallback);
  QCOMPARE(svc.commentHighlightColor(QStringLiteral("#ff00ff")), fallback);
  QCOMPARE(svc.commentHighlightColor(QString()), fallback);
}

} // namespace tests

QTEST_MAIN(tests::TestThemeService)
#include "test_themeservice.moc"