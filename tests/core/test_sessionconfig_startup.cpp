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
  void testLastUpdateCheckTimeSurvives32BitRange();
  void testLastUpdateCheckTimeBackwardCompatibleParsing();
  void testUpdateCheckDue();

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

void TestSessionConfigStartup::testLastUpdateCheckTimeSurvives32BitRange() {
  // Well beyond INT32_MAX (2147483647): a 32-bit read would truncate this.
  const qint64 stamp = Q_INT64_C(1785337074532);

  SessionConfig cfg(&m_mockMgr);
  cfg.setLastUpdateCheckTime(stamp);

  SessionConfig reloaded(&m_mockMgr);
  const auto json = cfg.toJson();
  QCOMPARE(
      json.value(QStringLiteral("core")).toObject().value(QStringLiteral("lastUpdateCheckTime")),
      QJsonValue(QString::number(stamp)));
  reloaded.fromJson(json);
  QCOMPARE(reloaded.getLastUpdateCheckTime(), stamp);
}

void TestSessionConfigStartup::testLastUpdateCheckTimeBackwardCompatibleParsing() {
  const qint64 stamp = Q_INT64_C(1785337074532);

  // A bare JSON number (hand-edited config) is tolerated.
  {
    QJsonObject json;
    json[QStringLiteral("lastUpdateCheckTime")] = static_cast<double>(stamp);
    SessionConfig cfg(&m_mockMgr);
    cfg.fromJson(QJsonObject{{QStringLiteral("core"), json}});
    QCOMPARE(cfg.getLastUpdateCheckTime(), stamp);
  }

  // Garbage and negative values degrade to "never checked" rather than
  // poisoning the throttle.
  const QVector<QJsonValue> junk{QJsonValue(QStringLiteral("not-a-number")), QJsonValue(true),
                                 QJsonValue(QStringLiteral("-1")), QJsonValue(-5.0),
                                 QJsonValue(QJsonValue::Null)};
  for (const auto &v : junk) {
    QJsonObject json;
    json[QStringLiteral("lastUpdateCheckTime")] = v;
    SessionConfig cfg(&m_mockMgr);
    cfg.fromJson(QJsonObject{{QStringLiteral("core"), json}});
    QCOMPARE(cfg.getLastUpdateCheckTime(), Q_INT64_C(0));
  }
}

void TestSessionConfigStartup::testUpdateCheckDue() {
  const qint64 day = SessionConfig::c_updateCheckIntervalMs;
  const qint64 now = Q_INT64_C(1785337074532);

  SessionConfig cfg(&m_mockMgr);

  // Never checked -> due.
  QVERIFY(cfg.isUpdateCheckDue(now, day));

  // Checked just now -> not due.
  cfg.setLastUpdateCheckTime(now);
  QVERIFY(!cfg.isUpdateCheckDue(now, day));

  // One ms short of the interval -> not due.
  cfg.setLastUpdateCheckTime(now - day + 1);
  QVERIFY(!cfg.isUpdateCheckDue(now, day));

  // Exactly the interval -> due.
  cfg.setLastUpdateCheckTime(now - day);
  QVERIFY(cfg.isUpdateCheckDue(now, day));

  // Future-dated (clock moved backwards) -> treated as stale, so the throttle
  // cannot wedge the user out of update checks.
  cfg.setLastUpdateCheckTime(now + day);
  QVERIFY(cfg.isUpdateCheckDue(now, day));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSessionConfigStartup)
#include "test_sessionconfig_startup.moc"
