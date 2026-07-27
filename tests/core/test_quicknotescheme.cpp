#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include <core/iconfigmgr.h>
#include <core/sessionconfig.h>

using namespace vnotex;

namespace tests {

// Minimal mock that prevents crash when update() is called.
class MockConfigMgr : public IConfigMgr {
public:
  void updateMainConfig(const QJsonObject &) override {}
  void updateSessionConfig(const QJsonObject &) override {} // no-op
};

class TestQuickNoteScheme : public QObject {
  Q_OBJECT

private slots:
  void testSchemeTemplateRoundTrip();
  void testSchemeEmptyTemplateRoundTrip();
  void testBackwardCompatNoTemplate();
  void testOperatorEqualConsidersTemplate();
  void testSessionConfigSchemesTemplateRoundTrip();

private:
  MockConfigMgr m_mockMgr;
};

void TestQuickNoteScheme::testSchemeTemplateRoundTrip() {
  SessionConfig::QuickNoteScheme scheme;
  scheme.m_name = QStringLiteral("journal");
  scheme.m_folderPath = QStringLiteral("journal/%yyyy%");
  scheme.m_noteName = QStringLiteral("quick_note_%da%.md");
  scheme.m_template = QStringLiteral("daily.md");

  const QJsonObject json = scheme.toJson();
  QCOMPARE(json[QStringLiteral("template")].toString(), QStringLiteral("daily.md"));

  SessionConfig::QuickNoteScheme scheme2;
  scheme2.fromJson(json);

  QCOMPARE(scheme2.m_name, scheme.m_name);
  QCOMPARE(scheme2.m_folderPath, scheme.m_folderPath);
  QCOMPARE(scheme2.m_noteName, scheme.m_noteName);
  QCOMPARE(scheme2.m_template, QStringLiteral("daily.md"));
}

void TestQuickNoteScheme::testSchemeEmptyTemplateRoundTrip() {
  SessionConfig::QuickNoteScheme scheme;
  scheme.m_name = QStringLiteral("plain");
  scheme.m_noteName = QStringLiteral("note.md");
  // m_template is default empty — "None" in the settings UI.

  SessionConfig::QuickNoteScheme scheme2;
  scheme2.fromJson(scheme.toJson());

  QVERIFY(scheme2.m_template.isEmpty());
}

void TestQuickNoteScheme::testBackwardCompatNoTemplate() {
  QJsonObject jobj;
  jobj[QStringLiteral("name")] = QStringLiteral("legacy");
  jobj[QStringLiteral("folderPath")] = QStringLiteral("/notes");
  jobj[QStringLiteral("noteName")] = QStringLiteral("legacy.md");
  // No "template" key — simulates old-format JSON.

  SessionConfig::QuickNoteScheme scheme;
  scheme.fromJson(jobj);

  QCOMPARE(scheme.m_name, QStringLiteral("legacy"));
  QVERIFY(scheme.m_template.isEmpty());
}

void TestQuickNoteScheme::testOperatorEqualConsidersTemplate() {
  SessionConfig::QuickNoteScheme a;
  a.m_name = QStringLiteral("same");
  a.m_folderPath = QStringLiteral("/notes");
  a.m_noteName = QStringLiteral("note.md");
  a.m_template = QStringLiteral("daily.md");

  SessionConfig::QuickNoteScheme b = a;
  QVERIFY(a == b);

  b.m_template = QStringLiteral("weekly.md");
  QVERIFY(!(a == b));

  b.m_template.clear();
  QVERIFY(!(a == b));
}

void TestQuickNoteScheme::testSessionConfigSchemesTemplateRoundTrip() {
  SessionConfig sc1(&m_mockMgr);

  QJsonObject json;
  QJsonArray arr;

  QJsonObject scheme1;
  scheme1[QStringLiteral("name")] = QStringLiteral("journal");
  scheme1[QStringLiteral("folderPath")] = QStringLiteral("/journal");
  scheme1[QStringLiteral("noteName")] = QStringLiteral("j.md");
  scheme1[QStringLiteral("template")] = QStringLiteral("daily.md");
  arr.append(scheme1);

  QJsonObject scheme2;
  scheme2[QStringLiteral("name")] = QStringLiteral("plain");
  scheme2[QStringLiteral("folderPath")] = QStringLiteral("/plain");
  scheme2[QStringLiteral("noteName")] = QStringLiteral("p.md");
  // No template.
  arr.append(scheme2);

  json[QStringLiteral("quickNoteSchemes")] = arr;

  sc1.fromJson(json);

  const auto &schemes = sc1.getQuickNoteSchemes();
  QCOMPARE(schemes.size(), 2);
  QCOMPARE(schemes[0].m_template, QStringLiteral("daily.md"));
  QVERIFY(schemes[1].m_template.isEmpty());

  // Round-trip through toJson → fromJson.
  SessionConfig sc2(&m_mockMgr);
  sc2.fromJson(sc1.toJson());

  const auto &schemes2 = sc2.getQuickNoteSchemes();
  QCOMPARE(schemes2.size(), 2);
  QVERIFY(schemes2[0] == schemes[0]);
  QVERIFY(schemes2[1] == schemes[1]);
  QCOMPARE(schemes2[0].m_template, QStringLiteral("daily.md"));
  QVERIFY(schemes2[1].m_template.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestQuickNoteScheme)
#include "test_quicknotescheme.moc"
