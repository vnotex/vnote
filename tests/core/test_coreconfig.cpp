#include <QJsonObject>
#include <QtTest>

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
  void testSkippedUpdateVersionDefaultsEmptyAndRoundTrips();
  void testLastUpdateCheckTimeStoredAsDecimalString();
  void testLastUpdateCheckTimeSurvives32BitRange();
  void testLastUpdateCheckTimeBackwardCompatibleParsing();
  void testUpdateCheckDue();
  void testUpdateSourceDefaultsToGitee();
  void testUpdateSourceIsAlwaysPersistedSoExistingConfigsKeepTheirs();
  void testUpdateSourceNormalizesUnknownAndCaseVariants();
  void testUpdateSourceRoundTrips();
  void testAppNameDefaultAndNormalization();
  void testAppNameSetterAndRoundTrip();

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

void TestCoreConfig::testSkippedUpdateVersionDefaultsEmptyAndRoundTrips() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.fromJson(QJsonObject());
  QVERIFY(cfg.getSkippedUpdateVersion().isEmpty());

  cfg.setSkippedUpdateVersion(QStringLiteral("4.3.2"));
  QCOMPARE(cfg.getSkippedUpdateVersion(), QStringLiteral("4.3.2"));

  const auto out = cfg.toJson();
  QCOMPARE(out.value(QStringLiteral("skippedUpdateVersion")).toString(), QStringLiteral("4.3.2"));

  CoreConfig reloaded(&m_mockMgr, nullptr);
  reloaded.fromJson(out);
  QCOMPARE(reloaded.getSkippedUpdateVersion(), QStringLiteral("4.3.2"));
}

// The timestamp is persisted as a decimal STRING, not a JSON number, because
// IConfig::readInt (and QJsonValue::toInt) are 32-bit.
void TestCoreConfig::testLastUpdateCheckTimeStoredAsDecimalString() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.fromJson(QJsonObject());
  QCOMPARE(cfg.getLastUpdateCheckTime(), Q_INT64_C(0));

  const qint64 stamp = Q_INT64_C(1785337074532);
  cfg.setLastUpdateCheckTime(stamp);

  const auto out = cfg.toJson();
  const auto value = out.value(QStringLiteral("lastUpdateCheckTime"));
  QVERIFY2(value.isString(), "lastUpdateCheckTime must serialize as a string, not a number");
  QCOMPARE(value.toString(), QString::number(stamp));
}

void TestCoreConfig::testLastUpdateCheckTimeSurvives32BitRange() {
  // Well beyond INT32_MAX (2147483647): a 32-bit read would truncate this.
  const qint64 stamp = Q_INT64_C(1785337074532);
  QVERIFY(stamp > Q_INT64_C(2147483647));

  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.setLastUpdateCheckTime(stamp);

  CoreConfig reloaded(&m_mockMgr, nullptr);
  reloaded.fromJson(cfg.toJson());
  QCOMPARE(reloaded.getLastUpdateCheckTime(), stamp);
}

void TestCoreConfig::testLastUpdateCheckTimeBackwardCompatibleParsing() {
  const qint64 stamp = Q_INT64_C(1785337074532);

  // A bare JSON number (hand-edited config) is tolerated.
  {
    QJsonObject json;
    json[QStringLiteral("lastUpdateCheckTime")] = static_cast<double>(stamp);
    CoreConfig cfg(&m_mockMgr, nullptr);
    cfg.fromJson(json);
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
    CoreConfig cfg(&m_mockMgr, nullptr);
    cfg.fromJson(json);
    QCOMPARE(cfg.getLastUpdateCheckTime(), Q_INT64_C(0));
  }
}

void TestCoreConfig::testUpdateCheckDue() {
  const qint64 day = CoreConfig::c_updateCheckIntervalMs;
  const qint64 now = Q_INT64_C(1785337074532);

  CoreConfig cfg(&m_mockMgr, nullptr);

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

// The default lives in C++ (there is no bundled vnotex.json entry for any of
// the update keys), so an absent OR empty value must still land on gitee.
void TestCoreConfig::testUpdateSourceDefaultsToGitee() {
  {
    CoreConfig cfg(&m_mockMgr, nullptr);
    QCOMPARE(cfg.getUpdateSource(), QStringLiteral("gitee"));
    cfg.fromJson(QJsonObject());
    QCOMPARE(cfg.getUpdateSource(), QStringLiteral("gitee"));
  }
  {
    QJsonObject json;
    json[QStringLiteral("updateSource")] = QString();
    CoreConfig cfg(&m_mockMgr, nullptr);
    cfg.fromJson(json);
    QCOMPARE(cfg.getUpdateSource(), QStringLiteral("gitee"));
  }
}

// toJson() ALWAYS writes updateSource, so an existing installation keeps
// whatever it already had. This is the property that makes the Gitee default a
// fresh-install-only change: no migration runs, and a user who deliberately
// picked GitHub is never moved.
void TestCoreConfig::testUpdateSourceIsAlwaysPersistedSoExistingConfigsKeepTheirs() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  QJsonObject legacy;
  legacy[QStringLiteral("updateSource")] = QStringLiteral("github");
  cfg.fromJson(legacy);
  QCOMPARE(cfg.getUpdateSource(), QStringLiteral("github"));

  const auto out = cfg.toJson();
  QVERIFY2(out.contains(QStringLiteral("updateSource")),
           "updateSource was dropped, so an existing choice would silently reset");
  QCOMPARE(out.value(QStringLiteral("updateSource")).toString(), QStringLiteral("github"));

  CoreConfig reloaded(&m_mockMgr, nullptr);
  reloaded.fromJson(out);
  QCOMPARE(reloaded.getUpdateSource(), QStringLiteral("github"));
}

// A hand-edited or future-written value must never reach UpdateService as-is:
// anything that is not an explicit "github" degrades to gitee rather than to
// "no source at all".
void TestCoreConfig::testUpdateSourceNormalizesUnknownAndCaseVariants() {
  const QVector<QPair<QString, QString>> cases{
      {QStringLiteral("gitee"), QStringLiteral("gitee")},
      {QStringLiteral("GiTee"), QStringLiteral("gitee")},
      {QStringLiteral("  GITEE  "), QStringLiteral("gitee")},
      {QStringLiteral("GitHub"), QStringLiteral("github")},
      {QStringLiteral("  GITHUB  "), QStringLiteral("github")},
      {QStringLiteral("gitlab"), QStringLiteral("gitee")},
      {QStringLiteral("sourceforge"), QStringLiteral("gitee")},
  };

  for (const auto &c : cases) {
    QJsonObject json;
    json[QStringLiteral("updateSource")] = c.first;
    CoreConfig cfg(&m_mockMgr, nullptr);
    cfg.fromJson(json);
    QCOMPARE(cfg.getUpdateSource(), c.second);

    // The setter normalizes on the same rules as the loader.
    CoreConfig viaSetter(&m_mockMgr, nullptr);
    viaSetter.setUpdateSource(c.first);
    QCOMPARE(viaSetter.getUpdateSource(), c.second);
  }
}

void TestCoreConfig::testUpdateSourceRoundTrips() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.setUpdateSource(QStringLiteral("github"));

  const auto out = cfg.toJson();
  QCOMPARE(out.value(QStringLiteral("updateSource")).toString(), QStringLiteral("github"));

  CoreConfig reloaded(&m_mockMgr, nullptr);
  reloaded.fromJson(out);
  QCOMPARE(reloaded.getUpdateSource(), QStringLiteral("github"));
}

void TestCoreConfig::testAppNameDefaultAndNormalization() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  QCOMPARE(cfg.getAppName(), QStringLiteral("VNote"));

  cfg.fromJson(QJsonObject());
  QCOMPARE(cfg.getAppName(), QStringLiteral("VNote"));

  QJsonObject json;
  json[QStringLiteral("appName")] = QStringLiteral("  Notes  ");
  cfg.fromJson(json);
  QCOMPARE(cfg.getAppName(), QStringLiteral("Notes"));

  json[QStringLiteral("appName")] = QStringLiteral("   ");
  cfg.fromJson(json);
  QCOMPARE(cfg.getAppName(), QStringLiteral("VNote"));
}

void TestCoreConfig::testAppNameSetterAndRoundTrip() {
  CoreConfig cfg(&m_mockMgr, nullptr);
  cfg.setAppName(QStringLiteral("  Notes <Work> & Home  "));
  QCOMPARE(cfg.getAppName(), QStringLiteral("Notes <Work> & Home"));

  const auto out = cfg.toJson();
  QCOMPARE(out.value(QStringLiteral("appName")).toString(), QStringLiteral("Notes <Work> & Home"));

  CoreConfig reloaded(&m_mockMgr, nullptr);
  reloaded.fromJson(out);
  QCOMPARE(reloaded.getAppName(), QStringLiteral("Notes <Work> & Home"));

  reloaded.setAppName(QStringLiteral("\t"));
  QCOMPARE(reloaded.getAppName(), QStringLiteral("VNote"));
}

} // namespace tests
QTEST_GUILESS_MAIN(tests::TestCoreConfig)
#include "test_coreconfig.moc"
