#include <QtTest>

#include <QJsonObject>
#include <QSignalSpy>

#include <controllers/snippetcontroller.h>
#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>

#include <vxcore/vxcore.h>

namespace tests {

class TestSnippetController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testApplySnippetSignal();
  void testNewSnippetSignal();
  void testDeleteSnippetSignal();
  void testPropertiesSignal();
  void testCreateSnippet();
  void testDeleteSnippet();
  void testRenameSnippet();
  void testUpdateSnippet();
  void testGetSnippet();
  void testSetShortcut();
  void testRemoveShortcut();
  void testGetAvailableShortcuts();
  void testShortcutCleanupOnDelete();
  void testShortcutTransferOnRename();

private:
  static QJsonObject makeValidSnippet(const QString &p_content = QStringLiteral("hello @@world"));

  VxCoreContextHandle m_ctx = nullptr;
  vnotex::ServiceLocator *m_services = nullptr;
  vnotex::SnippetCoreService *m_snippetService = nullptr;
  vnotex::SnippetController *m_controller = nullptr;
};

QJsonObject TestSnippetController::makeValidSnippet(const QString &p_content) {
  QJsonObject json;
  json[QLatin1String("type")] = QStringLiteral("text");
  json[QLatin1String("description")] = QStringLiteral("test snippet");
  json[QLatin1String("content")] = p_content;
  json[QLatin1String("cursorMark")] = QStringLiteral("@@");
  json[QLatin1String("selectionMark")] = QStringLiteral("$$");
  json[QLatin1String("indentAsFirstLine")] = false;
  return json;
}

void TestSnippetController::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_ctx != nullptr);

  m_services = new vnotex::ServiceLocator();
  m_snippetService = new vnotex::SnippetCoreService(m_ctx, this);
  m_services->registerService<vnotex::SnippetCoreService>(m_snippetService);

  m_controller = new vnotex::SnippetController(*m_services, this);
  QVERIFY(m_controller != nullptr);
}

void TestSnippetController::cleanupTestCase() {
  m_controller = nullptr;
  m_snippetService = nullptr;

  delete m_services;
  m_services = nullptr;

  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestSnippetController::cleanup() {
  m_snippetService->deleteSnippet(QStringLiteral("test"));
  m_snippetService->deleteSnippet(QStringLiteral("old"));
  m_snippetService->deleteSnippet(QStringLiteral("new"));
  m_snippetService->deleteSnippet(QStringLiteral("snip"));

  m_controller->removeShortcut(QStringLiteral("test"));
  m_controller->removeShortcut(QStringLiteral("old"));
  m_controller->removeShortcut(QStringLiteral("new"));
  m_controller->removeShortcut(QStringLiteral("snip"));
}

void TestSnippetController::testApplySnippetSignal() {
  QSignalSpy spy(m_controller, &vnotex::SnippetController::applySnippetRequested);

  m_controller->requestApplySnippet(QStringLiteral("test"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test"));
}

void TestSnippetController::testNewSnippetSignal() {
  QSignalSpy spy(m_controller, &vnotex::SnippetController::newSnippetRequested);

  m_controller->requestNewSnippet();

  QCOMPARE(spy.count(), 1);
}

void TestSnippetController::testDeleteSnippetSignal() {
  QSignalSpy spy(m_controller, &vnotex::SnippetController::deleteSnippetRequested);

  m_controller->requestDeleteSnippet(QStringLiteral("test"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test"));
}

void TestSnippetController::testPropertiesSignal() {
  QSignalSpy spy(m_controller, &vnotex::SnippetController::propertiesRequested);

  m_controller->requestProperties(QStringLiteral("test"));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test"));
}

void TestSnippetController::testCreateSnippet() {
  QSignalSpy spy(m_controller, &vnotex::SnippetController::snippetListChanged);

  bool created = m_controller->createSnippet(QStringLiteral("test"), makeValidSnippet());

  QVERIFY(created);
  QCOMPARE(spy.count(), 1);
}

void TestSnippetController::testDeleteSnippet() {
  QVERIFY(m_controller->createSnippet(QStringLiteral("test"), makeValidSnippet()));
  QSignalSpy spy(m_controller, &vnotex::SnippetController::snippetListChanged);

  bool deleted = m_controller->deleteSnippet(QStringLiteral("test"));

  QVERIFY(deleted);
  QCOMPARE(spy.count(), 1);
}

void TestSnippetController::testRenameSnippet() {
  QVERIFY(m_controller->createSnippet(QStringLiteral("old"), makeValidSnippet()));
  QSignalSpy spy(m_controller, &vnotex::SnippetController::snippetListChanged);

  bool renamed = m_controller->renameSnippet(QStringLiteral("old"), QStringLiteral("new"));

  QVERIFY(renamed);
  QCOMPARE(spy.count(), 1);
}

void TestSnippetController::testUpdateSnippet() {
  QVERIFY(m_controller->createSnippet(QStringLiteral("test"), makeValidSnippet()));
  QSignalSpy spy(m_controller, &vnotex::SnippetController::snippetListChanged);

  bool updated = m_controller->updateSnippet(
      QStringLiteral("test"), makeValidSnippet(QStringLiteral("updated content")));

  QVERIFY(updated);
  QCOMPARE(spy.count(), 0);
}

void TestSnippetController::testGetSnippet() {
  QVERIFY(m_controller->createSnippet(QStringLiteral("test"), makeValidSnippet()));

  QJsonObject snippet = m_controller->getSnippet(QStringLiteral("test"));

  QVERIFY(!snippet.isEmpty());
}

void TestSnippetController::testSetShortcut() {
  m_controller->setShortcut(QStringLiteral("test"), 5);

  QCOMPARE(m_controller->getShortcutForSnippet(QStringLiteral("test")), 5);
}

void TestSnippetController::testRemoveShortcut() {
  m_controller->setShortcut(QStringLiteral("test"), 5);
  m_controller->removeShortcut(QStringLiteral("test"));

  QCOMPARE(m_controller->getShortcutForSnippet(QStringLiteral("test")), -1);
}

void TestSnippetController::testGetAvailableShortcuts() {
  m_controller->setShortcut(QStringLiteral("test"), 5);
  m_controller->setShortcut(QStringLiteral("old"), 10);

  const QList<int> available = m_controller->getAvailableShortcuts();
  QCOMPARE(available.size(), 98);
  QVERIFY(!available.contains(5));
  QVERIFY(!available.contains(10));
  QVERIFY(available.contains(0));
  QVERIFY(available.contains(99));
}

void TestSnippetController::testShortcutCleanupOnDelete() {
  QVERIFY(m_controller->createSnippet(QStringLiteral("snip"), makeValidSnippet()));
  m_controller->setShortcut(QStringLiteral("snip"), 5);

  QVERIFY(m_controller->deleteSnippet(QStringLiteral("snip")));

  const QList<int> available = m_controller->getAvailableShortcuts();
  QVERIFY(available.contains(5));
}

void TestSnippetController::testShortcutTransferOnRename() {
  QVERIFY(m_controller->createSnippet(QStringLiteral("old"), makeValidSnippet()));
  m_controller->setShortcut(QStringLiteral("old"), 10);

  QVERIFY(m_controller->renameSnippet(QStringLiteral("old"), QStringLiteral("new")));

  QCOMPARE(m_controller->getShortcutForSnippet(QStringLiteral("new")), 10);
  QCOMPARE(m_controller->getShortcutForSnippet(QStringLiteral("old")), -1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSnippetController)
#include "test_snippetcontroller.moc"
