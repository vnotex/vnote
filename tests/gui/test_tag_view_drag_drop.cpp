#include <QtTest>

#include <QDataStream>
#include <QMimeData>
#include <QStandardItemModel>
#include <QTemporaryDir>

#include <memory>

#include <controllers/tagcontroller.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <models/tagmodel.h>
#include <models/treefilterproxymodel.h>
#include <views/tagview.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTagViewDragDrop : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void moveSubtree_data();
  void moveSubtree();
  void invalidHierarchy_data();
  void invalidHierarchy();
  void incompatibleDestination();
  void filteredHierarchy();
  void stalePayload_data();
  void stalePayload();
  void invalidPayload_data();
  void invalidPayload();
  void singleSourceOnly();
  void failedPersistence_data();
  void failedPersistence();
  void notebookSwitchBeforeReveal();

private:
  using Tags = QMap<QString, QJsonObject>;
  Tags tags(const QString &p_notebookId) const;
  QModelIndex index(const QString &p_name) const;
  std::unique_ptr<QMimeData> mime(const QString &p_name) const;
  QString createNotebook(const QString &p_name);
  void selectNotebook(const QString &p_id);
  void verifyMove(const Tags &p_before, const QString &p_parent);

  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_directory;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<TagCoreService> m_core;
  std::unique_ptr<TagService> m_tags;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<TagModel> m_model;
  std::unique_ptr<TreeFilterProxyModel> m_proxy;
  std::unique_ptr<TagController> m_controller;
  std::unique_ptr<TagView> m_view;
  QString m_notebookId;
  QJsonArray m_matches;
  QJsonArray m_displayedMatches;
  int m_beforeMoves = 0;
  int m_afterMoves = 0;
};

void TestTagViewDragDrop::init() {
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create("{}", &m_context), VXCORE_OK);
  m_directory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_directory->isValid());
  m_gate = std::make_unique<NotebookIoGate>();
  m_hooks = std::make_unique<HookManager>();
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_notebooks->setHookManager(m_hooks.get());
  m_core = std::make_unique<TagCoreService>(m_context);
  m_tags = std::make_unique<TagService>(m_context, m_hooks.get());
  m_services = std::make_unique<ServiceLocator>();
  m_services->registerService(m_core.get());
  m_services->registerService(m_tags.get());
  m_services->registerService(m_notebooks.get());

  m_notebookId = createNotebook(QStringLiteral("notebook"));
  QVERIFY(!m_notebookId.isEmpty());
  QVERIFY(m_core->createTagPath(m_notebookId, QStringLiteral("Alpha/Child/Grandchild")));
  QVERIFY(m_core->createTag(m_notebookId, QStringLiteral("Beta")));
  QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), QStringLiteral("tagged.md")).isEmpty());
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("tagged.md"), QStringLiteral("Child")));
  m_matches = m_core->findFilesByTags(m_notebookId, {QStringLiteral("Child")});
  QCOMPARE(m_matches.size(), 1);
  QCOMPARE(m_matches.first().toObject().value(QStringLiteral("filePath")).toString(),
           QStringLiteral("tagged.md"));
  QCOMPARE(m_matches.first().toObject().value(QStringLiteral("tags")).toArray(),
           QJsonArray{QStringLiteral("Child")});

  m_model = std::make_unique<TagModel>(*m_services);
  m_proxy = std::make_unique<TreeFilterProxyModel>();
  m_proxy->setSourceModel(m_model.get());
  m_controller = std::make_unique<TagController>(*m_services);
  m_view = std::make_unique<TagView>();
  m_view->setModel(m_proxy.get());
  m_view->setAnimated(false);
  m_view->resize(400, 500);
  connect(m_model.get(), &TagModel::moveTagRequested, m_controller.get(),
          &TagController::onMoveTagAction);
  connect(m_controller.get(), &TagController::tagOperationCompleted, m_model.get(),
          &TagModel::reload);
  connect(m_view.get(), &TagView::tagsSelectionChanged, m_controller.get(),
          &TagController::onTagsSelected);
  connect(m_controller.get(), &TagController::tagCompatibilityChanged, m_model.get(),
          &TagModel::setIncompatibleTags);
  connect(m_controller.get(), &TagController::matchingNodesChanged, m_view.get(),
          [this](const QJsonArray &p_matches) { m_displayedMatches = p_matches; });
  connect(
      m_controller.get(), &TagController::tagMoved, m_view.get(),
      [this](const QString &p_notebookId, const QString &p_name) {
        if (p_notebookId == m_notebookId) {
          m_view->selectAndScrollToTag(p_name);
        }
      },
      Qt::QueuedConnection);
  selectNotebook(m_notebookId);
  m_beforeMoves = m_afterMoves = 0;
  m_displayedMatches = QJsonArray();
  m_hooks->addAction(HookNames::TagBeforeMove,
                     [this](HookContext &, const QVariantMap &) { ++m_beforeMoves; });
  m_hooks->addAction(HookNames::TagAfterMove,
                     [this](HookContext &, const QVariantMap &) { ++m_afterMoves; });
}

void TestTagViewDragDrop::cleanup() {
  m_view.reset();
  m_controller.reset();
  m_proxy.reset();
  m_model.reset();
  m_services.reset();
  if (m_notebooks) {
    for (const auto &notebook : m_notebooks->listNotebooks()) {
      QVERIFY(
          m_notebooks->closeNotebook(notebook.toObject().value(QStringLiteral("id")).toString()));
    }
  }
  m_tags.reset();
  m_core.reset();
  m_notebooks.reset();
  m_hooks.reset();
  m_gate.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_directory.reset();
}

QString TestTagViewDragDrop::createNotebook(const QString &p_name) {
  return m_notebooks->createNotebook(m_directory->filePath(p_name),
                                     QStringLiteral("{\"name\":\"Tag drag test\"}"),
                                     NotebookType::Bundled);
}

void TestTagViewDragDrop::selectNotebook(const QString &p_id) {
  m_notebookId = p_id;
  m_controller->setNotebookId(p_id);
  m_model->setNotebookId(p_id);
}

TestTagViewDragDrop::Tags TestTagViewDragDrop::tags(const QString &p_notebookId) const {
  Tags result;
  for (const auto &value : m_core->listTags(p_notebookId)) {
    const auto tag = value.toObject();
    result.insert(tag.value(QStringLiteral("name")).toString(), tag);
  }
  return result;
}

QModelIndex TestTagViewDragDrop::index(const QString &p_name) const {
  return m_proxy->mapFromSource(m_model->indexFromTagName(p_name));
}

std::unique_ptr<QMimeData> TestTagViewDragDrop::mime(const QString &p_name) const {
  return std::unique_ptr<QMimeData>(m_proxy->mimeData({index(p_name)}));
}

void TestTagViewDragDrop::verifyMove(const Tags &p_before, const QString &p_parent) {
  Tags expected = p_before;
  expected[QStringLiteral("Child")].insert(QStringLiteral("parent"), p_parent);
  QCOMPARE(tags(m_notebookId), expected);
  QCOMPARE(m_core->findFilesByTags(m_notebookId, {QStringLiteral("Child")}), m_matches);
  QCOMPARE(m_model->fullTagPath(QStringLiteral("Grandchild")),
           (p_parent.isEmpty() ? QString() : p_parent + QLatin1Char('/')) +
               QStringLiteral("Child/Grandchild"));
  // Every sibling list remains alphabetical; moving does not introduce a row-order policy.
  QStringList parents = expected.keys();
  parents.prepend(QString());
  for (const auto &parent : parents) {
    QStringList actual;
    const auto parentIndex = m_model->indexFromTagName(parent);
    for (int row = 0; row < m_model->rowCount(parentIndex); ++row) {
      actual.append(m_model->index(row, 0, parentIndex).data(TagModel::TagNameRole).toString());
    }
    QStringList sorted = actual;
    sorted.sort();
    QCOMPARE(actual, sorted);
  }
}

void TestTagViewDragDrop::moveSubtree_data() {
  QTest::addColumn<QString>("parent");
  QTest::newRow("nest-and-reopen") << QStringLiteral("Beta");
  QTest::newRow("root-and-reopen") << QString();
}

void TestTagViewDragDrop::moveSubtree() {
  QFETCH(QString, parent);
  const auto before = tags(m_notebookId);
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  m_view->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
  QSignalSpy moved(m_controller.get(), &TagController::tagMoved);
  QSignalSpy errors(m_controller.get(), &TagController::errorOccurred);
  QVERIFY(m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(parent)));
  QCOMPARE(moved.size(), 1);
  QVERIFY(errors.isEmpty());
  QCOMPARE(m_beforeMoves, 1);
  QCOMPARE(m_afterMoves, 1);
  QTRY_COMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Child")});
  verifyMove(before, parent);
  if (!parent.isEmpty()) {
    QVERIFY(m_view->isExpanded(index(parent)));
  }
  QVERIFY(
      m_view->visualRect(index(QStringLiteral("Child"))).intersects(m_view->viewport()->rect()));
  QJsonObject displayed = m_displayedMatches.first().toObject();
  QCOMPARE(displayed.take(QStringLiteral("notebookId")).toString(), m_notebookId);
  QCOMPARE(QJsonArray{displayed}, m_matches);

  const QString id = m_notebookId;
  selectNotebook(QString());
  QVERIFY(m_notebooks->closeNotebook(id));
  QCOMPARE(m_notebooks->openNotebook(m_directory->filePath(QStringLiteral("notebook"))), id);
  selectNotebook(id);
  // Closing removes the local index. Load the folder as the notebook explorer does.
  VxCoreError error = VXCORE_OK;
  m_notebooks->listFolderChildren(id, QString(), &error);
  QCOMPARE(error, VXCORE_OK);
  verifyMove(before, parent);
}

void TestTagViewDragDrop::invalidHierarchy_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<QString>("parent");
  QTest::newRow("self") << QStringLiteral("Alpha") << QStringLiteral("Alpha");
  QTest::newRow("descendant") << QStringLiteral("Alpha") << QStringLiteral("Grandchild");
  QTest::newRow("same-parent") << QStringLiteral("Child") << QStringLiteral("Alpha");
  QTest::newRow("already-root") << QStringLiteral("Beta") << QString();
}

void TestTagViewDragDrop::invalidHierarchy() {
  QFETCH(QString, source);
  QFETCH(QString, parent);
  const auto before = tags(m_notebookId);
  auto data = mime(source);
  QVERIFY(data);
  QVERIFY(!m_proxy->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, index(parent)));
  QVERIFY(!m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(parent)));
  QCOMPARE(tags(m_notebookId), before);
  QCOMPARE(m_beforeMoves, 0);
  QCOMPARE(m_afterMoves, 0);
}

void TestTagViewDragDrop::incompatibleDestination() {
  const auto before = tags(m_notebookId);
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  QVERIFY(!(index(QStringLiteral("Beta")).flags() & Qt::ItemIsEnabled));
  QVERIFY(index(QStringLiteral("Beta")).flags() & Qt::ItemIsDropEnabled);
  QVERIFY(!mime(QStringLiteral("Beta")));
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  QVERIFY(m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(QStringLiteral("Beta"))));
  QTRY_COMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Child")});
  verifyMove(before, QStringLiteral("Beta"));
}

void TestTagViewDragDrop::filteredHierarchy() {
  QVERIFY(m_core->createTag(m_notebookId, QStringLiteral("Quiet")));
  m_model->reload();
  const auto before = tags(m_notebookId);
  m_proxy->setFilterText(QStringLiteral("a"));
  // Alpha and Beta are visible, Child is retained as Grandchild's ancestor.
  QVERIFY(!index(QStringLiteral("Quiet")).isValid());
  QVERIFY(index(QStringLiteral("Child")).isValid());
  QVERIFY(index(QStringLiteral("Beta")).isValid());
  auto cycle = mime(QStringLiteral("Alpha"));
  m_proxy->setFilterText(QStringLiteral("grand"));
  QVERIFY(!index(QStringLiteral("Beta")).isValid());
  QVERIFY(!m_proxy->dropMimeData(cycle.get(), Qt::MoveAction, -1, -1,
                                 index(QStringLiteral("Grandchild"))));
  QCOMPARE(tags(m_notebookId), before);
  m_proxy->setFilterText(QStringLiteral("a"));
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  QVERIFY(m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(QStringLiteral("Beta"))));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  m_proxy->setFilterText(QString());
  verifyMove(before, QStringLiteral("Beta"));
}

void TestTagViewDragDrop::stalePayload_data() {
  QTest::addColumn<int>("change");
  QTest::newRow("reload") << 0;
  QTest::newRow("notebook-switch") << 1;
  QTest::newRow("delete-recreate") << 2;
}

void TestTagViewDragDrop::stalePayload() {
  QFETCH(int, change);
  const QString originalId = m_notebookId;
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  if (change == 1) {
    const QString other = createNotebook(QStringLiteral("other"));
    QVERIFY(!other.isEmpty());
    QVERIFY(m_core->createTagPath(other, QStringLiteral("Alpha/Child/Grandchild")));
    QVERIFY(m_core->createTag(other, QStringLiteral("Beta")));
    selectNotebook(other);
  } else {
    if (change == 2) {
      QVERIFY(m_core->deleteTag(m_notebookId, QStringLiteral("Child")));
      QVERIFY(m_core->createTagPath(m_notebookId, QStringLiteral("Alpha/Child")));
    }
    m_model->reload();
  }
  const auto originalBefore = tags(originalId);
  const auto currentBefore = tags(m_notebookId);
  // Reacquire the destination after reset; stale QModelIndex is not a supported Qt API input.
  QVERIFY(
      !m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(QStringLiteral("Beta"))));
  QCOMPARE(tags(originalId), originalBefore);
  QCOMPARE(tags(m_notebookId), currentBefore);
  QCOMPARE(m_beforeMoves, 0);
}

void TestTagViewDragDrop::invalidPayload_data() {
  QTest::addColumn<QString>("kind");
  for (const auto *kind :
       {"wrong-mime", "truncated", "trailing", "multiple-records", "foreign-notebook",
        "empty-notebook", "empty-tag", "missing-tag", "copy", "link", "ignore", "insertion",
        "column", "foreign-destination", "null"}) {
    QTest::newRow(kind) << QString::fromLatin1(kind);
  }
}

void TestTagViewDragDrop::invalidPayload() {
  QFETCH(QString, kind);
  const auto before = tags(m_notebookId);
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  const QString type = m_model->mimeTypes().first();
  QByteArray payload = data->data(type);
  Qt::DropAction action = Qt::MoveAction;
  int row = -1;
  int column = 0;
  QModelIndex parent = m_model->indexFromTagName(QStringLiteral("Beta"));
  QStandardItemModel foreign(1, 2);
  if (kind == QLatin1String("wrong-mime")) {
    data->clear();
    data->setText(QStringLiteral("Child"));
  } else if (kind == QLatin1String("truncated")) {
    data->setData(type, payload.chopped(1));
  } else if (kind == QLatin1String("trailing")) {
    data->setData(type, payload + 'x');
  } else if (kind == QLatin1String("multiple-records")) {
    data->setData(type, payload + payload);
  } else if (kind.endsWith(QLatin1String("notebook")) || kind.endsWith(QLatin1String("tag"))) {
    QString notebook;
    QString tag;
    quint64 revision = 0;
    QDataStream input(payload);
    input >> notebook >> tag >> revision;
    if (kind == QLatin1String("foreign-notebook"))
      notebook = QStringLiteral("foreign");
    if (kind == QLatin1String("empty-notebook"))
      notebook.clear();
    if (kind == QLatin1String("empty-tag"))
      tag.clear();
    if (kind == QLatin1String("missing-tag"))
      tag = QStringLiteral("Missing");
    payload.clear();
    QDataStream output(&payload, QIODevice::WriteOnly);
    output << notebook << tag << revision;
    data->setData(type, payload);
  } else if (kind == QLatin1String("copy"))
    action = Qt::CopyAction;
  else if (kind == QLatin1String("link"))
    action = Qt::LinkAction;
  else if (kind == QLatin1String("ignore"))
    action = Qt::IgnoreAction;
  else if (kind == QLatin1String("insertion"))
    row = 0;
  else if (kind == QLatin1String("column"))
    column = 1;
  else if (kind == QLatin1String("foreign-destination"))
    parent = foreign.index(0, 0);
  else if (kind == QLatin1String("null"))
    data.reset();
  QVERIFY(!m_model->canDropMimeData(data.get(), action, row, column, parent));
  QVERIFY(!m_model->dropMimeData(data.get(), action, row, column, parent));
  QCOMPARE(tags(m_notebookId), before);
  QCOMPARE(m_beforeMoves, 0);
}

void TestTagViewDragDrop::singleSourceOnly() {
  const auto before = tags(m_notebookId);
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("tagged.md"), QStringLiteral("Beta")));
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  m_view->selectionModel()->select(index(QStringLiteral("Beta")), QItemSelectionModel::Select);
  QCOMPARE(m_view->selectedTagNames(),
           (QStringList{QStringLiteral("Child"), QStringLiteral("Beta")}));
  QCOMPARE(m_displayedMatches.first().toObject().value(QStringLiteral("filePath")).toString(),
           QStringLiteral("tagged.md"));
  QVERIFY(!m_proxy->mimeData(m_view->selectionModel()->selectedRows()));
  QVERIFY(!m_model->mimeData({}));
  QVERIFY(!m_model->mimeData({QModelIndex()}));
  QStandardItemModel foreign(1, 2);
  QVERIFY(!m_model->mimeData({foreign.index(0, 0)}));
  QVERIFY(!m_model->mimeData({foreign.index(0, 1)}));
  QCOMPARE(m_model->flags(foreign.index(0, 0)), Qt::NoItemFlags);
  QCOMPARE(tags(m_notebookId), before);
  QCOMPARE(m_beforeMoves, 0);
}

void TestTagViewDragDrop::failedPersistence_data() {
  QTest::addColumn<bool>("readOnly");
  QTest::newRow("cancel-hook") << false;
  QTest::newRow("read-only-storage") << true;
}

void TestTagViewDragDrop::failedPersistence() {
  QFETCH(bool, readOnly);
  if (readOnly) {
    const QString id = m_notebookId;
    selectNotebook(QString());
    QVERIFY(m_notebooks->closeNotebook(id));
    QCOMPARE(m_notebooks->openNotebookEx(m_directory->filePath(QStringLiteral("notebook")),
                                         QStringLiteral("{\"readOnly\":true}")),
             id);
    selectNotebook(id);
    VxCoreError error = VXCORE_OK;
    m_notebooks->listFolderChildren(id, QString(), &error);
    QCOMPARE(error, VXCORE_OK);
  } else {
    m_hooks->addAction(
        HookNames::TagBeforeMove,
        [](HookContext &p_context, const QVariantMap &) { p_context.cancel(); }, 20);
  }
  const auto before = tags(m_notebookId);
  QSignalSpy errors(m_controller.get(), &TagController::errorOccurred);
  QSignalSpy moved(m_controller.get(), &TagController::tagMoved);
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  QVERIFY(m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(QStringLiteral("Beta"))));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  QCOMPARE(errors.size(), 1);
  QVERIFY(moved.isEmpty());
  QCOMPARE(m_beforeMoves, 1);
  QCOMPARE(m_afterMoves, 0);
  QCOMPARE(tags(m_notebookId), before);
  QCOMPARE(m_model->fullTagPath(QStringLiteral("Grandchild")),
           QStringLiteral("Alpha/Child/Grandchild"));
  QVERIFY(index(QStringLiteral("Child")).isValid());
  QCOMPARE(m_core->findFilesByTags(m_notebookId, {QStringLiteral("Child")}), m_matches);
}

void TestTagViewDragDrop::notebookSwitchBeforeReveal() {
  const QString original = m_notebookId;
  const QString other = createNotebook(QStringLiteral("other"));
  QVERIFY(!other.isEmpty());
  QVERIFY(m_core->createTagPath(other, QStringLiteral("Beta/Child")));
  const auto otherBefore = tags(other);
  auto data = mime(QStringLiteral("Child"));
  QVERIFY(data);
  QVERIFY(m_proxy->dropMimeData(data.get(), Qt::MoveAction, -1, -1, index(QStringLiteral("Beta"))));
  selectNotebook(other);
  // Drain the old service reload before establishing the new notebook's selection. The
  // queued success itself is still pending on the view and must not select its namesake.
  QCoreApplication::sendPostedEvents(m_model.get(), QEvent::MetaCall);
  m_view->selectAndScrollToTag(QStringLiteral("Beta"));
  QCoreApplication::sendPostedEvents(m_view.get(), QEvent::MetaCall);
  QCOMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Beta")});
  QCOMPARE(tags(other), otherBefore);
  QCOMPARE(tags(original).value(QStringLiteral("Child")).value(QStringLiteral("parent")).toString(),
           QStringLiteral("Beta"));
}

} // namespace tests

QTEST_MAIN(tests::TestTagViewDragDrop)
#include "test_tag_view_drag_drop.moc"
