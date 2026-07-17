#include <QtTest>

#include <QString>

#include <export/exportstyleresolver.h>

using namespace vnotex;

namespace tests {

class TestExportStyleResolver : public QObject {
  Q_OBJECT

private slots:
  void resolve_data();
  void resolve();
};

void TestExportStyleResolver::resolve_data() {
  QTest::addColumn<QString>("optionFile");
  QTest::addColumn<QString>("themeHighlightFile");
  QTest::addColumn<QString>("expected");

  const QString themeHl = QStringLiteral("/themes/pure/highlight.css");

  // Empty option -> fall back to the theme highlight.css.
  QTest::newRow("empty-option") << QString() << themeHl << themeHl;

  // A stale web.css selection (the regression) -> fall back to theme highlight.css.
  QTest::newRow("web-css") << QStringLiteral("/themes/pure/web.css") << themeHl << themeHl;

  // An explicit highlight.css (even from another theme) -> honored.
  QTest::newRow("other-highlight")
      << QStringLiteral("/themes/native/highlight.css") << themeHl
      << QStringLiteral("/themes/native/highlight.css");

  // Arbitrary/non-css option -> fall back.
  QTest::newRow("arbitrary") << QStringLiteral("/tmp/custom.txt") << themeHl << themeHl;

  // Both empty -> empty (degenerate).
  QTest::newRow("both-empty") << QString() << QString() << QString();
}

void TestExportStyleResolver::resolve() {
  QFETCH(QString, optionFile);
  QFETCH(QString, themeHighlightFile);
  QFETCH(QString, expected);

  QCOMPARE(resolveSyntaxHighlightFile(optionFile, themeHighlightFile), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExportStyleResolver)
#include "test_exportstyleresolver.moc"
