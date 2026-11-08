#include <QtTest>

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <controllers/taskcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/taskservice.h>
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>
#include <widgets/mainwindowtaskcontext.h>

using namespace vnotex;

namespace tests {

class TestNotebookTaskTrust : public QObject {
  Q_OBJECT

private slots:
  void consent_data();
  void consent();
};

namespace {
enum Action { Cancel, Escape, Close, DefaultNo, Accept, MissingService };
}

void TestNotebookTaskTrust::consent_data() {
  QTest::addColumn<int>("action");
  QTest::addColumn<bool>("viaController");
  QTest::newRow("cancel-service") << int(Cancel) << false;
  QTest::newRow("cancel-controller") << int(Cancel) << true;
  QTest::newRow("escape-service") << int(Escape) << false;
  QTest::newRow("close-controller") << int(Close) << true;
  QTest::newRow("default-no-service") << int(DefaultNo) << false;
  QTest::newRow("accept-controller") << int(Accept) << true;
  QTest::newRow("accept-service") << int(Accept) << false;
  QTest::newRow("missing-service") << int(MissingService) << true;
}

void TestNotebookTaskTrust::consent() {
  QFETCH(int, action);
  QFETCH(bool, viaController);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  vxcore_set_test_mode(1);
  VxCoreContextHandle core = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &core), VXCORE_OK);
  auto destroyCore = qScopeGuard([&] { vxcore_context_destroy(core); });
  ConfigCoreService configService(core);
  ConfigMgr2 config(&configService);
  config.init();
  NotebookCoreService notebooks(core);
  const QString name = QStringLiteral("<b>Literal & notebook</b>");
  const auto id = notebooks.createNotebook(
      directory.path(), QString::fromUtf8(QJsonDocument(QJsonObject{{"name", name}}).toJson()),
      NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  auto closeNotebook = qScopeGuard([&] { notebooks.closeNotebook(id); });
  QVERIFY(!notebooks.isNotebookTrusted(id));

  QWidget parent;
  parent.setWindowTitle(QStringLiteral("Notebook task trust integration"));
  parent.resize(480, 200);
  parent.show();
  QVERIFY(QTest::qWaitForWindowExposed(&parent));
  MainWindowTaskContext::Providers providers;
  providers.currentNotebookId = [&] { return id; };
  MainWindowTaskContext context(std::move(providers), &parent);
  TaskService tasks(&config, &notebooks, nullptr, &context);
  tasks.init();
  ServiceLocator services;
  if (action != MissingService) {
    services.registerService(&tasks);
  }
  TaskController controller(services);

  const auto taskFolder = tasks.getNotebookTaskFolder();
  QVERIFY(QDir().mkpath(taskFolder));
  QFile taskFile(QDir(taskFolder).filePath(QStringLiteral("consent.json")));
  QVERIFY(taskFile.open(QIODevice::WriteOnly));
  const QJsonObject definition{{"version", "0.1.3"},
                               {"label", "Native marker"},
                               {"type", "process"},
                               {"command", QCoreApplication::applicationFilePath()},
                               {"args", QJsonArray{"--task-marker"}},
                               {"options", QJsonObject{{"cwd", directory.path()}}}};
  const auto definitionBytes = QJsonDocument(definition).toJson();
  QCOMPARE(taskFile.write(definitionBytes), qint64(definitionBytes.size()));
  taskFile.close();
  const auto roots = tasks.getNotebookTasks();
  QCOMPARE(roots.size(), 1);
  const auto task = roots.first();
  QSignalSpy finished(task.data(), &Task::finished);
  const auto marker = directory.filePath(QStringLiteral("trust-marker.txt"));

  bool acted = false;
  bool timedOut = false;
  bool literalText = false;
  bool warning = false;
  QElapsedTimer deadline;
  deadline.start();
  QTimer driver;
  driver.setInterval(10);
  connect(&driver, &QTimer::timeout, &parent, [&] {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (box && box->parentWidget() == &parent) {
      driver.stop();
      acted = true;
      warning = box->icon() == QMessageBox::Warning;
      literalText = box->textFormat() == Qt::PlainText && box->text().contains(name);
      bool literalLabel = false;
      for (auto *label : box->findChildren<QLabel *>()) {
        if (label->text().contains(name) && label->textFormat() == Qt::PlainText) {
          literalLabel = true;
        }
      }
      literalText = literalText && literalLabel;
      switch (action) {
      case Accept:
        QTest::mouseClick(box->button(QMessageBox::Yes), Qt::LeftButton);
        break;
      case Escape:
        QTest::keyClick(box, Qt::Key_Escape);
        break;
      case Close:
        box->close();
        break;
      case DefaultNo:
        QTest::keyClick(box, Qt::Key_Return);
        break;
      default:
        QTest::mouseClick(box->button(QMessageBox::No), Qt::LeftButton);
        break;
      }
    } else if (deadline.hasExpired(5000)) {
      timedOut = true;
      driver.stop();
      if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
        dialog->reject();
      }
    }
  });
  driver.start();
  QProcess *process = nullptr;
  if (viaController) {
    controller.runTask(task.data());
  } else {
    process = tasks.runTask(task.data());
  }
  driver.stop();
  QVERIFY(!timedOut);
  QCOMPARE(acted, action != MissingService);
  if (acted) {
    QVERIFY(warning);
    QVERIFY(literalText);
  }

  const bool accepted = action == Accept;
  QCOMPARE(notebooks.isNotebookTrusted(id), accepted);
  QFile session(QDir(configService.getDataPath(DataLocation::Local))
                    .filePath(QStringLiteral("vxsession.json")));
  QVERIFY(session.open(QIODevice::ReadOnly));
  const auto records =
      QJsonDocument::fromJson(session.readAll()).object().value("notebooks").toArray();
  bool found = false;
  for (const auto &value : records) {
    const auto record = value.toObject();
    if (record.value(QLatin1String(vxcore::kJsonKeyId)).toString() == id) {
      QCOMPARE(record.value(QLatin1String(vxcore::kJsonKeyTrusted)).toBool(), accepted);
      found = true;
    }
  }
  QVERIFY(found);
  session.close();

  if (!accepted) {
    QVERIFY(!process);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!QFile::exists(marker));
    return;
  }
  if (!viaController) {
    QVERIFY(process);
  }
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
  QFile output(marker);
  QVERIFY(output.open(QIODevice::ReadOnly));
  QCOMPARE(output.readAll(), QByteArray("trusted\n"));
  output.close();
  QVERIFY(QFile::remove(marker));

  // A later launch must retain consent without showing another modal.
  acted = false;
  deadline.restart();
  driver.start();
  QVERIFY(tasks.runTask(task.data()));
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
  driver.stop();
  QVERIFY(!acted);
  QVERIFY(output.open(QIODevice::ReadOnly));
  QCOMPARE(output.readAll(), QByteArray("trusted\n"));
}

} // namespace tests

int main(int argc, char **argv) {
  if (argc > 1 && QByteArray(argv[1]) == "--task-marker") {
    QCoreApplication app(argc, argv);
    QFile marker(QStringLiteral("trust-marker.txt"));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
      return 1;
    }
    return marker.write("trusted\n") == 8 ? 0 : 1;
  }
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    return 1;
  }
  const auto path = QFile::encodeName(temporary.path());
  qputenv("TEMP", path);
  qputenv("TMP", path);
  qputenv("TMPDIR", path);
  QApplication app(argc, argv);
  tests::TestNotebookTaskTrust test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_notebooktasktrust.moc"
