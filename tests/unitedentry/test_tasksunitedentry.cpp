#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/task.h>
#include <core/services/taskservice.h>
#include <unitedentry/taskentrydelegate.h>
#include <unitedentry/tasksunitedentry.h>
#include <utils/pathutils.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTasksUnitedEntry : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testLeafFlattening();
  void testNotebookScopePath();
  void testFilterCaseInsensitive();
  void testGroupingParentOnlyNoCommandExcluded();
  void testEmptyStateLabel();
  void testNoMatchingLabel();
  void testActivateRunsAndCloses();
  void testPopulatedToNoMatchClearsTree();
  void testStaleRowRejectedAfterReload();

private:
  QSharedPointer<Task> writeTask(const QString &p_name, const QJsonObject &p_obj);

  // Replaces the real app-tasks folder contents with @p_tasks (label -> command)
  // and reloads m_realService so getAppTasks() reflects them.
  void setAppTasks(const QVector<QPair<QString, QString>> &p_tasks);

  QTemporaryDir m_dir;
  // A TaskService with null dependencies: it never loads/evaluates, it only
  // serves as the required owner pointer for Task::fromFile().
  QScopedPointer<TaskService> m_taskService;

  // A fully-wired, vxcore-backed service for the end-to-end entry tests.
  VxCoreContextHandle m_ctx = nullptr;
  ConfigCoreService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  SnippetCoreService *m_snippetService = nullptr;
  TaskService *m_realService = nullptr;
};

void TestTasksUnitedEntry::initTestCase() {
  QVERIFY(m_dir.isValid());
  m_taskService.reset(new TaskService(nullptr, nullptr, nullptr, nullptr));

  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_ctx), VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  m_configService = new ConfigCoreService(m_ctx);
  m_configMgr = new ConfigMgr2(m_configService);
  m_configMgr->init();
  m_notebookService = new NotebookCoreService(m_ctx);
  m_snippetService = new SnippetCoreService(m_ctx);
  m_realService =
      new TaskService(m_configMgr, m_notebookService, m_snippetService, nullptr);
  m_realService->init();
}

void TestTasksUnitedEntry::cleanupTestCase() {
  delete m_realService;
  m_realService = nullptr;
  delete m_snippetService;
  m_snippetService = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configService;
  m_configService = nullptr;
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
  m_taskService.reset();
}

void TestTasksUnitedEntry::setAppTasks(const QVector<QPair<QString, QString>> &p_tasks) {
  const QString folder = m_realService->getAppTaskFolder();
  QVERIFY(!folder.isEmpty());
  QDir().mkpath(folder);

  // Clear any prior task files for isolation.
  QDir dir(folder);
  for (const auto &f : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
    QFile::remove(dir.filePath(f));
  }

  int i = 0;
  for (const auto &t : p_tasks) {
    QJsonObject obj;
    obj.insert("version", "0.1.3");
    obj.insert("label", t.first);
    obj.insert("command", t.second);
    const QString path = dir.filePath(QStringLiteral("task_%1.json").arg(i++));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(obj).toJson());
    file.close();
  }

  m_realService->reload();
}

QSharedPointer<Task> TestTasksUnitedEntry::writeTask(const QString &p_name,
                                                     const QJsonObject &p_obj) {
  const QString path = m_dir.filePath(p_name + ".json");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return nullptr;
  }
  file.write(QJsonDocument(p_obj).toJson());
  file.close();
  return Task::fromFile(path, QStringLiteral("en_US"), m_taskService.data());
}

void TestTasksUnitedEntry::testLeafFlattening() {
  // A grouping parent (no command, has children) flattens to its runnable
  // leaves; a standalone leaf with a command is kept.
  QJsonObject group;
  group.insert("version", "0.1.3");
  group.insert("label", "Group");
  QJsonArray children;
  children.append(QJsonObject{{"label", "Child A"}, {"command", "echo a"}});
  children.append(QJsonObject{{"label", "Child B"}, {"command", "echo b"}});
  group.insert("tasks", children);

  QJsonObject solo;
  solo.insert("version", "0.1.3");
  solo.insert("label", "Solo");
  solo.insert("command", "echo solo");

  auto groupTask = writeTask("group", group);
  auto soloTask = writeTask("solo", solo);
  QVERIFY(groupTask);
  QVERIFY(soloTask);

  QVector<QSharedPointer<Task>> roots{groupTask, soloTask};
  const auto leaves =
      TasksUnitedEntry::collectRunnableLeaves(roots, QStringLiteral("app"), QString());
  QCOMPARE(leaves.size(), 3);

  QStringList labels;
  QStringList paths;
  for (const auto &leaf : leaves) {
    labels << leaf.task->getLabel();
    paths << leaf.path;
  }
  QVERIFY(labels.contains("Child A"));
  QVERIFY(labels.contains("Child B"));
  QVERIFY(labels.contains("Solo"));
  // The grouping parent itself is never a row.
  QVERIFY(!labels.contains("Group"));

  // The path is scope / ancestor labels / leaf label.
  QVERIFY(paths.contains(QStringLiteral("app/Group/Child A")));
  QVERIFY(paths.contains(QStringLiteral("app/Group/Child B")));
  QVERIFY(paths.contains(QStringLiteral("app/Solo")));
}

void TestTasksUnitedEntry::testNotebookScopePath() {
  QJsonObject obj{{"version", "0.1.3"}, {"label", "Sync"}, {"command", "echo sync"}};
  auto task = writeTask("nbscope", obj);
  QVERIFY(task);

  QVector<QSharedPointer<Task>> roots{task};
  const auto leaves =
      TasksUnitedEntry::collectRunnableLeaves(roots, QStringLiteral("notebook"), QString());
  QCOMPARE(leaves.size(), 1);
  QCOMPARE(leaves.first().path, QStringLiteral("notebook/Sync"));
}

void TestTasksUnitedEntry::testFilterCaseInsensitive() {
  QJsonObject a{{"version", "0.1.3"}, {"label", "Build Project"}, {"command", "make"}};
  QJsonObject b{{"version", "0.1.3"}, {"label", "Run Tests"}, {"command", "ctest"}};

  auto ta = writeTask("filter_a", a);
  auto tb = writeTask("filter_b", b);
  QVERIFY(ta);
  QVERIFY(tb);

  QVector<QSharedPointer<Task>> roots{ta, tb};
  const auto leaves =
      TasksUnitedEntry::collectRunnableLeaves(roots, QStringLiteral("app"), QStringLiteral("build"));
  QCOMPARE(leaves.size(), 1);
  QCOMPARE(leaves.first().task->getLabel(), QStringLiteral("Build Project"));
}

void TestTasksUnitedEntry::testGroupingParentOnlyNoCommandExcluded() {
  // A parent with no command and no children is not runnable and is excluded.
  QJsonObject empty{{"version", "0.1.3"}, {"label", "Empty"}};
  auto t = writeTask("empty", empty);
  QVERIFY(t);

  QVector<QSharedPointer<Task>> roots{t};
  const auto leaves =
      TasksUnitedEntry::collectRunnableLeaves(roots, QStringLiteral("app"), QString());
  QVERIFY(leaves.isEmpty());
}

void TestTasksUnitedEntry::testEmptyStateLabel() {
  ServiceLocator services;
  TaskService taskService(nullptr, nullptr, nullptr, nullptr);
  services.registerService<TaskService>(&taskService);

  TasksUnitedEntry entry(services, nullptr);
  QSharedPointer<QWidget> captured;
  entry.process(QString(), [&captured](const QSharedPointer<QWidget> &p_w) { captured = p_w; });

  auto *label = qobject_cast<QLabel *>(captured.data());
  QVERIFY(label);
  QCOMPARE(label->text(), QStringLiteral("No tasks"));
}

void TestTasksUnitedEntry::testNoMatchingLabel() {
  ServiceLocator services;
  TaskService taskService(nullptr, nullptr, nullptr, nullptr);
  services.registerService<TaskService>(&taskService);

  TasksUnitedEntry entry(services, nullptr);
  QSharedPointer<QWidget> captured;
  entry.process(QStringLiteral("nope"),
                [&captured](const QSharedPointer<QWidget> &p_w) { captured = p_w; });

  auto *label = qobject_cast<QLabel *>(captured.data());
  QVERIFY(label);
  QCOMPARE(label->text(), QStringLiteral("No matching tasks"));
}

void TestTasksUnitedEntry::testActivateRunsAndCloses() {
  setAppTasks({{QStringLiteral("Build"), QStringLiteral("echo build")},
               {QStringLiteral("Deploy"), QStringLiteral("echo deploy")}});

  ServiceLocator services;
  services.registerService<TaskService>(m_realService);

  TasksUnitedEntry entry(services, nullptr);
  QSharedPointer<QWidget> captured;
  entry.process(QString(), [&captured](const QSharedPointer<QWidget> &p_w) { captured = p_w; });

  auto *tree = qobject_cast<QTreeWidget *>(captured.data());
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 2);

  // The row-0 task is the one that must actually be dispatched.
  Task *build = nullptr;
  for (const auto &root : m_realService->getAppTasks()) {
    if (root->getLabel() == QStringLiteral("Build")) {
      build = root.data();
      break;
    }
  }
  QVERIFY(build);
  // Row 1 is the bare task label (no origin suffix); the scope path lives in
  // the delegate's PathRole and is drawn as a dimmed second line.
  QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Build"));
  QCOMPARE(tree->topLevelItem(0)->data(0, TaskEntryDelegate::PathRole).toString(),
           QStringLiteral("app/Build"));

  // Task::finished(QProcess*) only fires if runTask actually started a process
  // for this task, so it doubles as the "runTask was reached" assertion.
  // (A lambda rather than QSignalSpy: QProcess* is not a registered metatype.)
  bool ran = false;
  connect(build, &Task::finished, &entry, [&ran](QProcess *) { ran = true; });

  QSignalSpy spy(&entry, &IUnitedEntry::itemActivated);
  emit tree->itemActivated(tree->topLevelItem(0), 0);

  // Activation closes the popup (quit=true, restoreFocus=false).
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.first().at(0).toBool(), true);
  QCOMPARE(spy.first().at(1).toBool(), false);

  // Wait for the spawned process to exit so TaskService releases it before
  // teardown (otherwise the QProcess outlives the test and Qt warns).
  QTRY_VERIFY_WITH_TIMEOUT(ran, 30000);
}

void TestTasksUnitedEntry::testPopulatedToNoMatchClearsTree() {
  setAppTasks({{QStringLiteral("Build"), QStringLiteral("echo")}});

  ServiceLocator services;
  services.registerService<TaskService>(m_realService);

  TasksUnitedEntry entry(services, nullptr);

  // First query populates and shows the tree.
  QSharedPointer<QWidget> first;
  entry.process(QStringLiteral("build"),
                [&first](const QSharedPointer<QWidget> &p_w) { first = p_w; });
  auto *tree = qobject_cast<QTreeWidget *>(first.data());
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 1);

  // A subsequent no-match query shows a label AND clears the (still-owned)
  // tree so a hidden stale row cannot be keyboard-activated.
  QSharedPointer<QWidget> second;
  entry.process(QStringLiteral("zzz"),
                [&second](const QSharedPointer<QWidget> &p_w) { second = p_w; });
  QVERIFY(qobject_cast<QLabel *>(second.data()));

  auto *staleTree = qobject_cast<QTreeWidget *>(entry.currentPopupWidget().data());
  QVERIFY(staleTree);
  QCOMPARE(staleTree->topLevelItemCount(), 0);
}

void TestTasksUnitedEntry::testStaleRowRejectedAfterReload() {
  setAppTasks({{QStringLiteral("Build"), QStringLiteral("echo")}});

  ServiceLocator services;
  services.registerService<TaskService>(m_realService);

  TasksUnitedEntry entry(services, nullptr);
  QSharedPointer<QWidget> captured;
  entry.process(QString(), [&captured](const QSharedPointer<QWidget> &p_w) { captured = p_w; });
  auto *tree = qobject_cast<QTreeWidget *>(captured.data());
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 1);

  // Reload with an empty task set: the previously displayed Task objects are
  // destroyed while the popup is still open.
  setAppTasks({});

  QSignalSpy spy(&entry, &IUnitedEntry::itemActivated);
  emit tree->itemActivated(tree->topLevelItem(0), 0);

  // The QPointer-backed row is now null, so activation is safely rejected.
  QCOMPARE(spy.count(), 0);
}

} // namespace tests

QTEST_MAIN(tests::TestTasksUnitedEntry)
#include "test_tasksunitedentry.moc"
