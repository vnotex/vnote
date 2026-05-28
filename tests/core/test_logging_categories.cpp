// test_logging_categories.cpp - Test logging category defaults and VNOTE_LOG_RULES override
#include <QtTest>

#include <QLoggingCategory>

#include <core/logging.h>

using namespace vnotex;

namespace tests {

class TestLoggingCategories : public QObject {
  Q_OBJECT

private slots:
  void test_defaultRules_noEnv();
  void test_envOverride_syncOnly();
  void test_envOverride_webJsExplicit();
  void test_envOverride_wildcardWins();
  void test_envOverride_wildcardWithExplicitOff();
};

void TestLoggingCategories::test_defaultRules_noEnv() {
  // Clear environment variable if set
  qunsetenv("VNOTE_LOG_RULES");

  // Apply default rules
  installDefaultLoggingRules();

  // Verify defaults: all should be disabled
  QVERIFY(!lcSync().isDebugEnabled());
  QVERIFY(!lcWebJs().isDebugEnabled());
  QVERIFY(!lcVim().isDebugEnabled());
}

void TestLoggingCategories::test_envOverride_syncOnly() {
  // Set environment to enable only sync
  qputenv("VNOTE_LOG_RULES", "vnote.sync.debug=true");

  // Apply rules (reads env and appends to baseline)
  installDefaultLoggingRules();

  // Verify: only sync should be enabled
  QVERIFY(lcSync().isDebugEnabled());
  QVERIFY(!lcWebJs().isDebugEnabled());
  QVERIFY(!lcVim().isDebugEnabled());
  QVERIFY(!lcBuffer().isDebugEnabled());

  // Cleanup
  qunsetenv("VNOTE_LOG_RULES");
}

void TestLoggingCategories::test_envOverride_webJsExplicit() {
  // Set environment to enable web.js via explicit rule
  qputenv("VNOTE_LOG_RULES", "vnote.web.js.debug=true");

  // Apply rules (explicit rule in env appends after baseline's =false, so env wins)
  installDefaultLoggingRules();

  // Verify: web.js should be enabled (later rule wins over baseline =false)
  QVERIFY(lcWebJs().isDebugEnabled());
  QVERIFY(!lcSync().isDebugEnabled());
  QVERIFY(!lcVim().isDebugEnabled());

  // Cleanup
  qunsetenv("VNOTE_LOG_RULES");
}

void TestLoggingCategories::test_envOverride_wildcardWins() {
  // Set environment to enable all via wildcard
  qputenv("VNOTE_LOG_RULES", "*.debug=true");

  // Apply rules (wildcard in env appends after baseline explicit rules, so wildcard wins)
  installDefaultLoggingRules();

  // Verify: wildcards override earlier explicit false rules
  QVERIFY(lcWebJs().isDebugEnabled());
  QVERIFY(lcVim().isDebugEnabled());
  QVERIFY(lcSync().isDebugEnabled());

  // Cleanup
  qunsetenv("VNOTE_LOG_RULES");
}

void TestLoggingCategories::test_envOverride_wildcardWithExplicitOff() {
  // Set environment: wildcard true, then explicit false for web.js and vim
  // Rules are applied in order; LAST matching rule wins
  qputenv("VNOTE_LOG_RULES", "*.debug=true\nvnote.web.js.debug=false\nvnote.vim.debug=false");

  // Apply rules
  installDefaultLoggingRules();

  // Verify: sync enabled (wildcard), web.js and vim disabled (explicit rules after wildcard)
  QVERIFY(lcSync().isDebugEnabled());
  QVERIFY(!lcWebJs().isDebugEnabled());
  QVERIFY(!lcVim().isDebugEnabled());

  // Cleanup
  qunsetenv("VNOTE_LOG_RULES");
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestLoggingCategories)
#include "test_logging_categories.moc"
