#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <core/services/snippetcoreservice.h>
#include <models/snippetlistmodel.h>

#include <vxcore/vxcore.h>

namespace tests {

class TestSnippetModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testEmptyModel();
  void testRefreshPopulatesModel();
  void testDisplayRoleShowsName();
  void testDisplayRoleWithShortcut();
  void testNameRole();
  void testDescriptionRole();
  void testTypeRole();
  void testIsBuiltinRole();
  void testShortcutRole();
  void testInvalidIndexReturnsEmpty();
  void testSetShowBuiltInFalseFilters();
  void testSetShowBuiltInTrueShowsAll();
  void testSnippetAt();

private:
  static QJsonObject makeValidSnippet(const QString &p_description);
  static QString firstSnippetNameByBuiltin(const QJsonArray &p_list, bool p_isBuiltin);

  VxCoreContextHandle m_ctx = nullptr;
  vnotex::SnippetCoreService *m_snippetService = nullptr;
  vnotex::SnippetListModel *m_model = nullptr;
};

QJsonObject TestSnippetModel::makeValidSnippet(const QString &p_description) {
  QJsonObject json;
  json[QLatin1String("type")] = QStringLiteral("text");
  json[QLatin1String("description")] = p_description;
  json[QLatin1String("content")] = QStringLiteral("hello @@world");
  json[QLatin1String("cursorMark")] = QStringLiteral("@@");
  json[QLatin1String("selectionMark")] = QStringLiteral("$$");
  json[QLatin1String("indentAsFirstLine")] = false;
  return json;
}

QString TestSnippetModel::firstSnippetNameByBuiltin(const QJsonArray &p_list, bool p_isBuiltin) {
  for (const auto &val : p_list) {
    const QJsonObject obj = val.toObject();
    if (obj.value(QLatin1String("isBuiltin")).toBool() == p_isBuiltin) {
      return obj.value(QLatin1String("name")).toString();
    }
  }

  return QString();
}

void TestSnippetModel::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_ctx != nullptr);

  m_snippetService = new vnotex::SnippetCoreService(m_ctx, this);
  QVERIFY(m_snippetService != nullptr);

  m_model = new vnotex::SnippetListModel(m_snippetService, this);
  QVERIFY(m_model != nullptr);

  m_snippetService->deleteSnippet(QStringLiteral("user_model_alpha"));
  m_snippetService->deleteSnippet(QStringLiteral("user_model_beta"));

  QVERIFY(m_snippetService->createSnippet(QStringLiteral("user_model_alpha"),
                                          makeValidSnippet(QStringLiteral("alpha desc"))));
  QVERIFY(m_snippetService->createSnippet(QStringLiteral("user_model_beta"),
                                          makeValidSnippet(QStringLiteral("beta desc"))));
}

void TestSnippetModel::cleanupTestCase() {
  m_snippetService = nullptr;
  m_model = nullptr;

  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestSnippetModel::cleanup() {
  m_snippetService->deleteSnippet(QStringLiteral("user_model_alpha"));
  m_snippetService->deleteSnippet(QStringLiteral("user_model_beta"));

  if (m_snippetService->getSnippet(QStringLiteral("user_model_alpha")).isEmpty()) {
    m_snippetService->createSnippet(QStringLiteral("user_model_alpha"),
                                    makeValidSnippet(QStringLiteral("alpha desc")));
  }

  if (m_snippetService->getSnippet(QStringLiteral("user_model_beta")).isEmpty()) {
    m_snippetService->createSnippet(QStringLiteral("user_model_beta"),
                                    makeValidSnippet(QStringLiteral("beta desc")));
  }

  m_model->setShowBuiltIn(true);
  m_model->setShortcuts(QMap<QString, int>());
  m_model->refresh();
}

void TestSnippetModel::testEmptyModel() {
  vnotex::SnippetListModel model(m_snippetService);
  QCOMPARE(model.rowCount(), 0);
}

void TestSnippetModel::testRefreshPopulatesModel() {
  const int expected = m_snippetService->listSnippets().size();
  m_model->refresh();
  QCOMPARE(m_model->rowCount(), expected);
}

void TestSnippetModel::testDisplayRoleShowsName() {
  m_model->refresh();

  QModelIndex idx = m_model->index(0, 0);
  QString display = m_model->data(idx, Qt::DisplayRole).toString();
  QString name = m_model->data(idx, vnotex::SnippetListModel::NameRole).toString();
  QCOMPARE(display, name);
}

void TestSnippetModel::testDisplayRoleWithShortcut() {
  m_model->refresh();
  QString name = m_model->snippetAt(0);
  QVERIFY(!name.isEmpty());

  QMap<QString, int> shortcuts;
  shortcuts.insert(name, 7);
  m_model->setShortcuts(shortcuts);

  QString display = m_model->data(m_model->index(0, 0), Qt::DisplayRole).toString();
  QCOMPARE(display, QStringLiteral("[07] %1").arg(name));
}

void TestSnippetModel::testNameRole() {
  m_model->refresh();
  QModelIndex idx = m_model->index(0, 0);
  QString name = m_model->data(idx, vnotex::SnippetListModel::NameRole).toString();
  QVERIFY(!name.isEmpty());
}

void TestSnippetModel::testDescriptionRole() {
  m_model->refresh();

  QString userName;
  for (int i = 0; i < m_model->rowCount(); ++i) {
    QModelIndex idx = m_model->index(i, 0);
    if (!m_model->data(idx, vnotex::SnippetListModel::IsBuiltinRole).toBool()) {
      userName = m_model->data(idx, vnotex::SnippetListModel::NameRole).toString();
      break;
    }
  }
  QVERIFY(!userName.isEmpty());

  QString description;
  for (int i = 0; i < m_model->rowCount(); ++i) {
    QModelIndex idx = m_model->index(i, 0);
    if (m_model->data(idx, vnotex::SnippetListModel::NameRole).toString() == userName) {
      description = m_model->data(idx, vnotex::SnippetListModel::DescriptionRole).toString();
      break;
    }
  }

  if (userName == QStringLiteral("user_model_alpha")) {
    QCOMPARE(description, QStringLiteral("alpha desc"));
  } else if (userName == QStringLiteral("user_model_beta")) {
    QCOMPARE(description, QStringLiteral("beta desc"));
  } else {
    QFAIL("Unexpected user snippet found");
  }
}

void TestSnippetModel::testTypeRole() {
  m_model->refresh();
  QString type = m_model->data(m_model->index(0, 0), vnotex::SnippetListModel::TypeRole).toString();
  QVERIFY(type == QStringLiteral("text") || type == QStringLiteral("dynamic"));
}

void TestSnippetModel::testIsBuiltinRole() {
  m_model->refresh();

  bool seenBuiltin = false;
  bool seenUser = false;
  for (int i = 0; i < m_model->rowCount(); ++i) {
    const bool isBuiltin =
        m_model->data(m_model->index(i, 0), vnotex::SnippetListModel::IsBuiltinRole).toBool();
    seenBuiltin = seenBuiltin || isBuiltin;
    seenUser = seenUser || !isBuiltin;
  }

  QVERIFY(seenBuiltin);
  QVERIFY(seenUser);
}

void TestSnippetModel::testShortcutRole() {
  m_model->refresh();
  QString name = m_model->snippetAt(0);

  QMap<QString, int> shortcuts;
  shortcuts.insert(name, 12);
  m_model->setShortcuts(shortcuts);

  QCOMPARE(m_model->data(m_model->index(0, 0), vnotex::SnippetListModel::ShortcutRole).toInt(), 12);
}

void TestSnippetModel::testInvalidIndexReturnsEmpty() {
  m_model->refresh();
  QVERIFY(!m_model->data(QModelIndex(), Qt::DisplayRole).isValid());
  QVERIFY(!m_model->data(QModelIndex(), vnotex::SnippetListModel::NameRole).isValid());
}

void TestSnippetModel::testSetShowBuiltInFalseFilters() {
  m_model->refresh();
  const int allCount = m_model->rowCount();

  int expectedUserCount = 0;
  const QJsonArray snippets = m_snippetService->listSnippets();
  for (const auto &val : snippets) {
    if (!val.toObject().value(QLatin1String("isBuiltin")).toBool()) {
      ++expectedUserCount;
    }
  }

  m_model->setShowBuiltIn(false);
  QCOMPARE(m_model->rowCount(), expectedUserCount);
  QVERIFY(m_model->rowCount() < allCount);
}

void TestSnippetModel::testSetShowBuiltInTrueShowsAll() {
  m_model->refresh();
  const int allCount = m_model->rowCount();

  m_model->setShowBuiltIn(false);
  QVERIFY(m_model->rowCount() <= allCount);

  m_model->setShowBuiltIn(true);
  QCOMPARE(m_model->rowCount(), allCount);
}

void TestSnippetModel::testSnippetAt() {
  m_model->refresh();

  QString fromSnippetAt = m_model->snippetAt(0);
  QString fromRole =
      m_model->data(m_model->index(0, 0), vnotex::SnippetListModel::NameRole).toString();
  QCOMPARE(fromSnippetAt, fromRole);

  QCOMPARE(m_model->snippetAt(-1), QString());
  QCOMPARE(m_model->snippetAt(m_model->rowCount()), QString());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSnippetModel)
#include "test_snippetmodel.moc"
