#include <QtTest>

#include <core/iconfigmgr.h>
#include <core/sessionconfig.h>

using namespace vnotex;

namespace tests {

// Stub IConfigMgr that does nothing on update calls.
class StubConfigMgr : public IConfigMgr {
public:
  void updateMainConfig(const QJsonObject &) override {}
  void updateSessionConfig(const QJsonObject &) override {}
};

class TestUnitedEntryHistory : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void test_addSingleItem();
  void test_dedup();
  void test_ordering();
  void test_cap();
  void test_emptyWhitespaceRejected();
  void test_setReplaces();

private:
  StubConfigMgr m_stubMgr;
  SessionConfig *m_config = nullptr;
};

void TestUnitedEntryHistory::initTestCase() {
  m_config = new SessionConfig(&m_stubMgr);
}

void TestUnitedEntryHistory::test_addSingleItem() {
  m_config->setUnitedEntryHistory({});
  m_config->addUnitedEntryHistory("find hello");
  QCOMPARE(m_config->getUnitedEntryHistory().size(), 1);
  QCOMPARE(m_config->getUnitedEntryHistory().first(), QStringLiteral("find hello"));
}

void TestUnitedEntryHistory::test_dedup() {
  m_config->setUnitedEntryHistory({});
  m_config->addUnitedEntryHistory("find hello");
  m_config->addUnitedEntryHistory("find hello");
  QCOMPARE(m_config->getUnitedEntryHistory().size(), 1);
  QCOMPARE(m_config->getUnitedEntryHistory().first(), QStringLiteral("find hello"));
}

void TestUnitedEntryHistory::test_ordering() {
  m_config->setUnitedEntryHistory({});
  m_config->addUnitedEntryHistory("a");
  m_config->addUnitedEntryHistory("b");
  m_config->addUnitedEntryHistory("c");
  QStringList expected = {"c", "b", "a"};
  QCOMPARE(m_config->getUnitedEntryHistory(), expected);
}

void TestUnitedEntryHistory::test_cap() {
  m_config->setUnitedEntryHistory({});
  for (int i = 0; i < 25; ++i) {
    m_config->addUnitedEntryHistory(QStringLiteral("item_%1").arg(i));
  }
  QCOMPARE(m_config->getUnitedEntryHistory().size(), 20);
  // Most recent (item_24) should be first.
  QCOMPARE(m_config->getUnitedEntryHistory().first(), QStringLiteral("item_24"));
  // Oldest retained (item_5) should be last.
  QCOMPARE(m_config->getUnitedEntryHistory().last(), QStringLiteral("item_5"));
  // Items 0-4 should be dropped.
  QVERIFY(!m_config->getUnitedEntryHistory().contains(QStringLiteral("item_0")));
  QVERIFY(!m_config->getUnitedEntryHistory().contains(QStringLiteral("item_4")));
}

void TestUnitedEntryHistory::test_emptyWhitespaceRejected() {
  m_config->setUnitedEntryHistory({});
  m_config->addUnitedEntryHistory("");
  QVERIFY(m_config->getUnitedEntryHistory().isEmpty());
  m_config->addUnitedEntryHistory("   ");
  QVERIFY(m_config->getUnitedEntryHistory().isEmpty());
  m_config->addUnitedEntryHistory("\t");
  QVERIFY(m_config->getUnitedEntryHistory().isEmpty());
}

void TestUnitedEntryHistory::test_setReplaces() {
  m_config->setUnitedEntryHistory({"a", "b", "c"});
  m_config->setUnitedEntryHistory({"x", "y"});
  QStringList expected = {"x", "y"};
  QCOMPARE(m_config->getUnitedEntryHistory(), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestUnitedEntryHistory)
#include "test_unitedentry_history.moc"
