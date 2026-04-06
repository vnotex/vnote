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

class TestQuickAccessItem : public QObject {
  Q_OBJECT

private slots:
  void testItemRoundTrip();
  void testItemDefaultMode();
  void testItemReadMode();
  void testItemEditMode();
  void testItemOperatorEqual();
  void testSessionConfigItemsRoundTrip();
  void testSessionConfigEmptyQuickAccess();
  void testGetQuickAccessFilesCompat();
  void testSetQuickAccessFilesCompat();
  void testRemoveQuickAccessFileCompat();
  void testSetQuickAccessItemsTrimAndFilter();
  void testUnknownModeDefaultsToDefault();

private:
  MockConfigMgr m_mockMgr;
};

void TestQuickAccessItem::testItemRoundTrip() {
  SessionConfig::QuickAccessItem item;
  item.m_path = QStringLiteral("test.md");
  item.m_openMode = QuickAccessOpenMode::Read;

  QJsonObject json = item.toJson();

  SessionConfig::QuickAccessItem item2;
  item2.fromJson(json);

  QCOMPARE(item2.m_path, QStringLiteral("test.md"));
  QCOMPARE(item2.m_openMode, QuickAccessOpenMode::Read);
}

void TestQuickAccessItem::testItemDefaultMode() {
  SessionConfig::QuickAccessItem item;
  item.m_path = QStringLiteral("note.md");
  item.m_openMode = QuickAccessOpenMode::Default;

  QJsonObject json = item.toJson();
  QCOMPARE(json[QStringLiteral("openMode")].toString(), QStringLiteral("default"));
}

void TestQuickAccessItem::testItemReadMode() {
  SessionConfig::QuickAccessItem item;
  item.m_path = QStringLiteral("note.md");
  item.m_openMode = QuickAccessOpenMode::Read;

  QJsonObject json = item.toJson();
  QCOMPARE(json[QStringLiteral("openMode")].toString(), QStringLiteral("read"));
}

void TestQuickAccessItem::testItemEditMode() {
  SessionConfig::QuickAccessItem item;
  item.m_path = QStringLiteral("note.md");
  item.m_openMode = QuickAccessOpenMode::Edit;

  QJsonObject json = item.toJson();
  QCOMPARE(json[QStringLiteral("openMode")].toString(), QStringLiteral("edit"));
}

void TestQuickAccessItem::testItemOperatorEqual() {
  SessionConfig::QuickAccessItem a;
  a.m_path = QStringLiteral("same.md");
  a.m_openMode = QuickAccessOpenMode::Read;

  SessionConfig::QuickAccessItem b;
  b.m_path = QStringLiteral("same.md");
  b.m_openMode = QuickAccessOpenMode::Read;

  QVERIFY(a == b);

  // Change path → not equal
  b.m_path = QStringLiteral("different.md");
  QVERIFY(!(a == b));

  // Restore path, change mode → not equal
  b.m_path = QStringLiteral("same.md");
  b.m_openMode = QuickAccessOpenMode::Edit;
  QVERIFY(!(a == b));
}

void TestQuickAccessItem::testSessionConfigItemsRoundTrip() {
  SessionConfig sc1(&m_mockMgr);

  QJsonObject json;
  QJsonObject coreObj;
  QJsonArray arr;

  QJsonObject item1;
  item1[QStringLiteral("path")] = QStringLiteral("/a.md");
  item1[QStringLiteral("openMode")] = QStringLiteral("default");
  arr.append(item1);

  QJsonObject item2;
  item2[QStringLiteral("path")] = QStringLiteral("/b.md");
  item2[QStringLiteral("openMode")] = QStringLiteral("read");
  arr.append(item2);

  coreObj[QStringLiteral("quickAccess")] = arr;
  json[QStringLiteral("core")] = coreObj;

  sc1.fromJson(json);

  const auto &items = sc1.getQuickAccessItems();
  QCOMPARE(items.size(), 2);
  QCOMPARE(items[0].m_path, QStringLiteral("/a.md"));
  QCOMPARE(items[0].m_openMode, QuickAccessOpenMode::Default);
  QCOMPARE(items[1].m_path, QStringLiteral("/b.md"));
  QCOMPARE(items[1].m_openMode, QuickAccessOpenMode::Read);

  // Round-trip through toJson → fromJson
  QJsonObject json2 = sc1.toJson();
  SessionConfig sc2(&m_mockMgr);
  sc2.fromJson(json2);

  const auto &items2 = sc2.getQuickAccessItems();
  QCOMPARE(items2.size(), 2);
  QCOMPARE(items2[0].m_path, items[0].m_path);
  QCOMPARE(items2[0].m_openMode, items[0].m_openMode);
  QCOMPARE(items2[1].m_path, items[1].m_path);
  QCOMPARE(items2[1].m_openMode, items[1].m_openMode);
}

void TestQuickAccessItem::testSessionConfigEmptyQuickAccess() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  QCOMPARE(sc.getQuickAccessItems().size(), 0);
}

void TestQuickAccessItem::testGetQuickAccessFilesCompat() {
  SessionConfig sc(&m_mockMgr);

  QJsonObject json;
  QJsonObject coreObj;
  QJsonArray arr;

  QJsonObject item1;
  item1[QStringLiteral("path")] = QStringLiteral("x.md");
  item1[QStringLiteral("openMode")] = QStringLiteral("read");
  arr.append(item1);

  QJsonObject item2;
  item2[QStringLiteral("path")] = QStringLiteral("y.md");
  item2[QStringLiteral("openMode")] = QStringLiteral("edit");
  arr.append(item2);

  coreObj[QStringLiteral("quickAccess")] = arr;
  json[QStringLiteral("core")] = coreObj;

  sc.fromJson(json);

  QStringList files = sc.getQuickAccessFiles();
  QCOMPARE(files.size(), 2);
  QCOMPARE(files[0], QStringLiteral("x.md"));
  QCOMPARE(files[1], QStringLiteral("y.md"));
}

void TestQuickAccessItem::testSetQuickAccessFilesCompat() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  sc.setQuickAccessFiles(QStringList{QStringLiteral("a.md"), QStringLiteral("b.md")});

  const auto &items = sc.getQuickAccessItems();
  QCOMPARE(items.size(), 2);
  QCOMPARE(items[0].m_path, QStringLiteral("a.md"));
  QCOMPARE(items[0].m_openMode, QuickAccessOpenMode::Default);
  QCOMPARE(items[1].m_path, QStringLiteral("b.md"));
  QCOMPARE(items[1].m_openMode, QuickAccessOpenMode::Default);
}

void TestQuickAccessItem::testRemoveQuickAccessFileCompat() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  sc.setQuickAccessFiles(
      QStringList{QStringLiteral("a.md"), QStringLiteral("b.md"), QStringLiteral("c.md")});
  QCOMPARE(sc.getQuickAccessItems().size(), 3);

  sc.removeQuickAccessFile(QStringLiteral("b.md"));

  const auto &items = sc.getQuickAccessItems();
  QCOMPARE(items.size(), 2);
  QCOMPARE(items[0].m_path, QStringLiteral("a.md"));
  QCOMPARE(items[1].m_path, QStringLiteral("c.md"));
}

void TestQuickAccessItem::testSetQuickAccessItemsTrimAndFilter() {
  SessionConfig sc(&m_mockMgr);
  sc.fromJson(QJsonObject());

  QVector<SessionConfig::QuickAccessItem> items;

  SessionConfig::QuickAccessItem whitespace;
  whitespace.m_path = QStringLiteral("  ");
  whitespace.m_openMode = QuickAccessOpenMode::Read;
  items << whitespace;

  SessionConfig::QuickAccessItem empty;
  empty.m_path = QStringLiteral("");
  empty.m_openMode = QuickAccessOpenMode::Edit;
  items << empty;

  SessionConfig::QuickAccessItem valid;
  valid.m_path = QStringLiteral(" c.md ");
  valid.m_openMode = QuickAccessOpenMode::Read;
  items << valid;

  sc.setQuickAccessItems(items);

  const auto &result = sc.getQuickAccessItems();
  QCOMPARE(result.size(), 1);
  QCOMPARE(result[0].m_path, QStringLiteral("c.md"));
  QCOMPARE(result[0].m_openMode, QuickAccessOpenMode::Read);
}

void TestQuickAccessItem::testUnknownModeDefaultsToDefault() {
  QJsonObject jobj;
  jobj[QStringLiteral("path")] = QStringLiteral("test.md");
  jobj[QStringLiteral("openMode")] = QStringLiteral("bogus");

  SessionConfig::QuickAccessItem item;
  item.fromJson(jobj);

  QCOMPARE(item.m_path, QStringLiteral("test.md"));
  QCOMPARE(item.m_openMode, QuickAccessOpenMode::Default);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestQuickAccessItem)
#include "test_quickaccessitem.moc"
