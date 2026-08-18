// test_turndown_js.cpp
//
// TurndownConverter is the HTML -> Markdown converter behind "Parse to Markdown
// and Paste". Its image rule, fixImage(), was DEFINED BUT NEVER CALLED for its
// entire existence, so the bundled default rule won and every declared size was
// silently dropped before the pasted text ever reached
// fetchImagesToLocalAndReplace().
//
// Two things are gated here, and the first is the one that regressed:
//
//   1. fixImage() is actually installed. A rule that is never registered passes
//      every C++ test in the tree, because nothing else observes it.
//   2. The tag it emits is the SAME canonical spelling
//      vte::MarkdownUtils::generateImageTag() produces -- self-closing,
//      double-quoted, attribute order src alt title width height, values
//      escaped -- so both generators round trip through the one C++ scanner.
//
// === Why a stub TurndownService ===
// The real turndown bundle is browser-oriented vendored code and is not the
// subject here. The REAL src/data/extra/web/js/turndown.js is evaluated
// unmodified; only its collaborators are stubbed, so the registration and the
// replacement function under test are the production ones. Evaluating a
// transcribed copy would gate nothing.

#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QString>
#include <QtTest>

namespace tests {

class TestTurndownJs : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void imageRuleIsInstalled();
  void unsizedImageStaysMarkdown();
  void sizedImageBecomesTheCanonicalTag();
  void onlyValidPositiveIntegersCountAsASize();
  void valuesAreEscaped();
  void anImageWithNoSrcIsDropped();

private:
  // Run the installed `img_fix` replacement against a node with the given
  // attributes, and return what it produced.
  QString convert(const QString &p_src, const QString &p_alt = QString(),
                  const QString &p_title = QString(), const QString &p_width = QString(),
                  const QString &p_height = QString());

  QJSEngine m_engine;
};

void TestTurndownJs::initTestCase() {
  // A TurndownService stand-in that records the rules the production code
  // installs, so `img_fix` can be invoked directly.
  const QString harness = QStringLiteral(R"JS(
    var installedRules = {};
    var turndownPluginGfm = { options: {}, gfm: {} };
    function TurndownService(options) { this.options = options; }
    TurndownService.prototype.use = function() {};
    TurndownService.prototype.remove = function() {};
    TurndownService.prototype.addRule = function(name, rule) { installedRules[name] = rule; };
    TurndownService.prototype.turndown = function(html) { return html; };
  )JS");
  QJSValue result = m_engine.evaluate(harness);
  QVERIFY2(!result.isError(), qPrintable(result.toString()));

  // The REAL file, unmodified. A `class` declaration is script-scoped rather
  // than a property of the global object, so it is not visible to a LATER
  // evaluate() call -- the construction has to happen in this same script. The
  // rules it installs land in the harness' `installedRules`, which IS global.
  QFile file(QStringLiteral(VNOTE_SRC_DIR) + QStringLiteral("/data/extra/web/js/turndown.js"));
  QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.fileName()));
  const QString source = QString::fromUtf8(file.readAll()) +
                         QStringLiteral("\nvar converter = new TurndownConverter(null);\n");
  result = m_engine.evaluate(source, file.fileName());
  QVERIFY2(!result.isError(), qPrintable(result.toString()));
}

QString TestTurndownJs::convert(const QString &p_src, const QString &p_alt, const QString &p_title,
                                const QString &p_width, const QString &p_height) {
  const QString script =
      QStringLiteral(R"JS(
        (function() {
          var attrs = { src: %1, width: %2, height: %3 };
          var node = {
            alt: %4,
            title: %5,
            getAttribute: function(name) {
              return Object.prototype.hasOwnProperty.call(attrs, name) ? attrs[name] : null;
            }
          };
          return installedRules['img_fix'].replacement('', node);
        })()
      )JS")
          .arg(p_src.isNull() ? QStringLiteral("null") : QStringLiteral("\"%1\"").arg(p_src),
               p_width.isNull() ? QStringLiteral("null") : QStringLiteral("\"%1\"").arg(p_width),
               p_height.isNull() ? QStringLiteral("null") : QStringLiteral("\"%1\"").arg(p_height),
               QStringLiteral("\"%1\"").arg(
                   QString(p_alt).replace(QLatin1Char('"'), QStringLiteral("\\\""))),
               QStringLiteral("\"%1\"").arg(
                   QString(p_title).replace(QLatin1Char('"'), QStringLiteral("\\\""))));

  const QJSValue value = m_engine.evaluate(script);
  if (value.isError()) {
    return QStringLiteral("ERROR: ") + value.toString();
  }
  return value.toString();
}

// The regression this file exists for: the rule was dead code.
void TestTurndownJs::imageRuleIsInstalled() {
  const QJSValue installed = m_engine.evaluate(QStringLiteral("typeof installedRules['img_fix']"));
  QCOMPARE(installed.toString(), QStringLiteral("object"));
  const QJSValue filter = m_engine.evaluate(QStringLiteral("installedRules['img_fix'].filter"));
  QCOMPARE(filter.toString(), QStringLiteral("img"));
}

void TestTurndownJs::unsizedImageStaysMarkdown() {
  QCOMPARE(convert(QStringLiteral("a.png")), QStringLiteral("![](a.png)"));
  QCOMPARE(convert(QStringLiteral("a.png"), QStringLiteral("alt")),
           QStringLiteral("![alt](a.png)"));
  QCOMPARE(convert(QStringLiteral("a.png"), QStringLiteral("alt"), QStringLiteral("t")),
           QStringLiteral("![alt](a.png \"t\")"));
}

void TestTurndownJs::sizedImageBecomesTheCanonicalTag() {
  QCOMPARE(convert(QStringLiteral("a.png"), QString(), QString(), QStringLiteral("500")),
           QStringLiteral("<img src=\"a.png\" width=\"500\" />"));
  QCOMPARE(convert(QStringLiteral("a.png"), QString(), QString(), QString(), QStringLiteral("300")),
           QStringLiteral("<img src=\"a.png\" height=\"300\" />"));
  // The full canonical order, matching MarkdownUtils::generateImageTag().
  QCOMPARE(
      convert(QStringLiteral("a.png"), QStringLiteral("alt"), QStringLiteral("t"),
              QStringLiteral("500"), QStringLiteral("300")),
      QStringLiteral("<img src=\"a.png\" alt=\"alt\" title=\"t\" width=\"500\" height=\"300\" />"));
}

// The same "valid positive integer" rule as the C++ side.
void TestTurndownJs::onlyValidPositiveIntegersCountAsASize() {
  for (const auto &bad : {QStringLiteral("50%"), QStringLiteral("abc"), QStringLiteral("0"),
                          QStringLiteral("-5"), QStringLiteral("1.5"), QStringLiteral("")}) {
    QVERIFY2(convert(QStringLiteral("a.png"), QString(), QString(), bad) ==
                 QStringLiteral("![](a.png)"),
             qPrintable(bad));
  }
  // Surrounding whitespace is tolerated, as in the C++ scanner.
  QCOMPARE(convert(QStringLiteral("a.png"), QString(), QString(), QStringLiteral(" 500 ")),
           QStringLiteral("<img src=\"a.png\" width=\"500\" />"));
}

void TestTurndownJs::valuesAreEscaped() {
  const QString result =
      convert(QStringLiteral("a&b.png"), QStringLiteral("q\"x"), QString(), QStringLiteral("10"));
  QCOMPARE(result, QStringLiteral("<img src=\"a&amp;b.png\" alt=\"q&quot;x\" width=\"10\" />"));
}

void TestTurndownJs::anImageWithNoSrcIsDropped() {
  QCOMPARE(convert(QString(), QStringLiteral("alt"), QString(), QStringLiteral("500")), QString());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTurndownJs)
#include "test_turndown_js.moc"
