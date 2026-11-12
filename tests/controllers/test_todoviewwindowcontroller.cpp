#include <QtTest>

#include <QJsonArray>
#include <QMimeData>
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
  void implicitGroupMoveRemapsChildren();
  void crossGroupListMoveBoundaries();
  void groupProjectionIsPresentational();
  void groupCreationReturnsActualOwners();
  void listCommands();
  void setextAndEmptyTitles();
  void checkboxChangesOnlyOneCharacter();
  void taskFieldsAndUnknownMetadata();
  void notesAndTags();
  void taskAddRemoveUndo();
  void stepCommands();
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
  void modelRolesAndIntents();
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
  QVERIFY(m_controller->addTask({}, fields));
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
  QVERIFY(m_controller->removeGroup(key("g")));
  QVERIFY(!m_controller->getDocument().findGroup(key("g")));
  QVERIFY(!m_controller->getDocument().findList(key("inbox")));
  QVERIFY(!m_controller->getDocument().findItem(key("a")));
  QCOMPARE(item("b").m_fields.m_title, QStringLiteral("Other"));
  QVERIFY(m_document->toPlainText().startsWith(header() + QStringLiteral("<!-- file comment -->")));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeDelete);
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
  QVERIFY(!m_controller->removeGroup(key("g")));
  QVERIFY(!m_controller->moveGroup(key("g"), {}));
  QVERIFY(!m_controller->moveList(key("work"), key("h"), {}));
  QCOMPARE(m_document->toPlainText(), source);
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
                     "1e-9,\"large\":9007199254740993,\"object\":{\"untouched\":[1,true]}} -->")));
  const auto oldMetadata = m_controller->getDocument().text(item("a").m_metadataRange);
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
}

void TestTodoViewWindowController::taskAddRemoveUndo() {
  QVERIFY(load(projectionSource()));
  m_controller->setView(TodoViewKind::All);
  TodoTaskFields fields;
  fields.m_title = QStringLiteral("All-view task");
  QVERIFY(!m_controller->addTask({}, fields));
  QVERIFY(m_controller->addTask(key("home"), fields));
  const auto added = list("home").m_items.last();
  QCOMPARE(added.m_fields.m_title, fields.m_title);
  const auto beforeDelete = m_document->toPlainText();
  QVERIFY(m_controller->removeTask(added.m_key));
  QVERIFY(!m_controller->getDocument().findItem(added.m_key));
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeDelete);
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
  const auto source =
      QStringLiteral("<!-- File stays first -->\n\n") + groupedHeader() +
      QStringLiteral("## A\n<!-- todo-list: {\"id\":\"work\",\"extra\":-7} -->\n\n"
                     "<!-- List A comment -->\n\n"
                     "- [ ] One\n  <!-- todo-item: {\"id\":\"a\",\"extra\":1e-9} -->\n\n"
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
  QCOMPARE(list("work").m_properties.value(QStringLiteral("extra")).toInt(), -7);
  m_document->undo();
  QCOMPARE(m_document->toPlainText(), beforeListMove);
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
  m_controller->setSourceRefreshEnabled(false);
  QTextCursor cursor(m_document.get());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n<!-- external -->\n"));
  const auto changedSource = m_document->toPlainText();
  auto checked = fields;
  checked.m_checked = true;
  QVERIFY(!m_controller->updateTask(key("a"), checked));
  QCOMPARE(m_document->toPlainText(), changedSource);
  m_controller->setSourceRefreshEnabled(true);
  QVERIFY(m_controller->getDocument().isValid());
  QSignalSpy rejected(m_controller.get(), &TodoViewWindowController::operationRejected);
  m_controller->getTaskModel()->checkedChangeRequested(key("a"), true, revision);
  m_controller->getTaskModel()->moveRequested(key("a"), key("home"), {}, revision);
  QCOMPARE(rejected.count(), 2);
  QVERIFY(!item("a").m_fields.m_checked);
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
  QCOMPARE(index.data(TodoTaskModel::StepsCompletedRole).toInt(), 1);
  QCOMPARE(index.data(TodoTaskModel::StepsTotalRole).toInt(), 2);
  QCOMPARE(index.data(TodoTaskModel::RevisionRole).toInt(), m_document->revision());
  QVERIFY(!model->setData(index, Qt::PartiallyChecked, Qt::CheckStateRole));
  QVERIFY(!model->setData(index, QStringLiteral("true"), TodoTaskModel::CheckedRole));
  TodoTaskModel detached(*m_services);
  detached.setSnapshot(m_controller->getDocument(), m_controller->getProjection(), true,
                       m_document->revision());
  QSignalSpy intent(&detached, &TodoTaskModel::checkedChangeRequested);
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
  const int section = model->getActiveCount();
  QVERIFY(model->setData(model->index(section), false, TodoTaskModel::ExpandedRole));
  QVERIFY(!m_controller->getProjection().m_completedExpanded);
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
  const auto oldIndex = model->index(0);
  m_controller->setCompletedExpanded(true);
  QVERIFY(!model->canDropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
  QVERIFY(!oldIndex.data(TodoTaskModel::IdRole).isValid());
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
  const auto row = model->index(model->rowForKey(key("a")));
  const auto source = m_document->toPlainText();
  QTextCursor cursor(m_document.get());
  QTextBlockFormat format;
  format.setLeftMargin(12);
  cursor.setBlockFormat(format);
  QCOMPARE(m_document->toPlainText(), source);
  QVERIFY(model->setData(row, true, TodoTaskModel::CheckedRole));
  QVERIFY(item("a").m_fields.m_checked);
  QVERIFY(!item("b").m_fields.m_checked);
}

} // namespace tests

QTEST_MAIN(tests::TestTodoViewWindowController)
#include "test_todoviewwindowcontroller.moc"
