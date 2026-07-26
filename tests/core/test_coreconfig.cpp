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
  void testConsoleDockShortcutDefaultsToEmpty();
  void testConsoleDockShortcutRoundTripByName();
  void testLegacyCloseTabShortcutMigratesToCloseFocus();
  void testCloseFocusShortcutWinsOverLegacyCloseTab();

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

// The ConsoleDock shortcut ships blank so nothing changes out of the box until a
// user assigns a key.
void TestCoreConfig::testConsoleDockShortcutDefaultsToEmpty() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  QVERIFY(cfg.getShortcut(CoreConfig::Shortcut::ConsoleDock).isEmpty());

  // An older config predating the key must load blank without disturbing the
  // shortcuts that ARE present. Deliberately NOT the initDefaults() value for
  // LocationListDock, so this can only pass if the value came from the JSON.
  const auto locationKeys = QStringLiteral("Ctrl+G, Z");
  QJsonObject shortcuts;
  shortcuts[QStringLiteral("LocationListDock")] = locationKeys;
  QJsonObject json;
  json[QStringLiteral("shortcuts")] = shortcuts;

  cfg.fromJson(json);
  QVERIFY(cfg.getShortcut(CoreConfig::Shortcut::ConsoleDock).isEmpty());
  QCOMPARE(cfg.getShortcut(CoreConfig::Shortcut::LocationListDock), locationKeys);
}

// Shortcuts serialize by enum name, not index, so the new entry must survive a
// toJson()/fromJson() round trip under the key "ConsoleDock" without shifting
// the shortcuts declared after it in the enum.
void TestCoreConfig::testConsoleDockShortcutRoundTripByName() {
  // None of these match an initDefaults() value, so every assertion below fails
  // if the JSON is ignored and the defaults survive.
  const auto consoleKeys = QStringLiteral("Ctrl+G, Shift+O");
  const auto locationKeys = QStringLiteral("Ctrl+G, Z");
  // Declared after ConsoleDock in the enum; would be corrupted by index-based
  // serialization.
  const auto searchKeys = QStringLiteral("Ctrl+Alt+Y");

  QJsonObject shortcuts;
  shortcuts[QStringLiteral("ConsoleDock")] = consoleKeys;
  shortcuts[QStringLiteral("LocationListDock")] = locationKeys;
  shortcuts[QStringLiteral("Search")] = searchKeys;
  QJsonObject json;
  json[QStringLiteral("shortcuts")] = shortcuts;

  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.fromJson(json);
  QCOMPARE(cfg.getShortcut(CoreConfig::Shortcut::ConsoleDock), consoleKeys);
  QCOMPARE(cfg.getShortcut(CoreConfig::Shortcut::LocationListDock), locationKeys);
  QCOMPARE(cfg.getShortcut(CoreConfig::Shortcut::Search), searchKeys);

  const auto out = cfg.toJson().value(QStringLiteral("shortcuts")).toObject();
  QCOMPARE(out.value(QStringLiteral("ConsoleDock")).toString(), consoleKeys);
  QCOMPARE(out.value(QStringLiteral("LocationListDock")).toString(), locationKeys);
  QCOMPARE(out.value(QStringLiteral("Search")).toString(), searchKeys);
}

// The CloseTab shortcut was renamed to CloseFocus (it now also hides a focused
// dock). Shortcuts serialize by enum name, so a config written before the rename
// carries the legacy key; the user's customized binding must survive.
void TestCoreConfig::testLegacyCloseTabShortcutMigratesToCloseFocus() {
  // Deliberately NOT the initDefaults() value, so this can only pass if the
  // value came from the legacy JSON key.
  const auto legacyKeys = QStringLiteral("Ctrl+G, Shift+X");

  QJsonObject shortcuts;
  shortcuts[QStringLiteral("CloseTab")] = legacyKeys;
  QJsonObject json;
  json[QStringLiteral("shortcuts")] = shortcuts;

  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.fromJson(json);
  QCOMPARE(cfg.getShortcut(CoreConfig::Shortcut::CloseFocus), legacyKeys);

  // Saving emits only the new key.
  const auto out = cfg.toJson().value(QStringLiteral("shortcuts")).toObject();
  QCOMPARE(out.value(QStringLiteral("CloseFocus")).toString(), legacyKeys);
  QVERIFY(!out.contains(QStringLiteral("CloseTab")));
}

// Once the config has been re-saved, both keys may coexist in a hand-edited
// file. The new key is authoritative.
void TestCoreConfig::testCloseFocusShortcutWinsOverLegacyCloseTab() {
  const auto legacyKeys = QStringLiteral("Ctrl+G, Shift+X");
  const auto currentKeys = QStringLiteral("Ctrl+G, Alt+X");

  QJsonObject shortcuts;
  shortcuts[QStringLiteral("CloseTab")] = legacyKeys;
  shortcuts[QStringLiteral("CloseFocus")] = currentKeys;
  QJsonObject json;
  json[QStringLiteral("shortcuts")] = shortcuts;

  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.fromJson(json);
  QCOMPARE(cfg.getShortcut(CoreConfig::Shortcut::CloseFocus), currentKeys);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCoreConfig)
#include "test_coreconfig.moc"
