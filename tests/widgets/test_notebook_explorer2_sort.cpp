// T11 (notebook-explorer-drag-reorder) — wires SortDialog2 into the
// NotebookExplorer2 / CombinedNodeExplorer chain. This test exercises:
//
//   1. NotebookExplorer2::runSortDialogsForChildren (the testable seam used
//      by the onSortRequested slot). 7 sub-tests cover the dialog
//      orchestration semantics (two dialogs in sequence, cancel handling,
//      no-op detection, empty-side skipping).
//
//   2. CombinedNodeExplorer::sortRequested forwarding — emits from the inner
//      NotebookNodeController are re-emitted from the explorer wrapper.
//
//   3. CombinedNodeExplorer::requestReorderNodes — translates flat name lists
//      into NodeIdentifier lists and dispatches to the controller.
//
// Construction of a full NotebookExplorer2 is blocked in tests (see
// tests/widgets/test_notebookexplorer2_multiselect.cpp:37 for the
// long-standing blocker note). The seam approach factors the dialog logic
// out of the slot so the test does not need a full widget instance.

#include <QtTest>

#include <algorithm>

#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>

#include <vxcore/vxcore.h>

#include <controllers/notebooknodecontroller.h>
#include <core/configmgr2.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <views/combinednodeexplorer.h>
#include <widgets/dialogs/sortdialog2.h>
#include <widgets/notebookexplorer2_sortseam.h>

#include "../helpers/temp_dir_fixture.h"

using namespace vnotex;

namespace tests {

namespace {

// Constants mirrored from src/widgets/dialogs/sortdialog2.cpp (private to
// that TU). Tests discover the embedded tree widget via this name per
// src/widgets/dialogs/AGENTS.md "Test-discovery rule" — the object name is the
// contract, the widget type (now QTreeWidget) is not.
const char *kListWidgetName = "sortListWidget";

// Reorder the SortDialog2's rows so that getSortedOrder() returns
// @p_desiredOrder. Mutates the QTreeWidget directly.
void mutateDialogOrder(SortDialog2 *p_dlg, const QStringList &p_desiredOrder) {
  auto *tree = p_dlg->findChild<QTreeWidget *>(QLatin1String(kListWidgetName));
  Q_ASSERT(tree != nullptr);
  // Take every row out, then re-insert in the desired order. We assume
  // p_desiredOrder is a permutation of the current contents. Keying on the
  // raw name in column 0's UserRole matches what getSortedOrder() reads.
  QHash<QString, QTreeWidgetItem *> byName;
  while (tree->topLevelItemCount() > 0) {
    auto *item = tree->takeTopLevelItem(0);
    byName.insert(item->data(0, Qt::UserRole).toString(), item);
  }
  for (const auto &name : p_desiredOrder) {
    auto *it = byName.take(name);
    if (it) {
      tree->addTopLevelItem(it);
    }
  }
  // Any unused items get deleted (shouldn't happen for a true permutation).
  for (auto *leftover : byName.values()) {
    delete leftover;
  }
}

// Click a header section the portable way (signals are protected under Qt 5).
void clickDialogHeader(SortDialog2 *p_dlg, int p_column) {
  auto *tree = p_dlg->findChild<QTreeWidget *>(QLatin1String(kListWidgetName));
  Q_ASSERT(tree != nullptr);
  QMetaObject::invokeMethod(tree->header(), "sectionClicked", Qt::DirectConnection,
                            Q_ARG(int, p_column));
}

// Build a childrenJson value matching the shape returned by
// NotebookCoreService::listFolderChildren. Each entry carries "name", "type"
// and the two epoch-ms timestamps vxcore always emits.
//
// The timestamps are derived from the entry's position so each child gets a
// distinct, predictable key: created = (index+1) * 1000, modified in the
// REVERSE order so a Created sort and a Modified sort cannot both pass with a
// swapped field. Two special cases are injected to pin the "unknown timestamp"
// rule: a name ending in "!zero" emits a literal 0 (vxcore's default for a
// legacy record), and one ending in "!missing" omits the keys entirely. Both
// must reach the dialog as an INVALID QDateTime — 0 must not render 1970.
QJsonObject makeChildrenJson(const QStringList &p_folders, const QStringList &p_files) {
  auto fill = [](QJsonObject &p_o, const QString &p_name, int p_index, int p_total) {
    if (p_name.endsWith(QStringLiteral("!missing"))) {
      return;
    }
    if (p_name.endsWith(QStringLiteral("!zero"))) {
      p_o.insert(QStringLiteral("createdUtc"), 0);
      p_o.insert(QStringLiteral("modifiedUtc"), 0);
      return;
    }
    p_o.insert(QStringLiteral("createdUtc"), static_cast<double>((p_index + 1) * 1000));
    p_o.insert(QStringLiteral("modifiedUtc"), static_cast<double>((p_total - p_index) * 1000));
  };

  QJsonArray foldersArr;
  for (int i = 0; i < p_folders.size(); ++i) {
    QJsonObject o;
    o.insert(QStringLiteral("name"), p_folders.at(i));
    o.insert(QStringLiteral("type"), QStringLiteral("folder"));
    fill(o, p_folders.at(i), i, p_folders.size());
    foldersArr.append(o);
  }
  QJsonArray filesArr;
  for (int i = 0; i < p_files.size(); ++i) {
    QJsonObject o;
    o.insert(QStringLiteral("name"), p_files.at(i));
    o.insert(QStringLiteral("type"), QStringLiteral("file"));
    fill(o, p_files.at(i), i, p_files.size());
    filesArr.append(o);
  }
  QJsonObject root;
  root.insert(QStringLiteral("folders"), foldersArr);
  root.insert(QStringLiteral("files"), filesArr);
  return root;
}

// Polling-based dialog automation: drives a sequence of script steps against
// the modal SortDialog2 dialogs that runSortDialogsForChildren opens. Avoids
// the QTimer-chain race where a 0-ms chained singleShot can fire while the
// first dialog is still being torn down. Each step is invoked exactly once,
// the moment a SortDialog2 modal is observed for the first time after the
// previous step ran (or on initial poll for step 0).
class DialogDriver : public QObject {
public:
  using Step = std::function<void(SortDialog2 *)>;

  explicit DialogDriver(QVector<Step> p_script, QObject *p_parent = nullptr)
      : QObject(p_parent), m_script(std::move(p_script)) {}

  void start(int p_intervalMs = 10) {
    m_lastSeen = nullptr;
    m_currentStep = 0;
    m_timer = new QTimer(this);
    m_timer->setInterval(p_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &DialogDriver::tick);
    m_timer->start();
  }

  int stepsConsumed() const { return m_currentStep; }
  int modalsObserved() const { return m_modalsObserved; }

private:
  void tick() {
    if (m_currentStep >= m_script.size()) {
      m_timer->stop();
      return;
    }
    QWidget *modal = QApplication::activeModalWidget();
    auto *dlg = qobject_cast<SortDialog2 *>(modal);
    if (!dlg) {
      m_lastSeen = nullptr;
      return;
    }
    if (dlg == m_lastSeen) {
      // We already handled this dialog; wait for the next one.
      return;
    }
    m_lastSeen = dlg;
    ++m_modalsObserved;
    const int step = m_currentStep++;
    m_script[step](dlg);
  }

  QVector<Step> m_script;
  QTimer *m_timer = nullptr;
  QPointer<SortDialog2> m_lastSeen;
  int m_currentStep = 0;
  int m_modalsObserved = 0;
};

} // namespace

class TestNotebookExplorer2Sort : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // runSortDialogsForChildren (the testable seam used by onSortRequested).
  void testFoldersAndFilesShowTwoDialogs();     // (1)
  void testAcceptFoldersCancelFiles();          // (2)
  void testCancelBoth();                        // (3)
  void testAcceptUnchangedOrderNoChange();      // (4)
  void testOnlyFilesShowsOnlyFilesDialog();     // (5)
  void testOnlyFoldersShowsOnlyFoldersDialog(); // (6)
  void testEmptyJsonSkipsBothDialogs();         // (7)

  // Column plumbing: the seam must read createdUtc/modifiedUtc off each child
  // and hand them to the dialog, treating 0 / missing as unknown.
  void testHeaderSortByCreatedInSeam();        // (8)
  void testHeaderSortByModifiedInSeam();       // (9)
  void testZeroAndMissingTimestampsAreBlank(); // (10)

  // CombinedNodeExplorer chain.
  void testCombinedExplorerForwardsSortRequested();
  void testCombinedExplorerRequestReorderNodesBuildsIds();

private:
  // The CombinedNodeExplorer chain tests require a real ServiceLocator with
  // a seeded notebook. Bring them up on demand to keep the simpler dialog
  // tests fast.
  void bringUpExplorerHarness();
  void tearDownExplorerHarness();

  NodeIdentifier idFor(const QString &p_relPath) const;

  VxCoreContextHandle m_ctx = nullptr;
  ConfigCoreService *m_configCore = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_notebookSvc = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  ServiceLocator *m_services = nullptr;
  TempDirFixture *m_tempDir = nullptr;
  QString m_nbId;
};

void TestNotebookExplorer2Sort::initTestCase() {
  // CRITICAL: redirect vxcore to test-mode temp dirs BEFORE any context_create
  // (per tests/AGENTS.md).
  vxcore_set_test_mode(1);

  // Q_DECLARE_METATYPE(NodeIdentifier) is in nodeidentifier.h, but the
  // runtime registration is required for QSignalSpy::takeFirst() to extract
  // the value into a QVariant successfully.
  qRegisterMetaType<NodeIdentifier>("NodeIdentifier");
  qRegisterMetaType<NodeIdentifier>("vnotex::NodeIdentifier");
}

void TestNotebookExplorer2Sort::cleanupTestCase() {
  // Per-test resources torn down in cleanup().
}

void TestNotebookExplorer2Sort::init() {
  // Light-weight per-test fixture: only the temp dir. The full explorer
  // harness is opt-in via bringUpExplorerHarness() so the dialog tests do
  // not pay the construction tax.
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());
}

void TestNotebookExplorer2Sort::cleanup() {
  tearDownExplorerHarness();
  delete m_tempDir;
  m_tempDir = nullptr;
}

NodeIdentifier TestNotebookExplorer2Sort::idFor(const QString &p_relPath) const {
  NodeIdentifier id;
  id.notebookId = m_nbId;
  id.relativePath = p_relPath;
  return id;
}

void TestNotebookExplorer2Sort::bringUpExplorerHarness() {
  const QString ctxConfig = QStringLiteral("{}");
  VxCoreError err = vxcore_context_create(ctxConfig.toUtf8().constData(), &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  m_configCore = new ConfigCoreService(m_ctx);
  m_configMgr = new ConfigMgr2(m_configCore);
  m_configMgr->init();

  m_notebookSvc = new NotebookCoreService(m_ctx);
  m_hookMgr = new HookManager();
  m_ioGate = new NotebookIoGate();
  m_notebookSvc->setHookManager(m_hookMgr);
  m_notebookSvc->setNotebookIoGate(m_ioGate);

  m_services = new ServiceLocator();
  m_services->registerService<ConfigCoreService>(m_configCore);
  m_services->registerService<ConfigMgr2>(m_configMgr);
  m_services->registerService<NotebookCoreService>(m_notebookSvc);
  m_services->registerService<HookManager>(m_hookMgr);

  // Seed a notebook with two folders and two files at root.
  const QString nbPath = m_tempDir->filePath("nb");
  const QString cfg = QStringLiteral(R"({"name":"sort_test","version":"1"})");
  m_nbId = m_notebookSvc->createNotebook(nbPath, cfg, NotebookType::Bundled);
  QVERIFY(!m_nbId.isEmpty());

  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("alpha")).isEmpty());
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("beta")).isEmpty());
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QString(), QStringLiteral("one.md")).isEmpty());
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QString(), QStringLiteral("two.md")).isEmpty());
}

void TestNotebookExplorer2Sort::tearDownExplorerHarness() {
  delete m_services;
  m_services = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_notebookSvc;
  m_notebookSvc = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configCore;
  m_configCore = nullptr;
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
  m_nbId.clear();
}

// ============================================================================
// runSortDialogsForChildren sub-tests
// ============================================================================

// (1) Both folders and files non-empty → two dialogs open in sequence
//     (folders first, then files). Both accepted with REORDERED content so
//     both sides of the result are populated.
void TestNotebookExplorer2Sort::testFoldersAndFilesShowTwoDialogs() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");
  parentId.relativePath = QStringLiteral("docs");

  const QJsonObject childrenJson =
      makeChildrenJson({QStringLiteral("alpha"), QStringLiteral("beta")},
                       {QStringLiteral("one.md"), QStringLiteral("two.md")});

  QStringList firstTitle;
  QStringList secondTitle;

  DialogDriver driver({
      [&](SortDialog2 *p_dlg) {
        firstTitle << p_dlg->windowTitle();
        mutateDialogOrder(p_dlg, {QStringLiteral("beta"), QStringLiteral("alpha")});
        p_dlg->accept();
      },
      [&](SortDialog2 *p_dlg) {
        secondTitle << p_dlg->windowTitle();
        mutateDialogOrder(p_dlg, {QStringLiteral("two.md"), QStringLiteral("one.md")});
        p_dlg->accept();
      },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);

  QCOMPARE(driver.modalsObserved(), 2);
  QCOMPARE(firstTitle.size(), 1);
  QVERIFY2(firstTitle.first().contains(QStringLiteral("Folder")),
           qPrintable(QStringLiteral("First dialog title was: %1").arg(firstTitle.first())));
  QCOMPARE(secondTitle.size(), 1);
  QVERIFY2(secondTitle.first().contains(QStringLiteral("Note")),
           qPrintable(QStringLiteral("Second dialog title was: %1").arg(secondTitle.first())));

  QCOMPARE(result.newFolderOrder, (QStringList{QStringLiteral("beta"), QStringLiteral("alpha")}));
  QCOMPARE(result.newFileOrder, (QStringList{QStringLiteral("two.md"), QStringLiteral("one.md")}));
}

// (2) Folders dialog accepted with new order; files dialog cancelled.
//     Result should carry folder order set + EMPTY file order.
void TestNotebookExplorer2Sort::testAcceptFoldersCancelFiles() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson =
      makeChildrenJson({QStringLiteral("alpha"), QStringLiteral("beta")},
                       {QStringLiteral("one.md"), QStringLiteral("two.md")});

  DialogDriver driver({
      [](SortDialog2 *p_dlg) {
        mutateDialogOrder(p_dlg, {QStringLiteral("beta"), QStringLiteral("alpha")});
        p_dlg->accept();
      },
      [](SortDialog2 *p_dlg) { p_dlg->reject(); },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 2);
  QCOMPARE(result.newFolderOrder, (QStringList{QStringLiteral("beta"), QStringLiteral("alpha")}));
  QVERIFY(result.newFileOrder.isEmpty());
}

// (3) User cancels BOTH dialogs → result is empty/empty, no requestReorder.
void TestNotebookExplorer2Sort::testCancelBoth() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson =
      makeChildrenJson({QStringLiteral("alpha"), QStringLiteral("beta")},
                       {QStringLiteral("one.md"), QStringLiteral("two.md")});

  DialogDriver driver({
      [](SortDialog2 *p_dlg) { p_dlg->reject(); },
      [](SortDialog2 *p_dlg) { p_dlg->reject(); },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 2);
  QVERIFY(result.newFolderOrder.isEmpty());
  QVERIFY(result.newFileOrder.isEmpty());
}

// (4) User accepts BOTH dialogs WITHOUT mutating the order. The view-layer
//     no-op check converts "accepted with unchanged order" into "empty
//     result" so the caller skips requestReorderNodes.
void TestNotebookExplorer2Sort::testAcceptUnchangedOrderNoChange() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson =
      makeChildrenJson({QStringLiteral("alpha"), QStringLiteral("beta")},
                       {QStringLiteral("one.md"), QStringLiteral("two.md")});

  DialogDriver driver({
      [](SortDialog2 *p_dlg) { p_dlg->accept(); }, // unchanged
      [](SortDialog2 *p_dlg) { p_dlg->accept(); }, // unchanged
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 2);
  QVERIFY2(result.newFolderOrder.isEmpty(),
           "Accepted-but-unchanged folder order must be reported as empty (no-op).");
  QVERIFY2(result.newFileOrder.isEmpty(),
           "Accepted-but-unchanged file order must be reported as empty (no-op).");
}

// (5) Parent with only files → only the files dialog is shown.
void TestNotebookExplorer2Sort::testOnlyFilesShowsOnlyFilesDialog() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson =
      makeChildrenJson({}, {QStringLiteral("one.md"), QStringLiteral("two.md")});

  QString seenTitle;

  DialogDriver driver({
      [&](SortDialog2 *p_dlg) {
        seenTitle = p_dlg->windowTitle();
        mutateDialogOrder(p_dlg, {QStringLiteral("two.md"), QStringLiteral("one.md")});
        p_dlg->accept();
      },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 1);
  QVERIFY2(seenTitle.contains(QStringLiteral("Note")),
           qPrintable(QStringLiteral("Expected Notes dialog, got: %1").arg(seenTitle)));
  QVERIFY(result.newFolderOrder.isEmpty());
  QCOMPARE(result.newFileOrder, (QStringList{QStringLiteral("two.md"), QStringLiteral("one.md")}));
}

// (6) Parent with only folders → only the folders dialog is shown.
void TestNotebookExplorer2Sort::testOnlyFoldersShowsOnlyFoldersDialog() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson =
      makeChildrenJson({QStringLiteral("alpha"), QStringLiteral("beta")}, {});

  QString seenTitle;

  DialogDriver driver({
      [&](SortDialog2 *p_dlg) {
        seenTitle = p_dlg->windowTitle();
        mutateDialogOrder(p_dlg, {QStringLiteral("beta"), QStringLiteral("alpha")});
        p_dlg->accept();
      },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 1);
  QVERIFY2(seenTitle.contains(QStringLiteral("Folder")),
           qPrintable(QStringLiteral("Expected Folders dialog, got: %1").arg(seenTitle)));
  QCOMPARE(result.newFolderOrder, (QStringList{QStringLiteral("beta"), QStringLiteral("alpha")}));
  QVERIFY(result.newFileOrder.isEmpty());
}

// (7) listFolderChildren returns an EMPTY QJsonObject (e.g., the notebook is
//     unloaded or the parent path is invalid). Both dialogs are skipped; no
//     error, no service call.
void TestNotebookExplorer2Sort::testEmptyJsonSkipsBothDialogs() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject empty;

  // Fail-safe driver: if a dialog erroneously opens, accept it so the test
  // doesn't hang. The modalsObserved counter then becomes the failure
  // indicator (must remain 0).
  DialogDriver driver({
      [](SortDialog2 *p_dlg) { p_dlg->accept(); },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, empty, nullptr);
  // Give the driver a chance to observe a (non-existent) modal.
  QTest::qWait(50);
  QCOMPARE(driver.modalsObserved(), 0);
  QVERIFY(result.newFolderOrder.isEmpty());
  QVERIFY(result.newFileOrder.isEmpty());
}

// ============================================================================
// Column plumbing sub-tests
// ============================================================================

// (8) The seam feeds createdUtc through to the dialog. makeChildrenJson gives
//     created keys in list order, so an ascending Created sort is a no-op and
//     a descending one is the exact reverse — which a swapped or misread field
//     could not reproduce (modifiedUtc runs the OTHER way).
void TestNotebookExplorer2Sort::testHeaderSortByCreatedInSeam() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson = makeChildrenJson(
      {QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}, {});

  DialogDriver driver({
      [](SortDialog2 *p_dlg) {
        clickDialogHeader(p_dlg, 1); // Created, ascending
        clickDialogHeader(p_dlg, 1); // Created, descending
        p_dlg->accept();
      },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 1);
  QCOMPARE(result.newFolderOrder,
           (QStringList{QStringLiteral("gamma"), QStringLiteral("beta"), QStringLiteral("alpha")}));
}

// (9) The seam feeds modifiedUtc through to the dialog. modified keys run in
//     REVERSE list order, so an ASCENDING Modified sort reverses the list.
void TestNotebookExplorer2Sort::testHeaderSortByModifiedInSeam() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson = makeChildrenJson(
      {}, {QStringLiteral("one.md"), QStringLiteral("two.md"), QStringLiteral("three.md")});

  DialogDriver driver({
      [](SortDialog2 *p_dlg) {
        clickDialogHeader(p_dlg, 2); // Modified, ascending
        p_dlg->accept();
      },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 1);
  QCOMPARE(result.newFileOrder, (QStringList{QStringLiteral("three.md"), QStringLiteral("two.md"),
                                             QStringLiteral("one.md")}));
}

// (10) vxcore defaults both timestamps to 0 and emits them unconditionally, so
//      a legacy child arrives as the NUMBER zero rather than an absent key.
//      Both that and a genuinely absent key must render as an EMPTY cell (not
//      1970-01-01) and sort LAST.
void TestNotebookExplorer2Sort::testZeroAndMissingTimestampsAreBlank() {
  NodeIdentifier parentId;
  parentId.notebookId = QStringLiteral("nb-test");

  const QJsonObject childrenJson =
      makeChildrenJson({QStringLiteral("alpha"), QStringLiteral("legacy!zero"),
                        QStringLiteral("old!missing"), QStringLiteral("beta")},
                       {});

  QStringList blanks;
  QStringList ascOrder;

  DialogDriver driver({
      [&](SortDialog2 *p_dlg) {
        auto *tree = p_dlg->findChild<QTreeWidget *>(QLatin1String(kListWidgetName));
        Q_ASSERT(tree != nullptr);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
          auto *item = tree->topLevelItem(i);
          if (item->text(1).isEmpty() && item->text(2).isEmpty() &&
              !item->data(1, Qt::UserRole).isValid() && !item->data(2, Qt::UserRole).isValid()) {
            blanks << item->data(0, Qt::UserRole).toString();
          }
        }
        clickDialogHeader(p_dlg, 1); // Created, ascending
        p_dlg->accept();
      },
  });
  driver.start();

  const auto result = runSortDialogsForChildren(parentId, childrenJson, nullptr);
  QCOMPARE(driver.modalsObserved(), 1);

  std::sort(blanks.begin(), blanks.end());
  QCOMPARE(blanks, (QStringList{QStringLiteral("legacy!zero"), QStringLiteral("old!missing")}));

  // Unknown timestamps sort last, so the two dated folders lead.
  ascOrder = result.newFolderOrder;
  QCOMPARE(ascOrder.size(), 4);
  QCOMPARE(ascOrder.at(0), QStringLiteral("alpha"));
  QCOMPARE(ascOrder.at(1), QStringLiteral("beta"));
  QVERIFY(ascOrder.mid(2).contains(QStringLiteral("legacy!zero")));
  QVERIFY(ascOrder.mid(2).contains(QStringLiteral("old!missing")));
}

// ============================================================================
// CombinedNodeExplorer chain
// ============================================================================

// Forwarding test: NotebookNodeController::sortRequested is re-emitted by
// CombinedNodeExplorer with the same NodeIdentifier argument.
void TestNotebookExplorer2Sort::testCombinedExplorerForwardsSortRequested() {
  bringUpExplorerHarness();

  auto *explorer = new CombinedNodeExplorer(*m_services);
  explorer->setNotebookId(m_nbId);

  // Locate the inner controller — created with `explorer` as parent in
  // CombinedNodeExplorer::setupUI (see src/views/combinednodeexplorer.cpp).
  auto *controller = explorer->findChild<NotebookNodeController *>();
  QVERIFY2(controller != nullptr, "CombinedNodeExplorer must own a NotebookNodeController child");

  QSignalSpy spy(explorer, &CombinedNodeExplorer::sortRequested);
  QVERIFY(spy.isValid());

  // Trigger the controller's sortRequested via its public sortNodes() entry
  // point. sortNodes is the controller's "user invoked Sort" surface; it
  // emits sortRequested for callers to react to (per T8 contract).
  NodeIdentifier rootId = idFor(QString());
  controller->sortNodes(rootId);

  QCOMPARE(spy.count(), 1);
  const QList<QVariant> args = spy.takeFirst();
  const auto forwarded = args.at(0).value<NodeIdentifier>();
  QCOMPARE(forwarded.notebookId, m_nbId);
  QVERIFY(forwarded.relativePath.isEmpty());

  delete explorer;
}

// requestReorderNodes builds NodeIdentifier lists from flat name lists and
// dispatches to the inner controller. We verify the controller emits
// nodesReordered (success) once the service round-trips the request.
void TestNotebookExplorer2Sort::testCombinedExplorerRequestReorderNodesBuildsIds() {
  bringUpExplorerHarness();

  auto *explorer = new CombinedNodeExplorer(*m_services);
  explorer->setNotebookId(m_nbId);

  auto *controller = explorer->findChild<NotebookNodeController *>();
  QVERIFY(controller != nullptr);

  QSignalSpy reorderedSpy(controller, &NotebookNodeController::nodesReordered);
  QSignalSpy errorSpy(controller, &NotebookNodeController::errorOccurred);
  QVERIFY(reorderedSpy.isValid());
  QVERIFY(errorSpy.isValid());

  NodeIdentifier rootId = idFor(QString());
  // Reverse the folder order to provoke a real reorder operation.
  explorer->requestReorderNodes(rootId, {QStringLiteral("beta"), QStringLiteral("alpha")},
                                /*orderedFileNames=*/QStringList());

  // nodesReordered is emitted via QueuedConnection from the worker thread.
  QVERIFY(reorderedSpy.wait(5000));
  QCOMPARE(reorderedSpy.count(), 1);
  QCOMPARE(errorSpy.count(), 0);

  delete explorer;
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookExplorer2Sort)
#include "test_notebook_explorer2_sort.moc"
