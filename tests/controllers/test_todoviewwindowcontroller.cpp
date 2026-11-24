#include <QtTest>

#include <QAbstractItemModelTester>
#include <QDateTime>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <memory>

#include <controllers/todoviewwindowcontroller.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/memorybuffersource.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {
QString key(const char *p_id) { return QStringLiteral("id:") + QLatin1String(p_id); }
QString header() { return QStringLiteral("<!-- todo: {\"version\":1} -->\n\n"); }
QString groupedHeader() {
  return header() +
         QStringLiteral(
             "# Default\n<!-- todo-group: {\"id\":\"defaultgroup\",\"default\":true} -->\n\n");
}

QString projectionSource() {
  return groupedHeader() + QString::fromUtf8(R"TODO(## Work
<!-- todo-list: {"id":"work","hidden":"list-secret"} -->

> Description-only-token

- [ ] Alpha
  <!-- todo-item: {"id":"a","important":true,"due":"2026-11-11","myDay":"2026-11-12","tags":["Work","two words, literal"],"hidden":"item-secret"} -->

  > Needle in a **note**

  - [x] Gather figures
  - [ ] Draft slides

- [ ] beta
  <!-- todo-item: {"id":"b","due":"2026-11-12","tags":["work-plan"]} -->

  - [ ] Needle step

- [X] Completed
  <!-- todo-item: {"id":"c","important":true,"due":"2026-11-13","myDay":"2026-11-12","tags":["work"]} -->

## Home
<!-- todo-list: {"id":"home"} -->

- [ ] alpha
  <!-- todo-item: {"id":"d","myDay":"2026-11-13","tags":["work"]} -->

- [ ] Last
  <!-- todo-item: {"id":"e","due":"2026-11-12"} -->
)TODO");
}

QString sidebarSource() {
  return groupedHeader() + QStringLiteral("## First\n<!-- todo-list: {\"id\":\"first\"} -->\n\n"
                                          "- [ ] A\n  <!-- todo-item: {\"id\":\"a\"} -->\n\n"
                                          "## Second\n<!-- todo-list: {\"id\":\"second\"} -->\n\n"
                                          "- [ ] B\n  <!-- todo-item: {\"id\":\"b\"} -->\n\n"
                                          "# Work\n<!-- todo-group: {\"id\":\"work\"} -->\n\n"
                                          "<!-- work comment -->\n\n"
                                          "## Third\n<!-- todo-list: {\"id\":\"third\"} -->\n\n"
                                          "- [ ] C\n  <!-- todo-item: {\"id\":\"c\"} -->\n\n"
                                          "# Empty\n<!-- todo-group: {\"id\":\"empty\"} -->");
}

QStringList visibleKeys(const TodoTaskModel *p_model) {
  QStringList keys;
  for (int row = 0; row < p_model->rowCount(); ++row) {
    if (const auto *item = p_model->itemAt(row)) {
      keys.append(item->m_key);
    }
  }
  return keys;
}

QStringList itemTitles(const TodoList &p_list) {
  QStringList titles;
  for (const auto &item : p_list.m_items) {
    titles.append(item.m_fields.m_title);
  }
  return titles;
}

QStringList stepTitles(const TodoItem &p_item) {
  QStringList titles;
  for (const auto &step : p_item.m_steps) {
    titles.append(step.m_title);
  }
  return titles;
}
} // namespace

class TestTodoViewWindowController : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void initialCleanAndNoOps();
  void emptyQuickAddAtomic();
  void defaultGroupMaterialization();
  void defaultCreationAtSameEof();
  void groupCommandsPreserveOwnedData();
  void ungroupExistingDefault_data();
  void ungroupExistingDefault();
  void ungroupWithoutDefault();
  void ungroupEmptyIntoDefault();
  void duplicatePreservesOwnedData();
  void duplicateIdlessOwnersAndEof();
  void groupAndDuplicateAdmission();
  void listModelDrops();
  void listModelDropAdmission();
  void listModelSourceIdentityDrops();
  void implicitGroupMoveRemapsChildren();
  void crossGroupListMoveBoundaries();
  void groupProjectionIsPresentational();
  void rootListProjection_data();
  void rootListProjection();
  void internalGroupSearch();
  void rootListDropBoundaries_data();
  void rootListDropBoundaries();
  void groupCreationReturnsActualOwners();
  void listCommands();
  void listIconRoundTrip_data();
  void listIconRoundTrip();
  void listIconPreservesMetadataAndCopies();
  void listIconAdmission();
  void setextAndEmptyTitles();
  void checkboxChangesOnlyOneCharacter();
  void taskFieldsAndUnknownMetadata();
  void notesAndTags();
  void taskAddRemoveUndo();
  void stepCommands();
  void promoteStepPreservesOwnedSource();
  void promoteStepMaterializesOwners_data();
  void promoteStepMaterializesOwners();
  void promoteStepAdmission();
  void stepCompletionIsIndependent();
  void movesRetainOwnedBlocks();
  void movesAtUnterminatedEof();
  void sourceSelectionRemapping();
  void movedListRemapsUnidentifiedTasks();
  void invalidCommandsAreAtomic();
  void readOnlyAndUnavailable();
  void staleSourceAndRowIntents();
  void mutationGuardRejectsChangedGeneration();
  void coalescingAndHistoryBoundaries();
  void sourceRefreshAndReplacement();
  void smartViewsAndMidnight();
  void searchAndTagComposition();
  void stableSortingAndCounts();
  void timestampSorting_data();
  void timestampSorting();
  void completedExpansionPreservesIndexes();
  void modelRolesAndIntents();
  void myDayMembershipIntents();
  void nativeDragUsesMoveCommand();
  void dragCompletionGroupsAndStaleness();
  void formattingKeepsSourceIntents();
  void normalizedCrLfAndUnicode();
  void coincidentEofInsertions();

private:
  bool load(const QString &p_source, bool p_readOnly = false);
  const TodoItem &item(const char *p_id) const;
  const TodoList &list(const char *p_id) const;
  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_temp;
  QVector<QPair<QByteArray, QByteArray>> m_environment;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<SyncWorkQueueManager> m_queues;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<QTextDocument> m_document;
  std::unique_ptr<TodoViewWindowController> m_controller;
  Buffer2 m_buffer;
};

void TestTodoViewWindowController::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
}

void TestTodoViewWindowController::init() {
  m_temp = std::make_unique<QTemporaryDir>();
  QVERIFY(m_temp->isValid());
  for (const char *name : {"TMPDIR", "TMP", "TEMP"}) {
    m_environment.append({QByteArray(name), qgetenv(name)});
    QVERIFY(qputenv(name, m_temp->path().toUtf8()));
  }
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context);
  m_hooks = std::make_unique<HookManager>();
  m_gate = std::make_unique<NotebookIoGate>();
  m_queues = std::make_unique<SyncWorkQueueManager>();
  m_buffers =
      std::make_unique<BufferService>(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None);
  m_buffers->setSyncWorkQueueManager(m_queues.get());
  m_services = std::make_unique<ServiceLocator>();
  m_services->registerService(m_buffers.get());
  m_services->registerService(m_hooks.get());
}

void TestTodoViewWindowController::cleanup() {
  m_controller.reset();
  m_document.reset();
  m_buffer = Buffer2();
  bool drained = true;
  if (m_buffers) {
    drained = m_buffers->shutdown();
  }
  m_services.reset();
  m_buffers.reset();
  m_queues.reset();
  m_gate.reset();
  m_hooks.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  for (const auto &entry : m_environment) {
    if (entry.second.isNull()) {
      qunsetenv(entry.first.constData());
    } else {
      qputenv(entry.first.constData(), entry.second);
    }
  }
  m_environment.clear();
  m_temp.reset();
  QVERIFY(drained);
}

bool TestTodoViewWindowController::load(const QString &p_source, bool p_readOnly) {
  m_controller.reset();
  m_document = std::make_unique<QTextDocument>();
  MemoryBufferSource source;
  source.address =
      QStringLiteral("vx://todo-controller/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  source.displayName = QStringLiteral("Planner");
  source.editorType = QStringLiteral("todo");
  source.content = p_source.toUtf8();
  source.readOnly = p_readOnly;
  m_buffer = m_buffers->openMemoryBuffer(std::move(source));
  if (!m_buffer.isValid()) {
    return false;
  }
  m_document->setPlainText(p_source);
  m_document->setModified(false);
  m_document->clearUndoRedoStacks();
  m_controller =
      std::make_unique<TodoViewWindowController>(*m_services, m_document.get(), m_buffer);
  m_controller->setEditable(true);
  m_controller->setToday(QDate(2026, 11, 12));
  return true;
}

const TodoItem &TestTodoViewWindowController::item(const char *p_id) const {
  return *m_controller->getDocument().findItem(key(p_id));
}

const TodoList &TestTodoViewWindowController::list(const char *p_id) const {
  return *m_controller->getDocument().findList(key(p_id));
}

void TestTodoViewWindowController::initialCleanAndNoOps() {
  const QString source = QStringLiteral("## Inbox\n\n- [ ] Keep\n  - [ ] Step");
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  const auto listKey = m_controller->getDocument().m_lists.first().m_key;
  const auto taskKey = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  QVERIFY(m_controller->renameList(listKey, QStringLiteral("Inbox")));
  QVERIFY(m_controller->setListDescription(listKey, {}));
  QVERIFY(m_controller->setListIcon(listKey, {}));
  QVERIFY(
      m_controller->updateTask(taskKey, m_controller->getDocument().findItem(taskKey)->m_fields));
  QVERIFY(m_controller->updateStep(taskKey, 0, QStringLiteral("Step"), false));
  QVERIFY(m_controller->moveStep(taskKey, 0, 0));
  QVERIFY(m_controller->moveTask(taskKey, listKey, {}));
  QVERIFY(m_controller->moveList(listKey, {}, {}));
  m_controller->setSearch(QStringLiteral("Keep"));
  m_controller->setSearch({});
  m_controller->setSortOrder(TodoSortOrder::Title);
  m_controller->setView(TodoViewKind::All);
  m_controller->setCompletedExpanded(true);
  m_controller->refreshFromSource();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  QCOMPARE(changed.count(), 0);
  QVERIFY(m_controller->getDocument().m_properties.isEmpty());
}

void TestTodoViewWindowController::listIconRoundTrip_data() {
  QTest::addColumn<QString>("source");
  QTest::newRow("implicit-default-heading-eof") << QStringLiteral("## Inbox");
  QTest::newRow("implicit-default-empty-icon-eof") << QStringLiteral(
      "## Inbox\n<!-- todo-list: {\"icon\":\"\",\"future\":{\"nested\":[1,true]}} -->");
  QTest::newRow("explicit-idless-group") << QStringLiteral("# Group\n## Inbox");
  QTest::newRow("idless-task-selection") << QStringLiteral("## Inbox\n- [ ] Task");
}

void TestTodoViewWindowController::listIconRoundTrip() {
  QFETCH(QString, source);
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  const auto original = m_controller->getDocument().m_lists.first();
  m_controller->setView(TodoViewKind::List, original.m_key);
  if (!original.m_items.isEmpty()) {
    m_controller->setSelectedItemKey(original.m_items.first().m_key);
  }
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  QVERIFY(m_controller->setListIcon(original.m_key, {}));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  QCOMPARE(changed.count(), 0);

  const auto icon = QString::fromUtf8(u8"👩🏽‍💻");
  const auto replacement = QString::fromUtf8(u8"🇻🇳");
  QVERIFY(m_controller->setListIcon(original.m_key, icon));
  QCOMPARE(changed.count(), 1);
  const auto assigned = m_document->toPlainText();
  QCOMPARE(assigned.count(QStringLiteral("<!-- todo-list:")), 1);
  QCOMPARE(assigned.count(QStringLiteral("<!-- todo-group:")), 1);
  QCOMPARE(assigned.count(QStringLiteral("<!-- todo:")), 1);
  QVERIFY(assigned.contains(icon));
  const auto listKey = m_controller->getProjection().m_listKey;
  QVERIFY(listKey.startsWith(QLatin1String("id:")));
  const auto *assignedList = m_controller->getDocument().findList(listKey);
  QVERIFY(assignedList);
  QVERIFY(!m_controller->getDocument().groupForList(*assignedList)->m_id.isEmpty());
  auto expected = original.m_properties;
  expected.insert(QStringLiteral("id"), assignedList->m_id);
  expected.insert(QStringLiteral("icon"), icon);
  QCOMPARE(assignedList->m_properties, expected);
  if (!original.m_items.isEmpty()) {
    const auto *selected = m_controller->getDocument().findItem(m_controller->getSelectedItemKey());
    QVERIFY(selected);
    QCOMPARE(selected->m_listKey, listKey);
    QCOMPARE(selected->m_fields.m_title, original.m_items.first().m_fields.m_title);
  }
  auto *model = m_controller->getListModel();
  QCOMPARE(model->index(model->rowForKey(listKey)).data(TodoListModel::IconRole).toString(), icon);
  const auto reloaded = TodoDocument::parse(QString::fromUtf8(assigned.toUtf8()));
  QVERIFY(reloaded.isValid());
  QCOMPARE(reloaded.findList(listKey)->m_properties, expected);
  m_document->setModified(false);
  QVERIFY(m_controller->setListIcon(listKey, icon));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(m_document->toPlainText(), assigned);
  QVERIFY(!m_document->isModified());

  QVERIFY(m_controller->setListIcon(listKey, replacement));
  const auto replaced = m_document->toPlainText();
  QCOMPARE(replaced.count(QStringLiteral("<!-- todo-list:")), 1);
  QCOMPARE(model->index(model->rowForKey(listKey)).data(TodoListModel::IconRole).toString(),
           replacement);
  QVERIFY(m_controller->setListIcon(listKey, {}));
  const auto cleared = m_document->toPlainText();
  QVERIFY(!m_controller->getDocument().findList(listKey)->m_properties.contains(
      QStringLiteral("icon")));
  expected.remove(QStringLiteral("icon"));
  QCOMPARE(m_controller->getDocument().findList(listKey)->m_properties, expected);
  QCOMPARE(cleared.count(QStringLiteral("<!-- todo-list:")), 1);
  QVERIFY(
      model->index(model->rowForKey(listKey)).data(TodoListModel::IconRole).toString().isEmpty());
  m_document->setModified(false);
  QVERIFY(m_controller->setListIcon(listKey, {}));
  QCOMPARE(m_document->toPlainText(), cleared);
  QVERIFY(!m_document->isModified());
  QCOMPARE(changed.count(), 3);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), replaced);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), assigned);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), assigned);
}

void TestTodoViewWindowController::listIconPreservesMetadataAndCopies() {
  const auto icon = QString::fromUtf8(u8"👩🏽‍💻");
  const auto source =
      QStringLiteral("# Group\n<!-- todo-group: {\"id\":\"g\",\"icon\":\"group-only\"} -->\n"
                     "## List\n<!-- todo-list: {\"icon\":\"") +
      icon +
      QStringLiteral("\",\"future\":{\"nested\":[1,true]},\"number\":-1e-9} -->\n"
                     "# Other\n<!-- todo-group: {\"id\":\"other\"} -->");
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  const auto original = m_controller->getDocument().m_lists.first();
  QVERIFY(m_controller->setListIcon(original.m_key, icon));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  auto *model = m_controller->getListModel();
  QCOMPARE(model->roleNames().value(TodoListModel::IconRole), QByteArray("icon"));
  for (int row = 0; row < model->rowCount(); ++row) {
    QCOMPARE(model->index(row).data(TodoListModel::IconRole).toString(),
             model->listAt(row) ? icon : QString());
  }
  QString created;
  QVERIFY(m_controller->duplicateList(original.m_key, &created));
  const auto *copy = m_controller->getDocument().findList(created);
  QVERIFY(copy);
  auto copiedProperties = copy->m_properties;
  copiedProperties.remove(QStringLiteral("id"));
  QCOMPARE(copiedProperties, original.m_properties);
  const auto duplicated = m_document->toPlainText();
  QVERIFY(m_controller->moveList(created, key("other"), {}));
  copy = m_controller->getDocument().findList(created);
  QVERIFY(copy);
  QCOMPARE(m_controller->getDocument().groupForList(*copy)->m_key, key("other"));
  auto movedProperties = copy->m_properties;
  movedProperties.remove(QStringLiteral("id"));
  QCOMPARE(movedProperties, original.m_properties);
  QCOMPARE(model->index(model->rowForKey(created)).data(TodoListModel::IconRole).toString(), icon);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), duplicated);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  m_controller->setView(TodoViewKind::List, original.m_key);
  QVERIFY(m_controller->setListIcon(original.m_key, {}));
  const auto *cleared =
      m_controller->getDocument().findList(m_controller->getProjection().m_listKey);
  QVERIFY(cleared && !cleared->m_id.isEmpty());
  auto clearedProperties = cleared->m_properties;
  clearedProperties.remove(QStringLiteral("id"));
  auto expected = original.m_properties;
  expected.remove(QStringLiteral("icon"));
  QCOMPARE(clearedProperties, expected);
  QCOMPARE(m_document->toPlainText().count(QStringLiteral("<!-- todo-list:")), 1);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::listIconAdmission() {
  const auto source = projectionSource();
  const auto icon = QString::fromUtf8(u8"📝");
  QVERIFY(load(source));
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  QSignalSpy rejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  QVERIFY(!m_controller->setListIcon(key("missing"), icon));
  QVERIFY(!m_controller->setListIcon(key("defaultgroup"), icon));
  QVERIFY(!m_controller->setListIcon({}, icon));
  for (const auto separator :
       {QChar('\n'), QChar('\r'), QChar(QChar::ParagraphSeparator), QChar(QChar::LineSeparator)}) {
    QVERIFY(!m_controller->setListIcon(key("work"), icon + separator + icon));
  }
  QCOMPARE(rejected.count(), 7);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());

  m_controller->setEditable(false);
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QVERIFY(!m_controller->setListIcon(key("work"), {}));
  m_controller->setEditable(true);
  int calls = 0;
  m_controller->setMutationGuard([&calls]() {
    ++calls;
    return false;
  });
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QCOMPARE(calls, 1);
  m_controller->setMutationGuard([this]() {
    m_controller->setBuffer(m_buffer);
    m_controller->refreshFromSource();
    return true;
  });
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  m_controller->setMutationGuard({});
  m_controller->setBuffer(Buffer2());
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());

  QVERIFY(load(source, true));
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());

  QVERIFY(load(source));
  m_controller->setSourceRefreshEnabled(false);
  QTextCursor cursor(m_document.get());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n<!-- external -->\n"));
  const auto external = m_document->toPlainText();
  m_document->setModified(false);
  m_document->clearUndoRedoStacks();
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QCOMPARE(m_document->toPlainText(), external);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());

  QVERIFY(load(source));
  m_controller->setMutationGuard([this]() {
    QTextCursor cursor(m_document.get());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("\n<!-- guard source change -->\n"));
    return true;
  });
  QVERIFY(!m_controller->setListIcon(key("work"), icon));
  QCOMPARE(m_document->toPlainText(), source + QStringLiteral("\n<!-- guard source change -->\n"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::emptyQuickAddAtomic() {
  QVERIFY(load({}));
  QCOMPARE(m_controller->getProjection().m_view, TodoViewKind::All);
  QCOMPARE(m_controller->getTaskModel()->rowCount(), 0);
  QVERIFY(!m_document->isModified());
  TodoTaskFields fields;
  fields.m_title = QStringLiteral("Prepare **slides**");
  fields.m_checked = true;
  fields.m_important = true;
  fields.m_due = QDate(2026, 11, 20);
  fields.m_myDay = QDate(2026, 11, 12);
  fields.m_tags = {QStringLiteral("work")};
  fields.m_note = QStringLiteral("A [link](proposal.md)");
  const auto beforeCreation = QDateTime::currentDateTimeUtc();
  QVERIFY(m_controller->addTask({}, fields));
  const auto afterCreation = QDateTime::currentDateTimeUtc();
  const auto &document = m_controller->getDocument();
  QVERIFY(document.isValid());
  QCOMPARE(document.m_groups.size(), 1);
  QVERIFY(document.defaultGroup());
  QVERIFY(!document.defaultGroup()->m_implicit);
  QCOMPARE(document.defaultGroup()->m_listCount, 1);
  QCOMPARE(document.m_lists.size(), 1);
  QCOMPARE(document.groupForList(document.m_lists.first()), document.defaultGroup());
  QCOMPARE(document.m_lists.first().m_title, QStringLiteral("Tasks"));
  const auto added = document.m_lists.first().m_items.first();
  QVERIFY(added.m_createdAt.isValid());
  QVERIFY(added.m_createdAt >= beforeCreation && added.m_createdAt <= afterCreation);
  QCOMPARE(added.m_properties.value(QStringLiteral("createdAt")).toString(),
           added.m_createdAt.toUTC().toString(Qt::ISODateWithMs));
  QCOMPARE(added.m_fields.m_title, fields.m_title);
  QVERIFY(added.m_fields.m_checked);
  QVERIFY(added.m_fields.m_important);
  QCOMPARE(added.m_fields.m_due, fields.m_due);
  QCOMPARE(added.m_fields.m_myDay, fields.m_myDay);
  QCOMPARE(added.m_fields.m_tags, fields.m_tags);
  QCOMPARE(added.m_fields.m_note, fields.m_note);
  QCOMPARE(m_controller->getSelectedItemKey(), added.m_key);
  QVERIFY(!added.m_id.isEmpty());
  QVERIFY(!document.m_lists.first().m_id.isEmpty());
  QCOMPARE(document.m_properties.value(QStringLiteral("version")).toInt(), 1);
  const auto text = m_document->toPlainText();
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), QString());
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), text);
  QVERIFY(m_controller->getDocument().findItem(added.m_key));
  QCOMPARE(m_controller->getDocument().findItem(added.m_key)->m_createdAt, added.m_createdAt);
}

void TestTodoViewWindowController::defaultGroupMaterialization() {
  const auto source =
      header() + QStringLiteral("<!-- file stays outside -->\n\n"
                                "## Inbox\n<!-- todo-list: {\"id\":\"inbox\"} -->\n\n- [ ] Keep\n");
  QVERIFY(load(source));
  const auto *implicit = m_controller->getDocument().defaultGroup();
  QVERIFY(implicit && implicit->m_implicit);
  const auto oldKey = implicit->m_key;
  m_controller->setSelectedGroupKey(oldKey);
  m_controller->setGroupExpanded(oldKey, false);
  QVERIFY(m_controller->renameList(key("inbox"), QStringLiteral("Inbox")));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  QString created;
  QVERIFY(m_controller->addList(QStringLiteral("Second"), {}, &created));
  const auto &document = m_controller->getDocument();
  const auto *group = document.defaultGroup();
  QVERIFY(group && !group->m_implicit);
  QCOMPARE(document.m_groups.size(), 1);
  QCOMPARE(group->m_listCount, 2);
  QCOMPARE(document.groupForList(*document.findList(created)), group);
  QCOMPARE(m_controller->getProjection().m_groupKey, group->m_key);
  QVERIFY(m_controller->getProjection().m_collapsedGroups.contains(group->m_key));
  QVERIFY(!m_controller->getProjection().m_collapsedGroups.contains(oldKey));
  const auto changed = m_document->toPlainText();
  QVERIFY(changed.startsWith(header() + QStringLiteral("<!-- file stays outside -->\n\n")));
  QCOMPARE(document.text(document.m_metadataRange), header().trimmed() + QLatin1Char('\n'));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), changed);
  QVERIFY(m_controller->getDocument().findList(created));
}

void TestTodoViewWindowController::defaultCreationAtSameEof() {
  const auto source = header() + QStringLiteral("# Work\n<!-- todo-group: {\"id\":\"g\"} -->\n\n"
                                                "## Only\n<!-- todo-list: {\"id\":\"only\"} -->\n\n"
                                                "- [ ] Keep\n  <!-- todo-item: {\"id\":\"t\"} -->");
  QVERIFY(load(source));
  QVERIFY(!m_controller->getDocument().defaultGroup());
  QCOMPARE(list("only").m_range.m_end, source.size());
  QVERIFY(m_controller->moveList(key("only"), {}, {}));
  const auto &document = m_controller->getDocument();
  QVERIFY(document.isValid());
  QCOMPARE(document.m_groups.size(), 2);
  QCOMPARE(document.findGroup(key("g"))->m_listCount, 0);
  const auto *fallback = document.defaultGroup();
  QVERIFY(fallback && !fallback->m_implicit);
  QCOMPARE(document.groupForList(list("only")), fallback);
  QCOMPARE(fallback->m_listCount, 1);
  QCOMPARE(item("t").m_fields.m_title, QStringLiteral("Keep"));
  const auto changed = m_document->toPlainText();
  const int undoSteps = m_document->availableUndoSteps();
  QVERIFY(m_controller->moveList(key("only"), {}, {}));
  QCOMPARE(m_document->toPlainText(), changed);
  QCOMPARE(m_document->availableUndoSteps(), undoSteps);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), changed);
}

void TestTodoViewWindowController::groupCommandsPreserveOwnedData() {
  const auto source =
      header() + QStringLiteral("<!-- file comment -->\n\n") +
      QStringLiteral(
          "Office\n======\n<!-- todo-group: {\"id\":\"g\",\"extra\":-1e-9} -->\n\n"
          "<!-- group comment -->\n\n## Inbox\n<!-- todo-list: {\"id\":\"inbox\"} -->\n\n"
          "- [ ] Keep\n  <!-- todo-item: {\"id\":\"a\"} -->\n\n  > Note\n\n  - [x] Step\n\n"
          "# Personal\n<!-- todo-group: {\"id\":\"h\"} -->\n\n"
          "## Home\n<!-- todo-list: {\"id\":\"home\"} -->\n\n"
          "- [ ] Other\n  <!-- todo-item: {\"id\":\"b\"} -->\n");
  QVERIFY(load(source));
  QVERIFY(m_controller->renameGroup(key("g"), QStringLiteral("Office [renamed]")));
  QCOMPARE(m_controller->getDocument().findGroup(key("g"))->m_title,
           QStringLiteral("Office [renamed]"));
  QCOMPARE(m_controller->getDocument()
               .findGroup(key("g"))
               ->m_properties.value(QStringLiteral("extra"))
               .toDouble(),
           -1e-9);
  QVERIFY(m_document->toPlainText().contains(QStringLiteral("======")));
  const auto owned = m_controller->getDocument()
                         .text(m_controller->getDocument().findGroup(key("g"))->m_range)
                         .trimmed();
  QVERIFY(m_controller->moveGroup(key("g"), {}));
  QCOMPARE(m_controller->getDocument().m_groups.last().m_key, key("g"));
  QCOMPARE(m_controller->getDocument()
               .text(m_controller->getDocument().findGroup(key("g"))->m_range)
               .trimmed(),
           owned);
  QCOMPARE(item("a").m_fields.m_note, QStringLiteral("Note"));
  QVERIFY(item("a").m_steps.first().m_checked);
  QVERIFY(m_controller->renameGroup(key("g"), {}));
  QVERIFY(m_controller->getDocument().findGroup(key("g"))->m_title.isEmpty());
  QVERIFY(m_controller->getDocument().isValid());
  const auto beforeDelete = m_document->toPlainText();
  QVERIFY(m_controller->ungroupLists(key("g")));
  QVERIFY(!m_controller->getDocument().findGroup(key("g")));
  QVERIFY(m_controller->getDocument().findList(key("inbox")));
  QVERIFY(m_controller->getDocument().findItem(key("a")));
  QCOMPARE(m_controller->getDocument().groupForList(list("inbox")),
           m_controller->getDocument().defaultGroup());
  QCOMPARE(item("b").m_fields.m_title, QStringLiteral("Other"));
  QVERIFY(m_document->toPlainText().startsWith(header() + QStringLiteral("<!-- file comment -->")));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeDelete);
}

void TestTodoViewWindowController::ungroupExistingDefault_data() {
  QTest::addColumn<QString>("source");
  const auto custom = QStringLiteral(
      "Office\n======\n<!-- todo-group: {\"id\":\"g\",\"discard\":{\"nested\":7}} -->\n\n"
      "<!-- ordinary group comment -->\n\n"
      "## Selected\n\n- [ ] Selected task\n\n  > Keep note\n\n  - [x] Keep step\n\n"
      "## Owned\n<!-- todo-list: {\"id\":\"owned\",\"extra\":{\"x\":1}} -->\n\n"
      "- [X] Owned task\n  <!-- todo-item: {\"id\":\"owned-task\",\"extra\":[1,2]} -->\n\n");
  const auto defaultBody =
      QStringLiteral("## Default list\n<!-- todo-list: {\"id\":\"default-list\"} -->\n\n"
                     "- [ ] Default task\n  <!-- todo-item: {\"id\":\"default-task\"} -->\n\n");
  const auto other =
      QStringLiteral("# Other\n<!-- todo-group: {\"id\":\"other\"} -->\n\n"
                     "## Other list\n<!-- todo-list: {\"id\":\"other-list\"} -->\n\n");
  const auto fallback = groupedHeader().mid(header().size()) + defaultBody;
  QTest::newRow("default-before-adjacent") << header() + fallback + custom + other;
  QTest::newRow("default-before-separated") << header() + fallback + other + custom;
  QTest::newRow("default-after-adjacent") << header() + custom + fallback + other;
  QTest::newRow("default-after-separated") << header() + custom + other + fallback;
  QTest::newRow("implicit-default") << header() + defaultBody + custom + other;
  QTest::newRow("empty-default-at-eof")
      << header() + custom + other +
             QStringLiteral(
                 "# Default\n<!-- todo-group: {\"id\":\"defaultgroup\",\"default\":true} -->");
}

void TestTodoViewWindowController::ungroupExistingDefault() {
  QFETCH(QString, source);
  QVERIFY(load(source));
  const auto &before = m_controller->getDocument();
  QVERIFY(before.isValid());
  const auto *custom = before.findGroup(key("g"));
  QVERIFY(custom);
  const auto selected = before.m_lists.at(custom->m_firstList);
  const auto selectedBody = before.text(selected.m_range);
  const auto ownedBody = before.text(list("owned").m_range);
  const auto otherBody = before.text(before.findGroup(key("other"))->m_range);
  const int count = before.m_lists.size();
  m_controller->setView(TodoViewKind::List, selected.m_key);
  m_controller->setSelectedItemKey(selected.m_items.first().m_key);
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  QVERIFY(m_controller->ungroupLists(key("g")));
  QCOMPARE(changed.count(), 1);
  const auto &after = m_controller->getDocument();
  QVERIFY(after.isValid());
  QVERIFY(!after.findGroup(key("g")));
  const auto *fallback = after.defaultGroup();
  QVERIFY(fallback && !fallback->m_implicit && !fallback->m_id.isEmpty());
  QCOMPARE(after.m_lists.size(), count);
  QCOMPARE(after.text(after.findGroup(key("other"))->m_range), otherBody);
  const auto *moved = after.findList(m_controller->getProjection().m_listKey);
  QVERIFY(moved);
  QCOMPARE(moved->m_title, QStringLiteral("Selected"));
  QVERIFY(moved->m_id.isEmpty());
  QCOMPARE(after.groupForList(*moved), fallback);
  QCOMPARE(m_controller->getSelectedItemKey(), moved->m_items.first().m_key);
  QVERIFY(moved->m_items.first().m_id.isEmpty());
  QCOMPARE(after.text(moved->m_range), selectedBody);
  QCOMPARE(after.text(list("owned").m_range), ownedBody);
  QCOMPARE(after.groupForList(list("owned")), fallback);
  QCOMPARE(list("owned")
               .m_properties.value(QStringLiteral("extra"))
               .toObject()
               .value(QStringLiteral("x"))
               .toInt(),
           1);
  QCOMPARE(item("owned-task").m_properties.value(QStringLiteral("extra")).toArray().size(), 2);
  QCOMPARE(fallback->m_commentRanges.size(), 1);
  QVERIFY(after.text(fallback->m_commentRanges.first())
              .contains(QStringLiteral("ordinary group comment")));
  QVERIFY(!m_document->toPlainText().contains(QStringLiteral("\"discard\"")));
  for (const auto &entry : after.m_lists) {
    QVERIFY(!after.text(entry.m_range).contains(QStringLiteral("ordinary group comment")));
  }
  const auto result = m_document->toPlainText();
  QVERIFY(!m_controller->ungroupLists(fallback->m_key));
  QCOMPARE(m_document->toPlainText(), result);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), result);
}

void TestTodoViewWindowController::ungroupWithoutDefault() {
  for (const auto &source :
       {QStringLiteral("# Work\n\n## Selected\n\n- [ ] Keep"),
        header() + QStringLiteral(
                       "Office\n======\n<!-- todo-group: {\"id\":\"g\",\"discard\":true} -->\n\n"
                       "<!-- keep group comment -->\n\n"
                       "## Selected\n\n- [ ] Keep\n\n  > Note\n\n  - [x] Step"),
        header() + QStringLiteral("# Before\n<!-- todo-group: {\"id\":\"before\"} -->\n\n"
                                  "Office\n======\n<!-- todo-group: {\"id\":\"g\"} -->\n\n"
                                  "## Selected\n\n- [ ] Keep\n\n"
                                  "# After\n<!-- todo-group: {\"id\":\"after\"} -->")}) {
    QVERIFY(load(source));
    const auto &before = m_controller->getDocument();
    QVERIFY(before.isValid());
    QVERIFY(!before.defaultGroup());
    const auto selected = before.m_lists.first();
    const auto groupKey = before.groupForList(selected)->m_key;
    const auto groupPosition = before.groupForList(selected) - before.m_groups.constData();
    const auto body = before.text(selected.m_range);
    const int groupCount = before.m_groups.size();
    m_controller->setView(TodoViewKind::List, selected.m_key);
    m_controller->setSelectedItemKey(selected.m_items.first().m_key);
    QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
    QVERIFY(m_controller->ungroupLists(groupKey));
    QCOMPARE(changed.count(), 1);
    const auto &after = m_controller->getDocument();
    QVERIFY(after.isValid());
    QVERIFY(!after.findGroup(groupKey));
    QCOMPARE(after.m_groups.size(), groupCount);
    const auto *fallback = after.defaultGroup();
    QVERIFY(fallback && !fallback->m_id.isEmpty());
    QCOMPARE(fallback, &after.m_groups.at(groupPosition));
    QCOMPARE(after.text(after.m_lists.first().m_range), body);
    QCOMPARE(m_controller->getProjection().m_listKey, after.m_lists.first().m_key);
    QCOMPARE(m_controller->getSelectedItemKey(), after.m_lists.first().m_items.first().m_key);
    QVERIFY(after.m_lists.first().m_id.isEmpty());
    QVERIFY(after.m_lists.first().m_items.first().m_id.isEmpty());
    QVERIFY(!m_document->toPlainText().contains(QStringLiteral("======")));
    QVERIFY(!m_document->toPlainText().contains(QStringLiteral("\"discard\"")));
    const auto result = m_document->toPlainText();
    QVERIFY(!result.endsWith(QLatin1Char('\n')));
    m_document->undo();
    QCOMPARE(m_document->toPlainText(), source);
    QVERIFY(!m_document->isUndoAvailable());
    m_document->redo();
    QCOMPARE(m_document->toPlainText(), result);
  }
  for (const auto &source : {QStringLiteral("# Empty"),
                             QStringLiteral("Empty\n=====\n<!-- todo-group: {\"id\":\"g\"} -->")}) {
    QVERIFY(load(source));
    const auto groupKey = m_controller->getDocument().m_groups.first().m_key;
    QVERIFY(m_controller->ungroupLists(groupKey));
    QVERIFY(m_controller->getDocument().m_groups.isEmpty());
    QVERIFY(m_controller->getDocument().m_lists.isEmpty());
    m_document->undo();
    QCOMPARE(m_document->toPlainText(), source);
  }
  const auto commentOnly = QStringLiteral(
      "Comments\n========\n<!-- todo-group: {\"id\":\"g\",\"discard\":true} -->\n\n<!-- keep -->");
  QVERIFY(load(commentOnly));
  QVERIFY(m_controller->ungroupLists(key("g")));
  const auto *fallback = m_controller->getDocument().defaultGroup();
  QVERIFY(fallback && fallback->m_listCount == 0);
  QCOMPARE(fallback->m_commentRanges.size(), 1);
  QVERIFY(m_document->toPlainText().endsWith(QStringLiteral("<!-- keep -->")));
  QVERIFY(!m_document->toPlainText().contains(QStringLiteral("\"discard\"")));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), commentOnly);
}

void TestTodoViewWindowController::ungroupEmptyIntoDefault() {
  const auto fallback =
      groupedHeader().mid(header().size()) +
      QStringLiteral("## Keep\n<!-- todo-list: {\"id\":\"keep\"} -->\n\n- [ ] Keep task\n\n");
  for (const bool beforeDefault : {false, true}) {
    for (const bool comments : {false, true}) {
      auto custom =
          QStringLiteral("Empty\n=====\n<!-- todo-group: {\"id\":\"g\",\"discard\":true} -->\n\n");
      if (comments) {
        custom += QStringLiteral("<!-- retain ordinary comment -->\n\n");
      }
      const auto source = header() + (beforeDefault ? custom + fallback : fallback + custom);
      QVERIFY(load(source));
      const auto body = m_controller->getDocument().text(list("keep").m_range);
      QVERIFY(m_controller->ungroupLists(key("g")));
      const auto &after = m_controller->getDocument();
      QVERIFY(after.isValid());
      QCOMPARE(after.m_groups.size(), 1);
      QCOMPARE(after.defaultGroup()->m_key, key("defaultgroup"));
      QCOMPARE(after.defaultGroup()->m_commentRanges.size(), comments ? 1 : 0);
      QCOMPARE(after.text(list("keep").m_range), body);
      QVERIFY(!m_document->toPlainText().contains(QStringLiteral("\"discard\"")));
      const auto result = m_document->toPlainText();
      m_document->undo();
      QCOMPARE(m_document->toPlainText(), source);
      QVERIFY(!m_document->isUndoAvailable());
      m_document->redo();
      QCOMPARE(m_document->toPlainText(), result);
    }
  }
}

void TestTodoViewWindowController::duplicatePreservesOwnedData() {
  const auto source =
      header() +
      QStringLiteral("# Work\n<!-- todo-group: {\"id\":\"g\",\"parent\":true} -->\n\n"
                     "<!-- group stays once -->\n\n"
                     "Project [one]\n-------------\n<!-- todo-list: "
                     "{\"id\":\"original\",\"extra\":{\"nested\":[1,2]}} -->\n\n"
                     "> Description **bold**\n>\n> Second paragraph\n\n<!-- list comment -->\n\n"
                     "* [X] First\n  <!-- todo-item: "
                     "{\"id\":\"first\",\"important\":true,\"due\":\"2026-11-11\",\"myDay\":\"2026-"
                     "11-12\",\"tags\":[\"one\",\"two words\"],\"extra\":{\"nested\":true},"
                     "\"createdAt\":\"2020-01-02T03:04:05.6+02:00\"} -->\n\n"
                     "  <!-- task comment -->\n\n  > Keep **note**\n  >\n  > More note\n\n"
                     "  + [X] Done step\n  - [ ] Open step\n\n"
                     "- [ ] Second\n  <!-- todo-item: {\"extra\":[1,2]} -->\n\n"
                     "- [ ] Third\n\n"
                     "## Sibling\n<!-- todo-list: {\"id\":\"sibling\"} -->\n\n"
                     "# Other\n<!-- todo-group: {\"id\":\"other\"} -->\n\n"
                     "## Unrelated\n<!-- todo-list: {\"id\":\"unrelated\"} -->");
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  const auto original = list("original");
  const auto originalBody = m_controller->getDocument().text(original.m_range);
  const auto otherBody = m_controller->getDocument().text(
      m_controller->getDocument().findGroup(key("other"))->m_range);
  m_controller->setView(TodoViewKind::List, original.m_key);
  m_controller->setSelectedItemKey(original.m_items.at(1).m_key);
  const auto selection = m_controller->getSelectedItemKey();
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  QString created = QStringLiteral("old-output");
  const auto beforeDuplication = QDateTime::currentDateTimeUtc();
  QVERIFY(m_controller->duplicateList(original.m_key, &created));
  const auto afterDuplication = QDateTime::currentDateTimeUtc();
  QCOMPARE(changed.count(), 1);
  const auto &after = m_controller->getDocument();
  const auto *copy = after.findList(created);
  QVERIFY(copy && !copy->m_id.isEmpty());
  QCOMPARE(after.m_lists.at(1).m_key, created);
  QCOMPARE(after.m_lists.at(2).m_key, key("sibling"));
  QCOMPARE(after.groupForList(*copy)->m_key, key("g"));
  QCOMPARE(copy->m_title, QStringLiteral("Project [one] (copy)"));
  QVERIFY(copy->m_setext);
  QCOMPARE(copy->m_description, original.m_description);
  QCOMPARE(copy->m_properties.value(QStringLiteral("extra")),
           original.m_properties.value(QStringLiteral("extra")));
  QCOMPARE(after.text(list("original").m_range), originalBody);
  QCOMPARE(after.text(after.findGroup(key("other"))->m_range), otherBody);
  QCOMPARE(m_document->toPlainText().count(QStringLiteral("group stays once")), 1);
  QCOMPARE(m_document->toPlainText().count(QStringLiteral("list comment")), 2);
  QCOMPARE(m_document->toPlainText().count(QStringLiteral("task comment")), 2);
  QCOMPARE(m_controller->getProjection().m_listKey, original.m_key);
  QCOMPARE(m_controller->getSelectedItemKey(), selection);
  QCOMPARE(copy->m_items.size(), original.m_items.size());
  QSet<QString> ids{QStringLiteral("g"),         QStringLiteral("other"),
                    QStringLiteral("original"),  QStringLiteral("sibling"),
                    QStringLiteral("unrelated"), QStringLiteral("first")};
  QVERIFY(!ids.contains(copy->m_id));
  ids.insert(copy->m_id);
  const auto duplicatedAt = copy->m_items.first().m_createdAt;
  QVERIFY(duplicatedAt.isValid());
  QVERIFY(duplicatedAt >= beforeDuplication && duplicatedAt <= afterDuplication);
  QVERIFY(duplicatedAt != original.m_items.first().m_createdAt);
  for (int i = 0; i < original.m_items.size(); ++i) {
    const auto &a = original.m_items.at(i);
    const auto &b = copy->m_items.at(i);
    QVERIFY(!b.m_id.isEmpty() && !ids.contains(b.m_id));
    ids.insert(b.m_id);
    QCOMPARE(b.m_createdAt, duplicatedAt);
    QCOMPARE(b.m_properties.value(QStringLiteral("createdAt")).toString(),
             duplicatedAt.toUTC().toString(Qt::ISODateWithMs));
    auto properties = b.m_properties;
    properties.remove(QStringLiteral("id"));
    properties.remove(QStringLiteral("createdAt"));
    auto expected = a.m_properties;
    expected.remove(QStringLiteral("id"));
    expected.remove(QStringLiteral("createdAt"));
    QCOMPARE(properties, expected);
    QCOMPARE(b.m_fields.m_title, a.m_fields.m_title);
    QCOMPARE(b.m_fields.m_checked, a.m_fields.m_checked);
    QCOMPARE(b.m_fields.m_important, a.m_fields.m_important);
    QCOMPARE(b.m_fields.m_due, a.m_fields.m_due);
    QCOMPARE(b.m_fields.m_myDay, a.m_fields.m_myDay);
    QCOMPARE(b.m_fields.m_tags, a.m_fields.m_tags);
    QCOMPARE(b.m_fields.m_note, a.m_fields.m_note);
    QCOMPARE(stepTitles(b), stepTitles(a));
    for (int j = 0; j < a.m_steps.size(); ++j) {
      QCOMPARE(b.m_steps.at(j).m_checked, a.m_steps.at(j).m_checked);
    }
  }
  const auto result = m_document->toPlainText();
  const auto copyTask = copy->m_items.first().m_key;
  auto fields = copy->m_items.first().m_fields;
  fields.m_title = QStringLiteral("Independent");
  QVERIFY(m_controller->updateTask(copyTask, fields));
  QCOMPARE(item("first").m_fields.m_title, QStringLiteral("First"));
  QCOMPARE(m_controller->getDocument().findItem(copyTask)->m_fields.m_title, fields.m_title);
  QCOMPARE(m_controller->getDocument().findItem(copyTask)->m_createdAt, duplicatedAt);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), result);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), result);
  QVERIFY(m_controller->getDocument().findList(created));
  QCOMPARE(m_controller->getDocument().findItem(copyTask)->m_createdAt, duplicatedAt);
}

void TestTodoViewWindowController::duplicateIdlessOwnersAndEof() {
  for (const auto &source : {QStringLiteral("##"), QStringLiteral("## Empty"),
                             QStringLiteral("List\n----\n<!-- todo-list: {\"extra\":true} -->\n\n"
                                            "- [ ] Item\n  <!-- todo-item: {\"extra\":17} -->"),
                             QStringLiteral("# Parent\n\n## List\n\n- [ ] First\n\n- [x] Last")}) {
    QVERIFY(load(source));
    const auto original = m_controller->getDocument().m_lists.first();
    const auto originalBody = m_controller->getDocument().text(original.m_range);
    m_controller->setView(TodoViewKind::List, original.m_key);
    if (!original.m_items.isEmpty()) {
      m_controller->setSelectedItemKey(original.m_items.last().m_key);
    }
    QString created;
    const auto beforeDuplication = QDateTime::currentDateTimeUtc();
    QVERIFY(m_controller->duplicateList(original.m_key, &created));
    const auto afterDuplication = QDateTime::currentDateTimeUtc();
    const auto &after = m_controller->getDocument();
    QVERIFY(after.isValid());
    QCOMPARE(after.m_groups.size(), 1);
    QVERIFY(!after.m_groups.first().m_id.isEmpty());
    QCOMPARE(after.m_lists.size(), 2);
    const auto &retained = after.m_lists.first();
    const auto *copy = after.findList(created);
    QVERIFY(copy && !copy->m_id.isEmpty());
    QCOMPARE(copy->m_title,
             (original.m_title.isEmpty() ? QStringLiteral("Untitled list") : original.m_title) +
                 QStringLiteral(" (copy)"));
    QVERIFY(retained.m_id.isEmpty());
    QCOMPARE(retained.m_properties, original.m_properties);
    QCOMPARE(after.text(retained.m_range).trimmed(), originalBody.trimmed());
    QCOMPARE(m_controller->getProjection().m_listKey, retained.m_key);
    QCOMPARE(copy->m_items.size(), original.m_items.size());
    for (int i = 0; i < retained.m_items.size(); ++i) {
      QVERIFY(retained.m_items.at(i).m_id.isEmpty());
      QVERIFY(!copy->m_items.at(i).m_id.isEmpty());
      QVERIFY(!retained.m_items.at(i).m_createdAt.isValid());
      QVERIFY(!retained.m_items.at(i).m_properties.contains(QStringLiteral("createdAt")));
      const auto &copied = copy->m_items.at(i);
      QVERIFY(copied.m_createdAt.isValid());
      QVERIFY(copied.m_createdAt >= beforeDuplication && copied.m_createdAt <= afterDuplication);
      QCOMPARE(copied.m_createdAt, copy->m_items.first().m_createdAt);
      QCOMPARE(copied.m_properties.value(QStringLiteral("createdAt")).toString(),
               copied.m_createdAt.toUTC().toString(Qt::ISODateWithMs));
      QCOMPARE(copy->m_items.at(i).m_fields.m_checked, retained.m_items.at(i).m_fields.m_checked);
      QCOMPARE(copy->m_items.at(i).m_properties.value(QStringLiteral("extra")),
               retained.m_items.at(i).m_properties.value(QStringLiteral("extra")));
    }
    if (!retained.m_items.isEmpty()) {
      QCOMPARE(m_controller->getSelectedItemKey(), retained.m_items.last().m_key);
    }
    const auto result = m_document->toPlainText();
    m_document->undo();
    QCOMPARE(m_document->toPlainText(), source);
    QVERIFY(!m_document->isUndoAvailable());
    m_document->redo();
    QCOMPARE(m_document->toPlainText(), result);
  }
}

void TestTodoViewWindowController::groupAndDuplicateAdmission() {
  const auto source =
      header() + QStringLiteral("# Work\n<!-- todo-group: {\"id\":\"g\"} -->\n\n"
                                "## List\n<!-- todo-list: {\"id\":\"list\"} -->\n\n- [ ] Task");
  for (const bool readOnly : {false, true}) {
    QVERIFY(load(source, readOnly));
    if (!readOnly) {
      m_controller->setEditable(false);
    }
    QString created = QStringLiteral("must-clear");
    QVERIFY(!m_controller->duplicateList(key("list"), &created));
    QVERIFY(created.isEmpty());
    QVERIFY(!m_controller->ungroupLists(key("g")));
    QCOMPARE(m_document->toPlainText(), source);
    QVERIFY(!m_document->isUndoAvailable());
  }
  QVERIFY(load(source));
  QString created = QStringLiteral("must-clear");
  QVERIFY(!m_controller->duplicateList(key("missing"), &created));
  QVERIFY(created.isEmpty());
  QVERIFY(!m_controller->ungroupLists(key("missing")));
  m_controller->setSourceRefreshEnabled(false);
  QTextCursor cursor(m_document.get());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral(" changed"));
  const auto changed = m_document->toPlainText();
  created = QStringLiteral("must-clear");
  QVERIFY(!m_controller->duplicateList(key("list"), &created));
  QVERIFY(created.isEmpty());
  QVERIFY(!m_controller->ungroupLists(key("g")));
  QCOMPARE(m_document->toPlainText(), changed);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  QVERIFY(load(source + QStringLiteral("\n\nUnsupported paragraph")));
  QVERIFY(!m_controller->getDocument().isValid());
  created = QStringLiteral("must-clear");
  QVERIFY(!m_controller->duplicateList(key("list"), &created));
  QVERIFY(created.isEmpty());
  QVERIFY(!m_controller->ungroupLists(key("g")));
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::implicitGroupMoveRemapsChildren() {
  const auto source = QStringLiteral("## Inbox\n\n- [ ] Selected\n\n"
                                     "# Other\n\n## Work\n\n- [ ] Other task");
  QVERIFY(load(source));
  const auto *group = m_controller->getDocument().defaultGroup();
  QVERIFY(group && group->m_implicit);
  const auto originalGroup = group->m_key;
  const auto &first = m_controller->getDocument().m_lists.first();
  m_controller->setView(TodoViewKind::List, first.m_key);
  m_controller->setSelectedItemKey(first.m_items.first().m_key);
  m_controller->setSelectedGroupKey(originalGroup);
  m_controller->setGroupExpanded(originalGroup, false);
  QVERIFY(m_controller->moveGroup(originalGroup, {}));
  const auto &document = m_controller->getDocument();
  const auto *movedGroup = document.defaultGroup();
  QVERIFY(movedGroup && !movedGroup->m_implicit);
  QCOMPARE(document.m_groups.last().m_key, movedGroup->m_key);
  const auto &movedList = document.m_lists.at(movedGroup->m_firstList);
  QCOMPARE(movedList.m_title, QStringLiteral("Inbox"));
  QCOMPARE(m_controller->getProjection().m_listKey, movedList.m_key);
  QCOMPARE(m_controller->getSelectedItemKey(), movedList.m_items.first().m_key);
  QVERIFY(movedList.m_items.first().m_id.isEmpty());
  QCOMPARE(movedList.m_items.first().m_fields.m_title, QStringLiteral("Selected"));
  QCOMPARE(m_controller->getProjection().m_groupKey, movedGroup->m_key);
  QVERIFY(m_controller->getProjection().m_collapsedGroups.contains(movedGroup->m_key));
  QVERIFY(!m_controller->getProjection().m_collapsedGroups.contains(originalGroup));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
}

void TestTodoViewWindowController::crossGroupListMoveBoundaries() {
  QVERIFY(load(QStringLiteral("## First\n\n- [ ] A\n\n## Stay\n\n- [ ] B\n\n"
                              "# Work\n\n## Last\n\n- [ ] C")));
  const auto firstKey = m_controller->getDocument().m_lists.first().m_key;
  const auto targetGroup = m_controller->getDocument().m_groups.last().m_key;
  const auto beforeKey = m_controller->getDocument().m_lists.last().m_key;
  QVERIFY(m_controller->moveList(firstKey, targetGroup, beforeKey));
  const auto &document = m_controller->getDocument();
  QCOMPARE(document.m_groups.size(), 2);
  QCOMPARE(document.defaultGroup()->m_listCount, 1);
  QCOMPARE(document.m_groups.last().m_listCount, 2);
  QCOMPARE(document.m_lists[0].m_title, QStringLiteral("Stay"));
  QCOMPARE(document.m_lists[1].m_title, QStringLiteral("First"));
  QCOMPARE(document.m_lists[2].m_title, QStringLiteral("Last"));
  QCOMPARE(document.groupForList(document.m_lists[1])->m_key, document.m_groups.last().m_key);
  const auto stay = document.m_lists[0].m_key;
  const auto last = document.m_lists[2].m_key;
  const auto work = document.m_groups.last().m_key;
  const auto unchanged = m_document->toPlainText();
  QVERIFY(!m_controller->moveList(stay, work, stay));
  QVERIFY(!m_controller->moveList(stay, key("missing"), {}));
  QCOMPARE(m_document->toPlainText(), unchanged);
  QVERIFY(m_controller->moveList(last, {}, {}));
  QCOMPARE(m_controller->getDocument().defaultGroup()->m_listCount, 2);
  QCOMPARE(m_controller->getDocument().m_groups.last().m_listCount, 1);
  QCOMPARE(m_controller->getDocument().m_lists[0].m_title, QStringLiteral("Stay"));
  QCOMPARE(m_controller->getDocument().m_lists[1].m_title, QStringLiteral("Last"));
  QCOMPARE(m_controller->getDocument().m_lists[2].m_title, QStringLiteral("First"));
}

void TestTodoViewWindowController::groupProjectionIsPresentational() {
  const auto source =
      header() +
      QStringLiteral("# Office\n<!-- todo-group: {\"id\":\"g\",\"hidden\":\"group-secret\"} -->\n\n"
                     "## Inbox\n<!-- todo-list: {\"id\":\"work\"} -->\n\n"
                     "- [ ] A\n  <!-- todo-item: {\"id\":\"a\"} -->\n\n"
                     "- [ ] B\n  <!-- todo-item: {\"id\":\"b\"} -->\n\n"
                     "# Personal\n<!-- todo-group: {\"id\":\"h\"} -->\n\n"
                     "## Inbox\n<!-- todo-list: {\"id\":\"home\"} -->\n\n"
                     "- [ ] C\n  <!-- todo-item: {\"id\":\"c\"} -->\n");
  QVERIFY(load(source));
  auto *tasks = m_controller->getTaskModel();
  auto *groups = m_controller->getListModel();
  const auto visible = visibleKeys(tasks);
  const int count = groups->rowCount();
  QPersistentModelIndex saved(tasks->index(0));
  std::unique_ptr<QMimeData> drag(tasks->mimeData({tasks->index(0)}));
  m_controller->setGroupExpanded(key("g"), false);
  QVERIFY(saved.isValid());
  QCOMPARE(saved.data(TodoTaskModel::IdRole).toString(), key("a"));
  QVERIFY(tasks->canDropMimeData(drag.get(), Qt::MoveAction, 1, 0, {}));
  QCOMPARE(groups->rowCount(), count);
  const auto groupRow = groups->index(groups->rowForGroup(key("g")));
  QVERIFY(groupRow.data(TodoListModel::GroupRole).toBool());
  QVERIFY(!groupRow.data(TodoListModel::ExpandedRole).toBool());
  QCOMPARE(groupRow.data(TodoListModel::ListCountRole).toInt(), 1);
  QCOMPARE(groupRow.data(TodoListModel::IncompleteCountRole).toInt(), 2);
  QCOMPARE(visibleKeys(tasks), visible);
  m_controller->setSelectedGroupKey(key("h"));
  QCOMPARE(visibleKeys(tasks), visible);
  m_controller->setSearch(QStringLiteral("Office"));
  QCOMPARE(visibleKeys(tasks), QStringList({key("a"), key("b")}));
  m_controller->setGroupExpanded(key("g"), true);
  QCOMPARE(visibleKeys(tasks), QStringList({key("a"), key("b")}));
  m_controller->setSearch(QStringLiteral("group-secret"));
  QVERIFY(visibleKeys(tasks).isEmpty());
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  QVERIFY(load(source, true));
  m_controller->setSelectedGroupKey(key("h"));
  m_controller->setGroupExpanded(key("g"), false);
  QVERIFY(m_controller->getProjection().m_collapsedGroups.contains(key("g")));
  QVERIFY(!m_controller->addGroup(QStringLiteral("Blocked")));
  QVERIFY(!m_controller->renameGroup(key("g"), QStringLiteral("Blocked")));
  QVERIFY(!m_controller->ungroupLists(key("g")));
  QVERIFY(!m_controller->moveGroup(key("g"), {}));
  QVERIFY(!m_controller->moveList(key("work"), key("h"), {}));
  QCOMPARE(m_document->toPlainText(), source);
}

void TestTodoViewWindowController::rootListProjection_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<int>("defaultIndex");
  QTest::addColumn<bool>("implicit");
  QTest::addColumn<int>("rootCount");
  const auto roots =
      QStringLiteral("## Inbox\n<!-- todo-list: {\"id\":\"root\"} -->\n\n"
                     "- [ ] Root task\n  <!-- todo-item: {\"id\":\"root-task\",\"important\":true,"
                     "\"due\":\"2026-11-12\",\"myDay\":\"2026-11-12\"} -->\n\n"
                     "- [x] Finished\n  <!-- todo-item: {\"id\":\"finished\"} -->\n\n"
                     "## Spare\n<!-- todo-list: {\"id\":\"spare\"} -->\n\n"
                     "- [ ] Spare task\n  <!-- todo-item: {\"id\":\"spare-task\"} -->\n\n");
  const auto internal = QStringLiteral(
      "# Internal archive\n<!-- todo-group: {\"id\":\"internal\",\"default\":true} -->\n\n");
  const auto office =
      QStringLiteral("# Office\n<!-- todo-group: {\"id\":\"office\"} -->\n\n"
                     "## Projects\n<!-- todo-list: {\"id\":\"office-list\"} -->\n\n"
                     "- [ ] Office task\n  <!-- todo-item: {\"id\":\"office-task\"} -->\n\n");
  const auto namedDefault =
      QStringLiteral("# Default\n<!-- todo-group: {\"id\":\"named-default\"} -->\n\n"
                     "## Personal\n<!-- todo-list: {\"id\":\"custom-list\"} -->\n\n"
                     "- [ ] Custom task\n  <!-- todo-item: {\"id\":\"custom-task\"} -->\n\n");
  QTest::newRow("implicit") << header() + roots + office + namedDefault << 0 << true << 2;
  QTest::newRow("explicit-before")
      << header() + internal + roots + office + namedDefault << 0 << false << 2;
  QTest::newRow("explicit-middle")
      << header() + office + internal + roots + namedDefault << 1 << false << 2;
  QTest::newRow("explicit-after") << header() + office + namedDefault + internal + roots << 2
                                  << false << 2;
  QTest::newRow("empty-before") << header() + internal + office + namedDefault << 0 << false << 0;
  QTest::newRow("empty-middle") << header() + office + internal + namedDefault << 1 << false << 0;
  QTest::newRow("empty-after") << header() + office + namedDefault + internal << 2 << false << 0;
}

void TestTodoViewWindowController::rootListProjection() {
  QFETCH(QString, source);
  QFETCH(int, defaultIndex);
  QFETCH(bool, implicit);
  QFETCH(int, rootCount);
  QVERIFY(load(source));
  auto *model = m_controller->getListModel();
  auto *tasks = m_controller->getTaskModel();
  const auto &document = m_controller->getDocument();
  const auto *owner = document.defaultGroup();
  QVERIFY(owner);
  QCOMPARE(owner, &document.m_groups.at(defaultIndex));
  QCOMPARE(owner->m_implicit, implicit);
  QCOMPARE(owner->m_listCount, rootCount);
  const auto defaultKey = owner->m_key;
  const auto internalTitle = implicit ? QStringLiteral("Default") : owner->m_title;
  const auto sourceGroups = document.m_groups;
  const auto sourceLists = document.m_lists;
  m_controller->setView(TodoViewKind::All);
  m_controller->setGroupExpanded(defaultKey, false);
  QVERIFY(m_controller->getProjection().m_collapsedGroups.contains(defaultKey));
  QCOMPARE(model->rowForGroup(defaultKey), -1);
  QVERIFY(!model->mimeData({model->index(model->rowForGroup(defaultKey))}));
  QCOMPARE(model->rowCount(), 8 + rootCount);
  for (int row = 0; row < 4; ++row) {
    QVERIFY(!model->groupAt(row));
    QVERIFY(!model->listAt(row));
  }
  const QStringList rootKeys{key("root"), key("spare")};
  for (int offset = 0; offset < rootCount; ++offset) {
    const auto index = model->index(8 + offset);
    QCOMPARE(model->rowForKey(rootKeys.at(offset)), index.row());
    QCOMPARE(index.data(TodoListModel::IdRole).toString(), rootKeys.at(offset));
    QCOMPARE(index.data(TodoListModel::ViewKindRole).value<TodoViewKind>(), TodoViewKind::List);
    QVERIFY(!index.data(TodoListModel::GroupRole).toBool());
    QVERIFY(index.data(TodoListModel::GroupIdRole).toString().isEmpty());
    QVERIFY(index.data(TodoListModel::GroupTitleRole).toString().isEmpty());
    QVERIFY(index.data(TodoListModel::ExpandedRole).toBool());
    QVERIFY(!index.data(TodoListModel::ListCountRole).isValid());
    QCOMPARE(index.data(TodoListModel::IncompleteCountRole).toInt(), 1);
    const auto *actualOwner = model->groupAt(index.row());
    QVERIFY(actualOwner && actualOwner->m_isDefault);
    QCOMPARE(actualOwner->m_key, defaultKey);
    QCOMPARE(actualOwner->m_firstList, owner->m_firstList);
    QCOMPARE(model->listAt(index.row())->m_key, rootKeys.at(offset));
    QVERIFY(!index.data(Qt::AccessibleTextRole).toString().contains(internalTitle));
  }
  QCOMPARE(model->rowForGroup(key("office")), 4);
  QCOMPARE(model->rowForKey(key("office-list")), 5);
  QCOMPARE(model->rowForGroup(key("named-default")), 6);
  QCOMPARE(model->rowForKey(key("custom-list")), 7);
  const auto custom = model->index(model->rowForGroup(key("named-default")));
  QVERIFY(custom.data(TodoListModel::GroupRole).toBool());
  QCOMPARE(custom.data(TodoListModel::TitleRole).toString(), QStringLiteral("Default"));
  QCOMPARE(custom.data(TodoListModel::ListCountRole).toInt(), 1);
  QCOMPARE(custom.data(TodoListModel::IncompleteCountRole).toInt(), 1);
  QCOMPARE(custom.data(TodoListModel::GroupIdRole).toString(), key("named-default"));
  QVERIFY(!model->groupAt(custom.row())->m_isDefault);
  const auto customTask = tasks->index(tasks->rowForKey(key("custom-task")));
  QCOMPARE(customTask.data(TodoTaskModel::SourceGroupNameRole).toString(),
           QStringLiteral("Default"));
  QCOMPARE(customTask.data(TodoTaskModel::SourceGroupIdRole).toString(), key("named-default"));
  QVERIFY(customTask.data(Qt::AccessibleTextRole)
              .toString()
              .contains(QStringLiteral("Group: Default")));
  QCOMPARE(model->index(3).data(TodoListModel::IncompleteCountRole).toInt(), rootCount + 2);
  for (int row = 0; row < 3; ++row) {
    QCOMPARE(model->index(row).data(TodoListModel::IncompleteCountRole).toInt(), rootCount ? 1 : 0);
  }
  QCOMPARE(tasks->getActiveCount(), rootCount + 2);
  QCOMPARE(tasks->getCompletedCount(), rootCount ? 1 : 0);
  if (rootCount) {
    const auto rootTask = tasks->index(tasks->rowForKey(key("root-task")));
    QCOMPARE(rootTask.data(TodoTaskModel::SourceGroupIdRole).toString(), defaultKey);
    QVERIFY(rootTask.data(TodoTaskModel::SourceGroupNameRole).toString().isEmpty());
    for (int role : {Qt::AccessibleTextRole, Qt::ToolTipRole}) {
      const auto label = rootTask.data(role).toString();
      QVERIFY(!label.contains(QStringLiteral("Group:")));
      QVERIFY(!label.contains(internalTitle));
      QVERIFY(label.contains(QStringLiteral("List: Inbox")));
    }
  }
  // Rebuilding a filtered snapshot must also ignore the latent Default collapse key.
  m_controller->setSearch(internalTitle);
  QCOMPARE(visibleKeys(tasks), implicit ? QStringList{key("custom-task")} : QStringList());
  QCOMPARE(model->index(3).data(TodoListModel::IncompleteCountRole).toInt(), implicit ? 1 : 0);
  m_controller->setSearch({});
  for (int offset = 0; offset < rootCount; ++offset) {
    QVERIFY(model->index(4 + offset).data(TodoListModel::ExpandedRole).toBool());
  }
  for (int index = 0; index < sourceGroups.size(); ++index) {
    QCOMPARE(m_controller->getDocument().m_groups.at(index).m_key, sourceGroups.at(index).m_key);
  }
  for (int index = 0; index < sourceLists.size(); ++index) {
    QCOMPARE(m_controller->getDocument().m_lists.at(index).m_key, sourceLists.at(index).m_key);
    QCOMPARE(m_controller->getDocument().m_lists.at(index).m_groupIndex,
             sourceLists.at(index).m_groupIndex);
  }
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::internalGroupSearch() {
  const auto source =
      groupedHeader() +
      QStringLiteral("## Inbox\n<!-- todo-list: {\"id\":\"inbox\"} -->\n\n"
                     "- [ ] Unrelated\n  <!-- todo-item: {\"id\":\"unrelated\"} -->\n\n"
                     "- [ ] Default task\n  <!-- todo-item: {\"id\":\"title\"} -->\n\n"
                     "- [ ] Noted\n  <!-- todo-item: {\"id\":\"note\"} -->\n\n  > Default note\n\n"
                     "## Default list\n<!-- todo-list: {\"id\":\"list-match\"} -->\n\n"
                     "- [ ] List content\n  <!-- todo-item: {\"id\":\"list-task\"} -->\n\n"
                     "# Default\n<!-- todo-group: {\"id\":\"custom\"} -->\n\n"
                     "## Custom list\n<!-- todo-list: {\"id\":\"custom-list\"} -->\n\n"
                     "- [ ] Custom content\n  <!-- todo-item: {\"id\":\"custom-task\"} -->\n");
  QVERIFY(load(source));
  auto *lists = m_controller->getListModel();
  m_controller->setSearch(QStringLiteral("DEFAULT"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("title"), key("note"), key("list-task"), key("custom-task")}));
  QCOMPARE(lists->index(3).data(TodoListModel::IncompleteCountRole).toInt(), 4);
  QCOMPARE(
      lists->index(lists->rowForKey(key("inbox"))).data(TodoListModel::IncompleteCountRole).toInt(),
      2);
  QCOMPARE(lists->index(lists->rowForGroup(key("custom")))
               .data(TodoListModel::IncompleteCountRole)
               .toInt(),
           1);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::rootListDropBoundaries_data() {
  QTest::addColumn<QString>("source");
  const auto alpha =
      QStringLiteral("# Alpha\n\n## One\n<!-- todo-list: {\"id\":\"one\"} -->\n\n- [ ] First\n\n");
  const auto beta =
      QStringLiteral("# Beta\n\n## Two\n<!-- todo-list: {\"id\":\"two\"} -->\n\n- [ ] Second\n\n");
  const auto internal = QStringLiteral(
      "# Renamed internal\n<!-- todo-group: {\"id\":\"internal\",\"default\":true} -->\n\n");
  const auto roots =
      QStringLiteral("## Root\n<!-- todo-list: {\"id\":\"root\"} -->\n\n- [ ] Root task\n\n"
                     "## Spare\n<!-- todo-list: {\"id\":\"spare\"} -->\n\n- [ ] Spare task\n\n");
  QTest::newRow("implicit") << header() + roots + alpha + beta;
  QTest::newRow("before") << header() + internal + roots + alpha + beta;
  QTest::newRow("middle") << header() + alpha + internal + roots + beta;
  QTest::newRow("after") << header() + alpha + beta + internal + roots;
  QTest::newRow("empty-before") << header() + internal + alpha + beta;
  QTest::newRow("empty-middle") << header() + alpha + internal + beta;
  QTest::newRow("empty-after") << header() + alpha + beta + internal;
  QTest::newRow("absent") << header() + alpha + beta;
}

void TestTodoViewWindowController::rootListDropBoundaries() {
  QFETCH(QString, source);
  QVERIFY(load(source));
  auto *model = m_controller->getListModel();
  const auto alpha = m_controller->getDocument().groupForList(list("one"))->m_key;
  const auto beta = m_controller->getDocument().groupForList(list("two"))->m_key;
  const auto *initialDefault = m_controller->getDocument().defaultGroup();
  const int rootCount = initialDefault ? initialDefault->m_listCount : 0;
  QSignalSpy groupMoves(model, &TodoListModel::groupMoveRequested);
  QSignalSpy listMoves(model, &TodoListModel::listMoveRequested);
  std::unique_ptr<QMimeData> mime(model->mimeData({model->index(model->rowForGroup(alpha))}));
  QVERIFY(mime);
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(alpha), 0, {}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(beta), 0, {}));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                               model->index(model->rowForGroup(beta))));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("one")), 0, {}));
  if (rootCount) {
    QVERIFY(
        !model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("spare")), 0, {}));
    QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                                 model->index(model->rowForKey(key("root")))));
  }
  mime.reset(model->mimeData({model->index(model->rowForGroup(beta))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowCount(), 0, {}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  QCOMPARE(groupMoves.count(), 0);
  QCOMPARE(listMoves.count(), 0);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());

  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(alpha), 0, {}));
  QCOMPARE(groupMoves.count(), 1);
  QCOMPARE(model->groupAt(4)->m_title, QStringLiteral("Beta"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  mime.reset(model->mimeData({model->index(model->rowForGroup(alpha))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowCount(), 0, {}));
  QCOMPARE(model->groupAt(4)->m_title, QStringLiteral("Beta"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);

  if (rootCount) {
    mime.reset(model->mimeData({model->index(model->rowForGroup(alpha))}));
    QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("root")), 0, {}));
    QCOMPARE(model->groupAt(4)->m_title, QStringLiteral("Beta"));
    QCOMPARE(model->groupAt(6)->m_title, QStringLiteral("Alpha"));
    QCOMPARE(model->rowForKey(key("root")), 8);
    const auto movedAlpha = m_controller->getDocument().groupForList(list("one"))->m_key;
    mime.reset(model->mimeData({model->index(model->rowForGroup(movedAlpha))}));
    const auto undoSteps = m_document->availableUndoSteps();
    QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("root")), 0, {}));
    QCOMPARE(m_document->availableUndoSteps(), undoSteps);
    m_document->undo();
    QCOMPARE(m_document->toPlainText(), source);
  }

  // The first custom header gap always appends to Default, even if it must be created.
  mime.reset(model->mimeData({model->index(model->rowForKey(key("one")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(alpha), 0, {}));
  const auto *owner = m_controller->getDocument().groupForList(list("one"));
  QVERIFY(owner && owner->m_isDefault);
  QCOMPARE(owner->m_listCount, rootCount + 1);
  QCOMPARE(model->rowForKey(key("one")), model->rowCount() - 1);
  QCOMPARE(m_controller->getDocument().m_lists.at(owner->m_firstList + rootCount).m_key,
           key("one"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);

  // A later header gap appends to the previous custom group, skipping physical Default.
  mime.reset(model->mimeData({model->index(model->rowForKey(key("two")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(beta), 0, {}));
  owner = m_controller->getDocument().groupForList(list("two"));
  QVERIFY(owner && !owner->m_isDefault);
  QCOMPARE(owner->m_title, QStringLiteral("Alpha"));
  QCOMPARE(owner->m_listCount, 2);
  QCOMPARE(m_controller->getDocument().m_lists.at(owner->m_firstList + 1).m_key, key("two"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);

  // On-header drops append, whereas on/before a hidden child uses its actual source index.
  m_controller->setGroupExpanded(beta, false);
  mime.reset(model->mimeData({model->index(model->rowForKey(key("one")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                              model->index(model->rowForGroup(beta))));
  owner = m_controller->getDocument().groupForList(list("one"));
  QCOMPARE(owner->m_title, QStringLiteral("Beta"));
  QCOMPARE(m_controller->getDocument().m_lists.at(owner->m_firstList + 1).m_key, key("one"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  for (bool onItem : {false, true}) {
    m_controller->setGroupExpanded(beta, false);
    const auto target = model->index(model->rowForKey(key("two")));
    QVERIFY(!target.data(TodoListModel::ExpandedRole).toBool());
    mime.reset(model->mimeData({model->index(model->rowForKey(key("one")))}));
    QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, onItem ? -1 : target.row(), 0,
                                onItem ? target : QModelIndex()));
    owner = m_controller->getDocument().groupForList(list("one"));
    QCOMPARE(owner->m_title, QStringLiteral("Beta"));
    QCOMPARE(m_controller->getDocument().m_lists.at(owner->m_firstList).m_key, key("one"));
    QCOMPARE(m_controller->getDocument().m_lists.at(owner->m_firstList + 1).m_key, key("two"));
    m_document->undo();
    QCOMPARE(m_document->toPlainText(), source);
  }

  if (rootCount) {
    for (bool onItem : {false, true}) {
      const auto target = model->index(model->rowForKey(key("root")));
      mime.reset(model->mimeData({model->index(model->rowForKey(key("two")))}));
      QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, onItem ? -1 : target.row(), 0,
                                  onItem ? target : QModelIndex()));
      owner = m_controller->getDocument().groupForList(list("two"));
      QVERIFY(owner && owner->m_isDefault);
      QCOMPARE(owner->m_listCount, rootCount + 1);
      QCOMPARE(model->rowForKey(key("two")) + 1, model->rowForKey(key("root")));
      QCOMPARE(m_controller->getDocument().m_lists.at(owner->m_firstList).m_key, key("two"));
      m_document->undo();
      QCOMPARE(m_document->toPlainText(), source);
    }
  }
  for (int row : {-1, model->rowCount()}) {
    mime.reset(model->mimeData({model->index(model->rowForKey(key("two")))}));
    QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, row, 0, {}));
    owner = m_controller->getDocument().groupForList(list("two"));
    QVERIFY(owner && owner->m_isDefault);
    QCOMPARE(owner->m_listCount, rootCount + 1);
    QCOMPARE(model->rowForKey(key("two")), model->rowCount() - 1);
    m_document->undo();
    QCOMPARE(m_document->toPlainText(), source);
  }
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::groupCreationReturnsActualOwners() {
  QVERIFY(load({}));
  QString firstGroup, tailGroup, firstList, tailList, inserted;
  QVERIFY(m_controller->addGroup(QStringLiteral("First"), &firstGroup));
  QVERIFY(m_controller->addGroup(QStringLiteral("Tail"), &tailGroup));
  QVERIFY(m_controller->addList(QStringLiteral("First list"), firstGroup, &firstList));
  QVERIFY(m_controller->addList(QStringLiteral("Tail list"), tailGroup, &tailList));
  QVERIFY(m_controller->addList(QStringLiteral("Inserted before tail"), firstGroup, &inserted));
  const auto &document = m_controller->getDocument();
  QVERIFY(!document.defaultGroup());
  QCOMPARE(document.groupForList(*document.findList(inserted))->m_key, firstGroup);
  QCOMPARE(document.m_lists.at(1).m_key, inserted);
  QCOMPARE(document.m_lists.last().m_key, tailList);
  QString defaultList;
  QVERIFY(m_controller->addList(QStringLiteral("No chosen group"), {}, &defaultList));
  const auto *fallback = m_controller->getDocument().defaultGroup();
  QVERIFY(fallback && !fallback->m_implicit);
  const auto fallbackKey = fallback->m_key;
  QCOMPARE(m_controller->getDocument()
               .groupForList(*m_controller->getDocument().findList(defaultList))
               ->m_key,
           fallbackKey);
  QVERIFY(m_controller->renameGroup(fallbackKey, QStringLiteral("Miscellaneous")));
  QVERIFY(m_controller->addList(QStringLiteral("Still default"), {}, &defaultList));
  QCOMPARE(m_controller->getDocument().m_groups.size(), 3);
  QCOMPARE(m_controller->getDocument().defaultGroup()->m_key, fallbackKey);
  QCOMPARE(m_controller->getDocument()
               .groupForList(*m_controller->getDocument().findList(defaultList))
               ->m_key,
           fallbackKey);
  const auto unchanged = m_document->toPlainText();
  QString failed = QStringLiteral("old result");
  QVERIFY(!m_controller->addList(QStringLiteral("Invalid"), key("missing"), &failed));
  QVERIFY(failed.isEmpty());
  failed = QStringLiteral("old result");
  QVERIFY(!m_controller->addGroup(QStringLiteral("\n"), &failed));
  QVERIFY(failed.isEmpty());
  QCOMPARE(m_document->toPlainText(), unchanged);
}

void TestTodoViewWindowController::listCommands() {
  QVERIFY(
      load(groupedHeader() +
           QStringLiteral(
               "## Work\n<!-- todo-list: "
               "{\"id\":\"work\",\"number\":-1e-9,\"extra\":{\"keep\":true}} -->\n\n- [ ] A\n")));
  QVERIFY(m_controller->renameList(key("work"), QStringLiteral("Work # [new]")));
  QCOMPARE(list("work").m_title, QStringLiteral("Work # [new]"));
  QCOMPARE(list("work").m_id, QStringLiteral("work"));
  QCOMPARE(list("work").m_properties.value(QStringLiteral("number")).toDouble(), -1e-9);
  QVERIFY(m_controller->setListDescription(key("work"),
                                           QStringLiteral("Description\n\n- [ ] not a task")));
  QCOMPARE(list("work").m_description, QStringLiteral("Description\n\n- [ ] not a task"));
  QCOMPARE(list("work").m_items.size(), 1);
  QVERIFY(m_controller->addList(QStringLiteral("Work # [new]")));
  QCOMPARE(m_controller->getDocument().m_lists.size(), 2);
  const auto addedKey = m_controller->getDocument().m_lists.last().m_key;
  QVERIFY(m_controller->moveList(addedKey, {}, key("work")));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_key, addedKey);
  QVERIFY(m_controller->moveList(addedKey, {}, {}));
  QCOMPARE(m_controller->getDocument().m_lists.last().m_key, addedKey);
  const auto beforeRemoval = m_document->toPlainText();
  QVERIFY(m_controller->removeList(key("work")));
  QCOMPARE(m_controller->getDocument().m_lists.size(), 1);
  QVERIFY(!m_document->toPlainText().contains(QStringLiteral("Description")));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeRemoval);
  QVERIFY(list("work")
              .m_properties.value(QStringLiteral("extra"))
              .toObject()
              .value(QStringLiteral("keep"))
              .toBool());
  QVERIFY(m_controller->setListDescription(key("work"), {}));
  QVERIFY(!list("work").m_descriptionRange.isValid());
}

void TestTodoViewWindowController::setextAndEmptyTitles() {
  QVERIFY(load(QStringLiteral("Old title\n---------\n\n- [ ] task\n  - [ ] step")));
  const auto originalKey = m_controller->getDocument().m_lists.first().m_key;
  QVERIFY(m_controller->renameList(originalKey, QStringLiteral("New *plain*")));
  const auto listKey = m_controller->getDocument().m_lists.first().m_key;
  QVERIFY(m_document->toPlainText().contains(QStringLiteral("New \\*plain\\*\n---------\n")));
  QVERIFY(m_controller->renameList(listKey, {}));
  QVERIFY(m_controller->getDocument().isValid());
  QVERIFY(!m_document->toPlainText().contains(QStringLiteral("---------")));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_title, QString());
  auto taskKey = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  auto fields = m_controller->getDocument().findItem(taskKey)->m_fields;
  fields.m_title.clear();
  QVERIFY(m_controller->updateTask(taskKey, fields));
  taskKey = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  QVERIFY(m_controller->updateStep(taskKey, 0, {}, false));
  QCOMPARE(m_controller->getDocument().findItem(taskKey)->m_fields.m_title, QString());
  QCOMPARE(m_controller->getDocument().findItem(taskKey)->m_steps.first().m_title, QString());
}

void TestTodoViewWindowController::checkboxChangesOnlyOneCharacter() {
  const QString source =
      groupedHeader() +
      QStringLiteral(
          "## Work\n<!-- todo-list: { \"id\": \"work\" } -->\n\n"
          "+ [X] **Bold**\n  <!-- todo-item: { \"id\": \"a\", \"important\":false, \"tags\":[], "
          "\"number\":-1e-9 } -->\n\n"
          "  > Keep *Markdown*\n\n  * [X] Step\n\n- [ ] End\n  <!-- todo-item: {\"id\":\"b\"} -->");
  QVERIFY(load(source));
  const auto range = item("a").m_checkboxRange;
  auto fields = item("a").m_fields;
  fields.m_checked = false;
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QString expected = source;
  expected.replace(range.m_start, 1, QStringLiteral(" "));
  QCOMPARE(m_document->toPlainText(), expected);
  QVERIFY(!m_document->toPlainText().endsWith(QLatin1Char('\n')));
  QCOMPARE(item("a").m_steps.first().m_checked, true);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
}

void TestTodoViewWindowController::taskFieldsAndUnknownMetadata() {
  QVERIFY(load(
      groupedHeader() +
      QStringLiteral("## Work\n<!-- todo-list: {\"id\":\"work\"} -->\n\n"
                     "- [ ] A\n  <!-- todo-item: "
                     "{\"id\":\"a\",\"important\":false,\"tags\":[],\"negative\":-12,\"exponent\":"
                     "1e-9,\"large\":9007199254740993,\"object\":{\"untouched\":[1,true]},"
                     "\"createdAt\":\"2020-01-02T03:04:05.6+02:00\"} -->")));
  const auto oldMetadata = m_controller->getDocument().text(item("a").m_metadataRange);
  const auto createdAt = item("a").m_createdAt;
  QVERIFY(createdAt.isValid());
  auto fields = item("a").m_fields;
  fields.m_title = QStringLiteral("Title a-->b <tag>");
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QCOMPARE(m_controller->getDocument().text(item("a").m_metadataRange), oldMetadata);
  fields = item("a").m_fields;
  fields.m_important = true;
  fields.m_due = QDate(2026, 11, 20);
  fields.m_myDay = QDate(2026, 11, 12);
  fields.m_tags = {QStringLiteral("a-->b"), QStringLiteral("<tag>"),
                   QString::fromUtf8("\xe4\xb8\xad\xe6\x96\x87")};
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QCOMPARE(item("a").m_id, QStringLiteral("a"));
  QCOMPARE(item("a").m_createdAt, createdAt);
  QCOMPARE(item("a").m_properties.value(QStringLiteral("createdAt")).toString(),
           QStringLiteral("2020-01-02T03:04:05.6+02:00"));
  QCOMPARE(item("a").m_properties.value(QStringLiteral("negative")).toInt(), -12);
  QCOMPARE(item("a").m_properties.value(QStringLiteral("exponent")).toDouble(), 1e-9);
  QCOMPARE(item("a").m_properties.value(QStringLiteral("large")).toInteger(),
           qint64(9007199254740993));
  QCOMPARE(item("a")
               .m_properties.value(QStringLiteral("object"))
               .toObject()
               .value(QStringLiteral("untouched"))
               .toArray()
               .size(),
           2);
  const auto metadata = m_controller->getDocument().text(item("a").m_metadataRange);
  QVERIFY(metadata.contains(QStringLiteral("a\\u002d\\u002d\\u003eb")));
  QVERIFY(metadata.contains(QStringLiteral("\\u003ctag\\u003e")));
  QVERIFY(metadata.contains(QStringLiteral("-12")));
  fields = item("a").m_fields;
  fields.m_important = false;
  fields.m_due = {};
  fields.m_myDay = {};
  fields.m_tags.clear();
  QVERIFY(m_controller->updateTask(key("a"), fields));
  for (const auto &name : {QStringLiteral("important"), QStringLiteral("due"),
                           QStringLiteral("myDay"), QStringLiteral("tags")}) {
    QVERIFY(!item("a").m_properties.contains(name));
  }
  QCOMPARE(item("a").m_createdAt, createdAt);
  QCOMPARE(item("a").m_properties.value(QStringLiteral("createdAt")).toString(),
           QStringLiteral("2020-01-02T03:04:05.6+02:00"));
}

void TestTodoViewWindowController::notesAndTags() {
  QVERIFY(load(projectionSource()));
  const auto originalTitle = m_controller->getDocument().text(item("a").m_titleRange);
  auto fields = item("a").m_fields;
  fields.m_note = QStringLiteral("# A heading\n\n- [ ] quoted checklist\n\n```md\n# Not a "
                                 "list\n```\n\n<!-- todo-item: not metadata -->");
  fields.m_tags = {QStringLiteral(" work "), QStringLiteral("WORK"),
                   QStringLiteral("two words, literal"), QStringLiteral("  "),
                   QStringLiteral("New")};
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QCOMPARE(item("a").m_fields.m_note, fields.m_note);
  QCOMPARE(item("a").m_steps.size(), 2);
  QCOMPARE(item("a").m_fields.m_tags,
           QStringList({QStringLiteral("work"), QStringLiteral("two words, literal"),
                        QStringLiteral("New")}));
  QCOMPARE(m_controller->getDocument().text(item("a").m_titleRange), originalTitle);
  QVERIFY(m_document->toPlainText().contains(QStringLiteral("  > - [ ] quoted checklist")));
  fields = item("a").m_fields;
  fields.m_note.clear();
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QVERIFY(!item("a").m_noteRange.isValid());
  QCOMPARE(item("a").m_steps.size(), 2);
  QVERIFY(
      load(groupedHeader() +
           QStringLiteral("## A\n<!-- todo-list: {\"id\":\"work\"} -->\n- [ ] A\n  <!-- todo-item: "
                          "{\"id\":\"a\",\"tags\":[\" Work \",\"work\",\"WORK\"]} -->\n")));
  fields = item("a").m_fields;
  fields.m_checked = true;
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QCOMPARE(item("a").m_fields.m_tags,
           QStringList({QStringLiteral(" Work "), QStringLiteral("work"), QStringLiteral("WORK")}));
  QVERIFY(!item("a").m_createdAt.isValid());
  QVERIFY(!item("a").m_properties.contains(QStringLiteral("createdAt")));
}

void TestTodoViewWindowController::taskAddRemoveUndo() {
  QVERIFY(load(projectionSource()));
  m_controller->setView(TodoViewKind::All);
  TodoTaskFields fields;
  fields.m_title = QStringLiteral("All-view task");
  QVERIFY(!m_controller->addTask({}, fields));
  const auto beforeCreation = QDateTime::currentDateTimeUtc();
  QVERIFY(m_controller->addTask(key("home"), fields));
  const auto afterCreation = QDateTime::currentDateTimeUtc();
  const auto added = list("home").m_items.last();
  QCOMPARE(added.m_fields.m_title, fields.m_title);
  QVERIFY(added.m_createdAt.isValid());
  QVERIFY(added.m_createdAt >= beforeCreation && added.m_createdAt <= afterCreation);
  QCOMPARE(added.m_properties.value(QStringLiteral("createdAt")).toString(),
           added.m_createdAt.toUTC().toString(Qt::ISODateWithMs));
  const auto beforeDelete = m_document->toPlainText();
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), projectionSource());
  QVERIFY(!m_controller->getDocument().findItem(added.m_key));
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), beforeDelete);
  QCOMPARE(m_controller->getDocument().findItem(added.m_key)->m_createdAt, added.m_createdAt);
  QVERIFY(m_controller->removeTask(added.m_key));
  QVERIFY(!m_controller->getDocument().findItem(added.m_key));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeDelete);
  QCOMPARE(m_controller->getDocument().findItem(added.m_key)->m_createdAt, added.m_createdAt);
  m_document->redo();
  QVERIFY(!m_controller->getDocument().findItem(added.m_key));
  QVERIFY(m_controller->removeTask(key("a")));
  QVERIFY(!m_document->toPlainText().contains(QStringLiteral("Gather figures")));
  QVERIFY(!m_document->toPlainText().contains(QStringLiteral("Needle in a")));
  QCOMPARE(item("b").m_fields.m_title, QStringLiteral("beta"));
}

void TestTodoViewWindowController::stepCommands() {
  QVERIFY(load(groupedHeader() +
               QStringLiteral("## Work\n<!-- todo-list: {\"id\":\"work\"} -->\n\n- [ ] "
                              "A\n  <!-- todo-item: {\"id\":\"a\"} -->")));
  QVERIFY(m_controller->addStep(key("a"), QStringLiteral("First")));
  QVERIFY(m_controller->addStep(key("a"), QStringLiteral("Second")));
  QVERIFY(m_controller->addStep(key("a"), QStringLiteral("Third")));
  QCOMPARE(stepTitles(item("a")), QStringList({QStringLiteral("First"), QStringLiteral("Second"),
                                               QStringLiteral("Third")}));
  QVERIFY(m_controller->updateStep(key("a"), 1, QStringLiteral("Second [plain]"), true));
  QVERIFY(item("a").m_steps[1].m_checked);
  QCOMPARE(item("a").m_steps[1].m_title, QStringLiteral("Second [plain]"));
  QVERIFY(m_controller->moveStep(key("a"), 2, 0));
  QCOMPARE(stepTitles(item("a")), QStringList({QStringLiteral("Third"), QStringLiteral("First"),
                                               QStringLiteral("Second [plain]")}));
  QVERIFY(m_controller->moveStep(key("a"), 0, 2));
  QCOMPARE(stepTitles(item("a")),
           QStringList({QStringLiteral("First"), QStringLiteral("Second [plain]"),
                        QStringLiteral("Third")}));
  const auto beforeDelete = m_document->toPlainText();
  QVERIFY(m_controller->removeStep(key("a"), 1));
  QCOMPARE(stepTitles(item("a")), QStringList({QStringLiteral("First"), QStringLiteral("Third")}));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeDelete);
  QVERIFY(item("a").m_steps[1].m_checked);
  m_document->redo();
  QCOMPARE(item("a").m_steps.size(), 2);
  QVERIFY(m_controller->removeStep(key("a"), 1));
  QVERIFY(m_controller->removeStep(key("a"), 0));
  QVERIFY(item("a").m_steps.isEmpty());
}

void TestTodoViewWindowController::promoteStepPreservesOwnedSource() {
  const auto source =
      groupedHeader() +
      QStringLiteral("## Work\n<!-- todo-list: {\"id\":\"work\",\"extra\":-7} -->\n\n"
                     "> List description\n\n"
                     "- [X] Parent\n"
                     "  <!-- todo-item: {\"id\":\"a\",\"important\":true,"
                     "\"due\":\"2026-11-11\",\"myDay\":\"2026-11-12\","
                     "\"tags\":[\"Work\"],\"hidden\":{\"keep\":true},"
                     "\"createdAt\":\"2020-01-02T03:04:05.6+02:00\"} -->\n\n"
                     "  <!-- Parent comment -->\n\n"
                     "  > Parent **note**\n\n"
                     "  - [x] Done\n"
                     "  - [ ] Promote **bold** \\[literal\\] &amp; text\n"
                     "  - [ ] Remaining\n\n"
                     "- [ ] Sibling\n  <!-- todo-item: {\"id\":\"b\"} -->\n\n"
                     "<!-- List boundary comment -->\n\n"
                     "# Other\n<!-- todo-group: {\"id\":\"other\"} -->\n\n"
                     "## Home\n<!-- todo-list: {\"id\":\"home\"} -->\n\n"
                     "- [ ] Unrelated\n  <!-- todo-item: {\"id\":\"c\"} -->");
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  m_controller->setView(TodoViewKind::All);
  m_controller->setSelectedItemKey(key("a"));
  const auto step = item("a").m_steps[1];
  const int insertion = list("work").m_taskInsertion;
  const auto beforeCreation = QDateTime::currentDateTimeUtc();
  QVERIFY(m_controller->promoteStep(key("a"), 1));
  const auto afterCreation = QDateTime::currentDateTimeUtc();
  QCOMPARE(itemTitles(list("work")),
           QStringList({QStringLiteral("Parent"), QStringLiteral("Sibling"),
                        QStringLiteral("Promote bold [literal] & text")}));
  const auto promoted = list("work").m_items.last();
  QCOMPARE(promoted.m_listKey, key("work"));
  QVERIFY(!QUuid(promoted.m_id).isNull());
  QVERIFY(promoted.m_key != key("a") && promoted.m_key != key("b"));
  QVERIFY(promoted.m_createdAt >= beforeCreation && promoted.m_createdAt <= afterCreation);
  const QJsonObject expectedProperties{
      {QStringLiteral("id"), promoted.m_id},
      {QStringLiteral("createdAt"), promoted.m_createdAt.toUTC().toString(Qt::ISODateWithMs)}};
  QCOMPARE(promoted.m_properties, expectedProperties);
  QVERIFY(!promoted.m_fields.m_checked);
  QVERIFY(!promoted.m_fields.m_important);
  QVERIFY(!promoted.m_fields.m_due.isValid());
  QVERIFY(!promoted.m_fields.m_myDay.isValid());
  QVERIFY(promoted.m_fields.m_tags.isEmpty());
  QVERIFY(promoted.m_fields.m_note.isEmpty());
  QVERIFY(promoted.m_steps.isEmpty());
  QCOMPARE(stepTitles(item("a")),
           QStringList({QStringLiteral("Done"), QStringLiteral("Remaining")}));
  QVERIFY(item("a").m_steps.first().m_checked);
  QCOMPARE(m_controller->getSelectedItemKey(), key("a"));

  auto expected = source;
  const auto block = QStringLiteral("- [ ] ") + TodoDocument::escapeTitle(step.m_title) +
                     QStringLiteral("\n  ") +
                     TodoDocument::encodeMetadata(QStringLiteral("todo-item"), expectedProperties) +
                     QStringLiteral("\n\n");
  expected.insert(insertion, block);
  expected.remove(step.m_range.m_start, step.m_range.length());
  QCOMPARE(m_document->toPlainText(), expected);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), expected);
  QCOMPARE(m_controller->getSelectedItemKey(), key("a"));
  QCOMPARE(m_controller->getDocument().findItem(promoted.m_key)->m_createdAt, promoted.m_createdAt);
}

void TestTodoViewWindowController::promoteStepMaterializesOwners_data() {
  QTest::addColumn<QString>("source");
  QTest::newRow("implicit-at-unterminated-eof")
      << QStringLiteral("## Inbox\n\n- [ ] Parent\n  - [ ] Promote");
  QTest::newRow("implicit-before-next-list")
      << QStringLiteral("## Inbox\n- [ ] Parent\n  - [ ] Promote\n\n"
                        "## Other\n- [ ] Unrelated");
  QTest::newRow("explicit-idless-with-properties")
      << QStringLiteral("# Team\n<!-- todo-group: {\"future\":[1,true]} -->\n"
                        "## Inbox\n<!-- todo-list: {\"future\":null} -->\n"
                        "- [ ] Parent\n  <!-- todo-item: {\"future\":{\"keep\":true}} -->\n"
                        "  - [ ] Promote\n");
}

void TestTodoViewWindowController::promoteStepMaterializesOwners() {
  QFETCH(QString, source);
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  const auto originalList = m_controller->getDocument().m_lists.first();
  const auto originalGroup = *m_controller->getDocument().groupForList(originalList);
  const auto originalParent = originalList.m_items.first();
  m_controller->setView(TodoViewKind::List, originalList.m_key);
  m_controller->setSelectedItemKey(originalParent.m_key);
  QVERIFY(m_controller->promoteStep(originalParent.m_key, 0));
  const auto &snapshot = m_controller->getDocument();
  const auto &owner = snapshot.m_lists.first();
  const auto *group = snapshot.groupForList(owner);
  QVERIFY(group);
  QVERIFY(!group->m_implicit);
  QCOMPARE(group->m_isDefault, originalGroup.m_isDefault);
  QCOMPARE(group->m_title, originalGroup.m_title);
  QVERIFY(!QUuid(group->m_id).isNull());
  QVERIFY(!QUuid(owner.m_id).isNull());
  QCOMPARE(itemTitles(owner), QStringList({QStringLiteral("Parent"), QStringLiteral("Promote")}));
  const auto parent = owner.m_items.first();
  const auto promoted = owner.m_items.last();
  QVERIFY(!QUuid(parent.m_id).isNull());
  QVERIFY(!QUuid(promoted.m_id).isNull());
  QVERIFY(parent.m_id != promoted.m_id);
  QCOMPARE(parent.m_listKey, owner.m_key);
  QCOMPARE(promoted.m_listKey, owner.m_key);
  QVERIFY(parent.m_steps.isEmpty());
  QVERIFY(promoted.m_createdAt.isValid());
  QVERIFY(!parent.m_createdAt.isValid());
  QCOMPARE(m_controller->getSelectedItemKey(), parent.m_key);
  QCOMPARE(m_controller->getProjection().m_listKey, owner.m_key);
  QCOMPARE(m_controller->getProjection().m_groupKey, group->m_key);
  auto groupProperties = originalGroup.m_properties;
  groupProperties.insert(QStringLiteral("id"), group->m_id);
  if (originalGroup.m_implicit) {
    groupProperties.insert(QStringLiteral("default"), true);
  }
  QCOMPARE(group->m_properties, groupProperties);
  auto listProperties = originalList.m_properties;
  listProperties.insert(QStringLiteral("id"), owner.m_id);
  QCOMPARE(owner.m_properties, listProperties);
  auto parentProperties = originalParent.m_properties;
  parentProperties.insert(QStringLiteral("id"), parent.m_id);
  QCOMPARE(parent.m_properties, parentProperties);
  if (snapshot.m_lists.size() > 1) {
    const auto &other = snapshot.m_lists.last();
    QCOMPARE(snapshot.text(other.m_range), QStringLiteral("## Other\n- [ ] Unrelated"));
    QVERIFY(other.m_id.isEmpty());
    QVERIFY(other.m_items.first().m_id.isEmpty());
  }
  const auto promotedSource = m_document->toPlainText();
  const auto reloaded = TodoDocument::parse(promotedSource);
  QVERIFY(reloaded.isValid());
  QCOMPARE(reloaded.findItem(parent.m_key)->m_listKey, owner.m_key);
  QCOMPARE(reloaded.findItem(promoted.m_key)->m_listKey, owner.m_key);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), promotedSource);
}

void TestTodoViewWindowController::promoteStepAdmission() {
  const auto source = projectionSource();
  QVERIFY(load(source));
  QVERIFY(!m_controller->promoteStep(key("a"), 0));
  QVERIFY(!m_controller->promoteStep(key("a"), -1));
  QVERIFY(!m_controller->promoteStep(key("a"), 2));
  QVERIFY(!m_controller->promoteStep(key("missing"), 0));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());

  for (const auto &title : {QString(), QStringLiteral("&#32;&#9;&#32;")}) {
    const auto blankSource = QStringLiteral("## Inbox\n- [ ] Parent\n  - [ ] ") + title;
    QVERIFY(load(blankSource));
    QVERIFY(m_controller->getDocument().isValid());
    const auto parent = m_controller->getDocument().m_lists.first().m_items.first();
    QCOMPARE(parent.m_steps.size(), 1);
    QVERIFY(parent.m_steps.first().m_title.trimmed().isEmpty());
    QVERIFY(!m_controller->promoteStep(parent.m_key, 0));
    QCOMPARE(m_document->toPlainText(), blankSource);
    QVERIFY(!m_document->isModified());
    QVERIFY(!m_document->isUndoAvailable());
  }

  QVERIFY(load(source, true));
  QVERIFY(!m_controller->promoteStep(key("a"), 1));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  QVERIFY(load(source));
  m_controller->setEditable(false);
  QVERIFY(!m_controller->promoteStep(key("a"), 1));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());

  m_controller->setEditable(true);
  m_controller->setSourceRefreshEnabled(false);
  QTextCursor cursor(m_document.get());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n<!-- external -->\n"));
  const auto staleSource = m_document->toPlainText();
  QVERIFY(!m_controller->promoteStep(key("a"), 1));
  QCOMPARE(m_document->toPlainText(), staleSource);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::stepCompletionIsIndependent() {
  QVERIFY(load(projectionSource()));
  QVERIFY(m_controller->updateStep(key("a"), 1, item("a").m_steps[1].m_title, true));
  QVERIFY(!item("a").m_fields.m_checked);
  auto fields = item("a").m_fields;
  fields.m_checked = true;
  QVERIFY(m_controller->updateTask(key("a"), fields));
  QVERIFY(m_controller->updateStep(key("a"), 0, item("a").m_steps[0].m_title, false));
  QVERIFY(item("a").m_fields.m_checked);
  QVERIFY(!item("a").m_steps[0].m_checked);
  QVERIFY(item("a").m_steps[1].m_checked);
}

void TestTodoViewWindowController::movesRetainOwnedBlocks() {
  const auto source = QStringLiteral("<!-- File stays first -->\n\n") + groupedHeader() +
                      QStringLiteral("## A\n<!-- todo-list: {\"id\":\"work\",\"extra\":-7} -->\n\n"
                                     "<!-- List A comment -->\n\n"
                                     "- [ ] One\n  <!-- todo-item: {\"id\":\"a\",\"extra\":1e-9,"
                                     "\"createdAt\":\"2020-01-02T03:04:05.6+02:00\"} -->\n\n"
                                     "  <!-- Task comment -->\n\n  > Note\n\n  + [X] Step\n\n"
                                     "* [ ] Two\n  <!-- todo-item: {\"id\":\"b\"} -->\n\n"
                                     "<!-- Boundary comment -->\n\n"
                                     "+ [ ] Three\n  <!-- todo-item: {\"id\":\"c\"} -->\n\n"
                                     "## B\n<!-- todo-list: {\"id\":\"home\"} -->\n\n"
                                     "- [ ] Four\n  <!-- todo-item: {\"id\":\"d\"} -->");
  QVERIFY(load(source));
  QVERIFY(m_controller->getDocument().isValid());
  QCOMPARE(list("work").m_items.size(), 3);
  const auto owned = m_controller->getDocument().text(item("a").m_range);
  const auto createdAt = item("a").m_createdAt;
  QVERIFY(createdAt.isValid());
  m_controller->setSelectedItemKey(key("a"));
  QVERIFY(m_controller->moveTask(key("a"), key("home"), key("d")));
  QCOMPARE(itemTitles(list("work")), QStringList({QStringLiteral("Two"), QStringLiteral("Three")}));
  QCOMPARE(itemTitles(list("home")), QStringList({QStringLiteral("One"), QStringLiteral("Four")}));
  QCOMPARE(item("a").m_listKey, key("home"));
  QCOMPARE(m_controller->getDocument().text(item("a").m_range).trimmed(), owned.trimmed());
  QCOMPARE(m_controller->getSelectedItemKey(), key("a"));
  QVERIFY(m_controller->getDocument()
              .text(list("work").m_range)
              .contains(QStringLiteral("Boundary comment")));
  QVERIFY(!m_controller->getDocument()
               .text(list("home").m_range)
               .contains(QStringLiteral("Boundary comment")));
  QVERIFY(m_controller->moveTask(key("a"), key("home"), {}));
  QCOMPARE(itemTitles(list("home")), QStringList({QStringLiteral("Four"), QStringLiteral("One")}));
  const auto beforeListMove = m_document->toPlainText();
  QVERIFY(m_controller->moveList(key("home"), {}, key("work")));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_key, key("home"));
  QVERIFY(m_document->toPlainText().startsWith(QStringLiteral("<!-- File stays first -->")));
  QCOMPARE(item("a").m_id, QStringLiteral("a"));
  QCOMPARE(item("a").m_createdAt, createdAt);
  QCOMPARE(item("a").m_properties.value(QStringLiteral("createdAt")).toString(),
           QStringLiteral("2020-01-02T03:04:05.6+02:00"));
  QCOMPARE(list("work").m_properties.value(QStringLiteral("extra")).toInt(), -7);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeListMove);
  QCOMPARE(item("a").m_createdAt, createdAt);
}

void TestTodoViewWindowController::movesAtUnterminatedEof() {
  QVERIFY(load(QStringLiteral("## A\n\n- [ ] One\n\n## B\n\n- [ ] Two")));
  const auto first = m_controller->getDocument().m_lists.first().m_key;
  const auto second = m_controller->getDocument().m_lists.last().m_key;
  QVERIFY(m_controller->moveList(second, {}, first));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_title, QStringLiteral("B"));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_items.first().m_fields.m_title,
           QStringLiteral("Two"));
  QVERIFY(m_controller->getDocument().isValid());
  const auto listA = m_controller->getDocument().m_lists.last().m_key;
  const auto taskB = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  QVERIFY(m_controller->moveTask(taskB, listA, {}));
  QCOMPARE(itemTitles(m_controller->getDocument().m_lists.last()),
           QStringList({QStringLiteral("One"), QStringLiteral("Two")}));
  QVERIFY(!m_controller->getDocument().m_lists.last().m_id.isEmpty());
  QVERIFY(!m_controller->getDocument().m_lists.last().m_items.last().m_id.isEmpty());
  for (const auto &task : m_controller->getDocument().m_lists.last().m_items) {
    QVERIFY(!task.m_createdAt.isValid());
    QVERIFY(!task.m_properties.contains(QStringLiteral("createdAt")));
  }
}

void TestTodoViewWindowController::sourceSelectionRemapping() {
  QVERIFY(
      load(QStringLiteral("## Work\n\n- [ ] First\n\n- [ ] Selected\n\n## Home\n\n- [ ] Other")));
  const auto work = m_controller->getDocument().m_lists.first().m_key;
  const auto selected = m_controller->getDocument().m_lists.first().m_items[1].m_key;
  m_controller->setSelectedItemKey(selected);
  QVERIFY(m_controller->renameList(work, QStringLiteral("Much longer work title")));
  const auto &updated = m_controller->getDocument().m_lists.first();
  QVERIFY(updated.m_key.startsWith(QLatin1String("id:")));
  QCOMPARE(m_controller->getProjection().m_listKey, updated.m_key);
  QCOMPARE(m_controller->getSelectedItemKey(), updated.m_items[1].m_key);
  QVERIFY(m_controller->getSelectedItemKey() != selected);
  auto fields = updated.m_items[1].m_fields;
  fields.m_checked = true;
  QVERIFY(m_controller->updateTask(updated.m_items[1].m_key, fields));
  const auto selectedId = m_controller->getSelectedItemKey();
  QVERIFY(selectedId.startsWith(QLatin1String("id:")));
  QVERIFY(m_controller->getDocument().findItem(selectedId)->m_fields.m_checked);
  m_controller->setCompletedExpanded(true);
  QVERIFY(m_controller->removeTask(selectedId));
  QCOMPARE(
      m_controller->getDocument().findItem(m_controller->getSelectedItemKey())->m_fields.m_title,
      QStringLiteral("First"));
}

void TestTodoViewWindowController::movedListRemapsUnidentifiedTasks() {
  QVERIFY(load(QStringLiteral("## A\n\n- [ ] One\n\n## B\n\n- [ ] Selected\n\n- [ ] Second")));
  const auto a = m_controller->getDocument().m_lists[0].m_key;
  const auto b = m_controller->getDocument().m_lists[1].m_key;
  m_controller->setView(TodoViewKind::List, b);
  m_controller->setSelectedItemKey(m_controller->getDocument().m_lists[1].m_items[0].m_key);
  QVERIFY(m_controller->moveList(b, {}, a));
  const auto &moved = m_controller->getDocument().m_lists.first();
  QCOMPARE(moved.m_title, QStringLiteral("B"));
  QCOMPARE(m_controller->getProjection().m_listKey, moved.m_key);
  QCOMPARE(m_controller->getSelectedItemKey(), moved.m_items.first().m_key);
  QVERIFY(moved.m_items.first().m_id.isEmpty());
  QCOMPARE(moved.m_items.first().m_fields.m_title, QStringLiteral("Selected"));
  const auto selected = m_controller->getSelectedItemKey();
  QVERIFY(m_controller->moveList(moved.m_key, {}, {}));
  const auto &last = m_controller->getDocument().m_lists.last();
  QCOMPARE(m_controller->getSelectedItemKey(), last.m_items.first().m_key);
  QVERIFY(m_controller->getSelectedItemKey() != selected);
}

void TestTodoViewWindowController::invalidCommandsAreAtomic() {
  QVERIFY(load(projectionSource()));
  const auto source = m_document->toPlainText();
  QSignalSpy rejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  TodoTaskFields fields;
  fields.m_title = QStringLiteral("Valid");
  QVERIFY(!m_controller->addList(QStringLiteral(" \t ")));
  QVERIFY(!m_controller->addList(QStringLiteral("Two\nlines")));
  QVERIFY(!m_controller->renameList(key("missing"), QStringLiteral("Name")));
  QVERIFY(!m_controller->setListDescription(key("missing"), QStringLiteral("Note")));
  QVERIFY(!m_controller->removeList(key("missing")));
  QVERIFY(!m_controller->moveList(key("work"), {}, key("missing")));
  QVERIFY(!m_controller->addTask(key("missing"), fields));
  QVERIFY(!m_controller->addTask(key("work"), TodoTaskFields()));
  QVERIFY(!m_controller->updateTask(key("missing"), fields));
  QVERIFY(!m_controller->removeTask(key("missing")));
  QVERIFY(!m_controller->moveTask(key("a"), key("missing"), {}));
  QVERIFY(!m_controller->moveTask(key("a"), key("work"), key("d")));
  QVERIFY(!m_controller->moveTask(key("a"), key("work"), key("missing")));
  QVERIFY(!m_controller->addStep(key("a"), QStringLiteral("  ")));
  QVERIFY(!m_controller->addStep(key("missing"), QStringLiteral("Step")));
  QVERIFY(!m_controller->updateStep(key("a"), -1, QStringLiteral("Step"), true));
  QVERIFY(!m_controller->removeStep(key("a"), 999));
  QVERIFY(!m_controller->moveStep(key("a"), 0, 99));
  QCOMPARE(rejected.count(), 18);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  // A whitespace-only Setext rename would destroy the heading. The tentative
  // parse rejects the whole title/identity/header transaction, not just one edit.
  QVERIFY(load(QStringLiteral("Title\n-----\n\n- [ ] Task")));
  const auto before = m_document->toPlainText();
  QVERIFY(!m_controller->renameList(m_controller->getDocument().m_lists.first().m_key,
                                    QStringLiteral(" ")));
  QCOMPARE(m_document->toPlainText(), before);
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::readOnlyAndUnavailable() {
  QVERIFY(load(projectionSource(), true));
  auto fields = item("a").m_fields;
  fields.m_checked = true;
  QVERIFY(!m_controller->updateTask(key("a"), fields));
  QVERIFY(!(m_controller->getTaskModel()->index(0).flags() & Qt::ItemIsUserCheckable));
  auto *model = m_controller->getTaskModel();
  const auto index = model->index(model->rowForKey(key("a")));
  QVERIFY(index.data(TodoTaskModel::InMyDayRole).toBool());
  QVERIFY(!model->setData(index, false, TodoTaskModel::InMyDayRole));
  QSignalSpy myDayRejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  model->myDayChangeRequested(key("a"), false, model->getRevision());
  QCOMPARE(myDayRejected.count(), 1);
  QCOMPARE(item("a").m_fields.m_myDay, QDate(2026, 11, 12));
  QVERIFY(!m_document->isUndoAvailable());
  m_controller->setSearch(QStringLiteral("Alpha"));
  QVERIFY(m_controller->getTaskModel()->rowCount() > 0);
  QCOMPARE(m_document->toPlainText(), projectionSource());
  QVERIFY(load(projectionSource()));
  m_controller->setEditable(false);
  QVERIFY(!m_controller->removeTask(key("a")));
  m_controller->setEditable(true);
  m_controller->setBuffer(Buffer2());
  QVERIFY(!m_controller->addList(QStringLiteral("No buffer")));
  m_document.reset();
  QVERIFY(!m_controller->addList(QStringLiteral("No document")));
  QVERIFY(
      load(QStringLiteral("## A\n- [ ] Task\n  <!-- todo-item: {\"due\":\"2026-02-30\"} -->\n")));
  QVERIFY(!m_controller->getDocument().isValid());
  QSignalSpy rejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  QVERIFY(!m_controller->addList(QStringLiteral("Blocked")));
  QCOMPARE(rejected.count(), 1);
  QVERIFY(rejected.first().first().toString().contains(QStringLiteral("Line")));
  QCOMPARE(m_controller->getTaskModel()->rowCount(), 0);
}

void TestTodoViewWindowController::staleSourceAndRowIntents() {
  QVERIFY(load(projectionSource()));
  const int revision = m_controller->getSnapshotRevision();
  const auto fields = item("a").m_fields;
  auto *model = m_controller->getTaskModel();
  const auto oldIndex = model->index(model->rowForKey(key("a")));
  m_controller->setSourceRefreshEnabled(false);
  QTextCursor cursor(m_document.get());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n<!-- external -->\n"));
  const auto changedSource = m_document->toPlainText();
  auto checked = fields;
  checked.m_checked = true;
  QVERIFY(!m_controller->updateTask(key("a"), checked));
  QSignalSpy staleRejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  QVERIFY(model->setData(oldIndex, false, TodoTaskModel::InMyDayRole));
  QCOMPARE(staleRejected.count(), 1);
  QCOMPARE(m_document->toPlainText(), changedSource);
  m_controller->setSourceRefreshEnabled(true);
  QVERIFY(!model->setData(oldIndex, false, TodoTaskModel::InMyDayRole));
  QVERIFY(m_controller->getDocument().isValid());
  QSignalSpy rejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  m_controller->getTaskModel()->checkedChangeRequested(key("a"), true, revision);
  m_controller->getTaskModel()->moveRequested(key("a"), key("home"), {}, revision);
  model->myDayChangeRequested(key("a"), false, revision);
  QCOMPARE(rejected.count(), 3);
  QVERIFY(!item("a").m_fields.m_checked);
  QCOMPARE(item("a").m_fields.m_myDay, fields.m_myDay);
  QCOMPARE(item("a").m_listKey, key("work"));
  QCOMPARE(m_document->toPlainText(), changedSource);
}

void TestTodoViewWindowController::mutationGuardRejectsChangedGeneration() {
  QVERIFY(load(projectionSource()));
  const auto source = m_document->toPlainText();
  int calls = 0;
  m_controller->setMutationGuard([&calls]() {
    ++calls;
    return false;
  });
  QVERIFY(!m_controller->removeTask(key("a")));
  QCOMPARE(calls, 1);
  auto *model = m_controller->getTaskModel();
  QVERIFY(
      model->setData(model->index(model->rowForKey(key("a"))), false, TodoTaskModel::InMyDayRole));
  QCOMPARE(calls, 2);
  QCOMPARE(item("a").m_fields.m_myDay, QDate(2026, 11, 12));
  QCOMPARE(m_document->toPlainText(), source);
  m_controller->setMutationGuard([this]() {
    m_controller->setBuffer(m_buffer);
    m_controller->refreshFromSource();
    return true;
  });
  QVERIFY(!m_controller->removeTask(key("a")));
  QCOMPARE(m_document->toPlainText(), source);
  m_controller->setMutationGuard([this]() {
    QTextCursor cursor(m_document.get());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("\n<!-- foreign -->\n"));
    return true;
  });
  QVERIFY(!m_controller->removeTask(key("a")));
  QVERIFY(m_controller->getDocument().findItem(key("a")));
  m_controller->setMutationGuard({});
  QVERIFY(m_controller->removeTask(key("a")));
}

void TestTodoViewWindowController::coalescingAndHistoryBoundaries() {
  QVERIFY(load(projectionSource()));
  const auto original = m_document->toPlainText();
  m_controller->setCoalescingKey(QStringLiteral("a/title"));
  auto fields = item("a").m_fields;
  fields.m_title = QStringLiteral("First edit");
  QVERIFY(m_controller->updateTask(key("a"), fields));
  fields.m_title = QStringLiteral("Second edit");
  QVERIFY(m_controller->updateTask(key("a"), fields));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), original);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(item("a").m_fields.m_title, QStringLiteral("Second edit"));
  const auto afterRedo = m_document->toPlainText();
  m_controller->setCoalescingKey(QStringLiteral("a/title"));
  fields = item("a").m_fields;
  fields.m_title = QStringLiteral("After redo");
  QVERIFY(m_controller->updateTask(key("a"), fields));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), afterRedo);
  m_document->redo();
  m_controller->setCoalescingKey(QStringLiteral("a/title"));
  fields = item("a").m_fields;
  fields.m_title = QStringLiteral("Typing");
  QVERIFY(m_controller->updateTask(key("a"), fields));
  const auto typing = m_document->toPlainText();
  fields = item("a").m_fields;
  fields.m_checked = true;
  QVERIFY(m_controller->updateTask(key("a"), fields));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), typing);
  m_controller->endCoalescing();
  fields = item("a").m_fields;
  fields.m_title = QStringLiteral("New focus");
  QVERIFY(m_controller->updateTask(key("a"), fields));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), typing);
}

void TestTodoViewWindowController::sourceRefreshAndReplacement() {
  QVERIFY(load(projectionSource()));
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  m_controller->refreshFromSource();
  m_controller->setView(TodoViewKind::All);
  m_controller->setSearch(QStringLiteral("Alpha"));
  m_controller->setSortOrder(TodoSortOrder::DueDate);
  QCOMPARE(changed.count(), 0);
  m_controller->setSourceRefreshEnabled(false);
  m_document->setPlainText(QStringLiteral("## Broken\n- [ ] Task\n  <!-- todo-item: {broken} -->"));
  QCOMPARE(changed.count(), 0);
  m_controller->setSourceRefreshEnabled(true);
  QCOMPARE(changed.count(), 1);
  QVERIFY(!m_controller->getDocument().isValid());
  m_document->setPlainText(QStringLiteral("## Repaired\n\n- [ ] Task"));
  QVERIFY(m_controller->getDocument().isValid());
  m_controller->setBuffer(Buffer2());
  QVERIFY(m_controller->getSelectedItemKey().isEmpty());
  QVERIFY(m_controller->getDocument().m_lists.isEmpty());
  m_controller->setBuffer(m_buffer);
  m_controller->refreshFromSource();
  QCOMPARE(m_controller->getDocument().m_lists.first().m_title, QStringLiteral("Repaired"));
  QCOMPARE(m_controller->getProjection().m_view, TodoViewKind::List);
  QVERIFY(m_controller->getProjection().m_search.isEmpty());
}

void TestTodoViewWindowController::smartViewsAndMidnight() {
  QVERIFY(load(projectionSource()));
  const auto source = m_document->toPlainText();
  m_controller->setView(TodoViewKind::MyDay);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a")}));
  QCOMPARE(m_controller->getTaskModel()->getCompletedCount(), 1);
  QCOMPARE(m_controller->getTaskModel()->rowCount(), 2);
  QVERIFY(m_controller->getTaskModel()->index(1).data(TodoTaskModel::SectionRole).toBool());
  m_controller->setCompletedExpanded(true);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("c")}));
  m_controller->setToday(QDate(2026, 11, 13));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("d")}));
  QCOMPARE(item("a").m_fields.m_myDay, QDate(2026, 11, 12));
  QCOMPARE(item("a").m_fields.m_due, QDate(2026, 11, 11));
  m_controller->setView(TodoViewKind::Important);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("c")}));
  m_controller->setView(TodoViewKind::Planned);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("a"), key("b"), key("e"), key("c")}));
  m_controller->setView(TodoViewKind::All);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("a"), key("b"), key("d"), key("e"), key("c")}));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
}

void TestTodoViewWindowController::searchAndTagComposition() {
  QVERIFY(load(projectionSource()));
  m_controller->setCompletedExpanded(true);
  m_controller->setSelectedItemKey(key("b"));
  m_controller->setView(TodoViewKind::Important);
  m_controller->setSelectedItemKey(key("a"));
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  m_controller->setSearch(QStringLiteral("NEEDLE"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("b")}));
  m_controller->setTagFilter(QStringLiteral("WORK"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a")}));
  m_controller->setSearch({});
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("d"), key("c")}));
  m_controller->setSearch(QStringLiteral("Alpha"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("d")}));
  m_controller->setTagFilter({});
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("d")}));
  m_controller->setSearch({});
  QCOMPARE(m_controller->getProjection().m_view, TodoViewKind::Important);
  QCOMPARE(m_controller->getSelectedItemKey(), key("a"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a"), key("c")}));
  for (const auto &hidden : {QStringLiteral("item-secret"), QStringLiteral("list-secret"),
                             QStringLiteral("Description-only-token"), QStringLiteral("todo-item"),
                             QStringLiteral("2026-11-11")}) {
    m_controller->setSearch(hidden);
    QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList());
  }
  m_controller->setSearch(QStringLiteral("Gather figures"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a")}));
  m_controller->setSearch(QStringLiteral("Home"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("d"), key("e")}));
  m_controller->setSearch(QStringLiteral("two words, literal"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("a")}));
  m_controller->setSearch(QStringLiteral("Completed"));
  QCOMPARE(visibleKeys(m_controller->getTaskModel()), QStringList({key("c")}));
  QCOMPARE(changed.count(), 0);
  QCOMPARE(m_document->toPlainText(), projectionSource());
}

void TestTodoViewWindowController::stableSortingAndCounts() {
  QVERIFY(load(projectionSource()));
  m_controller->setView(TodoViewKind::All);
  m_controller->setCompletedExpanded(true);
  m_controller->setSortOrder(TodoSortOrder::Title);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("a"), key("d"), key("b"), key("e"), key("c")}));
  m_controller->setSortOrder(TodoSortOrder::DueDate);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("a"), key("b"), key("e"), key("d"), key("c")}));
  m_controller->setSortOrder(TodoSortOrder::Importance);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("a"), key("b"), key("d"), key("e"), key("c")}));
  m_controller->setSortOrder(TodoSortOrder::AddedToMyDay);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("d"), key("a"), key("b"), key("e"), key("c")}));
  m_controller->setSortOrder(TodoSortOrder::CreationDate);
  QCOMPARE(visibleKeys(m_controller->getTaskModel()),
           QStringList({key("a"), key("b"), key("d"), key("e"), key("c")}));
  auto *sidebar = m_controller->getListModel();
  const auto count = [sidebar](TodoViewKind p_view, const QString &p_key = QString()) {
    return sidebar->index(sidebar->rowForView(p_view, p_key))
        .data(TodoListModel::IncompleteCountRole)
        .toInt();
  };
  QCOMPARE(count(TodoViewKind::List, key("work")), 2);
  QCOMPARE(count(TodoViewKind::List, key("home")), 2);
  QCOMPARE(count(TodoViewKind::All), 4);
  QCOMPARE(count(TodoViewKind::Important), 1);
  QCOMPARE(count(TodoViewKind::MyDay), 1);
  QCOMPARE(count(TodoViewKind::Planned), 3);
  m_controller->setSearch(QStringLiteral("Alpha"));
  QCOMPARE(count(TodoViewKind::All), 2);
  QCOMPARE(count(TodoViewKind::List, key("home")), 1);
  QCOMPARE(count(TodoViewKind::Planned), 1);
  m_controller->setTagFilter(QStringLiteral("work-plan"));
  QCOMPARE(count(TodoViewKind::All), 0);
  QCOMPARE(m_document->toPlainText(), projectionSource());
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::timestampSorting_data() {
  QTest::addColumn<TodoSortOrder>("order");
  QTest::addColumn<QStringList>("completedKeys");
  QTest::newRow("my-day") << TodoSortOrder::AddedToMyDay
                          << QStringList({key("h"), key("i"), key("j"), key("g")});
  QTest::newRow("creation-instant")
      << TodoSortOrder::CreationDate << QStringList({key("i"), key("j"), key("h"), key("g")});
}

void TestTodoViewWindowController::timestampSorting() {
  QFETCH(TodoSortOrder, order);
  QFETCH(QStringList, completedKeys);
  const auto source = groupedHeader() + QStringLiteral(R"TODO(## Work
<!-- todo-list: {"id":"work"} -->

- [ ] Missing first
  <!-- todo-item: {"id":"a"} -->

- [ ] Older
  <!-- todo-item: {"id":"b","myDay":"2026-11-11","createdAt":"2026-11-11T00:00:00Z"} -->

- [ ] Equal offset first
  <!-- todo-item: {"id":"c","myDay":"2026-11-12","createdAt":"2026-11-12T02:00:00.1+02:00"} -->

- [ ] Newest
  <!-- todo-item: {"id":"d","myDay":"2026-11-13","createdAt":"2026-11-12T00:00:00.101Z"} -->

- [ ] Equal UTC second
  <!-- todo-item: {"id":"e","myDay":"2026-11-12","createdAt":"2026-11-12T00:00:00.100Z"} -->

- [ ] Missing second
  <!-- todo-item: {"id":"f"} -->

- [x] Completed missing
  <!-- todo-item: {"id":"g"} -->

- [x] Completed older creation, newest My Day
  <!-- todo-item: {"id":"h","myDay":"2026-11-16","createdAt":"2026-11-14T00:00:00Z"} -->

- [x] Completed newest first
  <!-- todo-item: {"id":"i","myDay":"2026-11-15","createdAt":"2026-11-15T00:00:00Z"} -->

- [x] Completed newest equal offset
  <!-- todo-item: {"id":"j","myDay":"2026-11-15","createdAt":"2026-11-14T19:00:00-05:00"} -->
)TODO");
  QVERIFY(load(source));
  m_controller->setView(TodoViewKind::List, key("work"));
  auto *model = m_controller->getTaskModel();
  QVERIFY(model->isReorderingEnabled());
  auto fields = item("b").m_fields;
  fields.m_title = QStringLiteral("Edited before sorting");
  QVERIFY(m_controller->updateTask(key("b"), fields));
  const auto editedSource = m_document->toPlainText();
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(m_document->isRedoAvailable());
  const int undoSteps = m_document->availableUndoSteps();
  QSignalSpy changed(m_document.get(), &QTextDocument::contentsChanged);

  m_controller->setSelectedItemKey(key("c"));
  m_controller->setCompletedExpanded(true);
  m_controller->setSortOrder(order);
  QVERIFY(!model->isReorderingEnabled());
  const QStringList activeKeys{key("d"), key("c"), key("e"), key("b"), key("a"), key("f")};
  QCOMPARE(visibleKeys(model), activeKeys + completedKeys);
  QCOMPARE(model->getActiveCount(), 6);
  QCOMPARE(model->getCompletedCount(), 4);
  QCOMPARE(model->rowCount(), 11);
  QVERIFY(!model->itemAt(6));
  QCOMPARE(m_controller->getSelectedItemKey(), key("c"));
  m_controller->setView(TodoViewKind::All);
  QCOMPARE(visibleKeys(model), activeKeys + completedKeys);
  m_controller->setCompletedExpanded(false);
  QCOMPARE(visibleKeys(model), activeKeys);
  QCOMPARE(model->rowCount(), 7);
  QCOMPARE(model->getCompletedCount(), 4);
  m_controller->setCompletedExpanded(true);
  QCOMPARE(visibleKeys(model), activeKeys + completedKeys);

  m_controller->setSortOrder(TodoSortOrder::Manual);
  QCOMPARE(visibleKeys(model), QStringList({key("a"), key("b"), key("c"), key("d"), key("e"),
                                            key("f"), key("g"), key("h"), key("i"), key("j")}));
  QVERIFY(!model->isReorderingEnabled());
  m_controller->setView(TodoViewKind::List, key("work"));
  QVERIFY(model->isReorderingEnabled());
  QCOMPARE(changed.count(), 0);
  QCOMPARE(m_document->toPlainText(), source);
  QCOMPARE(m_document->availableUndoSteps(), undoSteps);
  QVERIFY(m_document->isRedoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), editedSource);
}

void TestTodoViewWindowController::completedExpansionPreservesIndexes() {
  const auto source = projectionSource();
  QVERIFY(load(source));
  auto *model = m_controller->getTaskModel();
  QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
  m_controller->setView(TodoViewKind::List, key("work"));
  m_controller->setSelectedItemKey(key("b"));
  const QPersistentModelIndex active(model->index(model->rowForKey(key("a"))));
  const QPersistentModelIndex section(model->index(model->getActiveCount()));
  QItemSelectionModel selection(model);
  selection.setCurrentIndex(section, QItemSelectionModel::ClearAndSelect);
  const int collapsedCount = model->rowCount();
  QCOMPARE(model->rowForKey(key("c")), -1);
  QVERIFY(!model->itemAt(collapsedCount));
  m_controller->setCompletedExpanded(true);
  QCOMPARE(model->rowCount(), collapsedCount + model->getCompletedCount());
  QVERIFY(active.isValid() && section.isValid());
  QCOMPARE(active.data(TodoTaskModel::IdRole).toString(), key("a"));
  QCOMPARE(selection.currentIndex(), QModelIndex(section));
  QCOMPARE(selection.selectedIndexes(), QModelIndexList{section});
  const QPersistentModelIndex completed(model->index(model->rowForKey(key("c"))));
  QVERIFY(completed.isValid());
  m_controller->setCompletedExpanded(false);
  QCOMPARE(model->rowCount(), collapsedCount);
  QVERIFY(active.isValid() && section.isValid());
  QVERIFY(!completed.isValid());
  QCOMPARE(selection.currentIndex(), QModelIndex(section));
  QCOMPARE(selection.selectedIndexes(), QModelIndexList{section});
  QCOMPARE(model->rowForKey(key("c")), -1);
  QVERIFY(!model->itemAt(collapsedCount));
  QVERIFY(!model->index(collapsedCount).isValid());
  QCOMPARE(m_controller->getSelectedItemKey(), key("b"));
  m_controller->setCompletedExpanded(true);
  QVERIFY(!completed.isValid());
  QCOMPARE(selection.currentIndex(), QModelIndex(section));
  QCOMPARE(model->index(model->rowForKey(key("c"))).data(TodoTaskModel::IdRole).toString(),
           key("c"));

  m_controller->setSearch(QStringLiteral("Completed"));
  m_controller->setCompletedExpanded(false);
  QCOMPARE(model->rowCount(), 1);
  const QPersistentModelIndex onlySection(model->index(0));
  selection.setCurrentIndex(onlySection, QItemSelectionModel::ClearAndSelect);
  m_controller->setCompletedExpanded(true);
  QCOMPARE(model->rowCount(), 2);
  QCOMPARE(selection.currentIndex(), QModelIndex(onlySection));
  m_controller->setCompletedExpanded(false);
  QCOMPARE(selection.currentIndex(), QModelIndex(onlySection));
  m_controller->setSearch(QStringLiteral("no matching task"));
  m_controller->setCompletedExpanded(true);
  QCOMPARE(model->rowCount(), 0);
  m_controller->setCompletedExpanded(false);
  QCOMPARE(model->rowCount(), 0);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::modelRolesAndIntents() {
  QVERIFY(load(projectionSource()));
  auto *model = m_controller->getTaskModel();
  auto index = model->index(model->rowForKey(key("a")));
  QCOMPARE(index.data(Qt::DisplayRole).toString(), QStringLiteral("Alpha"));
  QCOMPARE(index.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
  QVERIFY(!index.data(Qt::AccessibleTextRole).toString().isEmpty());
  QCOMPARE(index.data(TodoTaskModel::SourceListNameRole).toString(), QStringLiteral("Work"));
  QCOMPARE(index.data(TodoTaskModel::DueRole).toDate(), QDate(2026, 11, 11));
  QCOMPARE(index.data(TodoTaskModel::MyDayRole).metaType().id(), int(QMetaType::QDate));
  QCOMPARE(index.data(TodoTaskModel::MyDayRole).toDate(), QDate(2026, 11, 12));
  QCOMPARE(index.data(TodoTaskModel::InMyDayRole).metaType().id(), int(QMetaType::Bool));
  QVERIFY(index.data(TodoTaskModel::InMyDayRole).toBool());
  QCOMPARE(index.data(TodoTaskModel::StepsCompletedRole).toInt(), 1);
  QCOMPARE(index.data(TodoTaskModel::StepsTotalRole).toInt(), 2);
  QCOMPARE(index.data(TodoTaskModel::RevisionRole).toInt(), m_document->revision());
  QVERIFY(!model->setData(index, Qt::PartiallyChecked, Qt::CheckStateRole));
  QVERIFY(!model->setData(index, QStringLiteral("true"), TodoTaskModel::CheckedRole));
  for (const QVariant &value : {QVariant(), QVariant(0), QVariant(1), QVariant(Qt::Checked),
                                QVariant(QStringLiteral("true")), QVariant(QDate(2026, 11, 12))}) {
    QVERIFY(!model->setData(index, value, TodoTaskModel::InMyDayRole));
  }
  QVERIFY(!model->setData(index, QDate(2026, 11, 13), TodoTaskModel::MyDayRole));
  QVERIFY(!model->setData({}, false, TodoTaskModel::InMyDayRole));
  QVERIFY(!model->setData(model->index(model->getActiveCount()), true, TodoTaskModel::InMyDayRole));
  QCOMPARE(m_document->toPlainText(), projectionSource());
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
  TodoTaskModel detached(*m_services);
  detached.setSnapshot(m_controller->getDocument(), m_controller->getProjection(), true,
                       m_document->revision());
  QSignalSpy intent(&detached, &TodoTaskModel::checkedChangeRequested);
  QVERIFY(!model->setData(detached.index(0), false, TodoTaskModel::InMyDayRole));
  QVERIFY(detached.setData(detached.index(0), true, TodoTaskModel::CheckedRole));
  QCOMPARE(intent.count(), 1);
  QCOMPARE(intent.first()[0].toString(), key("a"));
  QCOMPARE(intent.first()[2].toInt(), m_document->revision());
  QVERIFY(!detached.itemAt(0)->m_fields.m_checked);
  QVERIFY(model->setData(index, Qt::Checked, Qt::CheckStateRole));
  QVERIFY(item("a").m_fields.m_checked);
  QVERIFY(!model->setData(index, false, TodoTaskModel::CheckedRole));
  m_controller->setCompletedExpanded(true);
  index = model->index(model->rowForKey(key("a")));
  QVERIFY(model->setData(index, false, TodoTaskModel::ImportantRole));
  QVERIFY(!item("a").m_fields.m_important);
  m_controller->setEditable(false);
  index = model->index(model->rowForKey(key("a")));
  QVERIFY(!model->setData(index, false, TodoTaskModel::CheckedRole));
  QVERIFY(!model->setData(index, false, TodoTaskModel::InMyDayRole));
  QSignalSpy rejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  const auto source = m_document->toPlainText();
  model->myDayChangeRequested(key("a"), false, model->getRevision());
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(m_document->toPlainText(), source);
  const int section = model->getActiveCount();
  QVERIFY(model->setData(model->index(section), false, TodoTaskModel::ExpandedRole));
  QVERIFY(!m_controller->getProjection().m_completedExpanded);
}

void TestTodoViewWindowController::myDayMembershipIntents() {
  const auto source = projectionSource();
  QVERIFY(load(source));
  m_controller->setView(TodoViewKind::List, key("work"));
  auto *model = m_controller->getTaskModel();
  const auto taskIndex = [model](const char *p_id) {
    return model->index(model->rowForKey(key(p_id)));
  };
  QVERIFY(model->setData(taskIndex("a"), true, TodoTaskModel::InMyDayRole));
  QVERIFY(!taskIndex("b").data(TodoTaskModel::InMyDayRole).toBool());
  QVERIFY(model->setData(taskIndex("b"), false, TodoTaskModel::InMyDayRole));

  const QDate displayedToday(2031, 4, 9);
  m_controller->setToday(displayedToday);
  QVERIFY(!taskIndex("a").data(TodoTaskModel::InMyDayRole).toBool());
  QCOMPARE(taskIndex("a").data(TodoTaskModel::MyDayRole).toDate(), QDate(2026, 11, 12));
  QVERIFY(model->setData(taskIndex("a"), false, TodoTaskModel::InMyDayRole));
  // Even direct same-membership intents must not discard a historical assignment.
  model->myDayChangeRequested(key("a"), false, model->getRevision());
  QCOMPARE(item("a").m_fields.m_myDay, QDate(2026, 11, 12));
  m_controller->setToday({});
  QVERIFY(!taskIndex("a").data(TodoTaskModel::InMyDayRole).toBool());
  QVERIFY(!taskIndex("b").data(TodoTaskModel::InMyDayRole).toBool());
  QVERIFY(!model->setData(taskIndex("a"), true, TodoTaskModel::InMyDayRole));
  QVERIFY(!model->setData(taskIndex("b"), true, TodoTaskModel::InMyDayRole));
  QVERIFY(model->setData(taskIndex("a"), false, TodoTaskModel::InMyDayRole));
  model->myDayChangeRequested(key("a"), true, model->getRevision());
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());

  const auto executionDay = QDate::currentDate();
  m_controller->setToday(displayedToday);
  const auto oldIndex = taskIndex("a");
  QVERIFY(model->setData(oldIndex, true, TodoTaskModel::InMyDayRole));
  QCOMPARE(item("a").m_fields.m_myDay, executionDay);
  QCOMPARE(m_controller->getProjection().m_today, executionDay);
  QVERIFY(taskIndex("a").data(TodoTaskModel::InMyDayRole).toBool());
  QCOMPARE(taskIndex("a").data(TodoTaskModel::MyDayRole).toDate(), executionDay);
  QCOMPARE(item("a").m_properties.value(QStringLiteral("hidden")).toString(),
           QStringLiteral("item-secret"));
  QCOMPARE(m_controller->getProjection().m_view, TodoViewKind::List);
  QCOMPARE(m_controller->getProjection().m_listKey, key("work"));
  QVERIFY(!model->setData(oldIndex, false, TodoTaskModel::InMyDayRole));
  const auto assigned = m_document->toPlainText();
  const int undoSteps = m_document->availableUndoSteps();
  QVERIFY(model->setData(taskIndex("a"), true, TodoTaskModel::InMyDayRole));
  QCOMPARE(m_document->toPlainText(), assigned);
  QCOMPARE(m_document->availableUndoSteps(), undoSteps);

  m_controller->setView(TodoViewKind::MyDay);
  QCOMPARE(visibleKeys(model), QStringList{key("a")});
  QVERIFY(model->setData(taskIndex("a"), false, TodoTaskModel::InMyDayRole));
  QVERIFY(!item("a").m_fields.m_myDay.isValid());
  QVERIFY(!item("a").m_properties.contains(QStringLiteral("myDay")));
  QVERIFY(visibleKeys(model).isEmpty());
  QCOMPARE(m_controller->getProjection().m_view, TodoViewKind::MyDay);
  m_document->undo();
  QCOMPARE(item("a").m_fields.m_myDay, executionDay);
  QCOMPARE(visibleKeys(model), QStringList{key("a")});
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());

  m_controller->setView(TodoViewKind::List, key("work"));
  QVERIFY(model->setData(taskIndex("b"), true, TodoTaskModel::InMyDayRole));
  QCOMPARE(item("b").m_fields.m_myDay, executionDay);
  QVERIFY(taskIndex("b").data(TodoTaskModel::InMyDayRole).toBool());
  m_document->undo();
  QVERIFY(!item("b").m_fields.m_myDay.isValid());
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());

  auto yesterdayTask = item("a").m_fields;
  yesterdayTask.m_myDay = executionDay.addDays(-1);
  QVERIFY(m_controller->updateTask(key("a"), yesterdayTask));
  auto todayTask = item("b").m_fields;
  todayTask.m_myDay = executionDay;
  QVERIFY(m_controller->updateTask(key("b"), todayTask));
  m_controller->setToday(executionDay.addDays(-1));
  m_controller->setView(TodoViewKind::MyDay);
  QCOMPARE(m_controller->getSelectedItemKey(), key("a"));
  const auto beforeRemoval = m_document->toPlainText();
  QVERIFY(model->setData(taskIndex("a"), false, TodoTaskModel::InMyDayRole));
  QVERIFY(!item("a").m_fields.m_myDay.isValid());
  QCOMPARE(item("b").m_fields.m_myDay, executionDay);
  QCOMPARE(visibleKeys(model), QStringList{key("b")});
  QCOMPARE(m_controller->getSelectedItemKey(), key("b"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeRemoval);
}

void TestTodoViewWindowController::listModelDrops() {
  const auto source = sidebarSource();
  QVERIFY(load(source));
  auto *model = m_controller->getListModel();
  QCOMPARE(model->getRevision(), m_controller->getSnapshotRevision());
  m_controller->setView(TodoViewKind::List, key("second"));
  m_controller->setSelectedItemKey(key("b"));
  std::unique_ptr<QMimeData> mime(model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(mime);
  QSignalSpy changed(m_controller.get(), &TodoViewWindowController::documentChanged);
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                              model->index(model->rowForKey(key("first")))));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(m_controller->getDocument().m_lists.first().m_key, key("second"));
  QCOMPARE(m_controller->getProjection().m_listKey, key("second"));
  QCOMPARE(m_controller->getSelectedItemKey(), key("b"));
  const auto reordered = m_document->toPlainText();
  QVERIFY(!model->canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), reordered);

  QVERIFY(load(source));
  model = m_controller->getListModel();
  const auto workBody =
      m_controller->getDocument().text(m_controller->getDocument().findGroup(key("work"))->m_range);
  mime.reset(model->mimeData({model->index(model->rowForGroup(key("empty")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(key("work")), 0, {}));
  QCOMPARE(m_controller->getDocument().m_groups.at(1).m_key, key("empty"));
  QCOMPARE(
      m_controller->getDocument().text(m_controller->getDocument().findGroup(key("work"))->m_range),
      workBody);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  mime.reset(model->mimeData({model->index(model->rowForGroup(key("work")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowCount(), 0, {}));
  QCOMPARE(m_controller->getDocument().m_groups.last().m_key, key("work"));
  QCOMPARE(item("c").m_listKey, key("third"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);

  QVERIFY(load(source));
  model = m_controller->getListModel();
  m_controller->setView(TodoViewKind::List, key("second"));
  m_controller->setSelectedItemKey(key("b"));
  m_controller->setSortOrder(TodoSortOrder::Title);
  m_controller->setSearch(QStringLiteral("no matching task"));
  m_controller->setTagFilter(QStringLiteral("missing tag"));
  m_controller->setGroupExpanded(key("work"), false);
  QVERIFY(model->flags(model->index(model->rowForKey(key("second")))) & Qt::ItemIsDragEnabled);
  const auto selectedBefore = m_controller->getSelectedItemKey();
  mime.reset(model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                              model->index(model->rowForGroup(key("work")))));
  QCOMPARE(m_controller->getDocument().groupForList(list("second"))->m_key, key("work"));
  QCOMPARE(m_controller->getDocument().m_lists.at(2).m_key, key("second"));
  QVERIFY(!m_controller->getProjection().m_collapsedGroups.contains(key("work")));
  QVERIFY(model->index(model->rowForGroup(key("work"))).data(TodoListModel::ExpandedRole).toBool());
  QCOMPARE(m_controller->getProjection().m_listKey, key("second"));
  QCOMPARE(m_controller->getSelectedItemKey(), selectedBefore);
  m_controller->setSearch({});
  m_controller->setTagFilter({});
  QCOMPARE(m_controller->getSelectedItemKey(), key("b"));
  const auto moved = m_document->toPlainText();
  mime.reset(model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  QCOMPARE(m_controller->getDocument().groupForList(list("second")),
           m_controller->getDocument().defaultGroup());
  QCOMPARE(m_controller->getDocument().m_lists.at(1).m_key, key("second"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), moved);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);

  // The gap before the first custom header appends a root list to Default,
  // regardless of its latent logical collapse state.
  m_controller->setGroupExpanded(key("defaultgroup"), false);
  mime.reset(model->mimeData({model->index(model->rowForKey(key("third")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(key("work")), 0, {}));
  QCOMPARE(m_controller->getDocument().groupForList(list("third")),
           m_controller->getDocument().defaultGroup());
  QVERIFY(!m_controller->getProjection().m_collapsedGroups.contains(key("defaultgroup")));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  mime.reset(model->mimeData({model->index(model->rowForKey(key("first")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                              model->index(model->rowForGroup(key("empty")))));
  QCOMPARE(m_controller->getDocument().groupForList(list("first"))->m_key, key("empty"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);

  // Adjacent/self moves succeed without stamping identities or dirtying text.
  QVERIFY(load(source));
  model = m_controller->getListModel();
  mime.reset(model->mimeData({model->index(model->rowForKey(key("first")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("first")), 0, {}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("second")), 0, {}));
  mime.reset(model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowCount(), 0, {}));
  mime.reset(model->mimeData({model->index(model->rowForGroup(key("work")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(key("work")), 0, {}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForGroup(key("empty")), 0, {}));
  mime.reset(model->mimeData({model->index(model->rowForGroup(key("empty")))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isModified());
  QVERIFY(!m_document->isUndoAvailable());
}

void TestTodoViewWindowController::listModelDropAdmission() {
  const auto source = sidebarSource();
  QVERIFY(load(source));
  auto *model = m_controller->getListModel();
  std::unique_ptr<QMimeData> mime(model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(mime);
  const auto target = model->index(model->rowForKey(key("first")));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::CopyAction, -1, 0, target));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -2, 0, {}));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, model->rowCount() + 1, 0, {}));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 1, target));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, 0, 0, target));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, model->index(0)));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
  QVERIFY(!model->mimeData({model->index(0)}));
  QVERIFY(!model->mimeData({target, model->index(model->rowForKey(key("second")))}));
  const auto format = model->mimeTypes().first();
  QMimeData malformed;
  malformed.setData(format, QByteArray("malformed"));
  QVERIFY(!model->dropMimeData(&malformed, Qt::MoveAction, -1, 0, target));
  malformed.setData(format, mime->data(format).chopped(1));
  QVERIFY(!model->dropMimeData(&malformed, Qt::MoveAction, -1, 0, target));
  malformed.setData(format, mime->data(format) + QByteArray("trailing"));
  QVERIFY(!model->dropMimeData(&malformed, Qt::MoveAction, -1, 0, target));
  TodoListModel foreign(*m_services);
  foreign.setSnapshot(m_controller->getDocument(), m_controller->getProjection(), true,
                      m_controller->getSnapshotRevision());
  std::unique_ptr<QMimeData> foreignMime(
      foreign.mimeData({foreign.index(foreign.rowForKey(key("second")))}));
  QVERIFY(!model->dropMimeData(foreignMime.get(), Qt::MoveAction, -1, 0, target));
  QVERIFY(!foreign.dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                                foreign.index(foreign.rowForKey(key("first")))));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, foreign.index(4)));
  std::unique_ptr<QMimeData> groupMime(
      model->mimeData({model->index(model->rowForGroup(key("work")))}));
  QVERIFY(!model->dropMimeData(groupMime.get(), Qt::MoveAction, -1, 0,
                               model->index(model->rowForGroup(key("empty")))));
  QVERIFY(!model->dropMimeData(groupMime.get(), Qt::MoveAction, model->rowForKey(key("second")), 0,
                               {}));
  QVERIFY(!model->dropMimeData(groupMime.get(), Qt::MoveAction, -1, 0,
                               model->index(model->rowForKey(key("first")))));
  QCOMPARE(model->rowForGroup(key("defaultgroup")), -1);
  QVERIFY(!model->mimeData({model->index(model->rowForGroup(key("defaultgroup")))}));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());

  m_controller->setSortOrder(TodoSortOrder::Title);
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                               model->index(model->rowForKey(key("first")))));
  mime.reset(model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, target));
  m_controller->setEditable(false);
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  QVERIFY(!model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(!(model->flags(model->index(model->rowForKey(key("second")))) & Qt::ItemIsDragEnabled));
  QVERIFY(!(model->flags({}) & Qt::ItemIsDropEnabled));
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(load(source, true));
  model = m_controller->getListModel();
  QVERIFY(!model->mimeData({model->index(model->rowForKey(key("second")))}));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  QVERIFY(!m_document->isUndoAvailable());

  QVERIFY(load(source));
  model = m_controller->getListModel();
  mime.reset(model->mimeData({model->index(model->rowForKey(key("second")))}));
  m_controller->setSourceRefreshEnabled(false);
  QTextCursor cursor(m_document.get());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n"));
  const auto changed = m_document->toPlainText();
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowForKey(key("first")), 0, {}));
  QCOMPARE(m_document->toPlainText(), changed);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());

  QVERIFY(load(source + QStringLiteral("\n\nUnsupported paragraph")));
  model = m_controller->getListModel();
  QVERIFY(!m_controller->getDocument().isValid());
  QVERIFY(!(model->flags({}) & Qt::ItemIsDropEnabled));
  QVERIFY(!model->mimeData({model->index(0)}));
  QVERIFY(!model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
}

void TestTodoViewWindowController::listModelSourceIdentityDrops() {
  const auto source = QStringLiteral("## A\n\n- [ ] First\n\n"
                                     "# Work\n\n## B\n\n- [ ] Selected\n\n"
                                     "# Other\n\n## C\n\n- [ ] Other");
  QVERIFY(load(source));
  auto *model = m_controller->getListModel();
  const auto originalGroup = m_controller->getDocument().m_groups.at(1).m_key;
  const auto selected = m_controller->getDocument().m_lists.at(1);
  m_controller->setView(TodoViewKind::List, selected.m_key);
  m_controller->setSelectedItemKey(selected.m_items.first().m_key);
  m_controller->setSelectedGroupKey(originalGroup);
  m_controller->setGroupExpanded(originalGroup, false);
  std::unique_ptr<QMimeData> mime(
      model->mimeData({model->index(model->rowForGroup(originalGroup))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  const auto &afterGroup = m_controller->getDocument();
  QCOMPARE(afterGroup.m_groups.last().m_title, QStringLiteral("Work"));
  QVERIFY(!afterGroup.m_groups.last().m_id.isEmpty());
  QCOMPARE(m_controller->getProjection().m_groupKey, afterGroup.m_groups.last().m_key);
  QVERIFY(
      m_controller->getProjection().m_collapsedGroups.contains(afterGroup.m_groups.last().m_key));
  QVERIFY(!m_controller->getProjection().m_collapsedGroups.contains(originalGroup));
  QVERIFY(afterGroup.defaultGroup()->m_implicit);
  QVERIFY(afterGroup.defaultGroup()->m_id.isEmpty());
  const auto moved = afterGroup.m_lists.last();
  QCOMPARE(moved.m_title, QStringLiteral("B"));
  QVERIFY(moved.m_id.isEmpty());
  QCOMPARE(m_controller->getProjection().m_listKey, moved.m_key);
  QCOMPARE(m_controller->getSelectedItemKey(), moved.m_items.first().m_key);
  const auto groupResult = m_document->toPlainText();
  const auto other = afterGroup.m_groups.at(1).m_key;
  m_controller->setGroupExpanded(other, false);
  mime.reset(model->mimeData({model->index(model->rowForKey(moved.m_key))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, -1, 0,
                              model->index(model->rowForGroup(other))));
  const auto &afterList = m_controller->getDocument();
  const auto &retained = afterList.m_lists.at(2);
  QCOMPARE(retained.m_title, QStringLiteral("B"));
  QVERIFY(!retained.m_id.isEmpty());
  QVERIFY(retained.m_items.first().m_id.isEmpty());
  QCOMPARE(m_controller->getProjection().m_listKey, retained.m_key);
  QCOMPARE(m_controller->getSelectedItemKey(), retained.m_items.first().m_key);
  const auto *destination = afterList.groupForList(retained);
  QVERIFY(!destination->m_id.isEmpty());
  QVERIFY(!m_controller->getProjection().m_collapsedGroups.contains(destination->m_key));
  const auto listResult = m_document->toPlainText();
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), groupResult);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(!m_document->isUndoAvailable());
  m_document->redo();
  m_document->redo();
  QCOMPARE(m_document->toPlainText(), listResult);

  const auto noDefault = QStringLiteral("# Work\n\n## Only\n\n- [ ] Keep");
  QVERIFY(load(noDefault));
  model = m_controller->getListModel();
  const auto only = m_controller->getDocument().m_lists.first().m_key;
  mime.reset(model->mimeData({model->index(model->rowForKey(only))}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->rowCount(), 0, {}));
  const auto &created = m_controller->getDocument();
  QVERIFY(created.defaultGroup());
  QCOMPARE(created.groupForList(created.m_lists.first()), created.defaultGroup());
  QCOMPARE(created.m_lists.first().m_items.first().m_fields.m_title, QStringLiteral("Keep"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), noDefault);
}

void TestTodoViewWindowController::nativeDragUsesMoveCommand() {
  QVERIFY(load(projectionSource()));
  auto *model = m_controller->getTaskModel();
  QVERIFY(model->isReorderingEnabled());
  std::unique_ptr<QMimeData> mime(model->mimeData({model->index(model->rowForKey(key("b")))}));
  QVERIFY(mime);
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
  QCOMPARE(itemTitles(list("work")), QStringList({QStringLiteral("beta"), QStringLiteral("Alpha"),
                                                  QStringLiteral("Completed")}));
  m_document->undo();
  QCOMPARE(itemTitles(list("work")), QStringList({QStringLiteral("Alpha"), QStringLiteral("beta"),
                                                  QStringLiteral("Completed")}));
  m_controller->setSortOrder(TodoSortOrder::Title);
  QVERIFY(!model->isReorderingEnabled());
  m_controller->setSortOrder(TodoSortOrder::Manual);
  m_controller->setView(TodoViewKind::All);
  QVERIFY(!model->isReorderingEnabled());
  m_controller->setView(TodoViewKind::List, key("work"));
  m_controller->setSearch(QStringLiteral("Alpha"));
  QVERIFY(!model->isReorderingEnabled());
  m_controller->setSearch({});
  m_controller->setTagFilter(QStringLiteral("work"));
  QVERIFY(!model->isReorderingEnabled());
  m_controller->setTagFilter({});
  m_controller->setEditable(false);
  QVERIFY(!model->isReorderingEnabled());
}

void TestTodoViewWindowController::dragCompletionGroupsAndStaleness() {
  QVERIFY(load(
      groupedHeader() +
      QStringLiteral("## Work\n<!-- todo-list: {\"id\":\"work\"} -->\n\n- [x] C\n  <!-- todo-item: "
                     "{\"id\":\"c\"} -->\n\n- [ ] A\n  <!-- todo-item: {\"id\":\"a\"} -->\n\n- [ ] "
                     "B\n  <!-- todo-item: {\"id\":\"b\"} -->\n")));
  auto *model = m_controller->getTaskModel();
  std::unique_ptr<QMimeData> mime(model->mimeData({model->index(0)}));
  QVERIFY(model->dropMimeData(mime.get(), Qt::MoveAction, model->getActiveCount(), 0, {}));
  QCOMPARE(itemTitles(list("work")),
           QStringList({QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("A")}));
  QCOMPARE(visibleKeys(model), QStringList({key("b"), key("a")}));
  QVERIFY(!model->canDropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
  mime.reset(model->mimeData({model->index(0)}));
  TodoTaskModel other(*m_services);
  other.setSnapshot(m_controller->getDocument(), m_controller->getProjection(), true,
                    m_document->revision());
  QVERIFY(!other.canDropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
  QVERIFY(!model->canDropMimeData(mime.get(), Qt::CopyAction, 0, 0, {}));
  QVERIFY(!model->canDropMimeData(mime.get(), Qt::MoveAction, 0, 0, model->index(0)));
  m_controller->setCompletedExpanded(true);
  QVERIFY(!model->canDropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
}

void TestTodoViewWindowController::normalizedCrLfAndUnicode() {
  const QString source =
      QString::fromUtf8("## \xe5\xb7\xa5\xe4\xbd\x9c \xf0\x9f\x98\x80\r\n\r\n- [ "
                        "] \xe4\xb8\xad\xe6\x96\x87\r\n\r\n- [ ] Last");
  QVERIFY(load(source));
  QVERIFY(!m_document->toPlainText().contains(QLatin1Char('\r')));
  const auto normalized = m_document->toPlainText();
  const auto listKey = m_controller->getDocument().m_lists.first().m_key;
  const auto taskKey = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  auto fields = m_controller->getDocument().findItem(taskKey)->m_fields;
  fields.m_checked = true;
  QVERIFY(m_controller->updateTask(taskKey, fields));
  QVERIFY(m_controller->getDocument().isValid());
  QVERIFY(m_controller->getDocument().m_lists.first().m_items.first().m_fields.m_checked);
  QVERIFY(!m_document->toPlainText().contains(QLatin1Char('\r')));
  QVERIFY(!m_document->toPlainText().endsWith(QLatin1Char('\n')));
  QVERIFY(m_controller->getDocument().m_lists.first().m_key != listKey);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), normalized);
}

void TestTodoViewWindowController::coincidentEofInsertions() {
  QVERIFY(load(QStringLiteral("## Empty")));
  TodoTaskFields fields;
  fields.m_title = QStringLiteral("Task");
  fields.m_note = QStringLiteral("Note");
  const auto initial = m_document->toPlainText();
  QVERIFY(m_controller->addTask(m_controller->getDocument().m_lists.first().m_key, fields));
  QVERIFY(m_controller->getDocument().isValid());
  QCOMPARE(m_controller->getDocument().m_lists.first().m_items.first().m_fields.m_note,
           fields.m_note);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), initial);
  QVERIFY(load(QStringLiteral("## List\n\n- [ ] Task")));
  const auto taskKey = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  fields = m_controller->getDocument().findItem(taskKey)->m_fields;
  fields.m_note = QStringLiteral("First note");
  QVERIFY(m_controller->updateTask(taskKey, fields));
  QVERIFY(m_controller->getDocument().isValid());
  const auto identified = m_controller->getDocument().m_lists.first().m_items.first().m_key;
  QCOMPARE(m_controller->getDocument().findItem(identified)->m_fields.m_note, fields.m_note);
  QVERIFY(m_controller->addStep(identified, QStringLiteral("Step at EOF")));
  QCOMPARE(m_controller->getDocument().findItem(identified)->m_steps.size(), 1);
  QVERIFY(load(QStringLiteral("##")));
  QVERIFY(m_controller->renameList(m_controller->getDocument().m_lists.first().m_key,
                                   QStringLiteral("New")));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_title, QStringLiteral("New"));
}

void TestTodoViewWindowController::formattingKeepsSourceIntents() {
  QVERIFY(load(projectionSource()));
  auto *model = m_controller->getTaskModel();
  const auto source = m_document->toPlainText();
  auto *lists = m_controller->getListModel();
  std::unique_ptr<QMimeData> listDrag(
      lists->mimeData({lists->index(lists->rowForKey(key("home")))}));
  QVERIFY(listDrag);
  QTextCursor cursor(m_document.get());
  QTextBlockFormat format;
  format.setLeftMargin(12);
  cursor.setBlockFormat(format);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(
      lists->dropMimeData(listDrag.get(), Qt::MoveAction, lists->rowForKey(key("work")), 0, {}));
  QCOMPARE(m_controller->getDocument().m_lists.first().m_key, key("home"));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), source);
  const auto currentRow = model->index(model->rowForKey(key("a")));
  format.setLeftMargin(18);
  cursor.setBlockFormat(format);
  QVERIFY(model->setData(currentRow, true, TodoTaskModel::CheckedRole));
  QVERIFY(item("a").m_fields.m_checked);
  QVERIFY(!item("b").m_fields.m_checked);
}

} // namespace tests

QTEST_MAIN(tests::TestTodoViewWindowController)
#include "test_todoviewwindowcontroller.moc"
