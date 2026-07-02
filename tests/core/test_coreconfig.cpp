#include <QtTest>
#include <QJsonObject>

#include <core/coreconfig.h>
#include <core/iconfigmgr.h>

using namespace vnotex;

namespace tests {

// Minimal mock that prevents crash when update() is called.
class MockConfigMgr : public IConfigMgr {
public:
  void updateMainConfig(const QJsonObject &) override {}
  void updateSessionConfig(const QJsonObject &) override {}
};

class TestCoreConfig : public QObject {
  Q_OBJECT

private slots:
  void testDefaultWhenAbsent();
  void testRoundTrip();
  void testClampAboveMax();
  void testZeroAndNegativeFallBackToDefault();
  void testSetterClampsToBounds();

private:
  MockConfigMgr m_mockMgr;
};

void TestCoreConfig::testDefaultWhenAbsent() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.fromJson(QJsonObject()); // No searchMaxResults key.
  QCOMPARE(cfg.getSearchMaxResults(), 1000);
}

void TestCoreConfig::testRoundTrip() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  QJsonObject json;
  json[QStringLiteral("searchMaxResults")] = 250;
  cfg.fromJson(json);
  QCOMPARE(cfg.getSearchMaxResults(), 250);

  const QJsonObject out = cfg.toJson();
  QCOMPARE(out.value(QStringLiteral("searchMaxResults")).toInt(), 250);
}

void TestCoreConfig::testClampAboveMax() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  QJsonObject json;
  json[QStringLiteral("searchMaxResults")] = 999999;
  cfg.fromJson(json);
  QCOMPARE(cfg.getSearchMaxResults(), 100000);
}

void TestCoreConfig::testZeroAndNegativeFallBackToDefault() {
  {
    CoreConfig cfg(&m_mockMgr, nullptr);
    QJsonObject json;
    json[QStringLiteral("searchMaxResults")] = 0;
    cfg.fromJson(json);
    QCOMPARE(cfg.getSearchMaxResults(), 1000);
  }
  {
    CoreConfig cfg(&m_mockMgr, nullptr);
    QJsonObject json;
    json[QStringLiteral("searchMaxResults")] = -5;
    cfg.fromJson(json);
    QCOMPARE(cfg.getSearchMaxResults(), 1000);
  }
}

// The setter clamps to [1, 100000]. This intentionally diverges from fromJson(),
// where a persisted value <= 0 (or a missing key) falls back to the default 1000,
// whereas the setter clamps a below-min value to the minimum (1).
void TestCoreConfig::testSetterClampsToBounds() {
  CoreConfig cfg(&m_mockMgr, nullptr);

  cfg.setSearchMaxResults(0);
  QCOMPARE(cfg.getSearchMaxResults(), 1);

  cfg.setSearchMaxResults(-5);
  QCOMPARE(cfg.getSearchMaxResults(), 1);

  cfg.setSearchMaxResults(999999);
  QCOMPARE(cfg.getSearchMaxResults(), 100000);

  cfg.setSearchMaxResults(250);
  QCOMPARE(cfg.getSearchMaxResults(), 250);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCoreConfig)
#include "test_coreconfig.moc"
