#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include <core/sessionconfig.h>

using namespace vnotex;

namespace tests {

class TestSessionConfigExtProg : public QObject {
  Q_OBJECT

private slots:
  void testSuffixesFromJson();
  void testSuffixesToJson();
  void testJsonRoundTrip();
  void testMatchesSuffix_exact();
  void testMatchesSuffix_caseInsensitive();
  void testMatchesSuffix_noMatch();
  void testMatchesSuffix_empty();
  void testFindBySuffix_firstMatchWins();
  void testFindBySuffix_noMatch();
  void testSuffixNormalization();
  void testFetchCommand();
  void testEmptyCommand();
};

void TestSessionConfigExtProg::testSuffixesFromJson() {
  QJsonObject jobj;
  jobj["name"] = "Test";
  jobj["command"] = "test %1";
  jobj["shortcut"] = "";
  QJsonArray arr;
  arr.append("py");
  arr.append("js");
  jobj["suffixes"] = arr;

  SessionConfig::ExternalProgram prog;
  prog.fromJson(jobj);

  QCOMPARE(prog.m_suffixes, QStringList({"py", "js"}));
}

void TestSessionConfigExtProg::testSuffixesToJson() {
  SessionConfig::ExternalProgram prog;
  prog.m_name = "Editor";
  prog.m_command = "edit %1";
  prog.m_shortcut = "Ctrl+E";
  prog.m_suffixes = {"cpp", "h"};

  QJsonObject jobj = prog.toJson();

  QJsonArray expected;
  expected.append("cpp");
  expected.append("h");
  QCOMPARE(jobj["suffixes"].toArray(), expected);
}

void TestSessionConfigExtProg::testJsonRoundTrip() {
  SessionConfig::ExternalProgram original;
  original.m_name = "VSCode";
  original.m_command = "code \"%1\"";
  original.m_shortcut = "Ctrl+Shift+V";
  original.m_suffixes = {"py", "js", "ts"};

  QJsonObject jobj = original.toJson();

  SessionConfig::ExternalProgram restored;
  restored.fromJson(jobj);

  QVERIFY(original == restored);
}

void TestSessionConfigExtProg::testMatchesSuffix_exact() {
  SessionConfig::ExternalProgram prog;
  prog.m_suffixes = {"py"};

  QVERIFY(prog.matchesSuffix("py"));
}

void TestSessionConfigExtProg::testMatchesSuffix_caseInsensitive() {
  SessionConfig::ExternalProgram prog;
  prog.m_suffixes = {"py"};

  QVERIFY(prog.matchesSuffix("PY"));
  QVERIFY(prog.matchesSuffix("Py"));
  QVERIFY(prog.matchesSuffix("pY"));
}

void TestSessionConfigExtProg::testMatchesSuffix_noMatch() {
  SessionConfig::ExternalProgram prog;
  prog.m_suffixes = {"py", "js"};

  QVERIFY(!prog.matchesSuffix("rs"));
  QVERIFY(!prog.matchesSuffix("cpp"));
}

void TestSessionConfigExtProg::testMatchesSuffix_empty() {
  SessionConfig::ExternalProgram prog;
  prog.m_suffixes = {};

  QVERIFY(!prog.matchesSuffix("py"));
}

void TestSessionConfigExtProg::testFindBySuffix_firstMatchWins() {
  // Simulate findExternalProgramBySuffix logic: iterate and return first match
  QVector<SessionConfig::ExternalProgram> programs;

  SessionConfig::ExternalProgram prog1;
  prog1.m_name = "Editor1";
  prog1.m_suffixes = {"py", "js"};
  programs.append(prog1);

  SessionConfig::ExternalProgram prog2;
  prog2.m_name = "Editor2";
  prog2.m_suffixes = {"py", "ts"};
  programs.append(prog2);

  QString found;
  for (const auto &p : programs) {
    if (p.matchesSuffix("py")) {
      found = p.m_name;
      break;
    }
  }

  QCOMPARE(found, QStringLiteral("Editor1"));
}

void TestSessionConfigExtProg::testFindBySuffix_noMatch() {
  QVector<SessionConfig::ExternalProgram> programs;

  SessionConfig::ExternalProgram prog1;
  prog1.m_name = "Editor1";
  prog1.m_suffixes = {"py", "js"};
  programs.append(prog1);

  SessionConfig::ExternalProgram prog2;
  prog2.m_name = "Editor2";
  prog2.m_suffixes = {"cpp", "h"};
  programs.append(prog2);

  bool found = false;
  for (const auto &p : programs) {
    if (p.matchesSuffix("rs")) {
      found = true;
      break;
    }
  }

  QVERIFY(!found);
}

void TestSessionConfigExtProg::testSuffixNormalization() {
  // matchesSuffix lowercases the input, so uppercase suffixes in m_suffixes
  // still match when input is lowercase (both sides compared case-insensitively)
  SessionConfig::ExternalProgram prog;
  prog.m_suffixes = {"PY", "JS"};

  QVERIFY(prog.matchesSuffix("py"));
  QVERIFY(prog.matchesSuffix("js"));
  QVERIFY(prog.matchesSuffix("PY"));
  QVERIFY(prog.matchesSuffix("Js"));
}

void TestSessionConfigExtProg::testFetchCommand() {
  SessionConfig::ExternalProgram prog;
  prog.m_command = "code %1";

  QString result = prog.fetchCommand("/path/to/file.py");
  QCOMPARE(result, QStringLiteral("code \"/path/to/file.py\""));
}

void TestSessionConfigExtProg::testEmptyCommand() {
  SessionConfig::ExternalProgram prog;
  prog.m_command = "";

  QString result = prog.fetchCommand("/some/file");
  QVERIFY(result.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSessionConfigExtProg)
#include "test_sessionconfig_extprog.moc"
