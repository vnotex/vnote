#include <QtTest>

#include <core/services/snippetcoreservice.h>

#include <vxcore/vxcore.h>

namespace tests {

class TestSnippetService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testGetSnippetFolderPath();
  void testListSnippetsContainsBuiltins();
  void testGetBuiltinSnippetFields();
  void testGetNonexistentSnippet();
  void testCreateSnippetSuccessAndGet();
  void testCreateSnippetBuiltInCollisionFails();
  void testCreateSnippetEmptyNameFails();
  void testUpdateSnippetSuccess();
  void testUpdateBuiltInSnippetFails();
  void testRenameSnippetSuccess();
  void testRenameBuiltInSnippetFails();
  void testDeleteSnippetSuccess();
  void testDeleteBuiltInSnippetFails();
  void testApplySnippetDate();
  void testApplySnippetNonexistent();

private:
  static QJsonObject makeValidSnippet(const QString &p_content = QStringLiteral("hello @@world"));

  VxCoreContextHandle m_context = nullptr;
  vnotex::SnippetCoreService *m_service = nullptr;
};

void TestSnippetService::initTestCase() {
  vxcore_set_test_mode(1);

  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_service = new vnotex::SnippetCoreService(m_context, this);
  QVERIFY(m_service != nullptr);
}

void TestSnippetService::cleanupTestCase() {
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSnippetService::cleanup() {
  m_service->deleteSnippet(QStringLiteral("test_snip"));
  m_service->deleteSnippet(QStringLiteral("test_snip_renamed"));
  m_service->deleteSnippet(QStringLiteral("test_snip_update"));
}

QJsonObject TestSnippetService::makeValidSnippet(const QString &p_content) {
  QJsonObject json;
  json[QLatin1String("type")] = QStringLiteral("text");
  json[QLatin1String("description")] = QStringLiteral("test snippet");
  json[QLatin1String("content")] = p_content;
  json[QLatin1String("cursorMark")] = QStringLiteral("@@");
  json[QLatin1String("selectionMark")] = QStringLiteral("$$");
  json[QLatin1String("indentAsFirstLine")] = false;
  return json;
}

void TestSnippetService::testGetSnippetFolderPath() {
  QString path = m_service->getSnippetFolderPath();
  QVERIFY(!path.isEmpty());
}

void TestSnippetService::testListSnippetsContainsBuiltins() {
  QJsonArray snippets = m_service->listSnippets();
  QVERIFY(!snippets.isEmpty());

  bool hasDate = false;
  bool hasTime = false;
  bool hasDa = false;
  bool hasDatetime = false;
  for (const auto &snippetVal : snippets) {
    const QString name = snippetVal.toObject().value(QLatin1String("name")).toString();
    if (name == QStringLiteral("date")) {
      hasDate = true;
    } else if (name == QStringLiteral("time")) {
      hasTime = true;
    } else if (name == QStringLiteral("da")) {
      hasDa = true;
    } else if (name == QStringLiteral("datetime")) {
      hasDatetime = true;
    }
  }

  QVERIFY(hasDate);
  QVERIFY(hasTime);
  QVERIFY(hasDa);
  QVERIFY(hasDatetime);
}

void TestSnippetService::testGetBuiltinSnippetFields() {
  QJsonObject snippet = m_service->getSnippet(QStringLiteral("date"));
  QVERIFY(!snippet.isEmpty());

  QVERIFY(snippet.contains(QLatin1String("name")));
  QVERIFY(snippet.contains(QLatin1String("type")));
  QVERIFY(snippet.contains(QLatin1String("description")));
  QVERIFY(snippet.contains(QLatin1String("content")));
  QVERIFY(snippet.contains(QLatin1String("cursorMark")));
  QVERIFY(snippet.contains(QLatin1String("selectionMark")));
  QVERIFY(snippet.contains(QLatin1String("indentAsFirstLine")));
  QVERIFY(snippet.contains(QLatin1String("isBuiltin")));
}

void TestSnippetService::testGetNonexistentSnippet() {
  QJsonObject snippet = m_service->getSnippet(QStringLiteral("nonexistent"));
  QVERIFY(snippet.isEmpty());
}

void TestSnippetService::testCreateSnippetSuccessAndGet() {
  bool created = m_service->createSnippet(QStringLiteral("test_snip"), makeValidSnippet());
  QVERIFY(created);

  QJsonObject snippet = m_service->getSnippet(QStringLiteral("test_snip"));
  QVERIFY(!snippet.isEmpty());
  QCOMPARE(snippet.value(QLatin1String("name")).toString(), QStringLiteral("test_snip"));
}

void TestSnippetService::testCreateSnippetBuiltInCollisionFails() {
  bool created = m_service->createSnippet(QStringLiteral("date"), makeValidSnippet());
  QVERIFY(!created);
}

void TestSnippetService::testCreateSnippetEmptyNameFails() {
  bool created = m_service->createSnippet(QString(), makeValidSnippet());
  QVERIFY(!created);
}

void TestSnippetService::testUpdateSnippetSuccess() {
  QVERIFY(m_service->createSnippet(QStringLiteral("test_snip_update"), makeValidSnippet()));

  const QString updatedContent = QStringLiteral("updated content");
  bool updated = m_service->updateSnippet(
      QStringLiteral("test_snip_update"), makeValidSnippet(updatedContent));
  QVERIFY(updated);

  QJsonObject snippet = m_service->getSnippet(QStringLiteral("test_snip_update"));
  QCOMPARE(snippet.value(QLatin1String("content")).toString(), updatedContent);
}

void TestSnippetService::testUpdateBuiltInSnippetFails() {
  bool updated = m_service->updateSnippet(QStringLiteral("date"), makeValidSnippet());
  QVERIFY(!updated);
}

void TestSnippetService::testRenameSnippetSuccess() {
  QVERIFY(m_service->createSnippet(QStringLiteral("test_snip"), makeValidSnippet()));

  bool renamed =
      m_service->renameSnippet(QStringLiteral("test_snip"), QStringLiteral("test_snip_renamed"));
  QVERIFY(renamed);

  QVERIFY(m_service->getSnippet(QStringLiteral("test_snip")).isEmpty());
  QVERIFY(!m_service->getSnippet(QStringLiteral("test_snip_renamed")).isEmpty());
}

void TestSnippetService::testRenameBuiltInSnippetFails() {
  bool renamed = m_service->renameSnippet(QStringLiteral("date"), QStringLiteral("foo"));
  QVERIFY(!renamed);
}

void TestSnippetService::testDeleteSnippetSuccess() {
  QVERIFY(m_service->createSnippet(QStringLiteral("test_snip_renamed"), makeValidSnippet()));

  bool deleted = m_service->deleteSnippet(QStringLiteral("test_snip_renamed"));
  QVERIFY(deleted);
  QVERIFY(m_service->getSnippet(QStringLiteral("test_snip_renamed")).isEmpty());
}

void TestSnippetService::testDeleteBuiltInSnippetFails() {
  bool deleted = m_service->deleteSnippet(QStringLiteral("date"));
  QVERIFY(!deleted);
}

void TestSnippetService::testApplySnippetDate() {
  QJsonObject result = m_service->applySnippet(
      QStringLiteral("date"), QString(), QString(), QJsonObject());
  QVERIFY(result.contains(QLatin1String("text")));
  QVERIFY(result.contains(QLatin1String("cursorOffset")));
  QVERIFY(!result.value(QLatin1String("text")).toString().isEmpty());
}

void TestSnippetService::testApplySnippetNonexistent() {
  QJsonObject result = m_service->applySnippet(
      QStringLiteral("nonexistent"), QString(), QString(), QJsonObject());
  QCOMPARE(result.value(QLatin1String("text")).toString(), QString());
  QCOMPARE(result.value(QLatin1String("cursorOffset")).toInt(), -1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSnippetService)
#include "test_snippetservice.moc"
