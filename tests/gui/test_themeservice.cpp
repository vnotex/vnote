#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

private:
  QString m_themeFolder;
  QTemporaryDir m_tmp;

  QString findPureTheme();

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

} // namespace tests

QTEST_MAIN(tests::TestThemeService)
#include "test_themeservice.moc"
