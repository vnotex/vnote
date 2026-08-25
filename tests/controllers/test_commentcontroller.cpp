// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_commentcontroller.cpp
//
// CommentController is the ONLY mutator of a file's comment set, so the
// properties tested here are data-durability properties, not UI conveniences:
//
//   * a FAILED save must leave the file dirty and retryable — clearing the
//     dirty flag optimistically loses the user's edits on close, with only a
//     banner to show for it;
//   * a malformed store must go READ-ONLY, or the next edit overwrites a file
//     the user could still have recovered by hand;
//   * a rename must RE-AIM the pending write rather than reload, because
//     CommentService has a sidecar move queued behind it;
//   * a file switch must never carry a pending write across.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>

#include <controllers/commentcontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/commentservice.h>
#include <core/services/commenttypes.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>

#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestCommentController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void addingACommentPublishesAndPersists();
  void editsAreCoalescedIntoOneWrite();
  void aFailedSaveKeepsTheFileDirtyAndRetryable();
  void aMalformedStoreIsReadOnlyAndIsNeverOverwritten();
  void switchingFilesFlushesThePendingWrite();
  void retargetingAfterARenameWritesToTheNewPath();
  void selectionIsClearedWhenItsCommentIsDeleted();
  void aDetachedControllerAcceptsNothing();

private:
  NodeIdentifier externalFile(const QString &p_name) const;

  static QJsonObject anchor(int p_page);

  // Drives the controller's debounce without waiting on wall-clock time.
  void flushAndSettle(CommentController &p_controller);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebooks = nullptr;
  NotebookIoGate *m_gate = nullptr;
  HookManager *m_hooks = nullptr;
  CommentService *m_service = nullptr;
  ServiceLocator *m_services = nullptr;
  TempDirFixture *m_tmp = nullptr;
};

void TestCommentController::initTestCase() {
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);

  m_hooks = new HookManager(this);
  m_notebooks = new NotebookCoreService(m_context);
  m_notebooks->setHookManager(m_hooks);
  m_gate = new NotebookIoGate();
  m_service = new CommentService(m_notebooks, m_gate, m_hooks);

  m_services = new ServiceLocator();
  m_services->registerService<NotebookCoreService>(m_notebooks);
  m_services->registerService<CommentService>(m_service);
}

void TestCommentController::cleanupTestCase() {
  delete m_services;
  m_services = nullptr;
  delete m_service;
  m_service = nullptr;
  delete m_gate;
  m_gate = nullptr;
  delete m_notebooks;
  m_notebooks = nullptr;
  delete m_tmp;
  m_tmp = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestCommentController::init() {
  delete m_tmp;
  m_tmp = new TempDirFixture();
  QVERIFY(m_tmp->isValid());
}

NodeIdentifier TestCommentController::externalFile(const QString &p_name) const {
  NodeIdentifier id;
  id.relativePath = QDir(m_tmp->path()).filePath(p_name);
  return id;
}

QJsonObject TestCommentController::anchor(int p_page) {
  QVector<QVector<double>> quads;
  quads.append(QVector<double>{0, 0, 10, 0, 10, 10, 0, 10});
  return PdfQuadsAnchor::make(p_page, quads, QStringLiteral("quoted"));
}

void TestCommentController::flushAndSettle(CommentController &p_controller) {
  QSignalSpy spy(m_service, &CommentService::saveFinished);
  p_controller.flushPendingSave();
  if (spy.isEmpty()) {
    spy.wait(5000);
  }
  QTest::qWait(50);
}

void TestCommentController::addingACommentPublishesAndPersists() {
  CommentController controller(*m_services);
  const auto nodeId = externalFile(QStringLiteral("a.pdf"));
  controller.setActiveFile(nodeId);
  QVERIFY(controller.isEditable());

  QSignalSpy changed(&controller, &CommentController::commentsChanged);
  QSignalSpy added(&controller, &CommentController::commentAdded);

  controller.addComment(anchor(1), QStringLiteral("green"));

  QCOMPARE(added.count(), 1);
  QVERIFY(changed.count() >= 1);
  QCOMPARE(controller.getComments().m_comments.size(), 1);
  QVERIFY(controller.hasUnsavedChanges());

  flushAndSettle(controller);
  QVERIFY2(!controller.hasUnsavedChanges(), "a confirmed save must clear the dirty state");

  const auto reloaded = m_service->load(nodeId);
  QCOMPARE(reloaded.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(reloaded.m_comments.m_comments.size(), 1);
  QCOMPARE(reloaded.m_comments.m_comments.first().m_color, QStringLiteral("green"));
}

// Two debounces exist and are not redundant: this one is the controller's.
void TestCommentController::editsAreCoalescedIntoOneWrite() {
  CommentController controller(*m_services);
  const auto nodeId = externalFile(QStringLiteral("busy.pdf"));
  controller.setActiveFile(nodeId);

  controller.addComment(anchor(0), CommentColor::defaultToken());
  const auto id = controller.getComments().m_comments.first().m_id;

  QSignalSpy saves(m_service, &CommentService::saveFinished);
  for (int i = 0; i < 30; ++i) {
    controller.setCommentText(id, QStringLiteral("draft %1").arg(i));
  }
  // Still inside the debounce window: nothing has been handed to the service.
  QCOMPARE(saves.count(), 0);

  flushAndSettle(controller);
  QCOMPARE(saves.count(), 1);

  const auto reloaded = m_service->load(nodeId);
  QCOMPARE(reloaded.m_comments.m_comments.first().m_text, QStringLiteral("draft 29"));
}

// The regression that matters most: an optimistic dirty-clear silently discards
// the user's work on close after a transient I/O failure.
void TestCommentController::aFailedSaveKeepsTheFileDirtyAndRetryable() {
  CommentController controller(*m_services);

  // A path whose parent is a FILE, so mkpath and the QSaveFile commit both fail.
  const QString blocker = QDir(m_tmp->path()).filePath(QStringLiteral("blocker"));
  QFile blockerFile(blocker);
  QVERIFY(blockerFile.open(QIODevice::WriteOnly));
  blockerFile.close();

  NodeIdentifier nodeId;
  nodeId.relativePath = QDir(blocker).filePath(QStringLiteral("inside.pdf"));
  controller.setActiveFile(nodeId);
  QVERIFY(controller.isEditable());

  QSignalSpy failures(&controller, &CommentController::failed);
  controller.addComment(anchor(0), CommentColor::defaultToken());
  QVERIFY(controller.hasUnsavedChanges());

  flushAndSettle(controller);

  QVERIFY2(!failures.isEmpty(), "a failed write must be surfaced");
  QVERIFY2(controller.hasUnsavedChanges(),
           "a FAILED save must leave the file dirty, or closing the tab loses the edits");

  // ...and the edits are still in memory, so a later successful flush persists them.
  QCOMPARE(controller.getComments().m_comments.size(), 1);
}

void TestCommentController::aMalformedStoreIsReadOnlyAndIsNeverOverwritten() {
  const auto nodeId = externalFile(QStringLiteral("corrupt.pdf"));
  const QString storePath = nodeId.relativePath + CommentService::siblingSuffix();

  const QByteArray original = "{ this is not json, but it may be recoverable by hand";
  QFile store(storePath);
  QVERIFY(store.open(QIODevice::WriteOnly));
  store.write(original);
  store.close();

  CommentController controller(*m_services);
  QSignalSpy failures(&controller, &CommentController::failed);
  controller.setActiveFile(nodeId);

  QVERIFY2(!controller.isEditable(),
           "a malformed store must go read-only rather than present as empty");
  QVERIFY(!failures.isEmpty());

  // Every mutation is refused, so the file on disk is untouched.
  controller.addComment(anchor(0), CommentColor::defaultToken());
  QVERIFY(controller.getComments().m_comments.isEmpty());
  flushAndSettle(controller);

  QFile check(storePath);
  QVERIFY(check.open(QIODevice::ReadOnly));
  QCOMPARE(check.readAll(), original);
}

void TestCommentController::switchingFilesFlushesThePendingWrite() {
  CommentController controller(*m_services);
  const auto first = externalFile(QStringLiteral("first.pdf"));
  const auto second = externalFile(QStringLiteral("second.pdf"));

  controller.setActiveFile(first);
  controller.addComment(anchor(0), CommentColor::defaultToken());
  QVERIFY(controller.hasUnsavedChanges());

  QSignalSpy saves(m_service, &CommentService::saveFinished);
  // The switch itself must flush; nothing else prompts it.
  controller.setActiveFile(second);
  if (saves.isEmpty()) {
    QVERIFY(saves.wait(5000));
  }
  QTest::qWait(50);

  const auto firstStore = m_service->load(first);
  QCOMPARE(firstStore.m_status, CommentService::LoadResult::Status::Loaded);
  QCOMPARE(firstStore.m_comments.m_comments.size(), 1);

  // ...and it did NOT leak into the newly active file.
  QCOMPARE(m_service->load(second).m_status, CommentService::LoadResult::Status::Missing);
  QVERIFY(controller.getComments().m_comments.isEmpty());
}

// After a rename the controller must keep its in-memory set and write to the NEW
// path. Reloading instead would race the sidecar move queued in CommentService.
void TestCommentController::retargetingAfterARenameWritesToTheNewPath() {
  CommentController controller(*m_services);
  const auto oldId = externalFile(QStringLiteral("before.pdf"));
  const auto newId = externalFile(QStringLiteral("after.pdf"));

  controller.setActiveFile(oldId);
  controller.addComment(anchor(2), QStringLiteral("blue"));
  const auto commentId = controller.getComments().m_comments.first().m_id;

  controller.retargetActiveFile(newId);

  QCOMPARE(controller.getActiveFile(), newId);
  QVERIFY2(controller.getComments().indexOfId(commentId) >= 0,
           "retarget must NOT reload and discard the in-memory set");

  flushAndSettle(controller);

  QCOMPARE(m_service->load(newId).m_comments.m_comments.size(), 1);
  QCOMPARE(m_service->load(oldId).m_status, CommentService::LoadResult::Status::Missing);
}

void TestCommentController::selectionIsClearedWhenItsCommentIsDeleted() {
  CommentController controller(*m_services);
  controller.setActiveFile(externalFile(QStringLiteral("sel.pdf")));

  controller.addComment(anchor(0), CommentColor::defaultToken());
  const auto id = controller.getComments().m_comments.first().m_id;

  // addComment() already selects the new comment, so re-selecting it is a
  // deliberate no-op; clear first so the round trip is actually observed.
  QSignalSpy selection(&controller, &CommentController::selectionChanged);
  controller.selectComment(QString());
  QVERIFY(!selection.isEmpty());
  QVERIFY(selection.last().at(0).toString().isEmpty());

  selection.clear();
  controller.selectComment(id);
  QVERIFY(!selection.isEmpty());
  QCOMPARE(selection.last().at(0).toString(), id);

  selection.clear();
  controller.deleteComment(id);
  QVERIFY(!selection.isEmpty());
  QVERIFY2(selection.last().at(0).toString().isEmpty(),
           "a dangling selection would keep a deleted highlight ringed");
  QVERIFY(controller.getComments().m_comments.isEmpty());

  // An unknown id clears rather than being ignored, which is what makes a click
  // on empty space work.
  controller.selectComment(QStringLiteral("nope"));
  QVERIFY(controller.getComments().indexOfId(QStringLiteral("nope")) < 0);
}

void TestCommentController::aDetachedControllerAcceptsNothing() {
  CommentController controller(*m_services);
  QSignalSpy failures(&controller, &CommentController::failed);

  controller.setActiveFile(NodeIdentifier());
  QVERIFY(!controller.isEditable());

  controller.addComment(anchor(0), CommentColor::defaultToken());
  QVERIFY(controller.getComments().m_comments.isEmpty());
  QVERIFY(!controller.hasUnsavedChanges());
  QVERIFY(!failures.isEmpty());

  // A virtual document (vx://home) has no file behind it either.
  NodeIdentifier virtualId;
  virtualId.relativePath = QStringLiteral("vx://home");
  controller.setActiveFile(virtualId);
  QVERIFY(!controller.isEditable());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCommentController)
#include "test_commentcontroller.moc"
