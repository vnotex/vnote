#include <QtTest>

#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/markdownrendercontroller.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {
using Anchor = MarkdownRenderController::HeadingAnchorResult;

// Hold the real shared executor, not a substitute renderer. This makes admission and
// cancellation assertions independent of document size or machine speed.
class PoolBlocker {
public:
  PoolBlocker() : m_previousMaximum(QThreadPool::globalInstance()->maxThreadCount()) {
    QThreadPool::globalInstance()->setMaxThreadCount(1);
    m_future = QtConcurrent::run([this]() {
      m_started.release();
      m_release.acquire();
    });
    m_didStart = m_started.tryAcquire(1, 10000);
  }
  ~PoolBlocker() {
    unblock();
    m_future.waitForFinished();
    QThreadPool::globalInstance()->setMaxThreadCount(m_previousMaximum);
  }
  bool started() const { return m_didStart; }
  void unblock() {
    if (!m_unblocked) {
      m_unblocked = true;
      m_release.release();
    }
  }

private:
  int m_previousMaximum;
  QSemaphore m_started;
  QSemaphore m_release;
  QFuture<void> m_future;
  bool m_didStart = false;
  bool m_unblocked = false;
};
} // namespace

class TestMarkdownRenderController : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void latestPreviewWinsAndDropsSupersededInput();
  void emptyPreviewClearsPriorOutput();
  void completionCanProcessEventsAndSubmitNextRequest();
  void explicitHeadingsAreIndependentAndBounded();
  void invalidationSettlesOnceAndActuallyDrains();
  void suspensionBlocksReentrantAdmission();
  void destroyedReceiverReleasesCapturedDocument();
  void protectedLockDrainsBeforeClearance();
  void protectedDestroyedReceiverKeepsDrainBarrier();
  void suspendedSiblingCloseDoesNotRevoke();
  void closedBufferRejectsLateDelivery();
  void invalidAndMissingHeadingsAreDistinct();
  void tokensAreOwnerScoped();

private:
  ServiceLocator m_services;
  QTemporaryDir m_directory;
  VxCoreContextHandle m_context = nullptr;
  HookManager m_hooks;
  NotebookIoGate m_gate;
  SyncWorkQueueManager m_queues;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  QString m_notebook;
  Buffer2 m_ordinary;
  Buffer2 m_private;
};

void TestMarkdownRenderController::initTestCase() {
  QVERIFY(m_directory.isValid());
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_notebooks->setHookManager(&m_hooks);
  m_notebooks->setNotebookIoGate(&m_gate);
  m_buffers = std::make_unique<BufferService>(m_context, &m_hooks, &m_gate, AutoSavePolicy::None);
  m_buffers->setSyncWorkQueueManager(&m_queues);
  m_services.registerService(m_buffers.get());
  m_services.registerService(&m_hooks);
  m_ordinary = m_buffers->openVirtualBuffer(QStringLiteral("vx://native-render-controller"));
  QVERIFY(m_ordinary.isValid());

  m_notebook = m_notebooks->createNotebook(m_directory.filePath(QStringLiteral("notebook")),
                                           QStringLiteral("{\"name\":\"Native rendering\"}"),
                                           NotebookType::Bundled);
  QVERIFY(!m_notebook.isEmpty());
  const QByteArray password("native-render-controller-password");
  QString node;
  QCOMPARE(QtConcurrent::run([&]() -> VxCoreError {
             auto setup = m_notebooks->prepareNotebookEncryption(m_notebook, {}, password);
             if (!setup.isValid())
               return setup.m_error;
             auto maintenance = m_queues.tryAcquireMaintenance({m_notebook});
             if (!maintenance.isValid())
               return VXCORE_ERR_INVALID_STATE;
             NotebookIoGate::ScopedLock lock(m_gate, m_notebook);
             const auto error = m_notebooks->commitNotebookEncryption(setup);
             return error == VXCORE_OK
                        ? m_notebooks->createEncryptedNote(
                              m_notebook, {}, QStringLiteral("private.md"),
                              QStringLiteral("markdown"), QByteArrayLiteral("# Private\n"), &node)
                        : error;
           }).result(),
           VXCORE_OK);
  QCOMPARE(m_notebooks->lockAllEncryption(), VXCORE_OK);
  QCOMPARE(QtConcurrent::run([&]() {
             return m_notebooks->unlockNoteEncryption(m_notebook, QStringLiteral("private.md.vne"),
                                                      password);
           }).result(),
           VXCORE_OK);
  m_private = m_buffers->openBufferByNodeId(node);
  QVERIFY(m_private.isValid());
  QVERIFY(m_private.isEncrypted());
}

void TestMarkdownRenderController::cleanupTestCase() {
  if (m_buffers) {
    m_buffers->cancelProtectedLocking();
    QTRY_VERIFY(m_buffers->protectedOperationsIdle());
    m_buffers->closeBuffer(m_private.id());
    m_buffers->closeBuffer(m_ordinary.id());
  }
  if (m_notebooks && !m_notebook.isEmpty())
    m_notebooks->closeNotebook(m_notebook);
  m_buffers.reset();
  m_notebooks.reset();
  if (m_context)
    vxcore_context_destroy(m_context);
}

void TestMarkdownRenderController::latestPreviewWinsAndDropsSupersededInput() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  MarkdownRenderController controller(m_services, m_ordinary);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  QSignalSpy drained(&controller, &MarkdownRenderController::drained);
  const auto first = controller.render(QByteArrayLiteral("# First\n"), {});
  auto obsolete = vte::MarkdownDocument::parse(QByteArrayLiteral("# Obsolete\n"),
                                               MarkdownHtmlRenderer::parseOptions({}));
  QVERIFY(obsolete);
  const auto weakObsolete = obsolete.toWeakRef();
  const auto second = controller.render(obsolete->source(), {}, obsolete);
  obsolete.clear();
  QVERIFY(!weakObsolete.isNull());
  const auto last = controller.render(QByteArrayLiteral("# Latest\n"), {});
  QVERIFY(weakObsolete.isNull());
  QVERIFY(controller.isBusy());
  QVERIFY(!controller.isCurrent(first));
  QVERIFY(!controller.isCurrent(second));
  QVERIFY(controller.isCurrent(last));
  QCOMPARE(rendered.size(), 0);
  pool.unblock();
  QTRY_COMPARE(rendered.size(), 1);
  QCOMPARE(rendered.first().at(0).toString(), last);
  const auto result = qvariant_cast<MarkdownHtmlResult>(rendered.first().at(1));
  QVERIFY(result.error.isEmpty());
  QCOMPARE(result.headings.first().anchor, QStringLiteral("latest"));
  QVERIFY(result.html.contains(QStringLiteral("Latest")));
  QVERIFY(!result.html.contains(QStringLiteral("Obsolete")));
  QTRY_COMPARE(drained.size(), 1);
  QVERIFY(!controller.isBusy());
}

void TestMarkdownRenderController::emptyPreviewClearsPriorOutput() {
  MarkdownRenderController controller(m_services, m_ordinary);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  controller.render(QByteArrayLiteral("# Before\n"), {});
  QTRY_COMPARE(rendered.size(), 1);
  const auto token = controller.render({}, {});
  QCOMPARE(rendered.size(), 1); // Delivery must never precede returning the token.
  QTRY_COMPARE(rendered.size(), 2);
  QCOMPARE(rendered.last().at(0).toString(), token);
  const auto result = qvariant_cast<MarkdownHtmlResult>(rendered.last().at(1));
  QVERIFY(result.error.isEmpty());
  QVERIFY(result.html.isEmpty());
  QVERIFY(!result.html.isNull());
  QVERIFY(result.headings.isEmpty());
}

void TestMarkdownRenderController::explicitHeadingsAreIndependentAndBounded() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  MarkdownRenderController controller(m_services, m_ordinary);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  QList<Anchor> results;
  const auto callback = [&](const Anchor &p_result) { results.append(p_result); };
  for (int i = 0; i < 8; ++i)
    controller.fetchHeadingAnchor(QByteArrayLiteral("# a\n# a\n# a-1\n"), 2, {}, {}, callback);
  QCOMPARE(results.size(), 0);
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Rejected\n"), 0, {}, {}, callback);
  QCOMPARE(results.size(), 1);
  QVERIFY(!results.first().error.isEmpty());
  QVERIFY(!results.first().cancelled);
  controller.render(QByteArrayLiteral("# Preview\n"), {});
  controller.render(QByteArrayLiteral("# Replacement\n"), {});
  pool.unblock();
  QTRY_COMPARE(results.size(), 9);
  for (int i = 1; i < results.size(); ++i) {
    QVERIFY(results[i].error.isEmpty());
    QVERIFY(results[i].found);
    QCOMPARE(results[i].anchor, QStringLiteral("a-1-1"));
  }
  QTRY_COMPARE(rendered.size(), 1);
  QTRY_VERIFY(!controller.isBusy());
}

void TestMarkdownRenderController::invalidationSettlesOnceAndActuallyDrains() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  MarkdownRenderController controller(m_services, m_ordinary);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  QSignalSpy drained(&controller, &MarkdownRenderController::drained);
  auto document = vte::MarkdownDocument::parse(QByteArrayLiteral("# Cancel\n"),
                                               MarkdownHtmlRenderer::parseOptions({}));
  QVERIFY(document);
  const auto weak = document.toWeakRef();
  const auto token = controller.render(document->source(), {}, document);
  QList<Anchor> results;
  controller.fetchHeadingAnchor(document->source(), 0, {}, document,
                                [&](const Anchor &p_result) { results.append(p_result); });
  document.clear();
  controller.invalidate();
  controller.invalidate();
  QCOMPARE(results.size(), 1);
  QVERIFY(results.first().cancelled);
  QVERIFY(!results.first().error.isEmpty());
  QVERIFY(!controller.isCurrent(token));
  QVERIFY(controller.isBusy());
  QVERIFY(!weak.isNull());
  QCOMPARE(drained.size(), 0);
  pool.unblock();
  QTRY_COMPARE(drained.size(), 1);
  QVERIFY(!controller.isBusy());
  QVERIFY(weak.isNull());
  QCOMPARE(rendered.size(), 0);
  QCOMPARE(results.size(), 1);
}

void TestMarkdownRenderController::suspensionBlocksReentrantAdmission() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  MarkdownRenderController controller(m_services, m_ordinary);
  QList<Anchor> results;
  controller.fetchHeadingAnchor(
      QByteArrayLiteral("# Pending\n"), 0, {}, {}, [&](const Anchor &p_result) {
        results.append(p_result);
        controller.fetchHeadingAnchor(QByteArrayLiteral("# Reentrant\n"), 0, {}, {},
                                      [&](const Anchor &p_nested) { results.append(p_nested); });
      });
  controller.setSuspended(true);
  QCOMPARE(results.size(), 2);
  QVERIFY(results[0].cancelled);
  QVERIFY(results[1].cancelled);
  QVERIFY(controller.render(QByteArrayLiteral("# Refused\n"), {}).isEmpty());
  pool.unblock();
  QTRY_VERIFY(!controller.isBusy());
  controller.setSuspended(false);
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Resumed\n"), 0, {}, {},
                                [&](const Anchor &p_result) { results.append(p_result); });
  QTRY_COMPARE(results.size(), 3);
  QVERIFY(results.last().found);
  QCOMPARE(results.last().anchor, QStringLiteral("resumed"));
  controller.revoke();
  controller.setSuspended(true);
  controller.setSuspended(false);
  QVERIFY(controller.render(QByteArrayLiteral("# Revoked\n"), {}).isEmpty());
}

void TestMarkdownRenderController::destroyedReceiverReleasesCapturedDocument() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  auto controller = std::make_unique<MarkdownRenderController>(m_services, m_ordinary);
  auto document = vte::MarkdownDocument::parse(QByteArrayLiteral("# Owned\n"),
                                               MarkdownHtmlRenderer::parseOptions({}));
  QVERIFY(document);
  const auto weak = document.toWeakRef();
  QList<Anchor> results;
  controller->fetchHeadingAnchor(document->source(), 0, {}, document,
                                 [&](const Anchor &p_result) { results.append(p_result); });
  document.clear();
  controller.reset(); // Must return while the executor is still deliberately blocked.
  QCOMPARE(results.size(), 1);
  QVERIFY(results.first().cancelled);
  QVERIFY(!weak.isNull());
  pool.unblock();
  QTRY_VERIFY(weak.isNull());
  QCOMPARE(results.size(), 1);
}

void TestMarkdownRenderController::protectedLockDrainsBeforeClearance() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  MarkdownRenderController controller(m_services, m_private);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  QSignalSpy drained(&controller, &MarkdownRenderController::drained);
  bool idleAtDrain = false;
  connect(&controller, &MarkdownRenderController::drained, this,
          [&]() { idleAtDrain = m_buffers->protectedBufferOperationsIdle(m_private.id()); });
  controller.render(QByteArrayLiteral("# Secret preview\n"), {});
  QList<Anchor> results;
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Secret heading\n"), 0, {}, {},
                                [&](const Anchor &p_result) { results.append(p_result); });
  QVERIFY(!m_buffers->protectedBufferOperationsIdle(m_private.id()));
  QVERIFY(m_buffers->beginProtectedLocking());
  const auto restoreLock = qScopeGuard([&]() { m_buffers->cancelProtectedLocking(); });
  QCOMPARE(results.size(), 1);
  QVERIFY(results.first().cancelled);
  QVERIFY(controller.isBusy());
  QVERIFY(!m_buffers->protectedOperationsIdle());
  controller.setSuspended(true);
  controller.setSuspended(false);
  QVERIFY(controller.render(QByteArrayLiteral("# Still locked\n"), {}).isEmpty());
  QCOMPARE(drained.size(), 0);
  pool.unblock();
  QTRY_COMPARE(drained.size(), 1);
  QVERIFY(idleAtDrain);
  QVERIFY(m_buffers->protectedOperationsIdle());
  QCOMPARE(rendered.size(), 0);
  QCOMPARE(results.size(), 1);
  m_buffers->cancelProtectedLocking();
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Authorized again\n"), 0, {}, {},
                                [&](const Anchor &p_result) { results.append(p_result); });
  QTRY_COMPARE(results.size(), 2);
  QVERIFY(results.last().found);
  QCOMPARE(results.last().anchor, QStringLiteral("authorized-again"));
}

void TestMarkdownRenderController::protectedDestroyedReceiverKeepsDrainBarrier() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  auto controller = std::make_unique<MarkdownRenderController>(m_services, m_private);
  controller->render(QByteArrayLiteral("# Secret owned by worker\n"), {});
  QVERIFY(!m_buffers->protectedBufferOperationsIdle(m_private.id()));
  controller.reset();
  QVERIFY(!m_buffers->protectedBufferOperationsIdle(m_private.id()));
  QVERIFY(m_buffers->beginProtectedLocking());
  const auto restoreLock = qScopeGuard([&]() { m_buffers->cancelProtectedLocking(); });
  QVERIFY(!m_buffers->protectedOperationsIdle());
  pool.unblock();
  QTRY_VERIFY(m_buffers->protectedOperationsIdle());
}

void TestMarkdownRenderController::suspendedSiblingCloseDoesNotRevoke() {
  MarkdownRenderController controller(m_services, m_ordinary);
  controller.setSuspended(true);
  ViewWindowCloseEvent event;
  event.bufferId = m_ordinary.id();
  m_hooks.doAction(HookNames::ViewWindowAfterClose, event);
  controller.setSuspended(false);
  QList<Anchor> results;
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Alive\n"), 0, {}, {},
                                [&](const Anchor &p_result) { results.append(p_result); });
  QTRY_COMPARE(results.size(), 1);
  QVERIFY(results.first().found);
  QCOMPARE(results.first().anchor, QStringLiteral("alive"));
}

void TestMarkdownRenderController::closedBufferRejectsLateDelivery() {
  PoolBlocker pool;
  QVERIFY(pool.started());
  auto buffer = m_buffers->openVirtualBuffer(QStringLiteral("vx://closed-native-render"));
  QVERIFY(buffer.isValid());
  MarkdownRenderController controller(m_services, buffer);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  controller.render(QByteArrayLiteral("# Closed\n"), {});
  QList<Anchor> results;
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Closed\n"), 0, {}, {},
                                [&](const Anchor &p_result) { results.append(p_result); });
  QVERIFY(m_buffers->closeBuffer(buffer.id()));
  // Delivery checks authorization independently of the view-close notification.
  pool.unblock();
  QTRY_VERIFY(!controller.isBusy());
  QCOMPARE(rendered.size(), 0);
  QCOMPARE(results.size(), 1);
  QVERIFY(results.first().cancelled);
}

void TestMarkdownRenderController::invalidAndMissingHeadingsAreDistinct() {
  const Buffer2 invalid;
  MarkdownRenderController refused(m_services, invalid);
  QList<Anchor> results;
  const auto callback = [&](const Anchor &p_result) { results.append(p_result); };
  refused.fetchHeadingAnchor(QByteArrayLiteral("# No buffer\n"), 0, {}, {}, callback);
  QCOMPARE(results.size(), 1);
  QVERIFY(results.first().cancelled);
  QVERIFY(refused.render({}, {}).isEmpty());

  MarkdownRenderController controller(m_services, m_ordinary);
  QSignalSpy rendered(&controller, &MarkdownRenderController::rendered);
  controller.fetchHeadingAnchor(QByteArrayLiteral("# Heading\n"), -1, {}, {}, callback);
  QCOMPARE(results.size(), 2);
  QVERIFY(!results.last().cancelled);
  QVERIFY(!results.last().error.isEmpty());
  controller.fetchHeadingAnchor(QByteArrayLiteral("plain text\n"), 0, {}, {}, callback);
  controller.fetchHeadingAnchor(QByteArrayLiteral("# !!!\n"), 0, {}, {}, callback);
  QTRY_COMPARE(results.size(), 4);
  QVERIFY(!results[2].found);
  QVERIFY(results[2].error.isEmpty());
  QVERIFY(results[3].found);
  QVERIFY(results[3].anchor.isEmpty());
  QVERIFY(results[3].error.isEmpty());
  const auto hiddenHeading = QByteArrayLiteral("[^unused]: # Same\n\n# Same\n");
  controller.fetchHeadingAnchor(hiddenHeading, 0, {}, {}, callback);
  controller.fetchHeadingAnchor(hiddenHeading, 2, {}, {}, callback);
  QTRY_COMPARE(results.size(), 6);
  QVERIFY(!results[4].found);
  QVERIFY(results[4].error.isEmpty());
  QVERIFY(results[5].found);
  QCOMPARE(results[5].anchor, QStringLiteral("same"));
  QVERIFY(results[5].error.isEmpty());
  QCOMPARE(rendered.size(), 0); // Explicit headings do not publish or construct a reader.
}

void TestMarkdownRenderController::tokensAreOwnerScoped() {
  MarkdownRenderController first(m_services, m_ordinary);
  MarkdownRenderController second(m_services, m_ordinary);
  const auto firstToken = first.render(QByteArrayLiteral("# Same\n"), {});
  const auto secondToken = second.render(QByteArrayLiteral("# Same\n"), {});
  QVERIFY(!firstToken.isEmpty());
  QVERIFY(!secondToken.isEmpty());
  QVERIFY(firstToken != secondToken);
  QVERIFY(!first.isCurrent(secondToken));
  QVERIFY(!second.isCurrent(firstToken));
  QTRY_VERIFY(!first.isBusy() && !second.isBusy());
}

void TestMarkdownRenderController::completionCanProcessEventsAndSubmitNextRequest() {
  MarkdownRenderController controller(m_services, m_ordinary);
  QSignalSpy drained(&controller, &MarkdownRenderController::drained);
  QStringList renderedAnchors;
  connect(&controller, &MarkdownRenderController::rendered, this,
          [&](const QString &p_token, const MarkdownHtmlResult &p_result) {
            QVERIFY(controller.isCurrent(p_token));
            QVERIFY(controller.isBusy());
            QVERIFY(p_result.error.isEmpty());
            QCOMPARE(p_result.headings.size(), 1);
            renderedAnchors.append(p_result.headings.first().anchor);
            if (renderedAnchors.size() == 1)
              controller.render(QByteArrayLiteral("# Second preview\n"), {});
            // Consumers may pump events while updating their view or clipboard.
            // Completion must neither consume an empty future nor finish the next request.
            QCoreApplication::processEvents();
          });
  controller.render(QByteArrayLiteral("# First preview\n"), {});
  QTRY_COMPARE(drained.size(), 1);
  QCOMPARE(renderedAnchors,
           QStringList({QStringLiteral("first-preview"), QStringLiteral("second-preview")}));
  QVERIFY(!controller.isBusy());

  QStringList anchors;
  std::function<void(const Anchor &)> completed;
  completed = [&](const Anchor &p_result) {
    QVERIFY(controller.isBusy());
    QVERIFY(p_result.found);
    QVERIFY(p_result.error.isEmpty());
    anchors.append(p_result.anchor);
    if (anchors.size() == 1)
      controller.fetchHeadingAnchor(QByteArrayLiteral("# Second heading\n"), 0, {}, {}, completed);
    QCoreApplication::processEvents();
  };
  controller.fetchHeadingAnchor(QByteArrayLiteral("# First heading\n"), 0, {}, {}, completed);
  QTRY_COMPARE(drained.size(), 2);
  QCOMPARE(anchors,
           QStringList({QStringLiteral("first-heading"), QStringLiteral("second-heading")}));
  QVERIFY(!controller.isBusy());
}

} // namespace tests

int main(int argc, char **argv) {
  QTemporaryDir storage;
  if (!storage.isValid())
    return 1;
  const auto temporaryRoot = storage.path().toLocal8Bit();
  qputenv("TMP", temporaryRoot);
  qputenv("TEMP", temporaryRoot);
  qputenv("TMPDIR", temporaryRoot);
  QCoreApplication app(argc, argv);
  tests::TestMarkdownRenderController test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markdownrendercontroller.moc"
