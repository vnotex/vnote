// T6 (notebook-explorer-drag-reorder):
// NotebookCoreService::reorderFolderChildren — async, IoGate-serialized,
// hook-aware service method. Returns immediately; result delivered via
// reorderCompleted(notebookId, folderRelPath, success, errorMessage).
//
// Six sub-tests cover the full contract:
//   1. success path:        before_reorder -> vxcore -> after_reorder -> signal(true)
//   2. no-op path:          identical order -> no hooks, no vxcore call, signal(true)
//   3. vxcore error path:   bad permutation -> signal(false, msg); no after_reorder
//   4. hook cancel path:    before_reorder cancels -> no vxcore, signal(false, "Cancelled by hook")
//   5. IoGate serialization: external lock blocks reorder until released
//   6. after-hook lock free: after_reorder can re-acquire the IoGate (no deadlock)

#include <atomic>

#include <QAtomicInt>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookCoreServiceReorder : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testSuccessPathFiresBothHooksInOrder();
  void testNoOpSkipsHooksAndVxcore();
  void testVxcoreErrorSkipsAfterHookAndReportsFailure();
  void testHookCancelSkipsVxcoreAndReportsCancelled();
  void testIoGateSerializesWithExternalHolder();
  void testAfterHookFiresAfterLockRelease();

private:
  // Build a fresh notebook with the canonical "reorder fixture":
  //   <nb>/sub_a/   <nb>/sub_b/   <nb>/sub_c/
  //   <nb>/file1.md <nb>/file2.md <nb>/file3.md
  // Returns notebookId; out-params receive the seeded orders.
  QString seedNotebook(const QString &p_baseName, QStringList *p_outFolderOrder = nullptr,
                       QStringList *p_outFileOrder = nullptr);

  // Collect direct children of "" (notebook root) into QStringList of names.
  QStringList listFolders(const QString &p_notebookId, const QString &p_folderRelPath = QString());
  QStringList listFiles(const QString &p_notebookId, const QString &p_folderRelPath = QString());

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  TempDirFixture m_tempDir;
};

void TestNotebookCoreServiceReorder::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // Test mode MUST be set before context create — protects real AppData.
  vxcore_set_test_mode(1);

  QString configJson = QStringLiteral("{}");
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_ioGate = new NotebookIoGate();
  m_hookMgr = new HookManager(this);
  m_service = new NotebookCoreService(m_context, this);
  m_service->setHookManager(m_hookMgr);
  m_service->setNotebookIoGate(m_ioGate);
}

void TestNotebookCoreServiceReorder::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotebookCoreServiceReorder::cleanup() {
  // Close every open notebook so each test starts clean.
  QJsonArray nbs = m_service->listNotebooks();
  for (const auto &v : nbs) {
    QString id = v.toObject().value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      m_service->closeNotebook(id);
    }
  }
}

QString TestNotebookCoreServiceReorder::seedNotebook(const QString &p_baseName,
                                                     QStringList *p_outFolderOrder,
                                                     QStringList *p_outFileOrder) {
  const QString nbPath = m_tempDir.filePath(p_baseName);
  const QString cfg = QStringLiteral("{\"name\":\"Reorder Notebook\",\"version\":\"1\"}");
  const QString nbId = m_service->createNotebook(nbPath, cfg, NotebookType::Bundled);
  if (nbId.isEmpty()) {
    return QString();
  }

  // Create three subfolders + three files at notebook root.
  for (const QString &name :
       QStringList{QStringLiteral("sub_a"), QStringLiteral("sub_b"), QStringLiteral("sub_c")}) {
    QString id = m_service->createFolder(nbId, QString(), name);
    if (id.isEmpty()) {
      qWarning() << "seedNotebook: createFolder failed for" << name;
      return QString();
    }
  }
  for (const QString &name : QStringList{QStringLiteral("file1.md"), QStringLiteral("file2.md"),
                                         QStringLiteral("file3.md")}) {
    QString id = m_service->createFile(nbId, QString(), name);
    if (id.isEmpty()) {
      qWarning() << "seedNotebook: createFile failed for" << name;
      return QString();
    }
  }

  if (p_outFolderOrder) {
    *p_outFolderOrder = listFolders(nbId);
  }
  if (p_outFileOrder) {
    *p_outFileOrder = listFiles(nbId);
  }
  return nbId;
}

QStringList TestNotebookCoreServiceReorder::listFolders(const QString &p_notebookId,
                                                        const QString &p_folderRelPath) {
  QStringList out;
  const QJsonObject obj = m_service->listFolderChildren(p_notebookId, p_folderRelPath);
  for (const QJsonValue &v : obj.value(QStringLiteral("folders")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

QStringList TestNotebookCoreServiceReorder::listFiles(const QString &p_notebookId,
                                                      const QString &p_folderRelPath) {
  QStringList out;
  const QJsonObject obj = m_service->listFolderChildren(p_notebookId, p_folderRelPath);
  for (const QJsonValue &v : obj.value(QStringLiteral("files")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

// ===== 1. Success path =====
void TestNotebookCoreServiceReorder::testSuccessPathFiresBothHooksInOrder() {
  QStringList seedFolders, seedFiles;
  const QString nbId = seedNotebook(QStringLiteral("nb_success"), &seedFolders, &seedFiles);
  QVERIFY(!nbId.isEmpty());
  QCOMPARE(seedFolders.size(), 3);
  QCOMPARE(seedFiles.size(), 3);

  // Permute: reverse both lists.
  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());
  QStringList newFiles = seedFiles;
  std::reverse(newFiles.begin(), newFiles.end());

  // Record hook fire order + the payload each callback received.
  QList<QString> hookOrder;
  NodeReorderEvent capturedBefore, capturedAfter;
  int beforeId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeBeforeReorder,
      [&](HookContext &, const NodeReorderEvent &p_ev) {
        hookOrder.append(QStringLiteral("before"));
        capturedBefore = p_ev;
      },
      10);
  int afterId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeAfterReorder,
      [&](HookContext &, const NodeReorderEvent &p_ev) {
        hookOrder.append(QStringLiteral("after"));
        capturedAfter = p_ev;
      },
      10);

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);

  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);

  // Wait up to 5s for the async signal.
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(0).toString(), nbId);
  QCOMPARE(args.at(1).toString(), QString());
  QCOMPARE(args.at(2).toBool(), true);
  QCOMPARE(args.at(3).toString(), QString());

  // Hook ordering.
  QCOMPARE(hookOrder.size(), 2);
  QCOMPARE(hookOrder[0], QStringLiteral("before"));
  QCOMPARE(hookOrder[1], QStringLiteral("after"));

  // Payload fidelity.
  QCOMPARE(capturedBefore.notebookId, nbId);
  QCOMPARE(capturedBefore.previousFolderOrder, seedFolders);
  QCOMPARE(capturedBefore.previousFileOrder, seedFiles);
  QCOMPARE(capturedBefore.newFolderOrder, newFolders);
  QCOMPARE(capturedBefore.newFileOrder, newFiles);
  QCOMPARE(capturedAfter.newFolderOrder, newFolders);
  QCOMPARE(capturedAfter.newFileOrder, newFiles);

  // Disk truly reordered.
  QCOMPARE(listFolders(nbId), newFolders);
  QCOMPARE(listFiles(nbId), newFiles);

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

// ===== 2. No-op path =====
void TestNotebookCoreServiceReorder::testNoOpSkipsHooksAndVxcore() {
  QStringList seedFolders, seedFiles;
  const QString nbId = seedNotebook(QStringLiteral("nb_noop"), &seedFolders, &seedFiles);
  QVERIFY(!nbId.isEmpty());

  // We can't directly count vxcore calls from outside, but the no-op contract
  // mandates that disk be untouched. We assert on vx.json mtime stability AND
  // hook fire counts.
  QFileInfo vxJson(m_tempDir.filePath(QStringLiteral("nb_noop/vx_notebook/vx.json")));
  // Some vxcore impls put per-folder vx.json under the folder; for root the
  // notebook config is the relevant artifact. Use the notebook's vx_notebook
  // folder mtime as a conservative proxy.
  QFileInfo vxFolder(m_tempDir.filePath(QStringLiteral("nb_noop/vx_notebook")));
  const QDateTime preMtime = vxFolder.lastModified();

  int beforeCount = 0;
  int afterCount = 0;
  int beforeId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeBeforeReorder, [&](HookContext &, const NodeReorderEvent &) { ++beforeCount; },
      10);
  int afterId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeAfterReorder, [&](HookContext &, const NodeReorderEvent &) { ++afterCount; },
      10);

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);

  // Pass the EXACT current order for both.
  m_service->reorderFolderChildren(nbId, QString(), seedFolders, seedFiles);

  // Allow the queued signal (if any) to fly; no-op path emits synchronously,
  // so the spy should already have it. spy.wait() returns immediately if there's
  // already an entry, otherwise it polls.
  if (spy.isEmpty()) {
    QVERIFY(spy.wait(500));
  }
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(2).toBool(), true);
  QCOMPARE(args.at(3).toString(), QString());

  // No hooks fired.
  QCOMPARE(beforeCount, 0);
  QCOMPARE(afterCount, 0);

  // Disk untouched.
  vxFolder.refresh();
  QCOMPARE(vxFolder.lastModified(), preMtime);

  // Order unchanged.
  QCOMPARE(listFolders(nbId), seedFolders);
  QCOMPARE(listFiles(nbId), seedFiles);

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

// ===== 3. vxcore error path =====
void TestNotebookCoreServiceReorder::testVxcoreErrorSkipsAfterHookAndReportsFailure() {
  QStringList seedFolders, seedFiles;
  const QString nbId = seedNotebook(QStringLiteral("nb_vxerr"), &seedFolders, &seedFiles);
  QVERIFY(!nbId.isEmpty());

  // Bad permutation: includes a name that does not exist among children.
  QStringList badFolders = QStringList{QStringLiteral("sub_a"), QStringLiteral("sub_b"),
                                       QStringLiteral("does_not_exist")};

  int afterCount = 0;
  int afterId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeAfterReorder, [&](HookContext &, const NodeReorderEvent &) { ++afterCount; },
      10);

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), badFolders, QStringList());

  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(2).toBool(), false);
  QVERIFY(!args.at(3).toString().isEmpty());

  // After-hook MUST NOT fire on failure.
  QCOMPARE(afterCount, 0);

  // Original order preserved.
  QCOMPARE(listFolders(nbId), seedFolders);
  QCOMPARE(listFiles(nbId), seedFiles);

  m_hookMgr->removeAction(afterId);
}

// ===== 4. Hook cancel path =====
void TestNotebookCoreServiceReorder::testHookCancelSkipsVxcoreAndReportsCancelled() {
  QStringList seedFolders, seedFiles;
  const QString nbId = seedNotebook(QStringLiteral("nb_cancel"), &seedFolders, &seedFiles);
  QVERIFY(!nbId.isEmpty());

  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());

  int beforeCount = 0;
  int afterCount = 0;
  int beforeId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeBeforeReorder,
      [&](HookContext &p_ctx, const NodeReorderEvent &) {
        ++beforeCount;
        p_ctx.cancel();
      },
      10);
  int afterId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeAfterReorder, [&](HookContext &, const NodeReorderEvent &) { ++afterCount; },
      10);

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, QStringList());

  if (spy.isEmpty()) {
    QVERIFY(spy.wait(500));
  }
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(2).toBool(), false);
  QCOMPARE(args.at(3).toString(), QStringLiteral("Cancelled by hook"));

  QCOMPARE(beforeCount, 1);
  QCOMPARE(afterCount, 0);

  // vxcore not called: order preserved.
  QCOMPARE(listFolders(nbId), seedFolders);

  m_hookMgr->removeAction(beforeId);
  m_hookMgr->removeAction(afterId);
}

// ===== 5. IoGate serialization =====
void TestNotebookCoreServiceReorder::testIoGateSerializesWithExternalHolder() {
  QStringList seedFolders, seedFiles;
  const QString nbId = seedNotebook(QStringLiteral("nb_iogate"), &seedFolders, &seedFiles);
  QVERIFY(!nbId.isEmpty());

  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());

  // Acquire the gate from a foreign thread BEFORE invoking reorder.
  std::atomic<bool> holderHasLock{false};
  std::atomic<bool> releaseRequested{false};
  QThread *holder = QThread::create([&]() {
    NotebookIoGate::ScopedLock lock(*m_ioGate, nbId);
    holderHasLock.store(true);
    while (!releaseRequested.load()) {
      QThread::msleep(10);
    }
  });
  holder->start();
  while (!holderHasLock.load()) {
    QTest::qWait(10);
  }

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, QStringList());

  // While the external holder owns the gate, reorder must NOT progress.
  bool finishedEarly = spy.wait(400);
  QVERIFY2(!finishedEarly, "reorder completed while external IoGate holder still owned the lock");
  QCOMPARE(spy.size(), 0);

  // Release the gate.
  releaseRequested.store(true);
  holder->wait();
  delete holder;

  // Now reorder should drain.
  if (spy.isEmpty()) {
    QVERIFY(spy.wait(5000));
  }
  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  QCOMPARE(listFolders(nbId), newFolders);
}

// ===== 6. After-hook fires AFTER lock release =====
void TestNotebookCoreServiceReorder::testAfterHookFiresAfterLockRelease() {
  QStringList seedFolders, seedFiles;
  const QString nbId = seedNotebook(QStringLiteral("nb_afterhook"), &seedFolders, &seedFiles);
  QVERIFY(!nbId.isEmpty());

  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());

  // Inside the after-hook (which runs on the service thread), prove the gate
  // is available by acquiring it from a worker thread with a bounded wait.
  // If the worker had still held the gate, the acquisition would time out
  // and the assertion below would fail.
  std::atomic<bool> reAcquired{false};
  int afterId = m_hookMgr->addAction<NodeReorderEvent>(
      HookNames::NodeAfterReorder,
      [&](HookContext &, const NodeReorderEvent &) {
        std::atomic<bool> done{false};
        QThread *t = QThread::create([&]() {
          NotebookIoGate::ScopedLock lock(*m_ioGate, nbId);
          done.store(true);
        });
        t->start();
        // Spin-wait up to 2s. If the gate were still held by the service worker,
        // this would never flip and we'd fall through with done=false.
        QElapsedTimer timer;
        timer.start();
        while (!done.load() && timer.elapsed() < 2000) {
          QThread::msleep(10);
        }
        reAcquired.store(done.load());
        t->wait();
        delete t;
      },
      10);

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, QStringList());

  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  QVERIFY2(reAcquired.load(),
           "after_reorder ran while NotebookIoGate was still held by the worker");

  m_hookMgr->removeAction(afterId);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookCoreServiceReorder)
#include "test_notebook_core_service_reorder.moc"
