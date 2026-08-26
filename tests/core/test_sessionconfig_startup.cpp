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
  void updateSessionConfig(const QJsonObject &) override {}
};

class TestSessionConfigStartup : public QObject {
  Q_OBJECT

private slots:
  void testDefaultIsFalse();
  void testAbsentKeyPreservesDefault();
  void testRoundTrip();

private:
  MockConfigMgr m_mockMgr;
};

void TestSessionConfigStartup::testDefaultIsFalse() {
  SessionConfig sc(&m_mockMgr);
  QCOMPARE(sc.getStartOnSystemStartup(), false);
}

void TestSessionConfigStartup::testAbsentKeyPreservesDefault() {
  SessionConfig sc(&m_mockMgr);
  QJsonObject json;
  json[QStringLiteral("core")] = QJsonObject();
  sc.fromJson(json);
  QCOMPARE(sc.getStartOnSystemStartup(), false);
}

void TestSessionConfigStartup::testRoundTrip() {
  for (bool value : {true, false}) {
    SessionConfig sc1(&m_mockMgr);
    sc1.setStartOnSystemStartup(value);
    QCOMPARE(sc1.getStartOnSystemStartup(), value);

    SessionConfig sc2(&m_mockMgr);
    sc2.fromJson(sc1.toJson());
    QCOMPARE(sc2.getStartOnSystemStartup(), value);
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSessionConfigStartup)
#include "test_sessionconfig_startup.moc"
