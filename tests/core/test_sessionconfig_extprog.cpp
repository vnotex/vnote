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
  void testIsSystemProgram();
  void testSystemProgramConstant();
  void testSystemAlwaysLast();
  void testSystemCommandForcedEmpty();
  void testFindBySuffixPriority();
  void testSystemProgramRoundTrip();
  void testAutoCreationFromEmptyJson();
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

void TestSessionConfigExtProg::testIsSystemProgram() {
  SessionConfig::ExternalProgram prog;

  prog.m_name = "system";
  QVERIFY(prog.isSystemProgram());

  prog.m_name = "System";
  QVERIFY(prog.isSystemProgram());

  prog.m_name = "SYSTEM";
  QVERIFY(prog.isSystemProgram());

  prog.m_name = "SyStEm";
  QVERIFY(prog.isSystemProgram());

  prog.m_name = "mysystem";
  QVERIFY(!prog.isSystemProgram());

  prog.m_name = "";
  QVERIFY(!prog.isSystemProgram());

  prog.m_name = "notepad";
  QVERIFY(!prog.isSystemProgram());
}

void TestSessionConfigExtProg::testSystemProgramConstant() {
  QCOMPARE(SessionConfig::ExternalProgram::c_systemProgramName, QStringLiteral("system"));
}

void TestSessionConfigExtProg::testSystemAlwaysLast() {
  QVector<SessionConfig::ExternalProgram> input;

  SessionConfig::ExternalProgram sys;
  sys.m_name = "system";
  sys.m_suffixes = {"pdf"};
  input.append(sys);

  SessionConfig::ExternalProgram user1;
  user1.m_name = "Editor1";
  user1.m_command = "edit %1";
  user1.m_suffixes = {"txt"};
  input.append(user1);

  SessionConfig::ExternalProgram user2;
  user2.m_name = "Editor2";
  user2.m_command = "vim %1";
  user2.m_suffixes = {"cpp"};
  input.append(user2);

  // Simulate the ordering logic from setExternalPrograms:
  // normalize suffixes, extract system, re-append last.
  QVector<SessionConfig::ExternalProgram> normalized;
  for (const auto &pro : input) {
    SessionConfig::ExternalProgram clean;
    clean.m_name = pro.m_name.trimmed();
    clean.m_command = pro.m_command;
    clean.m_shortcut = pro.m_shortcut;
    for (const auto &s : pro.m_suffixes) {
      QString suffix = s.trimmed().toLower();
      while (suffix.startsWith(QLatin1Char('.'))) {
        suffix = suffix.mid(1);
      }
      if (!suffix.isEmpty()) {
        clean.m_suffixes << suffix;
      }
    }
    normalized << clean;
  }

  int sysIdx = -1;
  for (int i = 0; i < normalized.size(); ++i) {
    if (normalized[i].isSystemProgram()) {
      sysIdx = i;
      break;
    }
  }
  QVERIFY(sysIdx >= 0);
  SessionConfig::ExternalProgram sysExtracted = normalized[sysIdx];
  sysExtracted.m_command.clear();
  normalized.remove(sysIdx);
  normalized.append(sysExtracted);

  // Verify: system is last, command is empty
  QCOMPARE(normalized.size(), 3);
  QVERIFY(normalized.last().isSystemProgram());
  QVERIFY(normalized.last().m_command.isEmpty());
  QCOMPARE(normalized[0].m_name, QStringLiteral("Editor1"));
  QCOMPARE(normalized[1].m_name, QStringLiteral("Editor2"));
}

void TestSessionConfigExtProg::testSystemCommandForcedEmpty() {
  SessionConfig::ExternalProgram sys;
  sys.m_name = "system";
  sys.m_command = "notepad %1";  // Should be cleared during normalization
  sys.m_suffixes = {"txt"};

  // Simulate the normalization that setExternalPrograms does
  if (sys.isSystemProgram()) {
    sys.m_command.clear();
  }

  QVERIFY(sys.m_command.isEmpty());
  QCOMPARE(sys.m_suffixes, QStringList({"txt"}));  // Suffixes preserved
}

void TestSessionConfigExtProg::testFindBySuffixPriority() {
  QVector<SessionConfig::ExternalProgram> programs;

  SessionConfig::ExternalProgram user;
  user.m_name = "PdfViewer";
  user.m_command = "evince %1";
  user.m_suffixes = {"pdf"};
  programs.append(user);

  SessionConfig::ExternalProgram sys;
  sys.m_name = "system";
  sys.m_suffixes = {"pdf"};
  programs.append(sys);

  // Simulate findExternalProgramBySuffix: iterate in order, first match wins
  const SessionConfig::ExternalProgram *found = nullptr;
  for (const auto &p : programs) {
    if (p.matchesSuffix("pdf")) {
      found = &p;
      break;
    }
  }

  QVERIFY(found != nullptr);
  QCOMPARE(found->m_name, QStringLiteral("PdfViewer"));
  QVERIFY(!found->isSystemProgram());
}

void TestSessionConfigExtProg::testSystemProgramRoundTrip() {
  SessionConfig::ExternalProgram sys;
  sys.m_name = "system";
  sys.m_command = "";
  sys.m_shortcut = "";
  sys.m_suffixes = {"pdf", "docx"};

  QJsonObject jobj = sys.toJson();

  SessionConfig::ExternalProgram restored;
  restored.fromJson(jobj);

  QVERIFY(restored.isSystemProgram());
  QCOMPARE(restored.m_suffixes, QStringList({"pdf", "docx"}));
  QVERIFY(restored.m_command.isEmpty());
}

void TestSessionConfigExtProg::testAutoCreationFromEmptyJson() {
  // Simulate what loadExternalPrograms does with empty JSON
  QJsonObject session;
  // No "externalPrograms" key

  const auto arr = session.value(QStringLiteral("externalPrograms")).toArray();
  QVector<SessionConfig::ExternalProgram> programs;
  programs.resize(arr.size());
  for (int i = 0; i < arr.size(); ++i) {
    programs[i].fromJson(arr[i].toObject());
  }

  // Auto-create system program
  int sysIdx = -1;
  for (int i = 0; i < programs.size(); ++i) {
    if (programs[i].isSystemProgram()) {
      sysIdx = i;
      break;
    }
  }
  if (sysIdx < 0) {
    SessionConfig::ExternalProgram sys;
    sys.m_name = SessionConfig::ExternalProgram::c_systemProgramName;
    programs.append(sys);
  }

  QCOMPARE(programs.size(), 1);
  QVERIFY(programs[0].isSystemProgram());
  QVERIFY(programs[0].m_command.isEmpty());
  QVERIFY(programs[0].m_suffixes.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSessionConfigExtProg)
#include "test_sessionconfig_extprog.moc"
