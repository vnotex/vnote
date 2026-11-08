#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <functional>

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
    ++m_promptCount;
    p_cancelled = m_promptCancel;
    if (m_promptCancel) {
      return QString();
    }
    return m_promptResult.isNull() ? p_default : m_promptResult;
  }
  bool confirmNotebookTrust(const QString &p_name, const QString &p_rootFolder) const override {
    ++m_confirmationCount;
    m_confirmationName = p_name;
    m_confirmationRoot = p_rootFolder;
    if (m_confirmationCallback) {
      m_confirmationCallback();
    }
    return m_confirmTrust;
  }

  QString m_notebookId;
  QString m_bufferPath;
  QString m_bufferNotebookId;
  QString m_bufferRelativePath;
  QString m_selectedText;
  QString m_promptResult;
  bool m_promptCancel = false;
  mutable int m_promptCount = 0;
  bool m_confirmTrust = false;
  mutable int m_confirmationCount = 0;
  mutable QString m_confirmationName;
  mutable QString m_confirmationRoot;
  std::function<void()> m_confirmationCallback;
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
  void testShellArguments_data();
  void testShellArguments();
  void testShellPreflight_data();
  void testShellPreflight();
  void testVariableValuesNotReexpanded();
  void testProcessArguments();
  void testNotebookTrustDeclinesBeforeEvaluation();
  void testNotebookTrustAcceptsAndPersists();
  void testAppTaskNeedsNoNotebookConsent();
  void testNotebookTrustStaleOrigin_data();
  void testNotebookTrustStaleOrigin();
  void testUnknownTaskDoesNotRun();
  void testNotebookTrustSaveFailure();
  void testRunningNotebookRootSurvivesReload();

private:
  // Writes a task JSON file with @p_body into @p_dir and returns its path.
  QString writeTask(const QString &p_dir, const QString &p_name, const QString &p_body);
  QSharedPointer<Task> registerAppTask(TaskService &p_service, const QJsonObject &p_task,
                                       QString &p_path);
  QJsonObject notebookRecord(const QString &p_id) const;

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

static QSharedPointer<Task> taskByFile(const QVector<QSharedPointer<Task>> &p_tasks,
                                       const QString &p_path) {
  for (const auto &task : p_tasks) {
    if (QDir::cleanPath(task->getFile()) == QDir::cleanPath(p_path)) {
      return task;
    }
  }
  return {};
}

QSharedPointer<Task> TestTaskService::registerAppTask(TaskService &p_service,
                                                      const QJsonObject &p_task, QString &p_path) {
  p_path = writeTask(p_service.getAppTaskFolder(),
                     QStringLiteral("ut_%1.json").arg(QUuid::createUuid().toString(QUuid::Id128)),
                     QString::fromUtf8(QJsonDocument(p_task).toJson()));
  if (p_path.isEmpty()) {
    return {};
  }
  p_service.reload();
  return taskByFile(p_service.getAppTasks(), p_path);
}

QJsonObject TestTaskService::notebookRecord(const QString &p_id) const {
  QFile session(QDir(m_configService->getDataPath(DataLocation::Local))
                    .filePath(QStringLiteral("vxsession.json")));
  if (!session.open(QIODevice::ReadOnly)) {
    return {};
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(session.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }
  for (const auto &value : document.object().value(QStringLiteral("notebooks")).toArray()) {
    const auto record = value.toObject();
    if (record.value(QStringLiteral("id")).toString() == p_id) {
      return record;
    }
  }
  return {};
}

static QJsonObject markerTask(const QString &p_cwd, const QString &p_marker) {
  return {{"version", "0.1.3"},
          {"label", "Trust marker"},
          {"type", "process"},
          {"command", QCoreApplication::applicationFilePath()},
          {"args", QJsonArray{"--task-marker", p_marker}},
          {"options", QJsonObject{{"cwd", p_cwd}}}};
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
       {"themes", "tasks", "syntax-highlighting", "web", "dicts", "templates", "docs"}) {
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

// Use Git Bash on Windows, never the System32 WSL launcher.
static QString bashExecutable() {
#ifdef Q_OS_WIN
  const auto git = QStandardPaths::findExecutable(QStringLiteral("git"));
  const auto bash = QFileInfo(git).dir().filePath(QStringLiteral("../bin/bash.exe"));
  return QFileInfo(bash).isExecutable() ? QDir::cleanPath(bash) : QString();
#else
  return QStandardPaths::findExecutable(QStringLiteral("bash"));
#endif
}

void TestTaskService::testShellArguments_data() {
  QTest::addColumn<QString>("shell");
  QTest::addColumn<bool>("powershell");
  QTest::addColumn<bool>("configuredEnv");
  for (bool configured : {false, true}) {
    const auto suffix = configured ? "-configured-env" : "-inherited-env";
    QTest::newRow(qPrintable(QStringLiteral("bash") + suffix))
        << bashExecutable() << false << configured;
#ifdef Q_OS_WIN
    QTest::newRow(qPrintable(QStringLiteral("powershell") + suffix))
        << QStandardPaths::findExecutable(QStringLiteral("powershell")) << true << configured;
#endif
    QTest::newRow(qPrintable(QStringLiteral("pwsh") + suffix))
        << QStandardPaths::findExecutable(QStringLiteral("pwsh")) << true << configured;
  }
}

void TestTaskService::testShellArguments() {
  QFETCH(QString, shell);
  QFETCH(bool, powershell);
  QFETCH(bool, configuredEnv);
  if (shell.isEmpty()) {
    QSKIP("Requested shell is not installed");
  }
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  MockTaskContext context;
  context.m_bufferPath = tmp.filePath(QStringLiteral("report$(echo>VNOTE_TASKS_RCE_PROOF).md"));
  context.m_selectedText =
      QStringLiteral("'\"; & | $(echo>VNOTE_TASKS_RCE_PROOF) `echo>VNOTE_TASKS_RCE_PROOF`\\\n"
                     "${shell:echo>VNOTE_TASKS_RCE_PROOF} ${bufferName} %PATH% !PATH!\t");
  context.m_promptResult = QStringLiteral("quoted 'input' $(echo>VNOTE_TASKS_RCE_PROOF)");
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();

  // Shell builtins serialize argument boundaries without a second native-argv parser.
  const auto command =
      powershell
          ? QStringLiteral("function Write-VNoteArgs { "
                           "[IO.File]::WriteAllBytes((Join-Path (Get-Location) 'argv.bin'), "
                           "[Text.Encoding]::UTF8.GetBytes(($args -join [char]0) + [char]0)); "
                           "[IO.File]::WriteAllText((Join-Path (Get-Location) 'env.txt'), "
                           "$env:VNOTE_TEST_ENV); "
                           "if (!$env:PATH) { exit 9 } }; Write-VNoteArgs")
          : QStringLiteral(
                "vnote_args() { printf '%s\\0' \"$@\" > argv.bin; "
                "printf '%s' \"$VNOTE_TEST_ENV\" > env.txt; test -n \"$PATH\"; }; vnote_args");
  const QByteArray envName("VNOTE_TEST_ENV");
  const auto oldEnv = qgetenv(envName.constData());
  const bool hadEnv = qEnvironmentVariableIsSet(envName.constData());
  qputenv(envName.constData(), "inherited");
  auto restoreEnv = qScopeGuard([&]() {
    if (hadEnv) {
      qputenv(envName.constData(), oldEnv);
    } else {
      qunsetenv(envName.constData());
    }
  });
  QJsonObject options{{"cwd", tmp.path()}, {"shell", QJsonObject{{"executable", shell}}}};
  if (configuredEnv) {
    options.insert("env", QJsonObject{{"VNOTE_TEST_ENV", "configured"},
                                      {"VNOTE_TASK_ARGUMENT_0", "must be overwritten"}});
  }
  const QJsonObject obj{
      {"version", "0.1.3"},
      {"command", command},
      {"args", QJsonArray{"${bufferName}", "prefix=${selectedText}:suffix", "", "${input:msg}"}},
      {"inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}}},
      {"options", options}};
  QString taskFile;
  auto task = registerAppTask(service, obj, taskFile);
  auto cleanupTask = qScopeGuard([&]() { QFile::remove(taskFile); });
  QVERIFY(task);
  auto *process = service.runTask(task.data());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitStatus(), QProcess::NormalExit);
  QCOMPARE(process->exitCode(), 0);
  QFile argvFile(tmp.filePath(QStringLiteral("argv.bin")));
  QVERIFY(argvFile.open(QIODevice::ReadOnly));
  QByteArray expected;
  for (const auto &arg :
       QStringList{QFileInfo(context.m_bufferPath).fileName(),
                   "prefix=" + context.m_selectedText + ":suffix", "", context.m_promptResult}) {
    expected += arg.toUtf8();
    expected += '\0';
  }
  QCOMPARE(argvFile.readAll(), expected);
  QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("VNOTE_TASKS_RCE_PROOF"))));
  QFile envFile(tmp.filePath(QStringLiteral("env.txt")));
  QVERIFY(envFile.open(QIODevice::ReadOnly));
  QCOMPARE(envFile.readAll(), configuredEnv ? QByteArray("configured") : QByteArray("inherited"));
}

void TestTaskService::testShellPreflight_data() {
  QTest::addColumn<QString>("field");
  QTest::newRow("command") << QStringLiteral("command");
  QTest::newRow("shell-args") << QStringLiteral("args");
  QTest::newRow("shell-executable") << QStringLiteral("executable");
  QTest::newRow("cmd-with-args") << QStringLiteral("cmd");
  QTest::newRow("unknown-shell-with-args") << QStringLiteral("unknown");
  QTest::newRow("nul-argument") << QStringLiteral("nul");
}

void TestTaskService::testShellPreflight() {
  QFETCH(QString, field);
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  MockTaskContext context;
  context.m_selectedText = QStringLiteral("${shell:echo>VNOTE_TASKS_RCE_PROOF}");
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  QJsonObject shell;
  QJsonObject obj{
      {"version", "0.1.3"},
      {"command", "echo"},
      {"args", QJsonArray{"${selectedText}"}},
      {"inputs", QJsonArray{QJsonObject{{"id", "location"}, {"type", "promptString"}}}}};
  if (field == "command") {
    obj.insert("command", "echo ${selectedText}");
  } else if (field == "args") {
    shell.insert("args", QJsonArray{"${selectedText}"});
  } else if (field == "executable") {
    shell.insert("executable", "${selectedText}");
  } else if (field == "nul") {
    context.m_selectedText = QString(QChar::Null);
  } else {
    shell.insert("executable", field == "cmd" ? "cmd.exe" : "unsupported-shell");
  }
  QJsonObject options{{"cwd", tmp.path()}, {"shell", shell}};
  if (field == "command" || field == "args" || field == "executable") {
    // Rejection must precede cwd evaluation, so invalid tasks never prompt.
    context.m_promptResult = tmp.path();
    options.insert("cwd", "${input:location}");
  }
  obj.insert("options", options);
  QString taskFile;
  auto task = registerAppTask(service, obj, taskFile);
  auto cleanupTask = qScopeGuard([&]() { QFile::remove(taskFile); });
  QVERIFY(task);
  QSignalSpy output(task.data(), &Task::outputRequested);
  QVERIFY(!service.runTask(task.data()));
  QCOMPARE(output.count(), 1);
  QCOMPARE(context.m_promptCount, 0);
  QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("VNOTE_TASKS_RCE_PROOF"))));
}

void TestTaskService::testVariableValuesNotReexpanded() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  MockTaskContext context;
  context.m_selectedText = QStringLiteral("${shell:echo>VNOTE_TASKS_RCE_PROOF}");
  context.m_promptResult = QStringLiteral("${selectedText}");
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  const QJsonObject obj{
      {"version", "0.1.3"},
      {"command", "echo"},
      {"args", QJsonArray{"${selectedText}", "${input:msg}"}},
      {"inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}}},
      {"options", QJsonObject{{"cwd", tmp.path()}}}};
  auto task = Task::fromFile(writeTask(tmp.path(), QStringLiteral("task.json"),
                                       QString::fromUtf8(QJsonDocument(obj).toJson())),
                             QStringLiteral("en_US"), &service);
  QVERIFY(task);
  const auto args = task->getArgs();
  QCOMPARE(args.at(0), context.m_selectedText);
  QCOMPARE(args.at(1), context.m_promptResult);
  QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("VNOTE_TASKS_RCE_PROOF"))));

  // Only an authored shell variable may invoke its callback. Keep this boundary
  // deterministic, independent of shell startup vs the shell variable's 1s timeout.
  TaskVariableMgr variables(&service);
  variables.init();
  int executions = 0;
  variables.overrideVariable("shell", [&executions](Task *, const QString &) {
    ++executions;
    return QStringLiteral("executed");
  });
  QCOMPARE(variables.evaluate(task.data(), QStringLiteral("${selectedText}/${shell:explicit}")),
           context.m_selectedText + QStringLiteral("/executed"));
  QCOMPARE(executions, 1);
}

void TestTaskService::testProcessArguments() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  MockTaskContext context;
  context.m_selectedText = QStringLiteral("quotes '\" $(echo>VNOTE_TASKS_RCE_PROOF)\n");
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  const QJsonObject obj{{"version", "0.1.3"},
                        {"type", "process"},
                        {"command", "${exeFile}"},
                        {"args", QJsonArray{"--task-argv", "${selectedText}", ""}},
                        {"options", QJsonObject{{"cwd", tmp.path()}}}};
  QString taskFile;
  auto task = registerAppTask(service, obj, taskFile);
  auto cleanupTask = qScopeGuard([&]() { QFile::remove(taskFile); });
  QVERIFY(task);
  auto *process = service.runTask(task.data());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitCode(), 0);
  QFile argvFile(tmp.filePath(QStringLiteral("argv.bin")));
  QVERIFY(argvFile.open(QIODevice::ReadOnly));
  QCOMPARE(argvFile.readAll(), context.m_selectedText.toUtf8() + QByteArray(2, '\0'));
  QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("VNOTE_TASKS_RCE_PROOF"))));
}

void TestTaskService::testNotebookTrustDeclinesBeforeEvaluation() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto root = tmp.filePath(QStringLiteral("notebook"));
  const auto id = m_notebookService->createNotebook(
      root, QStringLiteral("{\"name\":\"Untrusted notebook\"}"), NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  auto cleanup = qScopeGuard([&]() { m_notebookService->closeNotebook(id); });
  MockTaskContext context;
  context.m_notebookId = id;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  int shellEvaluations = 0;
  const_cast<TaskVariableMgr &>(service.getVariableMgr())
      .overrideVariable(QStringLiteral("shell"), [&](Task *, const QString &value) {
        ++shellEvaluations;
        return value == QStringLiteral("command") ? QCoreApplication::applicationFilePath()
               : value == QStringLiteral("cwd")   ? root
                                                  : QStringLiteral("evaluated");
      });
  auto obj = markerTask(root, QStringLiteral("denied.marker"));
  obj.insert("command", "${shell:command}");
  obj.insert("args",
             QJsonArray{"--task-marker", "denied.marker", "${shell:argument}", "${input:msg}"});
  obj.insert("inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}});
  obj.insert("options", QJsonObject{{"cwd", "${shell:cwd}"}});
  // Authored metadata cannot turn a notebook-owned task into an app task or grant trust.
  obj.insert("_scope", "app");
  obj.insert("trusted", true);
  const auto path = writeTask(service.getNotebookTaskFolder(), QStringLiteral("denied.json"),
                              QString::fromUtf8(QJsonDocument(obj).toJson()));
  QVERIFY(!path.isEmpty());
  auto task = taskByFile(service.getNotebookTasks(), path);
  QVERIFY(task);
  QSignalSpy output(&service, &TaskService::taskOutputRequested);
  QVERIFY(!service.runTask(task.data()));
  QCOMPARE(context.m_confirmationCount, 1);
  QCOMPARE(context.m_confirmationName, QStringLiteral("Untrusted notebook"));
  QCOMPARE(QDir::cleanPath(context.m_confirmationRoot), QDir::cleanPath(root));
  QCOMPARE(context.m_promptCount, 0);
  QCOMPARE(shellEvaluations, 0);
  QCOMPARE(output.count(), 0);
  QVERIFY(!QFile::exists(QDir(root).filePath(QStringLiteral("denied.marker"))));
  QVERIFY(!m_notebookService->isNotebookTrusted(id));
  QCOMPARE(notebookRecord(id).value(QStringLiteral("trusted")), QJsonValue(false));
}

void TestTaskService::testNotebookTrustAcceptsAndPersists() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto root = tmp.filePath(QStringLiteral("notebook"));
  const auto id = m_notebookService->createNotebook(
      root, QStringLiteral("{\"name\":\"Approved notebook\"}"), NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  auto cleanup = qScopeGuard([&]() { m_notebookService->closeNotebook(id); });
  MockTaskContext context;
  context.m_notebookId = id;
  context.m_confirmTrust = true;
  context.m_promptResult = QStringLiteral("typed input");
  bool trustedDuringConfirmation = true;
  context.m_confirmationCallback = [&]() {
    trustedDuringConfirmation = m_notebookService->isNotebookTrusted(id);
  };
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  int shellEvaluations = 0;
  bool trustedDuringEvaluation = true;
  const_cast<TaskVariableMgr &>(service.getVariableMgr())
      .overrideVariable(QStringLiteral("shell"), [&](Task *, const QString &value) {
        ++shellEvaluations;
        trustedDuringEvaluation = trustedDuringEvaluation &&
                                  m_notebookService->isNotebookTrusted(id) &&
                                  notebookRecord(id).value(QStringLiteral("trusted")).toBool();
        return value == QStringLiteral("cwd") ? root : QStringLiteral("evaluated");
      });
  auto first = markerTask(root, QStringLiteral("first.marker"));
  first.insert("args",
               QJsonArray{"--task-marker", "first.marker", "${shell:argument}", "${input:msg}"});
  first.insert("inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}});
  first.insert("options", QJsonObject{{"cwd", "${shell:cwd}"}});
  const auto firstPath = writeTask(service.getNotebookTaskFolder(), QStringLiteral("first.json"),
                                   QString::fromUtf8(QJsonDocument(first).toJson()));
  QVERIFY(!firstPath.isEmpty());
  auto second = markerTask(root, QStringLiteral("second.marker"));
  second.insert("tasks",
                QJsonArray{QJsonObject{
                    {"label", "Group"},
                    {"tasks", QJsonArray{QJsonObject{
                                  {"label", "Nested task"},
                                  {"args", QJsonArray{"--task-marker", "nested.marker"}}}}}}});
  const auto secondPath = writeTask(service.getNotebookTaskFolder(), QStringLiteral("second.json"),
                                    QString::fromUtf8(QJsonDocument(second).toJson()));
  QVERIFY(!secondPath.isEmpty());
  auto firstTask = taskByFile(service.getNotebookTasks(), firstPath);
  auto secondTask = taskByFile(service.getNotebookTasks(), secondPath);
  QVERIFY(firstTask);
  QVERIFY(secondTask);
  QVERIFY(!m_notebookService->isNotebookTrusted(id));
  auto *process = service.runTask(firstTask.data());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitStatus(), QProcess::NormalExit);
  QCOMPARE(process->exitCode(), 0);
  QVERIFY(!trustedDuringConfirmation);
  QVERIFY(trustedDuringEvaluation);
  QCOMPARE(shellEvaluations, 2);
  QCOMPARE(context.m_promptCount, 1);
  QCOMPARE(context.m_confirmationCount, 1);
  QVERIFY(m_notebookService->isNotebookTrusted(id));
  QCOMPARE(notebookRecord(id).value(QStringLiteral("trusted")), QJsonValue(true));

  // Once approved, even a declining adapter is not asked for other tasks or descendants.
  context.m_confirmTrust = false;
  process = service.runTask(secondTask.data());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitStatus(), QProcess::NormalExit);
  QCOMPARE(process->exitCode(), 0);
  QCOMPARE(secondTask->getChildren().size(), 1);
  auto *group = secondTask->getChildren().first();
  QCOMPARE(group->getChildren().size(), 1);
  process = service.runTask(group->getChildren().first());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitStatus(), QProcess::NormalExit);
  QCOMPARE(process->exitCode(), 0);
  QCOMPARE(context.m_confirmationCount, 1);
  QCOMPARE(context.m_promptCount, 1);
  QCOMPARE(shellEvaluations, 2);
  for (const auto &name : {"first.marker", "second.marker", "nested.marker"}) {
    QFile marker(QDir(root).filePath(QLatin1String(name)));
    QVERIFY(marker.open(QIODevice::ReadOnly));
    QCOMPARE(marker.readAll(), QByteArray("run\n"));
  }
}

void TestTaskService::testAppTaskNeedsNoNotebookConsent() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto id = m_notebookService->createNotebook(tmp.filePath(QStringLiteral("notebook")),
                                                    QStringLiteral("{\"name\":\"Untrusted\"}"),
                                                    NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  auto cleanup = qScopeGuard([&]() { m_notebookService->closeNotebook(id); });
  MockTaskContext context;
  context.m_notebookId = id;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  auto obj = markerTask(tmp.path(), QStringLiteral("app.marker"));
  obj.insert("_scope", "notebook");
  QString taskFile;
  auto task = registerAppTask(service, obj, taskFile);
  auto cleanupTask = qScopeGuard([&]() { QFile::remove(taskFile); });
  QVERIFY(task);
  auto *process = service.runTask(task.data());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitCode(), 0);
  QCOMPARE(context.m_confirmationCount, 0);
  QVERIFY(!m_notebookService->isNotebookTrusted(id));
  QCOMPARE(notebookRecord(id).value(QStringLiteral("trusted")), QJsonValue(false));
  QFile marker(tmp.filePath(QStringLiteral("app.marker")));
  QVERIFY(marker.open(QIODevice::ReadOnly));
  QCOMPARE(marker.readAll(), QByteArray("run\n"));
  marker.close();

  // Global tasks also remain runnable when no notebook/editor context is installed.
  service.setTaskContext(nullptr);
  process = service.runTask(task.data());
  QVERIFY(process);
  QVERIFY2(process->waitForFinished(20000), qPrintable(process->errorString()));
  QCOMPARE(process->exitCode(), 0);
  QVERIFY(marker.open(QIODevice::ReadOnly));
  QCOMPARE(marker.readAll(), QByteArray("run\nrun\n"));
  QCOMPARE(context.m_confirmationCount, 0);
}

void TestTaskService::testNotebookTrustStaleOrigin_data() {
  QTest::addColumn<QString>("operation");
  QTest::addColumn<bool>("duringConfirmation");
  QTest::addColumn<bool>("initiallyTrusted");
  for (const auto &operation :
       {"reload-notebook", "reload-all", "switch", "close", "replace", "remove-context"}) {
    const auto name = QString::fromLatin1(operation);
    QTest::newRow(qPrintable(name + QStringLiteral("-before-untrusted"))) << name << false << false;
    QTest::newRow(qPrintable(name + QStringLiteral("-before-trusted"))) << name << false << true;
    QTest::newRow(qPrintable(name + QStringLiteral("-during-confirmation")))
        << name << true << false;
  }
}

void TestTaskService::testNotebookTrustStaleOrigin() {
  QFETCH(QString, operation);
  QFETCH(bool, duringConfirmation);
  QFETCH(bool, initiallyTrusted);
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto root = tmp.filePath(QStringLiteral("first"));
  const auto firstId = m_notebookService->createNotebook(
      root, QStringLiteral("{\"name\":\"First\"}"), NotebookType::Bundled);
  QVERIFY(!firstId.isEmpty());
  auto cleanupFirst = qScopeGuard([&]() { m_notebookService->closeNotebook(firstId); });
  const auto secondId = m_notebookService->createNotebook(tmp.filePath(QStringLiteral("second")),
                                                          QStringLiteral("{\"name\":\"Second\"}"),
                                                          NotebookType::Bundled);
  QVERIFY(!secondId.isEmpty());
  auto cleanupSecond = qScopeGuard([&]() { m_notebookService->closeNotebook(secondId); });
  if (initiallyTrusted) {
    QVERIFY(m_notebookService->setNotebookTrusted(firstId, true));
  }
  MockTaskContext context;
  context.m_notebookId = firstId;
  context.m_confirmTrust = true;
  MockTaskContext replacement;
  replacement.m_notebookId = firstId;
  replacement.m_confirmTrust = true;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  int shellEvaluations = 0;
  const_cast<TaskVariableMgr &>(service.getVariableMgr())
      .overrideVariable(QStringLiteral("shell"), [&](Task *, const QString &) {
        ++shellEvaluations;
        return QStringLiteral("evaluated");
      });
  auto obj = markerTask(root, QStringLiteral("stale.marker"));
  obj.insert("args", QJsonArray{"--task-marker", "stale.marker", "${shell:count}", "${input:msg}"});
  obj.insert("inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}});
  const auto path = writeTask(service.getNotebookTaskFolder(), QStringLiteral("stale.json"),
                              QString::fromUtf8(QJsonDocument(obj).toJson()));
  QVERIFY(!path.isEmpty());
  auto task = taskByFile(service.getNotebookTasks(), path);
  QVERIFY(task);
  const auto weakRoot = task.toWeakRef();
  auto *taskPointer = task.data();
  bool actionSucceeded = true;
  bool aliveAfterInvalidation = false;
  const auto invalidate = [&]() {
    if (operation == QStringLiteral("reload-notebook")) {
      service.reloadNotebookTasks();
      service.getNotebookTasks();
    } else if (operation == QStringLiteral("reload-all")) {
      service.reload();
    } else if (operation == QStringLiteral("switch")) {
      // Do not reload: selected identity itself must invalidate cached provenance.
      context.m_notebookId = secondId;
    } else if (operation == QStringLiteral("close")) {
      actionSucceeded = m_notebookService->closeNotebook(firstId);
    } else if (operation == QStringLiteral("replace")) {
      service.setTaskContext(&replacement);
    } else {
      service.setTaskContext(nullptr);
    }
    aliveAfterInvalidation = !weakRoot.isNull();
  };
  if (duringConfirmation) {
    context.m_confirmationCallback = invalidate;
    // Only the service can keep the original root alive across its modal callback.
    task.clear();
  } else {
    invalidate();
  }
  QSignalSpy output(&service, &TaskService::taskOutputRequested);
  QVERIFY(!service.runTask(taskPointer));
  QVERIFY(actionSucceeded);
  QVERIFY(aliveAfterInvalidation);
  QCOMPARE(context.m_confirmationCount, duringConfirmation ? 1 : 0);
  QCOMPARE(replacement.m_confirmationCount, 0);
  QCOMPARE(context.m_promptCount, 0);
  QCOMPARE(replacement.m_promptCount, 0);
  QCOMPARE(shellEvaluations, 0);
  QCOMPARE(output.count(), 0);
  QVERIFY(!QFile::exists(QDir(root).filePath(QStringLiteral("stale.marker"))));
  QVERIFY(!m_notebookService->isNotebookTrusted(secondId));
  QCOMPARE(notebookRecord(secondId).value(QStringLiteral("trusted")), QJsonValue(false));
  if (operation == QStringLiteral("close")) {
    QVERIFY(m_notebookService->getNotebookConfig(firstId).isEmpty());
    QVERIFY(notebookRecord(firstId).isEmpty());
  } else {
    QCOMPARE(m_notebookService->isNotebookTrusted(firstId), initiallyTrusted);
    QCOMPARE(notebookRecord(firstId).value(QStringLiteral("trusted")),
             QJsonValue(initiallyTrusted));
  }
}

void TestTaskService::testUnknownTaskDoesNotRun() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  MockTaskContext context;
  context.m_confirmTrust = true;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  int shellEvaluations = 0;
  const_cast<TaskVariableMgr &>(service.getVariableMgr())
      .overrideVariable(QStringLiteral("shell"), [&](Task *, const QString &) {
        ++shellEvaluations;
        return QStringLiteral("evaluated");
      });
  auto obj = markerTask(tmp.path(), QStringLiteral("unknown.marker"));
  obj.insert("args",
             QJsonArray{"--task-marker", "unknown.marker", "${shell:count}", "${input:msg}"});
  obj.insert("inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}});
  const auto file = writeTask(tmp.path(), QStringLiteral("unknown.json"),
                              QString::fromUtf8(QJsonDocument(obj).toJson()));
  QVERIFY(!file.isEmpty());
  auto unknown = Task::fromFile(file, QStringLiteral("en_US"), &service);
  QVERIFY(unknown);
  QSignalSpy output(&service, &TaskService::taskOutputRequested);
  QVERIFY(!service.runTask(nullptr));
  QVERIFY(!service.runTask(unknown.data()));

  QString registeredFile;
  auto registered = registerAppTask(service, obj, registeredFile);
  auto cleanupTask = qScopeGuard([&]() { QFile::remove(registeredFile); });
  QVERIFY(registered);
  TaskService otherService(m_configMgr, m_notebookService, m_snippetService, &context);
  otherService.init();
  // Loading the same file in another service does not register this Task pointer there.
  QVERIFY(taskByFile(otherService.getAppTasks(), registeredFile));
  QVERIFY(!otherService.runTask(registered.data()));
  TaskService noContextService(m_configMgr, m_notebookService, m_snippetService, nullptr);
  noContextService.init();
  QVERIFY(!noContextService.runTask(unknown.data()));
  TaskService noNotebookService(m_configMgr, nullptr, m_snippetService, &context);
  noNotebookService.init();
  QVERIFY(!noNotebookService.runTask(unknown.data()));
  QCOMPARE(context.m_confirmationCount, 0);
  QCOMPARE(context.m_promptCount, 0);
  QCOMPARE(shellEvaluations, 0);
  QCOMPARE(output.count(), 0);
  QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("unknown.marker"))));
}

void TestTaskService::testNotebookTrustSaveFailure() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto root = tmp.filePath(QStringLiteral("first"));
  const auto firstId = m_notebookService->createNotebook(
      root, QStringLiteral("{\"name\":\"First\"}"), NotebookType::Bundled);
  QVERIFY(!firstId.isEmpty());
  auto cleanupFirst = qScopeGuard([&]() { m_notebookService->closeNotebook(firstId); });
  const auto secondId = m_notebookService->createNotebook(tmp.filePath(QStringLiteral("second")),
                                                          QStringLiteral("{\"name\":\"Second\"}"),
                                                          NotebookType::Bundled);
  QVERIFY(!secondId.isEmpty());
  auto cleanupSecond = qScopeGuard([&]() { m_notebookService->closeNotebook(secondId); });
  MockTaskContext context;
  context.m_notebookId = firstId;
  context.m_confirmTrust = true;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  int shellEvaluations = 0;
  const_cast<TaskVariableMgr &>(service.getVariableMgr())
      .overrideVariable(QStringLiteral("shell"), [&](Task *, const QString &) {
        ++shellEvaluations;
        return QStringLiteral("evaluated");
      });
  auto obj = markerTask(root, QStringLiteral("save-failure.marker"));
  obj.insert("args",
             QJsonArray{"--task-marker", "save-failure.marker", "${shell:count}", "${input:msg}"});
  obj.insert("inputs", QJsonArray{QJsonObject{{"id", "msg"}, {"type", "promptString"}}});
  const auto path = writeTask(service.getNotebookTaskFolder(), QStringLiteral("save-failure.json"),
                              QString::fromUtf8(QJsonDocument(obj).toJson()));
  QVERIFY(!path.isEmpty());
  auto task = taskByFile(service.getNotebookTasks(), path);
  QVERIFY(task);
  const auto sessionPath = QDir(m_configService->getDataPath(DataLocation::Local))
                               .filePath(QStringLiteral("vxsession.json"));
  const auto backupPath =
      sessionPath + QLatin1Char('.') + QUuid::createUuid().toString(QUuid::Id128);
  QFile session(sessionPath);
  QVERIFY(session.open(QIODevice::ReadOnly));
  const auto before = session.readAll();
  session.close();
  QVERIFY(QFile::rename(sessionPath, backupPath));
  auto restoreSession = qScopeGuard([&]() {
    QDir().rmdir(sessionPath);
    QFile::rename(backupPath, sessionPath);
  });
  QVERIFY(QDir().mkdir(sessionPath));
  QSignalSpy output(&service, &TaskService::taskOutputRequested);
  QVERIFY(!service.runTask(task.data()));
  QCOMPARE(context.m_confirmationCount, 1);
  QCOMPARE(context.m_promptCount, 0);
  QCOMPARE(shellEvaluations, 0);
  QCOMPARE(output.count(), 1);
  QVERIFY(output.first().first().toString().contains(task->getLabel()));
  QVERIFY(!m_notebookService->isNotebookTrusted(firstId));
  QVERIFY(!m_notebookService->isNotebookTrusted(secondId));
  QVERIFY(!QFile::exists(QDir(root).filePath(QStringLiteral("save-failure.marker"))));

  QVERIFY(QDir().rmdir(sessionPath));
  QVERIFY(QFile::rename(backupPath, sessionPath));
  restoreSession.dismiss();
  QVERIFY(session.open(QIODevice::ReadOnly));
  QCOMPARE(session.readAll(), before);
  session.close();
  // A later successful session write must not publish the rejected grant from memory.
  QVERIFY(m_notebookService->setNotebookTrusted(secondId, true));
  QCOMPARE(notebookRecord(firstId).value(QStringLiteral("trusted")), QJsonValue(false));
  QCOMPARE(notebookRecord(secondId).value(QStringLiteral("trusted")), QJsonValue(true));
  context.m_confirmTrust = false;
  QVERIFY(!service.runTask(task.data()));
  QCOMPARE(context.m_confirmationCount, 2);
  QCOMPARE(context.m_promptCount, 0);
  QCOMPARE(shellEvaluations, 0);
  QCOMPARE(output.count(), 1);
  QVERIFY(!m_notebookService->isNotebookTrusted(firstId));
}

void TestTaskService::testRunningNotebookRootSurvivesReload() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto rootFolder = tmp.filePath(QStringLiteral("notebook"));
  const auto id = m_notebookService->createNotebook(
      rootFolder, QStringLiteral("{\"name\":\"Running\"}"), NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  auto cleanup = qScopeGuard([&]() { m_notebookService->closeNotebook(id); });
  MockTaskContext context;
  context.m_notebookId = id;
  context.m_confirmTrust = true;
  TaskService service(m_configMgr, m_notebookService, m_snippetService, &context);
  service.init();
  auto obj = markerTask(rootFolder, QStringLiteral("running.marker"));
  obj.insert("tasks",
             QJsonArray{QJsonObject{{"label", "Running child"},
                                    {"args", QJsonArray{"--task-marker", "running.marker",
                                                        "--wait-for", "${selectedText}"}}}});
  const auto path = writeTask(service.getNotebookTaskFolder(), QStringLiteral("running.json"),
                              QString::fromUtf8(QJsonDocument(obj).toJson()));
  QVERIFY(!path.isEmpty());
  auto root = taskByFile(service.getNotebookTasks(), path);
  QVERIFY(root);
  QCOMPARE(root->getChildren().size(), 1);
  auto *child = root->getChildren().first();
  const auto weakRoot = root.toWeakRef();
  QPointer<QProcess> firstProcess;
  QPointer<QProcess> secondProcess;
  auto stopProcesses = qScopeGuard([&]() {
    for (const auto &process : {firstProcess, secondProcess}) {
      if (process && process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(20000);
      }
    }
  });
  context.m_selectedText = QStringLiteral("release-first");
  firstProcess = service.runTask(child);
  QVERIFY(firstProcess);
  QSignalSpy firstFinished(firstProcess.data(),
                           QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished));
  QVERIFY2(firstProcess->waitForStarted(20000), qPrintable(firstProcess->errorString()));
  context.m_selectedText = QStringLiteral("release-second");
  secondProcess = service.runTask(child);
  QVERIFY(secondProcess);
  QSignalSpy secondFinished(secondProcess.data(),
                            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished));
  QVERIFY2(secondProcess->waitForStarted(20000), qPrintable(secondProcess->errorString()));
  QCOMPARE(context.m_confirmationCount, 1);
  root.clear();
  service.reloadNotebookTasks();
  QVERIFY(!weakRoot.isNull());
  QVERIFY(firstProcess);
  QVERIFY(secondProcess);
  QVERIFY(!QFile::exists(QDir(rootFolder).filePath(QStringLiteral("running.marker"))));
  QVERIFY(!writeTask(rootFolder, QStringLiteral("release-first"), QStringLiteral("go")).isEmpty());
  QTRY_COMPARE_WITH_TIMEOUT(firstFinished.count(), 1, 20000);
  QCOMPARE(firstFinished.first().at(0).toInt(), 0);
  QCOMPARE(firstFinished.first().at(1).value<QProcess::ExitStatus>(), QProcess::NormalExit);
  QVERIFY(!weakRoot.isNull());
  QVERIFY(secondProcess);
  QCOMPARE(secondProcess->state(), QProcess::Running);
  QFile marker(QDir(rootFolder).filePath(QStringLiteral("running.marker")));
  QVERIFY(marker.open(QIODevice::ReadOnly));
  QCOMPARE(marker.readAll(), QByteArray("run\n"));
  marker.close();
  QVERIFY(!writeTask(rootFolder, QStringLiteral("release-second"), QStringLiteral("go")).isEmpty());
  QTRY_COMPARE_WITH_TIMEOUT(secondFinished.count(), 1, 20000);
  QCOMPARE(secondFinished.first().at(0).toInt(), 0);
  QCOMPARE(secondFinished.first().at(1).value<QProcess::ExitStatus>(), QProcess::NormalExit);
  QTRY_VERIFY_WITH_TIMEOUT(weakRoot.isNull(), 20000);
  QVERIFY(marker.open(QIODevice::ReadOnly));
  QCOMPARE(marker.readAll(), QByteArray("run\nrun\n"));
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
  if (app.arguments().value(1) == QStringLiteral("--task-argv")) {
    QFile output(QStringLiteral("argv.bin"));
    if (!output.open(QIODevice::WriteOnly)) {
      return 1;
    }
    QByteArray bytes;
    for (const auto &arg : app.arguments().mid(2)) {
      bytes += arg.toUtf8();
      bytes += '\0';
    }
    return output.write(bytes) == bytes.size() ? 0 : 1;
  }
  if (app.arguments().value(1) == QStringLiteral("--task-marker")) {
    const auto arguments = app.arguments();
    const auto waitIndex = arguments.indexOf(QStringLiteral("--wait-for"));
    if (waitIndex >= 0) {
      const auto release = arguments.value(waitIndex + 1);
      if (release.isEmpty()) {
        return 2;
      }
      QElapsedTimer deadline;
      deadline.start();
      while (!QFile::exists(release)) {
        if (deadline.elapsed() >= 15000) {
          return 2;
        }
        QThread::msleep(10);
      }
    }
    QFile marker(arguments.value(2));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Append)) {
      return 1;
    }
    return marker.write("run\n", 4) == 4 ? 0 : 1;
  }
  tests::TestTaskService test;
  return QTest::qExec(&test, argc, argv);
}
#include "test_taskservice.moc"
