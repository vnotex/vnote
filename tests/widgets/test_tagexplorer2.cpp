#include <QtTest>

#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTemporaryDir>

#include <memory>

#include <controllers/tagcontroller.h>
#include <core/servicelocator.h>
#include <core/services/eventbridge.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <gui/services/themeservice.h>
#include <models/tagfilemodel.h>
#include <models/tagmodel.h>
#include <models/treefilterproxymodel.h>
#include <views/tagview.h>
#include <widgets/tagexplorer2.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTagExplorer2 : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void firstShowAndInactiveTab();
  void notebookArrivesAfterShow();
  void hiddenChangesAndCacheReuse();
  void membershipChangesPreserveSelection();
  void hierarchyChangesPreserveSelectionAndReveal();
  void hiddenNotebookSwitch();
  void compatibilityUsesMatchingIntersection();
  void postSyncPublicationDefersWhileHidden();
  void badgeWidthUpdatesWithoutReset();

private:
  QString createNotebook(const QString &p_name);
  void showTags();
  void drainEvents();
  int count(const QString &p_name) const;
  QModelIndex index(const QString &p_name) const;

  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_directory;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<TagCoreService> m_core;
  std::unique_ptr<TagService> m_tags;
  std::unique_ptr<EventBridge> m_bridge;
  std::unique_ptr<ThemeService> m_theme;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<SyncCredentialsStore> m_credentials;
  std::unique_ptr<SyncService> m_sync;
  std::unique_ptr<QMainWindow> m_window;
  QDockWidget *m_dock = nullptr;
  QDockWidget *m_otherDock = nullptr;
  TagExplorer2 *m_panel = nullptr;
  TagModel *m_model = nullptr;
  TagView *m_view = nullptr;
  TagFileModel *m_files = nullptr;
  TreeFilterProxyModel *m_proxy = nullptr;
  QString m_notebookId;
};

void TestTagExplorer2::init() {
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
  m_bridge = std::make_unique<EventBridge>(m_context);
  ThemeServiceConfig themeConfig;
  themeConfig.themeName = QStringLiteral("native");
  const auto nativeTheme = QFINDTESTDATA("../../src/data/extra/themes/native");
  QVERIFY(!nativeTheme.isEmpty());
  QDir themeRoot(nativeTheme);
  QVERIFY(themeRoot.cdUp());
  QVERIFY(themeRoot.cdUp());
  themeConfig.appDataPath = themeRoot.path();
  m_theme = std::make_unique<ThemeService>(themeConfig);
  m_services = std::make_unique<ServiceLocator>();
  m_services->registerService(m_core.get());
  m_services->registerService(m_tags.get());
  m_services->registerService(m_notebooks.get());
  m_services->registerService(m_bridge.get());
  m_services->registerService(m_theme.get());
  m_credentials = std::make_unique<SyncCredentialsStore>(*m_services);
  m_services->registerService(m_credentials.get());
  m_sync = std::make_unique<SyncService>(*m_services);
  m_services->registerService(m_sync.get());

  m_notebookId = createNotebook(QStringLiteral("first"));
  QVERIFY(!m_notebookId.isEmpty());
  QVERIFY(m_core->createTagPath(m_notebookId, QStringLiteral("Parent/Child")));
  QVERIFY(m_core->createTag(m_notebookId, QStringLiteral("Extra")));
  QVERIFY(m_core->createTag(m_notebookId, QStringLiteral("Empty")));
  for (const auto &name :
       {QStringLiteral("one.md"), QStringLiteral("two.md"), QStringLiteral("outside.md")}) {
    QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), name).isEmpty());
  }
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("one.md"), QStringLiteral("Parent")));
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("one.md"), QStringLiteral("Child")));
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("two.md"), QStringLiteral("Child")));
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("outside.md"), QStringLiteral("Extra")));
  QVERIFY(m_core->tagFile(m_notebookId, QStringLiteral("one.md"), QStringLiteral("Empty")));
  QVERIFY(m_core->untagFile(m_notebookId, QStringLiteral("one.md"), QStringLiteral("Empty")));
  drainEvents();

  m_window = std::make_unique<QMainWindow>();
  m_window->resize(640, 520);
  m_panel = new TagExplorer2(*m_services, m_window.get());
  m_model = m_panel->findChild<TagModel *>();
  m_view = m_panel->findChild<TagView *>();
  m_files = m_panel->findChild<TagFileModel *>();
  m_proxy = m_panel->findChild<TreeFilterProxyModel *>();
  QVERIFY(m_model && m_view && m_files && m_proxy);
  m_view->setAnimated(false);
  m_dock = new QDockWidget(QStringLiteral("Tags"), m_window.get());
  m_dock->setWidget(m_panel);
  m_window->addDockWidget(Qt::LeftDockWidgetArea, m_dock);
  m_otherDock = new QDockWidget(QStringLiteral("Other"), m_window.get());
  m_otherDock->setWidget(new QLabel(QStringLiteral("Other panel")));
  m_window->addDockWidget(Qt::LeftDockWidgetArea, m_otherDock);
  m_window->tabifyDockWidget(m_dock, m_otherDock);
  m_otherDock->raise();
  connect(m_dock, &QDockWidget::visibilityChanged, m_panel, &TagExplorer2::setPanelVisible);
}

void TestTagExplorer2::cleanup() {
  m_window.reset();
  m_sync.reset();
  m_credentials.reset();
  m_services.reset();
  m_theme.reset();
  m_bridge.reset();
  for (const auto &notebook : m_notebooks->listNotebooks()) {
    QVERIFY(m_notebooks->closeNotebook(notebook.toObject().value(QStringLiteral("id")).toString()));
  }
  m_tags.reset();
  m_core.reset();
  m_notebooks.reset();
  m_hooks.reset();
  m_gate.reset();
  vxcore_context_destroy(m_context);
  m_context = nullptr;
  m_directory.reset();
}

QString TestTagExplorer2::createNotebook(const QString &p_name) {
  return m_notebooks->createNotebook(m_directory->filePath(p_name),
                                     QStringLiteral("{\"name\":\"Tag count test\"}"),
                                     NotebookType::Bundled);
}

void TestTagExplorer2::drainEvents() {
  // Backend delivery, coalesced model refresh, then queued selection reveal.
  for (int i = 0; i < 4; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  }
  QCoreApplication::processEvents();
}

void TestTagExplorer2::showTags() {
  m_window->show();
  m_dock->show();
  m_dock->raise();
  QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
  drainEvents();
}

int TestTagExplorer2::count(const QString &p_name) const {
  return m_model->indexFromTagName(p_name).data(TagModel::NoteCountRole).toInt();
}

QModelIndex TestTagExplorer2::index(const QString &p_name) const {
  return m_proxy->mapFromSource(m_model->indexFromTagName(p_name));
}

void TestTagExplorer2::firstShowAndInactiveTab() {
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  m_panel->setNotebookId(m_notebookId);
  drainEvents();
  QCOMPARE(reloads.count(), 0);
  QCOMPARE(m_model->rowCount(), 0);
  m_window->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
  drainEvents();
  QCOMPARE(reloads.count(), 0); // A tabified dock can be QWidget-visible but inactive.
  QCOMPARE(m_model->rowCount(), 0);

  showTags();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(count(QStringLiteral("Parent")), 1);
  QCOMPARE(count(QStringLiteral("Child")), 2);
  QCOMPARE(count(QStringLiteral("Extra")), 1);
  QCOMPARE(count(QStringLiteral("Empty")), 0);
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  QCOMPARE(m_files->rowCount(), 2);
  m_proxy->setFilterText(QStringLiteral("Child"));
  QCOMPARE(index(QStringLiteral("Parent")).data(TagModel::NoteCountRole).toInt(), 1);
  QCOMPARE(index(QStringLiteral("Child")).data(TagModel::NoteCountRole).toInt(), 2);
}

void TestTagExplorer2::notebookArrivesAfterShow() {
  showTags();
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  m_panel->setNotebookId(m_notebookId);
  drainEvents();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(count(QStringLiteral("Child")), 2);
  m_panel->setNotebookId(m_notebookId);
  drainEvents();
  QCOMPARE(reloads.count(), 1);
}

void TestTagExplorer2::hiddenChangesAndCacheReuse() {
  m_panel->setNotebookId(m_notebookId);
  showTags();
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  auto *controller = m_panel->findChild<TagController *>();
  QSignalSpy matches(controller, &TagController::matchingNodesChanged);
  m_otherDock->raise();
  drainEvents();
  showTags();
  QCOMPARE(reloads.count(), 0);
  QCOMPARE(matches.count(), 0);

  m_otherDock->raise();
  QVERIFY(m_tags->untagFile(m_notebookId, QStringLiteral("two.md"), QStringLiteral("Child")));
  QVERIFY(m_tags->tagFile(m_notebookId, QStringLiteral("outside.md"), QStringLiteral("Child")));
  QVERIFY(m_tags->tagFile(m_notebookId, QStringLiteral("two.md"), QStringLiteral("Parent")));
  m_model->reload(); // Explicit callers must not bypass suspension.
  drainEvents();
  QCOMPARE(reloads.count(), 0);
  QCOMPARE(matches.count(), 0);
  QCOMPARE(count(QStringLiteral("Parent")), 1);
  showTags();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(matches.count(), 1);
  QCOMPARE(count(QStringLiteral("Parent")), 2);
  QCOMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Child")});
  QCOMPARE(m_files->rowCount(), 2);
  QStringList paths;
  for (int row = 0; row < m_files->rowCount(); ++row) {
    paths.append(m_files->nodeIdFromIndex(m_files->index(row, 0)).relativePath);
  }
  paths.sort();
  QCOMPARE(paths, (QStringList{QStringLiteral("one.md"), QStringLiteral("outside.md")}));

  reloads.clear();
  QVERIFY(m_tags->untagFile(m_notebookId, QStringLiteral("outside.md"), QStringLiteral("Child")));
  m_dock->hide(); // Hide before the already queued refresh runs.
  drainEvents();
  QCOMPARE(reloads.count(), 0);
  QCOMPARE(count(QStringLiteral("Child")), 2);
  showTags();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(count(QStringLiteral("Child")), 1);
}

void TestTagExplorer2::membershipChangesPreserveSelection() {
  m_panel->setNotebookId(m_notebookId);
  showTags();
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  QPersistentModelIndex selected(index(QStringLiteral("Child")));
  QSignalSpy resets(m_model, &QAbstractItemModel::modelReset);
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  QVERIFY(
      !m_notebooks
           ->copyFile(m_notebookId, QStringLiteral("one.md"), QString(), QStringLiteral("copy.md"))
           .isEmpty());
  drainEvents();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(count(QStringLiteral("Child")), 3);
  QCOMPARE(m_files->rowCount(), 3);
  QCOMPARE(resets.count(), 0);
  QVERIFY(selected.isValid());
  QVERIFY(m_view->isExpanded(index(QStringLiteral("Parent"))));
  QCOMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Child")});

  QVERIFY(m_notebooks->unindexNode(m_notebookId, QStringLiteral("copy.md")));
  drainEvents();
  QCOMPARE(count(QStringLiteral("Child")), 2);
  QCOMPARE(m_files->rowCount(), 2);
  QVERIFY(m_notebooks->deleteFile(m_notebookId, QStringLiteral("two.md")));
  drainEvents();
  QCOMPARE(count(QStringLiteral("Child")), 1);
  QCOMPARE(m_files->rowCount(), 1);
  QCOMPARE(resets.count(), 0);
}

void TestTagExplorer2::hierarchyChangesPreserveSelectionAndReveal() {
  m_panel->setNotebookId(m_notebookId);
  showTags();
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  QVERIFY(m_tags->createTag(m_notebookId, QStringLiteral("New")));
  drainEvents();
  QCOMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Child")});
  QCOMPARE(m_view->tagNameFromIndex(m_view->currentIndex()), QStringLiteral("Child"));
  QCOMPARE(m_files->rowCount(), 2);
  QTest::keyClick(m_view, Qt::Key_Up);
  QCOMPARE(m_view->tagNameFromIndex(m_view->currentIndex()), QStringLiteral("Parent"));
  m_view->selectAndScrollToTag(QStringLiteral("Child"));

  auto *controller = m_panel->findChild<TagController *>();
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  controller->onMoveTagAction(m_notebookId, QStringLiteral("Child"), QStringLiteral("Extra"));
  drainEvents();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(m_model->fullTagPath(QStringLiteral("Child")), QStringLiteral("Extra/Child"));
  QCOMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Child")});
  QCOMPARE(count(QStringLiteral("Child")), 2);
  controller->handleNewTagResult(m_notebookId, QStringLiteral("Created"));
  drainEvents();
  QCOMPARE(m_view->selectedTagNames(), QStringList{QStringLiteral("Created")});
}

void TestTagExplorer2::hiddenNotebookSwitch() {
  m_panel->setNotebookId(m_notebookId);
  showTags();
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  const auto second = createNotebook(QStringLiteral("second"));
  QVERIFY(!second.isEmpty());
  QVERIFY(m_core->createTag(second, QStringLiteral("Child")));
  drainEvents();
  m_dock->hide();
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  m_panel->setNotebookId(second);
  drainEvents();
  QCOMPARE(reloads.count(), 0);
  QCOMPARE(m_model->rowCount(), 0);
  QCOMPARE(m_files->rowCount(), 0);
  showTags();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(m_model->rowCount(), 1);
  QVERIFY(m_view->selectedTagNames().isEmpty());
  QVERIFY(!m_model->indexFromTagName(QStringLiteral("Parent")).isValid());
  QCOMPARE(m_files->rowCount(), 0);
}

void TestTagExplorer2::compatibilityUsesMatchingIntersection() {
  m_panel->setNotebookId(m_notebookId);
  showTags();
  m_view->selectAndScrollToTag(QStringLiteral("Parent"));
  m_view->selectionModel()->select(index(QStringLiteral("Child")), QItemSelectionModel::Select);
  QCOMPARE(m_files->rowCount(), 1);
  QVERIFY(
      !(m_model->flags(m_model->indexFromTagName(QStringLiteral("Extra"))) & Qt::ItemIsEnabled));
  QSignalSpy resets(m_model, &QAbstractItemModel::modelReset);
  QVERIFY(m_tags->tagFile(m_notebookId, QStringLiteral("one.md"), QStringLiteral("Extra")));
  drainEvents();
  QCOMPARE(resets.count(), 0);
  QVERIFY(m_model->flags(m_model->indexFromTagName(QStringLiteral("Extra"))) & Qt::ItemIsEnabled);
  QCOMPARE(m_files->rowCount(), 1);
  QVERIFY(m_tags->untagFile(m_notebookId, QStringLiteral("one.md"), QStringLiteral("Extra")));
  drainEvents();
  QVERIFY(
      !(m_model->flags(m_model->indexFromTagName(QStringLiteral("Extra"))) & Qt::ItemIsEnabled));
  QCOMPARE(m_view->selectedTagNames().size(), 2);
}

void TestTagExplorer2::postSyncPublicationDefersWhileHidden() {
  m_panel->setNotebookId(m_notebookId);
  showTags();
  m_view->selectAndScrollToTag(QStringLiteral("Child"));
  m_dock->hide();
  QSignalSpy reloads(m_model, &TagModel::tagsReloaded);
  {
    // External sync installation publishes metadata through workingTreeChanged,
    // not the local TagService mutation signal or per-file bridge deliveries.
    const QSignalBlocker block(m_bridge.get());
    QVERIFY(m_core->untagFile(m_notebookId, QStringLiteral("two.md"), QStringLiteral("Child")));
    QCOMPARE(m_notebooks->refreshAfterSync(m_notebookId), VXCORE_OK);
    drainEvents();
  }
  emit m_sync->workingTreeChanged(m_notebookId, {QStringLiteral("vx_notebook/vx.json")});
  drainEvents();
  QCOMPARE(reloads.count(), 0);
  QCOMPARE(count(QStringLiteral("Child")), 2);
  showTags();
  QCOMPARE(reloads.count(), 1);
  QCOMPARE(count(QStringLiteral("Child")), 1);
  QCOMPARE(m_files->rowCount(), 1);
}

void TestTagExplorer2::badgeWidthUpdatesWithoutReset() {
  const auto tag = QStringLiteral("Long_") + QString(100, QLatin1Char('w'));
  QVERIFY(m_core->createTag(m_notebookId, tag));
  for (int i = 0; i < 9; ++i) {
    const auto file = QStringLiteral("wide%1.md").arg(i);
    QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), file).isEmpty());
    QVERIFY(m_core->tagFile(m_notebookId, file, tag));
  }
  drainEvents();
  m_panel->setNotebookId(m_notebookId);
  showTags();
  QTRY_VERIFY(m_view->horizontalScrollBar()->maximum() > 0);
  QCOMPARE(count(tag), 9);
  const int width = m_view->columnWidth(0);
  const int maximum = m_view->horizontalScrollBar()->maximum();
  QPersistentModelIndex row(index(tag));
  QSignalSpy resets(m_model, &QAbstractItemModel::modelReset);
  QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), QStringLiteral("wide9.md")).isEmpty());
  QVERIFY(m_tags->tagFile(m_notebookId, QStringLiteral("wide9.md"), tag));
  QTRY_COMPARE(count(tag), 10);
  // The existing TreeHorizontalScrollHelper owns coalesced remeasurement on
  // dataChanged. No reset, resize, or forced resizeColumnToContents is needed.
  QTRY_VERIFY(m_view->columnWidth(0) > width);
  QVERIFY(m_view->horizontalScrollBar()->maximum() > maximum);
  QCOMPARE(resets.count(), 0);
  QVERIFY(row.isValid());
}

} // namespace tests

int main(int argc, char **argv) {
  QTemporaryDir isolation;
  if (!isolation.isValid()) {
    return 1;
  }
  const auto path = QFile::encodeName(isolation.path());
  qputenv("TEMP", path);
  qputenv("TMP", path);
  qputenv("TMPDIR", path);
  QApplication app(argc, argv);
  tests::TestTagExplorer2 test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_tagexplorer2.moc"
