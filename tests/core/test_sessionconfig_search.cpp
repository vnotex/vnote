#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include <core/sessionconfig.h>
#include <core/iconfigmgr.h>

using namespace vnotex;

namespace tests {

// Minimal mock that prevents crash when update() is called.
class MockConfigMgr : public IConfigMgr {
public:
  void updateMainConfig(const QJsonObject &) override {}
  void updateSessionConfig(const QJsonObject &) override {} // no-op
};

class TestSessionConfigSearch : public QObject {
  Q_OBJECT

private slots:
  void testRoundTrip();
  void testEmptyRoundTrip();
  void testCapAt20();
  void testDedup();
  void testTrim();
  void testEmptyStringRejection();

private:
  MockConfigMgr m_mockMgr;
};

void TestSessionConfigSearch::testRoundTrip() {
  SessionConfig sc1(&m_mockMgr);
  // Build a JSON with searchHistory
  QJsonObject json;
  QJsonArray arr;
  arr.append(QStringLiteral("foo"));
  arr.append(QStringLiteral("bar"));
  arr.append(QStringLiteral("baz"));
  json[QStringLiteral("searchHistory")] = arr;

  sc1.fromJson(json);
  QCOMPARE(sc1.getSearchHistory().size(), 3);
  QCOMPARE(sc1.getSearchHistory().at(0), QStringLiteral("foo"));
  QCOMPARE(sc1.getSearchHistory().at(1), QStringLiteral("bar"));
  QCOMPARE(sc1.getSearchHistory().at(2), QStringLiteral("baz"));

  // Round-trip through toJson
  QJsonObject json2 = sc1.toJson();
  SessionConfig sc2(&m_mockMgr);
  sc2.fromJson(json2);
  QCOMPARE(sc2.getSearchHistory(), sc1.getSearchHistory());
}

void TestSessionConfigSearch::testEmptyRoundTrip() {
  SessionConfig sc1(&m_mockMgr);
  sc1.fromJson(QJsonObject()); // empty JSON

  QCOMPARE(sc1.getSearchHistory().size(), 0);

  QJsonObject json = sc1.toJson();
  SessionConfig sc2(&m_mockMgr);
  sc2.fromJson(json);
  QCOMPARE(sc2.getSearchHistory().size(), 0);
}

void TestSessionConfigSearch::testCapAt20() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  // Add 25 entries
  for (int i = 0; i < 25; ++i) {
    sc.addSearchHistory(QString("keyword_%1").arg(i));
  }

  QCOMPARE(sc.getSearchHistory().size(), 20);
  // Most recent should be first
  QCOMPARE(sc.getSearchHistory().at(0), QStringLiteral("keyword_24"));
}

void TestSessionConfigSearch::testDedup() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  sc.addSearchHistory(QStringLiteral("alpha"));
  sc.addSearchHistory(QStringLiteral("beta"));
  sc.addSearchHistory(QStringLiteral("gamma"));
  // Now: gamma, beta, alpha

  // Re-add "alpha" — should move to front
  sc.addSearchHistory(QStringLiteral("alpha"));
  QCOMPARE(sc.getSearchHistory().size(), 3);
  QCOMPARE(sc.getSearchHistory().at(0), QStringLiteral("alpha"));
  QCOMPARE(sc.getSearchHistory().at(1), QStringLiteral("gamma"));
  QCOMPARE(sc.getSearchHistory().at(2), QStringLiteral("beta"));
}

void TestSessionConfigSearch::testTrim() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  sc.addSearchHistory(QStringLiteral("  hello  "));
  QCOMPARE(sc.getSearchHistory().size(), 1);
  QCOMPARE(sc.getSearchHistory().at(0), QStringLiteral("hello"));
}

void TestSessionConfigSearch::testEmptyStringRejection() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  sc.addSearchHistory(QStringLiteral(""));
  QCOMPARE(sc.getSearchHistory().size(), 0);

  sc.addSearchHistory(QStringLiteral("   "));
  QCOMPARE(sc.getSearchHistory().size(), 0);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSessionConfigSearch)
#include "test_sessionconfig_search.moc"
