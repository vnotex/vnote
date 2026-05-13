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
  void testFetchMarkdownEditorStyle_delegatesToCurrentTheme();
  void testFetchWebStyleSheet_nullThemeReturnsEmpty();

private:
  QString m_themeFolder;
  QTemporaryDir m_tmp;

  QString findPureTheme();
};

QString TestThemeService::findPureTheme() {
  QString p = QFINDTESTDATA("../../src/data/extra/themes/pure");
  if (p.isEmpty()) {
    p = QFINDTESTDATA("src/data/extra/themes/pure");
  }
  return p;
}

void TestThemeService::initTestCase() {
  QVERIFY(m_tmp.isValid());
  m_themeFolder = findPureTheme();
  QVERIFY2(!m_themeFolder.isEmpty(), "pure theme fixture not found");
}

void TestThemeService::cleanupTestCase() {}

void TestThemeService::testFetchWebStyleSheet_delegatesToCurrentTheme() {
  // Build a minimal ThemeServiceConfig pointing at the pure theme folder.
  // The themes dir must be the parent of the theme folder.
  QFileInfo info(m_themeFolder);
  QString themesDir = info.absolutePath();

  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("pure");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = themesDir;

  vnotex::ThemeService svc(cfg);
  // We need to ensure m_currentTheme is loaded; switchTheme triggers load.
  svc.switchTheme(QStringLiteral("pure"));

  QString out = svc.fetchWebStyleSheet();
  QVERIFY2(!out.isEmpty(), "expected non-empty CSS from pure theme");
}

void TestThemeService::testFetchTextEditorStyle_delegatesToCurrentTheme() {
  QFileInfo info(m_themeFolder);
  QString themesDir = info.absolutePath();

  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("pure");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = themesDir;

  vnotex::ThemeService svc(cfg);
  svc.switchTheme(QStringLiteral("pure"));

  QString out = svc.fetchTextEditorStyle();
  QVERIFY2(!out.isEmpty(), "expected non-empty JSON from pure text-editor.theme");
}

void TestThemeService::testFetchMarkdownEditorStyle_delegatesToCurrentTheme() {
  QFileInfo info(m_themeFolder);
  QString themesDir = info.absolutePath();

  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("pure");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = themesDir;

  vnotex::ThemeService svc(cfg);
  svc.switchTheme(QStringLiteral("pure"));

  // Pure theme has no markdown-text-editor.theme — fallback to text-editor.theme.
  QString out = svc.fetchMarkdownEditorStyle();
  QVERIFY2(!out.isEmpty(), "expected non-empty fallback from text-editor.theme");
}

void TestThemeService::testFetchWebStyleSheet_nullThemeReturnsEmpty() {
  // Force null m_currentTheme by switching to a non-existent theme name,
  // OR by constructing the service with an invalid path. Simplest: construct
  // with bogus appDataPath and skip switchTheme() — m_currentTheme remains null.
  vnotex::ThemeServiceConfig cfg;
  cfg.themeName = QStringLiteral("nonexistent");
  cfg.locale = QStringLiteral("en_US");
  cfg.appDataPath = QStringLiteral("/tmp/does-not-exist-vnote-themetest");

  vnotex::ThemeService svc(cfg);
  // Do NOT call switchTheme — m_currentTheme is null after construction.

  QString out = svc.fetchWebStyleSheet();
  QVERIFY2(out.isEmpty(), "null current theme should yield empty string");
}

} // namespace tests

QTEST_MAIN(tests::TestThemeService)
#include "test_themeservice.moc"
