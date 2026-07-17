#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <core/theme.h>
#include <gui/services/themeservice.h>

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

private:
  QString m_themeFolder;
  QTemporaryDir m_tmp;

  QString findPureTheme();

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
    QVERIFY2(!s.second.endsWith(QStringLiteral("highlight.css")), "web list must exclude highlight");
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

} // namespace tests

QTEST_MAIN(tests::TestThemeService)
#include "test_themeservice.moc"
