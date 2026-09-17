#include <QtTest>

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextStream>

#include <core/configmgr2.h>
#include <core/services/configcoreservice.h>
#include <core/services/itaskcontext.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/task.h>
#include <core/services/taskservice.h>
#include <utils/pathutils.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

// Canned ITaskContext implementation for exercising variable resolution.
class MockTaskContext : public ITaskContext {
public:
  QString currentNotebookId() const override { return m_notebookId; }
  QString currentBufferPath() const override { return m_bufferPath; }
  QString currentBufferNotebookId() const override { return m_bufferNotebookId; }
  QString currentBufferRelativePath() const override { return m_bufferRelativePath; }
  QString selectedText() const override { return m_selectedText; }
  QString promptString(const QString &, const QString &, const QString &p_default, bool,
                       bool &p_cancelled) const override {
    p_cancelled = m_promptCancel;
    if (m_promptCancel) {
      return QString();
    }
    return m_promptResult.isNull() ? p_default : m_promptResult;
  }

  QString m_notebookId;
  QString m_bufferPath;
  QString m_bufferNotebookId;
  QString m_bufferRelativePath;
  QString m_selectedText;
  QString m_promptResult;
  bool m_promptCancel = false;
};

class TestTaskService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testAppTaskLoading();
  void testEmptyTaskCacheReload();
  void testNotebookScopedTaskLoading();
  void testRawNotebookTaskFolderEmpty();
  void testBufferVariables();
  void testBufferNotebookAndRelativePath();
  void testSelectedTextVariable();
  void testNotebookVariables();
  void testNotebookFolderVariables();
  void testConfigVariable();
  void testMagicVariable();
  void testConfigFolderVariables();
  void testInputPromptResolves();
  void testInputPromptCancels();
  void testNullContextResolvesEmpty();

private:
  // Writes a task JSON file with @p_body into @p_dir and returns its path.
  QString writeTask(const QString &p_dir, const QString &p_name, const QString &p_body);

  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  SnippetCoreService *m_snippetService = nullptr;
  MockTaskContext *m_context2 = nullptr;
  TaskService *m_taskService = nullptr;
};

QString TestTaskService::writeTask(const QString &p_dir, const QString &p_name,
                                   const QString &p_body) {
  QDir().mkpath(p_dir);
  const QString path = PathUtils::concatenateFilePath(p_dir, p_name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return QString();
  }
  QTextStream out(&file);
  out << p_body;
  file.close();
  return path;
}

void TestTaskService::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_configService = new ConfigCoreService(m_context);
  m_configMgr = new ConfigMgr2(m_configService);
  m_configMgr->init();

  m_notebookService = new NotebookCoreService(m_context);
  m_snippetService = new SnippetCoreService(m_context);
  m_context2 = new MockTaskContext();

  m_taskService = new TaskService(m_configMgr, m_notebookService, m_snippetService, m_context2);
  m_taskService->init();
}

void TestTaskService::cleanupTestCase() {
  delete m_taskService;
  m_taskService = nullptr;
  delete m_context2;
  m_context2 = nullptr;
  delete m_snippetService;
  m_snippetService = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configService;
  m_configService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestTaskService::testAppTaskLoading() {
  QTemporaryDir bundle;
  QVERIFY(bundle.isValid());
  for (const auto *folder :
       {"themes", "tasks", "syntax-highlighting", "web", "dicts", "templates"}) {
    QVERIFY(QDir(bundle.path()).mkpath(QLatin1String(folder)));
  }
  QVERIFY(!writeTask(bundle.path() + QStringLiteral("/tasks"), QStringLiteral("ut_app_task.json"),
                     QStringLiteral("{\"version\":\"0.1.3\",\"label\":\"UtAppTask\","
                                    "\"command\":\"echo hi\"}"))
               .isEmpty());

  ConfigMgr2 config(m_configService);
  config.setExtraDataSourceRootOverrideForTesting(bundle.path());
  config.init();
  TaskService service(&config, m_notebookService, m_snippetService, nullptr);
  service.init();
  QSignalSpy updated(&service, &TaskService::tasksUpdated);
  const auto installedTask =
      config.getConfigDataFolder(ConfigMgr2::Tasks) + QStringLiteral("/ut_app_task.json");
  auto cleanup = qScopeGuard([&installedTask]() { QFile::remove(installedTask); });

  // Match first launch: initialize the service before installing bundled data.
  config.initAfterQtAppStarted();
  QVERIFY(config.extraDataCopyFailures().isEmpty());
  const auto &tasks = service.getAppTasks();
  QCOMPARE(tasks.size(), 1);
  QCOMPARE(tasks.first()->getLabel(), QStringLiteral("UtAppTask"));
  // A getter must not recursively refresh a model that is already reading it.
  QCOMPARE(updated.count(), 0);
}

void TestTaskService::testEmptyTaskCacheReload() {
  TaskService service(m_configMgr, m_notebookService, m_snippetService, nullptr);
  service.init();
  QVERIFY(service.getAppTasks().isEmpty());

  const auto folder = service.getAppTaskFolder();
  const auto path = writeTask(
      folder, QStringLiteral("ut_cached_task.json"),
      QStringLiteral("{\"version\":\"0.1.3\",\"label\":\"Before\",\"command\":\"echo hi\"}"));
  QVERIFY(!path.isEmpty());
  auto cleanup = qScopeGuard([&path]() { QFile::remove(path); });
  // Empty is a loaded result, not a reason to scan the directory again.
  QVERIFY(service.getAppTasks().isEmpty());

  QSignalSpy updated(&service, &TaskService::tasksUpdated);
  service.reload();
  QCOMPARE(updated.count(), 1);
  QCOMPARE(service.getAppTasks().size(), 1);
  QCOMPARE(service.getAppTasks().first()->getLabel(), QStringLiteral("Before"));

  QVERIFY(!writeTask(folder, QStringLiteral("ut_cached_task.json"),
                     QStringLiteral("{\"version\":\"0.1.3\",\"label\":\"After\","
                                    "\"command\":\"echo hi\"}"))
               .isEmpty());
  QCOMPARE(service.getAppTasks().first()->getLabel(), QStringLiteral("Before"));
  service.reload();
  QCOMPARE(service.getAppTasks().first()->getLabel(), QStringLiteral("After"));
  QCOMPARE(updated.count(), 2);
}

void TestTaskService::testNotebookScopedTaskLoading() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto firstId = m_notebookService->createNotebook(tmp.path() + QStringLiteral("/first"),
                                                         QStringLiteral("{\"name\":\"First\"}"),
                                                         NotebookType::Bundled);
  const auto secondId = m_notebookService->createNotebook(tmp.path() + QStringLiteral("/second"),
                                                          QStringLiteral("{\"name\":\"Second\"}"),
                                                          NotebookType::Bundled);
  QVERIFY(!firstId.isEmpty());
  QVERIFY(!secondId.isEmpty());
  auto cleanup = qScopeGuard([&]() {
    m_notebookService->closeNotebook(firstId);
    m_notebookService->closeNotebook(secondId);
  });

  MockTaskContext context;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, nullptr);
  service.init();
  QVERIFY(service.getNotebookTasks().isEmpty());

  context.m_notebookId = firstId;
  service.setTaskContext(&context);
  const auto firstFolder = service.getNotebookTaskFolder();
  QCOMPARE(QDir::cleanPath(firstFolder), tmp.path() + QStringLiteral("/first/vx_notebook/tasks"));
  // Context injection invalidates the empty cache but must not eagerly scan.
  QVERIFY(
      !writeTask(firstFolder, QStringLiteral("first.json"),
                 QStringLiteral("{\"version\":\"0.1.3\",\"label\":\"First\",\"command\":\"c\"}"))
           .isEmpty());
  QSignalSpy updated(&service, &TaskService::tasksUpdated);
  QCOMPARE(service.getNotebookTasks().size(), 1);
  QCOMPARE(service.getNotebookTasks().first()->getLabel(), QStringLiteral("First"));
  QCOMPARE(updated.count(), 0);

  context.m_notebookId = secondId;
  service.reloadNotebookTasks();
  QCOMPARE(updated.count(), 1);
  QVERIFY(service.getNotebookTasks().isEmpty());
  const auto secondFolder = service.getNotebookTaskFolder();
  QVERIFY(
      !writeTask(secondFolder, QStringLiteral("second.json"),
                 QStringLiteral("{\"version\":\"0.1.3\",\"label\":\"Second\",\"command\":\"c\"}"))
           .isEmpty());
  QVERIFY(service.getNotebookTasks().isEmpty());
  service.reloadNotebookTasks();
  // A file arriving after invalidation must be seen on the next request.
  QVERIFY(!writeTask(secondFolder, QStringLiteral("second.json"),
                     QStringLiteral("{\"version\":\"0.1.3\",\"label\":\"Second updated\","
                                    "\"command\":\"c\"}"))
               .isEmpty());
  QCOMPARE(service.getNotebookTasks().size(), 1);
  QCOMPARE(service.getNotebookTasks().first()->getLabel(), QStringLiteral("Second updated"));
  QCOMPARE(updated.count(), 2);

  MockTaskContext replacement;
  replacement.m_notebookId = firstId;
  service.setTaskContext(&replacement);
  QCOMPARE(service.getNotebookTasks().size(), 1);
  QCOMPARE(service.getNotebookTasks().first()->getLabel(), QStringLiteral("First"));
  service.setTaskContext(nullptr);
  QVERIFY(service.getNotebookTasks().isEmpty());
}

void TestTaskService::testRawNotebookTaskFolderEmpty() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString nbRoot = PathUtils::concatenateFilePath(tmp.path(), QStringLiteral("rawnb"));
  QDir().mkpath(nbRoot);

  const QString id = m_notebookService->createNotebook(nbRoot, QStringLiteral("{\"name\":\"Raw\"}"),
                                                       NotebookType::Raw);
  QVERIFY(!id.isEmpty());
  m_context2->m_notebookId = id;

  // Raw notebooks have no per-notebook config folder, so no task folder.
  QVERIFY(m_taskService->getNotebookTaskFolder().isEmpty());

  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"[${notebookTaskFolder}]\"}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  QCOMPARE(task->getCommand(), QStringLiteral("[]"));

  m_context2->m_notebookId.clear();
  m_notebookService->closeNotebook(id);
}

void TestTaskService::testBufferVariables() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString bufferPath =
      PathUtils::concatenateFilePath(tmp.path(), QStringLiteral("notes/foo.md"));
  m_context2->m_bufferPath = bufferPath;

  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${buffer}\"}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);

  QCOMPARE(task->getCommand(), PathUtils::cleanPath(bufferPath));

  // bufferName / bufferBaseName / bufferExt via args evaluation.
  const QString taskFile2 = writeTask(
      tmp.path(), QStringLiteral("t2.json"),
      QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"c\","
                     "\"args\":[\"${bufferName}\",\"${bufferBaseName}\",\"${bufferExt}\"]}"));
  auto task2 = Task::fromFile(taskFile2, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task2);
  const auto args = task2->getArgs();
  QCOMPARE(args.size(), 3);
  QCOMPARE(args.at(0), QStringLiteral("foo.md"));
  QCOMPARE(args.at(1), QStringLiteral("foo"));
  QCOMPARE(args.at(2), QStringLiteral("md"));

  m_context2->m_bufferPath.clear();
}

void TestTaskService::testBufferNotebookAndRelativePath() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString nbRoot = PathUtils::concatenateFilePath(tmp.path(), QStringLiteral("bufnb"));
  QDir().mkpath(nbRoot);

  const QString id = m_notebookService->createNotebook(
      nbRoot, QStringLiteral("{\"name\":\"BufNB\"}"), NotebookType::Bundled);
  QVERIFY(!id.isEmpty());

  const QString bufferPath = PathUtils::concatenateFilePath(nbRoot, QStringLiteral("sub/note.md"));
  m_context2->m_bufferPath = bufferPath;
  m_context2->m_bufferNotebookId = id;
  m_context2->m_bufferRelativePath = QStringLiteral("sub/note.md");

  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"c\","
                               "\"args\":[\"${bufferNotebookFolder}\",\"${bufferRelativePath}\","
                               "\"${bufferDir}\"]}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  const auto args = task->getArgs();
  QCOMPARE(args.size(), 3);
  QCOMPARE(args.at(0), PathUtils::cleanPath(nbRoot));
  QCOMPARE(args.at(1), PathUtils::cleanPath(QStringLiteral("sub/note.md")));
  QCOMPARE(args.at(2), PathUtils::parentDirPath(bufferPath));

  m_context2->m_bufferPath.clear();
  m_context2->m_bufferNotebookId.clear();
  m_context2->m_bufferRelativePath.clear();
  m_notebookService->closeNotebook(id);
}

void TestTaskService::testSelectedTextVariable() {
  m_context2->m_selectedText = QStringLiteral("hello world");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${selectedText}\"}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  QCOMPARE(task->getCommand(), QStringLiteral("hello world"));
  m_context2->m_selectedText.clear();
}

void TestTaskService::testNotebookVariables() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString nbRoot = PathUtils::concatenateFilePath(tmp.path(), QStringLiteral("mynb"));
  QDir().mkpath(nbRoot);

  const QString configJson =
      QStringLiteral("{\"name\":\"MyNotebook\",\"description\":\"My Desc\"}");
  const QString id = m_notebookService->createNotebook(nbRoot, configJson, NotebookType::Bundled);
  QVERIFY(!id.isEmpty());

  m_context2->m_notebookId = id;

  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"c\","
                               "\"args\":[\"${notebookName}\",\"${notebookDescription}\"]}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  const auto args = task->getArgs();
  QCOMPARE(args.size(), 2);
  QCOMPARE(args.at(0), QStringLiteral("MyNotebook"));
  QCOMPARE(args.at(1), QStringLiteral("My Desc"));

  m_context2->m_notebookId.clear();
  m_notebookService->closeNotebook(id);
}

void TestTaskService::testNotebookFolderVariables() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString nbRoot = PathUtils::concatenateFilePath(tmp.path(), QStringLiteral("foldernb"));
  QDir().mkpath(nbRoot);

  const QString id = m_notebookService->createNotebook(
      nbRoot, QStringLiteral("{\"name\":\"FolderNB\"}"), NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  m_context2->m_notebookId = id;

  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"c\","
                               "\"args\":[\"${notebookFolder}\",\"${notebookFolderName}\","
                               "\"${notebookTaskFolder}\"]}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  const auto args = task->getArgs();
  QCOMPARE(args.size(), 3);
  QCOMPARE(args.at(0), PathUtils::cleanPath(nbRoot));
  QCOMPARE(args.at(1), PathUtils::dirName(nbRoot));
  QCOMPARE(args.at(2), PathUtils::cleanPath(nbRoot + QStringLiteral("/vx_notebook/tasks")));

  m_context2->m_notebookId.clear();
  m_notebookService->closeNotebook(id);
}

void TestTaskService::testConfigVariable() {
  // ${config:...} resolves through ConfigMgr2::parseAndReadConfig. Use a numeric
  // core key (toolbarIconSize) and mirror the production type-mapping so the
  // assertion is independent of the concrete default value.
  const QJsonValue jsonVal =
      m_configMgr->parseAndReadConfig(QStringLiteral("main.core.toolbarIconSize"));
  QCOMPARE(jsonVal.type(), QJsonValue::Double);
  const QString expected = QString::number(jsonVal.toDouble());

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral(
                    "{\"version\":\"0.1.3\",\"command\":\"${config:main.core.toolbarIconSize}\"}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  QCOMPARE(task->getCommand(), expected);
}

void TestTaskService::testMagicVariable() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  // A dynamic built-in snippet (datetime) expands to non-empty text with no
  // residual '%' symbols, matching the SnippetCoreService symbol contract.
  const QString dtFile =
      writeTask(tmp.path(), QStringLiteral("dt.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${magic:datetime}\"}"));
  auto dtTask = Task::fromFile(dtFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(dtTask);
  const QString dtResult = dtTask->getCommand();
  QVERIFY(!dtResult.isEmpty());
  QVERIFY(!dtResult.contains(QLatin1Char('%')));
  // The value embeds seconds; capturing before/after avoids a clock-tick flake.
  const QString before = m_snippetService->applySnippetBySymbol(QStringLiteral("%datetime%"));
  QVERIFY(dtResult == before ||
          dtResult == m_snippetService->applySnippetBySymbol(QStringLiteral("%datetime%")));

  // Reduced-fidelity note: buffer-dependent symbols (note/no) get no overrides
  // in the new-arch path. Pin the behavior so it cannot change unnoticed.
  const QString noteFile =
      writeTask(tmp.path(), QStringLiteral("note.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${magic:note}\"}"));
  auto noteTask = Task::fromFile(noteFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(noteTask);
  QCOMPARE(noteTask->getCommand(),
           m_snippetService->applySnippetBySymbol(QStringLiteral("%note%")));
}

void TestTaskService::testConfigFolderVariables() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"c\","
                               "\"args\":[\"${taskFolder}\",\"${themeFolder}\"]}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  const auto args = task->getArgs();
  QCOMPARE(args.size(), 2);
  QCOMPARE(args.at(0), PathUtils::cleanPath(m_configMgr->getConfigDataFolder(ConfigMgr2::Tasks)));
  QCOMPARE(args.at(1), PathUtils::cleanPath(m_configMgr->getConfigDataFolder(ConfigMgr2::Themes)));
}

void TestTaskService::testInputPromptResolves() {
  m_context2->m_promptCancel = false;
  m_context2->m_promptResult = QStringLiteral("typed-value");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${input:myid}\","
                               "\"inputs\":[{\"id\":\"myid\",\"type\":\"promptString\","
                               "\"description\":\"Enter\"}]}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  QCOMPARE(task->getCommand(), QStringLiteral("typed-value"));
  QVERIFY(!task->isCancelled());
}

void TestTaskService::testInputPromptCancels() {
  m_context2->m_promptCancel = true;

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString taskFile =
      writeTask(tmp.path(), QStringLiteral("t.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${input:myid}\","
                               "\"inputs\":[{\"id\":\"myid\",\"type\":\"promptString\","
                               "\"description\":\"Enter\"}]}"));
  auto task = Task::fromFile(taskFile, QStringLiteral("en_US"), m_taskService);
  QVERIFY(task);
  QCOMPARE(task->getCommand(), QString());
  QVERIFY(task->isCancelled());

  m_context2->m_promptCancel = false;
}

void TestTaskService::testNullContextResolvesEmpty() {
  // TaskService with a null ITaskContext: context-derived variables resolve
  // empty and ${input:...} cancels the task.
  TaskService nullCtxService(m_configMgr, m_notebookService, m_snippetService,
                             /*ITaskContext*/ nullptr);
  nullCtxService.init();

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  const QString bufferTaskFile = writeTask(
      tmp.path(), QStringLiteral("b.json"),
      QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"c\","
                     "\"args\":[\"[${buffer}]\",\"[${bufferName}]\",\"[${bufferDir}]\","
                     "\"[${bufferNotebookFolder}]\",\"[${bufferRelativePath}]\","
                     "\"[${selectedText}]\",\"[${notebookFolder}]\",\"[${notebookName}]\","
                     "\"[${notebookTaskFolder}]\"]}"));
  auto bufferTask = Task::fromFile(bufferTaskFile, QStringLiteral("en_US"), &nullCtxService);
  QVERIFY(bufferTask);
  const auto args = bufferTask->getArgs();
  for (const auto &arg : args) {
    QCOMPARE(arg, QStringLiteral("[]"));
  }

  const QString inputTaskFile =
      writeTask(tmp.path(), QStringLiteral("i.json"),
                QStringLiteral("{\"version\":\"0.1.3\",\"command\":\"${input:myid}\","
                               "\"inputs\":[{\"id\":\"myid\",\"type\":\"promptString\","
                               "\"description\":\"Enter\"}]}"));
  auto inputTask = Task::fromFile(inputTaskFile, QStringLiteral("en_US"), &nullCtxService);
  QVERIFY(inputTask);
  QCOMPARE(inputTask->getCommand(), QString());
  QVERIFY(inputTask->isCancelled());
}

} // namespace tests

int main(int argc, char **argv) {
  // vxcore test mode uses the process temp directory. Give this executable its
  // own root so prior runs and parallel test executables cannot seed its caches.
  QTemporaryDir tempRoot;
  if (!tempRoot.isValid()) {
    return 1;
  }
  const auto path = QFile::encodeName(tempRoot.path());
  qputenv("TMPDIR", path);
  qputenv("TMP", path);
  qputenv("TEMP", path);
  QCoreApplication app(argc, argv);
  tests::TestTaskService test;
  return QTest::qExec(&test, argc, argv);
}
#include "test_taskservice.moc"
