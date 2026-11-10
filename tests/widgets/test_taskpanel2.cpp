#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTreeView>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/taskservice.h>
#include <gui/services/themeservice.h>
#include <models/tasktreemodel.h>
#include <models/treefilterproxymodel.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>
#include <widgets/taskpanel2.h>

using namespace vnotex;

namespace tests {

class TestTaskPanel2 : public QObject {
  Q_OBJECT

private slots:
  void containersToggleAndLeavesRun_data();
  void containersToggleAndLeavesRun();
};

namespace {
QModelIndex findRow(QAbstractItemModel *p_model, const QString &p_label,
                    const QModelIndex &p_parent = QModelIndex()) {
  for (int row = 0; row < p_model->rowCount(p_parent); ++row) {
    const auto index = p_model->index(row, 0, p_parent);
    if (index.data().toString() == p_label) {
      return index;
    }
  }
  return {};
}
} // namespace

void TestTaskPanel2::containersToggleAndLeavesRun_data() {
  QTest::addColumn<bool>("filtered");
  QTest::newRow("unfiltered") << false;
  QTest::newRow("filtered") << true;
}

void TestTaskPanel2::containersToggleAndLeavesRun() {
  QFETCH(bool, filtered);
  TempDirFixture fixture;
  QVERIFY(fixture.isValid());
  vxcore_set_test_mode(1);
  VxCoreContextHandle core = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &core), VXCORE_OK);
  auto destroyCore = qScopeGuard([&] { vxcore_context_destroy(core); });
  ConfigCoreService configService(core);
  ConfigMgr2 config(&configService);
  config.init();
  TaskService tasks(&config, nullptr, nullptr, nullptr);
  tasks.init();

  const auto nativeTheme = QFINDTESTDATA("../../src/data/extra/themes/native");
  QVERIFY(!nativeTheme.isEmpty());
  fixture.copyFrom(nativeTheme, QStringLiteral("themes/native"));
  ThemeServiceConfig themeConfig;
  themeConfig.themeName = QStringLiteral("native");
  themeConfig.appDataPath = fixture.path();
  ThemeService themes(themeConfig);
  ServiceLocator services;
  services.registerService(&tasks);
  services.registerService(&themes);

  const auto marker = fixture.filePath(QStringLiteral("activation.txt"));
  const auto command = [&](const QString &p_label, const QString &p_marker) {
    return QJsonObject{{"label", p_label},
                       {"type", "process"},
                       {"command", QCoreApplication::applicationFilePath()},
                       {"args", QJsonArray{"--task-marker", marker, p_marker}},
                       {"options", QJsonObject{{"cwd", fixture.path()}}}};
  };
  auto nested = command(QStringLiteral("Nested"), QStringLiteral("container"));
  nested.insert("tasks", QJsonArray{command(QStringLiteral("Status"), QStringLiteral("leaf"))});
  const QJsonObject root{{"version", "0.1.4"}, {"label", "Git"}, {"tasks", QJsonArray{nested}}};
  const auto taskFolder = tasks.getAppTaskFolder();
  QVERIFY(!taskFolder.isEmpty());
  QVERIFY(QDir().mkpath(taskFolder));
  const auto saveTask = [&](const QString &p_name, const QJsonObject &p_definition) {
    QFile file(QDir(taskFolder).filePath(p_name));
    const auto bytes = QJsonDocument(p_definition).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  QVERIFY(saveTask(QStringLiteral("git.json"), root));
  QVERIFY(saveTask(QStringLiteral("unrelated.json"),
                   QJsonObject{{"version", "0.1.4"}, {"label", "Unrelated"}}));
  tasks.reload();

  TaskPanel2 panel(services);
  auto *tree = panel.findChild<QTreeView *>();
  QVERIFY(tree);
  // This regression checks activation semantics; native smoke keeps animation enabled.
  tree->setAnimated(false);
  auto *proxy = qobject_cast<TreeFilterProxyModel *>(tree->model());
  QVERIFY(proxy);
  auto *model = qobject_cast<TaskTreeModel *>(proxy->sourceModel());
  QVERIFY(model);
  if (filtered) {
    proxy->setFilterText(QStringLiteral("Status"));
  }
  const auto scope = proxy->index(0, 0);
  const auto git = findRow(proxy, QStringLiteral("Git"), scope);
  const auto group = findRow(proxy, QStringLiteral("Nested"), git);
  const auto leaf = findRow(proxy, QStringLiteral("Status"), group);
  QVERIFY(git.isValid());
  QVERIFY(group.isValid());
  QVERIFY(leaf.isValid());
  auto *groupTask = model->taskForIndex(proxy->mapToSource(group));
  auto *leafTask = model->taskForIndex(proxy->mapToSource(leaf));
  QVERIFY(groupTask);
  QVERIFY(leafTask);

  for (const auto &index : {scope, git, group}) {
    tree->setExpanded(index, false);
    emit tree->activated(index);
    QVERIFY(tree->isExpanded(index));
    emit tree->activated(index);
    QVERIFY(!tree->isExpanded(index));
    emit tree->activated(index);
    QVERIFY(tree->isExpanded(index));
  }
  QVERIFY(groupTask->findChildren<QProcess *>().isEmpty());
  QVERIFY(!QFile::exists(marker));

  // Matching only the container hides all its children in the proxy. It must not run.
  proxy->setFilterText(QStringLiteral("Nested"));
  const auto hiddenGit = findRow(proxy, QStringLiteral("Git"), proxy->index(0, 0));
  const auto hiddenGroup = findRow(proxy, QStringLiteral("Nested"), hiddenGit);
  QVERIFY(hiddenGroup.isValid());
  QCOMPARE(proxy->rowCount(hiddenGroup), 0);
  QVERIFY(!groupTask->getChildren().isEmpty());
  tree->setExpanded(hiddenGroup, false);
  emit tree->activated(hiddenGroup);
  QVERIFY(tree->isExpanded(hiddenGroup));
  emit tree->activated(hiddenGroup);
  QVERIFY(!tree->isExpanded(hiddenGroup));
  QVERIFY(groupTask->findChildren<QProcess *>().isEmpty());
  QVERIFY(!QFile::exists(marker));

  proxy->setFilterText(filtered ? QStringLiteral("Status") : QString());
  const auto visibleGit = findRow(proxy, QStringLiteral("Git"), proxy->index(0, 0));
  const auto visibleGroup = findRow(proxy, QStringLiteral("Nested"), visibleGit);
  const auto visibleLeaf = findRow(proxy, QStringLiteral("Status"), visibleGroup);
  QVERIFY(visibleLeaf.isValid());
  int finished = 0;
  connect(leafTask, &Task::finished, &panel, [&](QProcess *) { ++finished; });
  emit tree->activated(visibleLeaf);
  QTRY_COMPARE_WITH_TIMEOUT(finished, 1, 10000);
  QFile output(marker);
  QVERIFY(output.open(QIODevice::ReadOnly));
  QCOMPARE(output.readAll(), QByteArray("leaf\n"));
}

} // namespace tests

int main(int argc, char **argv) {
  if (argc == 4 && QByteArray(argv[1]) == "--task-marker") {
    QCoreApplication app(argc, argv);
    QFile marker(app.arguments().at(2));
    const auto bytes = app.arguments().at(3).toUtf8() + '\n';
    return marker.open(QIODevice::WriteOnly | QIODevice::Append) &&
                   marker.write(bytes) == bytes.size()
               ? 0
               : 1;
  }

  QTemporaryDir isolation;
  if (!isolation.isValid()) {
    return 1;
  }
  const auto path = QFile::encodeName(isolation.path());
  qputenv("TEMP", path);
  qputenv("TMP", path);
  qputenv("TMPDIR", path);
  QApplication app(argc, argv);
  tests::TestTaskPanel2 test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_taskpanel2.moc"
