#include <QtTest>

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QtConcurrent/QtConcurrentRun>

#include <controllers/markdownwebjobcontroller.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/services/webviewservice.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {
// Drive the actual session scheduler/owner lifetimes through its public bridge slots.
// No fake controller policy and no payload-echo handler is involved.
struct Endpoint {
  QWebEngineProfile profile;
  QWebEnginePage page{&profile};
  QWebChannel channel;
  WebViewJobSession session{&page, &channel};
  QList<QList<QVariant>> requests;
  QList<QJsonObject> releasedScopes;

  explicit Endpoint(const Buffer2 &p_buffer) {
    page.setWebChannel(&channel);
    session.setProperty("markdownJobProtected", p_buffer.isEncrypted());
    QObject::connect(
        &session, &WebViewJobSession::jobRequested, &session,
        [this](const QString &p_id, const QString &p_operation, const QJsonObject &p_arguments) {
          if (p_operation == QStringLiteral("markdown.releaseScope")) {
            releasedScopes.append(p_arguments);
            session.complete(p_id, true, QJsonValue(QJsonValue::Null), {});
          } else {
            requests.append({p_id, p_operation, QVariant::fromValue(p_arguments)});
          }
        });
    session.setProperty("markdownJobBufferId", p_buffer.id());
  }
  void ready() { session.setReady(session.generation()); }
  void complete(int p_index, const QString &p_markdown = QStringLiteral("**Converted**")) {
    session.complete(requests.at(p_index).at(0).toString(), true, p_markdown, {});
  }
};

struct SharedRuntime {
  ServiceLocator services;
  QWebEngineProfile profile;
  WebViewJobSession *session = nullptr;
  int created = 0;
  QList<QJsonObject> arguments;
  QList<QJsonObject> releasedScopes;
  QStringList requests;
  WebViewService service;

  SharedRuntime(BufferService *p_buffers, HookManager *p_hooks, bool p_ready = true)
      : service(services, &profile, [this, p_ready](QWebEnginePage *, QWebChannel *p_channel) {
          ++created;
          session = qobject_cast<WebViewJobSession *>(
              p_channel->registeredObjects().value(QStringLiteral("vxJobs")));
          QObject::connect(session, &WebViewJobSession::jobRequested, session,
                           [this](const QString &p_id, const QString &p_operation,
                                  const QJsonObject &p_arguments) {
                             if (p_operation == QStringLiteral("markdown.releaseScope")) {
                               releasedScopes.append(p_arguments);
                               session->complete(p_id, true, QJsonValue(QJsonValue::Null), {});
                             } else {
                               requests.append(p_id);
                               arguments.append(p_arguments);
                             }
                           });
          if (p_ready)
            session->setReady(session->generation());
        }) {
    services.registerService(p_buffers);
    services.registerService(p_hooks);
    services.registerService(&service);
  }
  void complete(int p_index, const QString &p_markdown = QStringLiteral("**Converted**")) {
    session->complete(requests.at(p_index), true, p_markdown, {});
  }
};
} // namespace

class TestMarkdownWebJobController : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void endpointPolicy_data();
  void endpointPolicy();
  void constructionDoesNotCreateRuntime();
  void originalEndpointFinishesAfterReaderBecomesReady();
  void generationCancellationSettlesOnce();
  void suspensionCancelsOnceAndResumes_data();
  void suspensionCancelsOnceAndResumes();
  void suspensionRejectsColdWork_data();
  void suspensionRejectsColdWork();
  void suspendedSharedReadinessDoesNotPublish();
  void suspendedProtectedSessionIsReleased();
  void suspendedSiblingCloseDoesNotRevoke_data();
  void suspendedSiblingCloseDoesNotRevoke();
  void suspensionDoesNotUndoRevocation_data();
  void suspensionDoesNotUndoRevocation();
  void suspensionDoesNotUnlockProtectedBuffer();
  void staleAutomaticGenerationCannotPublish();
  void ownerDestructionAndEndpointTeardown();
  void protectedSessionsAreWindowScoped();
  void protectedLockCancelsBeforeReaderExistsAndCanResume();
  void protectedFailureNeverUsesOrdinaryService();
  void protectedStylesUseSelectedBundledBytes();
  void invalidBufferRejectsWork();
  void emptyConversionIsDifferentFromFailure();
  void bufferCloseRejectsLateCompletion();
  void collidingLocalIdsRemainOwnerScoped();
  void previewScopesAreStableAndGenerationBound();
  void scopeCleanupSurvivesOwnerAndDrainsActiveWork();
  void readyTracksEffectiveEndpointOnly();
  void scopeCleanupReachesEveryObservedEndpoint();
  void protectedPreviewRejectsActiveImages();
  void individualCancellationPreservesOtherBlocks();

private:
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

void TestMarkdownWebJobController::initTestCase() {
  QVERIFY(m_directory.isValid());
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_notebooks->setHookManager(&m_hooks);
  m_notebooks->setNotebookIoGate(&m_gate);
  m_buffers = std::make_unique<BufferService>(m_context, &m_hooks, &m_gate, AutoSavePolicy::None);
  m_buffers->setSyncWorkQueueManager(&m_queues);
  QFile ordinary(m_directory.filePath(QStringLiteral("ordinary.md")));
  QVERIFY(ordinary.open(QIODevice::WriteOnly));
  ordinary.write("# Ordinary\n");
  ordinary.close();
  m_ordinary = m_buffers->openBuffer(NodeIdentifier{QString(), ordinary.fileName()});
  QVERIFY(m_ordinary.isValid());

  m_notebook = m_notebooks->createNotebook(m_directory.filePath(QStringLiteral("notebook")),
                                           QStringLiteral("{\"name\":\"Controller isolation\"}"),
                                           NotebookType::Bundled);
  QVERIFY(!m_notebook.isEmpty());
  const QByteArray password("controller-job-test-password");
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

void TestMarkdownWebJobController::cleanupTestCase() {
  if (m_buffers) {
    m_buffers->cancelProtectedLocking();
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

void TestMarkdownWebJobController::endpointPolicy_data() {
  QTest::addColumn<bool>("protectedBuffer");
  QTest::addColumn<int>("readerState");
  QTest::addColumn<bool>("preferred");
  for (bool protectedBuffer : {false, true}) {
    const QByteArray domain = protectedBuffer ? "protected-" : "ordinary-";
    QTest::newRow((domain + "absent").constData()) << protectedBuffer << 0 << false;
    QTest::newRow((domain + "loading").constData()) << protectedBuffer << 1 << false;
    QTest::newRow((domain + "ready").constData()) << protectedBuffer << 2 << true;
    QTest::newRow((domain + "stale").constData()) << protectedBuffer << 3 << false;
    QTest::newRow((domain + "wrong-owner").constData()) << protectedBuffer << 4 << false;
    QTest::newRow((domain + "wrong-protection").constData()) << protectedBuffer << 5 << false;
  }
}

void TestMarkdownWebJobController::endpointPolicy() {
  QFETCH(bool, protectedBuffer);
  QFETCH(int, readerState);
  QFETCH(bool, preferred);
  const auto &buffer = protectedBuffer ? m_private : m_ordinary;
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint reader(buffer);
  Endpoint fallback(buffer);
  fallback.ready();
  MarkdownWebJobController controller(shared.services, buffer);
  int protectedRequests = 0;
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller, [&]() {
    ++protectedRequests;
    controller.setProtectedSession(&fallback.session, QByteArrayLiteral(".token { color: red; }"));
  });
  if (readerState >= 2)
    reader.ready();
  if (readerState == 4)
    reader.session.setProperty("markdownJobBufferId", QStringLiteral("other"));
  if (readerState == 5)
    reader.session.setProperty("markdownJobProtected", !protectedBuffer);
  if (readerState != 0)
    controller.setPreferredSession(&reader.session);
  if (readerState == 3)
    controller.invalidate();
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(17, 23, QStringLiteral("<strong>Converted</strong>"));
  if (preferred) {
    QTRY_COMPARE(reader.requests.size(), 1);
    QCOMPARE(reader.requests.first().at(1).toString(), QStringLiteral("markdown.htmlToMarkdown"));
    QCOMPARE(
        reader.requests.first().at(2).toJsonObject().value(QStringLiteral("source")).toString(),
        QStringLiteral("<strong>Converted</strong>"));
    reader.complete(0);
  } else if (protectedBuffer) {
    QTRY_COMPARE(fallback.requests.size(), 1);
    QCOMPARE(fallback.requests.first().at(1).toString(), QStringLiteral("markdown.htmlToMarkdown"));
    fallback.complete(0);
  } else {
    QTRY_COMPARE(shared.requests.size(), 1);
    QCOMPARE(shared.arguments.first().value(QStringLiteral("source")).toString(),
             QStringLiteral("<strong>Converted</strong>"));
    shared.complete(0);
  }
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(17));
  QCOMPARE(converted.first().at(1).toULongLong(), quint64(23));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Converted**"));
  QVERIFY(failed.isEmpty());
  QCOMPARE(reader.requests.size(), preferred ? 1 : 0);
  QCOMPARE(shared.created, !protectedBuffer && !preferred ? 1 : 0);
  QCOMPARE(protectedRequests, protectedBuffer && !preferred ? 1 : 0);
}

void TestMarkdownWebJobController::constructionDoesNotCreateRuntime() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController first(shared.services, m_ordinary);
  MarkdownWebJobController second(shared.services, m_ordinary);
  QCOMPARE(shared.created, 0);
  first.invalidate();
  second.setRenderContext(2, 480);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::originalEndpointFinishesAfterReaderBecomesReady() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint reader(m_ordinary);
  MarkdownWebJobController controller(shared.services, m_ordinary);
  controller.setPreferredSession(&reader.session);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  controller.htmlToMarkdown(1, 10, QStringLiteral("<strong>First</strong>"));
  QTRY_COMPARE(shared.requests.size(), 1);
  reader.ready();
  controller.htmlToMarkdown(2, 20, QStringLiteral("<em>Second</em>"));
  QTRY_COMPARE(reader.requests.size(), 1);
  reader.complete(0, QStringLiteral("*Second*"));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(2));
  QCOMPARE(converted.first().at(1).toULongLong(), quint64(20));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("*Second*"));
  shared.complete(0, QStringLiteral("**First**"));
  QCOMPARE(converted.size(), 2);
  QCOMPARE(converted.last().at(0).toULongLong(), quint64(1));
  QCOMPARE(converted.last().at(1).toULongLong(), quint64(10));
  QCOMPARE(converted.last().at(2).toString(), QStringLiteral("**First**"));
  QCOMPARE(shared.requests.size(), 1);
}

void TestMarkdownWebJobController::generationCancellationSettlesOnce() {
  SharedRuntime shared(m_buffers.get(), &m_hooks, false);
  MarkdownWebJobController controller(shared.services, m_ordinary);
  QSignalSpy pasteReady(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy highlighted(&controller, &MarkdownWebJobController::highlightCodeBlockReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 9, QStringLiteral("<b>Paste</b>"));
  controller.highlightCodeBlock(2, 9, QStringLiteral("```cpp\nint pending;\n```"));
  QVERIFY(shared.requests.isEmpty());
  controller.invalidate();
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(0).toString(), QStringLiteral("markdown.htmlToMarkdown"));
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(1));
  QCOMPARE(failed.first().at(2).toULongLong(), quint64(9));
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QCOMPARE(pasteReady.size(), 0);
  QCOMPARE(highlighted.size(), 0);
  shared.session->setReady(shared.session->generation());
  QCoreApplication::processEvents();
  QVERIFY(shared.requests.isEmpty());
  controller.revoke();
  QCOMPARE(failed.size(), 1);
}

void TestMarkdownWebJobController::suspensionCancelsOnceAndResumes_data() {
  QTest::addColumn<bool>("protectedBuffer");
  QTest::newRow("ordinary") << false;
  QTest::newRow("protected") << true;
}

void TestMarkdownWebJobController::suspensionCancelsOnceAndResumes() {
  QFETCH(bool, protectedBuffer);
  const auto &buffer = protectedBuffer ? m_private : m_ordinary;
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint reader(buffer);
  reader.ready();
  MarkdownWebJobController controller(shared.services, buffer);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  QSignalSpy release(&controller, &MarkdownWebJobController::protectedSessionReleaseRequested);
  QSignalSpy requested(&controller, &MarkdownWebJobController::protectedSessionRequested);
  QSignalSpy pasted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy highlighted(&controller, &MarkdownWebJobController::highlightCodeBlockReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.setPreferredSession(&reader.session);
  QCOMPARE(ready.size(), 1);
  connect(&controller, &MarkdownWebJobController::operationFailed, &controller,
          [&](const QString &p_operation, quint64 p_id, quint64, const QString &) {
            if (p_operation == QStringLiteral("markdown.htmlToMarkdown") && p_id == 1) {
              // Failure delivery must observe the already closed admission gate.
              controller.htmlToMarkdown(2, 10, QStringLiteral("<em>Reentrant</em>"));
            }
          });
  controller.htmlToMarkdown(1, 9, QStringLiteral("<strong>Pending</strong>"));
  QTRY_COMPARE(reader.requests.size(), 1);
  controller.htmlToMarkdown(3, 10, QStringLiteral("<b>Queued paste</b>"));
  controller.highlightCodeBlock(4, 10, QStringLiteral("```cpp\npending\n```"));
  controller.setSuspended(true);
  QCOMPARE(failed.size(), 3);
  QSet<quint64> failedIds;
  for (const auto &failure : failed) {
    QCOMPARE(failure.at(0).toString(), QStringLiteral("markdown.htmlToMarkdown"));
    QVERIFY(!failure.at(3).toString().isEmpty());
    failedIds.insert(failure.at(1).toULongLong());
  }
  QCOMPARE(failedIds, (QSet<quint64>{1, 2, 3}));
  QCOMPARE(release.size(), 1);
  controller.setSuspended(true);
  QCOMPARE(release.size(), 1);
  QCOMPARE(failed.size(), 3);

  reader.complete(0, QStringLiteral("**Stale**"));
  QTRY_COMPARE(reader.releasedScopes.size(), 1);
  QCOMPARE(pasted.size(), 0);
  QCOMPARE(highlighted.size(), 0);
  QCOMPARE(failed.size(), 3);
  QCOMPARE(reader.requests.size(), 1);
  QCOMPARE(ready.size(), 1);
  QCOMPARE(requested.size(), 0);
  QCOMPARE(shared.created, 0);

  controller.setSuspended(false);
  controller.setSuspended(false);
  QCoreApplication::processEvents();
  QCOMPARE(ready.size(), 1);
  QCOMPARE(requested.size(), 0);
  QCOMPARE(shared.created, 0);
  QCOMPARE(reader.requests.size(), 1);
  Endpoint replacement(buffer);
  replacement.ready();
  controller.setPreferredSession(&replacement.session);
  QCOMPARE(ready.size(), 2);
  controller.htmlToMarkdown(5, 11, QStringLiteral("<em>Fresh</em>"));
  QTRY_COMPARE(replacement.requests.size(), 1);
  replacement.complete(0, QStringLiteral("*Fresh*"));
  QCOMPARE(pasted.size(), 1);
  QCOMPARE(pasted.first().at(0).toULongLong(), quint64(5));
  QCOMPARE(pasted.first().at(1).toULongLong(), quint64(11));
  QCOMPARE(pasted.first().at(2).toString(), QStringLiteral("*Fresh*"));
  QCOMPARE(failed.size(), 3);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::suspensionRejectsColdWork_data() {
  QTest::addColumn<bool>("protectedBuffer");
  QTest::newRow("ordinary") << false;
  QTest::newRow("protected") << true;
}

void TestMarkdownWebJobController::suspensionRejectsColdWork() {
  QFETCH(bool, protectedBuffer);
  const auto &buffer = protectedBuffer ? m_private : m_ordinary;
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  std::unique_ptr<Endpoint> fallback;
  MarkdownWebJobController controller(shared.services, buffer);
  QSignalSpy requested(&controller, &MarkdownWebJobController::protectedSessionRequested);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller, [&]() {
    fallback = std::make_unique<Endpoint>(buffer);
    fallback->ready();
    controller.setProtectedSession(&fallback->session, QByteArrayLiteral("bundled"));
  });
  controller.setSuspended(true);
  int styleCallbacks = 0;
  controller.fetchHighlightStyles([&](const auto *p_styles) {
    ++styleCallbacks;
    QVERIFY(!p_styles);
  });
  controller.previewMath(1, 1, QStringLiteral("x^2"), 1);
  controller.previewGraph(2, 1, QStringLiteral("mermaid"), QStringLiteral("graph TD; A-->B"), 1);
  controller.highlightMath(3, 1, QStringLiteral("x^2"));
  controller.highlightCodeBlock(4, 1, QStringLiteral("```cpp\nint x;\n```"));
  controller.htmlToMarkdown(5, 1, QStringLiteral("<b>Dormant</b>"));
  QCOMPARE(styleCallbacks, 1);
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(0).toString(), QStringLiteral("markdown.htmlToMarkdown"));
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(5));
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QVERIFY(converted.isEmpty());
  QCoreApplication::processEvents();
  QCOMPARE(requested.size(), 0);
  QCOMPARE(ready.size(), 0);
  QCOMPARE(shared.created, 0);
  QVERIFY(!fallback);

  controller.setSuspended(false);
  QCoreApplication::processEvents();
  QCOMPARE(requested.size(), 0);
  QCOMPARE(ready.size(), 0);
  QCOMPARE(shared.created, 0);
  QVERIFY(!fallback);
  controller.htmlToMarkdown(6, 2, QStringLiteral("<strong>Converted</strong>"));
  if (protectedBuffer) {
    QVERIFY(fallback);
    QTRY_COMPARE(fallback->requests.size(), 1);
    fallback->complete(0);
  } else {
    QTRY_COMPARE(shared.requests.size(), 1);
    shared.complete(0);
  }
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(6));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Converted**"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(requested.size(), protectedBuffer ? 1 : 0);
  QCOMPARE(shared.created, protectedBuffer ? 0 : 1);
  QCOMPARE(ready.size(), 1);
}

void TestMarkdownWebJobController::suspendedSharedReadinessDoesNotPublish() {
  SharedRuntime shared(m_buffers.get(), &m_hooks, false);
  MarkdownWebJobController controller(shared.services, m_ordinary);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Waiting for runtime</b>"));
  QCOMPARE(shared.created, 1);
  QVERIFY(shared.requests.isEmpty());
  controller.setSuspended(true);
  QCOMPARE(failed.size(), 1);
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QVERIFY(converted.isEmpty());
  shared.session->setReady(shared.session->generation());
  QTRY_COMPARE(shared.releasedScopes.size(), 1);
  QCOMPARE(ready.size(), 0);
  QVERIFY(shared.requests.isEmpty());

  // The application-wide runtime remains usable by another window while this one sleeps.
  MarkdownWebJobController sibling(shared.services, m_ordinary);
  QSignalSpy siblingConverted(&sibling, &MarkdownWebJobController::htmlToMarkdownReady);
  sibling.htmlToMarkdown(1, 1, QStringLiteral("<b>Visible sibling</b>"));
  QTRY_COMPARE(shared.requests.size(), 1);
  shared.complete(0, QStringLiteral("**Visible sibling**"));
  QCOMPARE(siblingConverted.size(), 1);
  QCOMPARE(siblingConverted.first().at(2).toString(), QStringLiteral("**Visible sibling**"));
  QVERIFY(converted.isEmpty());
  QCOMPARE(ready.size(), 0);
  controller.setSuspended(false);
  QCOMPARE(ready.size(), 0);
  QCOMPARE(shared.created, 1);
  controller.htmlToMarkdown(2, 2, QStringLiteral("<b>Awake</b>"));
  QTRY_COMPARE(shared.requests.size(), 2);
  shared.complete(1, QStringLiteral("**Awake**"));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(2));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Awake**"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(ready.size(), 1);
  QCOMPARE(shared.created, 1);
}

void TestMarkdownWebJobController::suspendedProtectedSessionIsReleased() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  std::unique_ptr<Endpoint> fallback;
  MarkdownWebJobController controller(shared.services, m_private);
  QSignalSpy requested(&controller, &MarkdownWebJobController::protectedSessionRequested);
  QSignalSpy release(&controller, &MarkdownWebJobController::protectedSessionReleaseRequested);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  connect(&controller, &MarkdownWebJobController::protectedSessionReleaseRequested, &controller,
          [&]() {
            if (fallback) {
              fallback.reset();
            }
          });
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Waiting for protected host</b>"));
  QCOMPARE(requested.size(), 1);
  QVERIFY(converted.isEmpty());
  QVERIFY(failed.isEmpty());
  controller.setSuspended(true);
  QCOMPARE(failed.size(), 1);
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QCOMPARE(release.size(), 1);

  fallback = std::make_unique<Endpoint>(m_private);
  fallback->ready();
  const QPointer<WebViewJobSession> lateSession(&fallback->session);
  controller.setProtectedSession(lateSession, QByteArrayLiteral("late bundled CSS"));
  QCOMPARE(release.size(), 2);
  QVERIFY(lateSession.isNull());
  QCOMPARE(ready.size(), 0);
  QCOMPARE(failed.size(), 1);
  QVERIFY(converted.isEmpty());
  QCOMPARE(shared.created, 0);

  controller.setSuspended(false);
  QCOMPARE(requested.size(), 1);
  QCOMPARE(ready.size(), 0);
  QVERIFY(!fallback);
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller, [&]() {
    fallback = std::make_unique<Endpoint>(m_private);
    fallback->ready();
    controller.setProtectedSession(&fallback->session, QByteArrayLiteral("fresh bundled CSS"));
  });
  controller.htmlToMarkdown(2, 2, QStringLiteral("<strong>Converted</strong>"));
  QCOMPARE(requested.size(), 2);
  QVERIFY(fallback);
  QTRY_COMPARE(fallback->requests.size(), 1);
  fallback->complete(0);
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(2));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Converted**"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(ready.size(), 1);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::suspendedSiblingCloseDoesNotRevoke_data() {
  QTest::addColumn<bool>("protectedBuffer");
  QTest::newRow("ordinary") << false;
  QTest::newRow("protected") << true;
}

void TestMarkdownWebJobController::suspendedSiblingCloseDoesNotRevoke() {
  QFETCH(bool, protectedBuffer);
  const auto &buffer = protectedBuffer ? m_private : m_ordinary;
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController controller(shared.services, buffer);
  auto sibling = std::make_unique<MarkdownWebJobController>(shared.services, buffer);
  QSignalSpy release(&controller, &MarkdownWebJobController::protectedSessionReleaseRequested);
  controller.setSuspended(true);
  QCOMPARE(release.size(), 1);
  sibling.reset();
  ViewWindowCloseEvent event;
  event.bufferId = buffer.id();
  m_hooks.doAction(HookNames::ViewWindowAfterClose, event);
  QCOMPARE(release.size(), 1);
  controller.setSuspended(false);
  QCOMPARE(shared.created, 0);
  Endpoint replacement(buffer);
  replacement.ready();
  controller.setPreferredSession(&replacement.session);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Remaining window</b>"));
  QTRY_COMPARE(replacement.requests.size(), 1);
  replacement.complete(0, QStringLiteral("**Remaining window**"));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Remaining window**"));
  QVERIFY(failed.isEmpty());
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::suspensionDoesNotUndoRevocation_data() {
  QTest::addColumn<bool>("protectedBuffer");
  QTest::newRow("ordinary") << false;
  QTest::newRow("protected") << true;
}

void TestMarkdownWebJobController::suspensionDoesNotUndoRevocation() {
  QFETCH(bool, protectedBuffer);
  const auto &buffer = protectedBuffer ? m_private : m_ordinary;
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint reader(buffer);
  reader.ready();
  MarkdownWebJobController controller(shared.services, buffer);
  QSignalSpy requested(&controller, &MarkdownWebJobController::protectedSessionRequested);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.setSuspended(true);
  controller.revoke();
  controller.setSuspended(false);
  controller.setPreferredSession(&reader.session);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Revoked</b>"));
  QCOMPARE(failed.size(), 1);
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QVERIFY(converted.isEmpty());
  QCoreApplication::processEvents();
  QCOMPARE(ready.size(), 0);
  QCOMPARE(requested.size(), 0);
  QCOMPARE(shared.created, 0);
  QVERIFY(reader.requests.isEmpty());
}

void TestMarkdownWebJobController::suspensionDoesNotUnlockProtectedBuffer() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  std::unique_ptr<Endpoint> fallback;
  MarkdownWebJobController controller(shared.services, m_private);
  QSignalSpy requested(&controller, &MarkdownWebJobController::protectedSessionRequested);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller, [&]() {
    fallback = std::make_unique<Endpoint>(m_private);
    fallback->ready();
    controller.setProtectedSession(&fallback->session, QByteArrayLiteral("bundled"));
  });
  controller.setSuspended(true);
  QVERIFY(m_buffers->beginProtectedLocking());
  const auto restoreLock = qScopeGuard([&]() { m_buffers->cancelProtectedLocking(); });
  controller.setSuspended(false);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Still locked</b>"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(1));
  QVERIFY(converted.isEmpty());
  QCOMPARE(requested.size(), 0);
  QCOMPARE(ready.size(), 0);
  QCOMPARE(shared.created, 0);
  QVERIFY(!fallback);

  controller.setSuspended(true);
  m_buffers->cancelProtectedLocking();
  QCOMPARE(ready.size(), 0);
  controller.htmlToMarkdown(2, 2, QStringLiteral("<b>Still suspended</b>"));
  QCOMPARE(failed.size(), 2);
  QCOMPARE(failed.last().at(1).toULongLong(), quint64(2));
  QVERIFY(converted.isEmpty());
  QCOMPARE(requested.size(), 0);
  controller.setSuspended(false);
  QCOMPARE(ready.size(), 0);
  QVERIFY(!fallback);
  controller.htmlToMarkdown(3, 3, QStringLiteral("<b>Unlocked and awake</b>"));
  QCOMPARE(requested.size(), 1);
  QVERIFY(fallback);
  QTRY_COMPARE(fallback->requests.size(), 1);
  fallback->complete(0, QStringLiteral("**Unlocked and awake**"));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(3));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Unlocked and awake**"));
  QCOMPARE(failed.size(), 2);
  QCOMPARE(ready.size(), 1);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::staleAutomaticGenerationCannotPublish() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController controller(shared.services, m_ordinary);
  QSignalSpy highlighted(&controller, &MarkdownWebJobController::highlightCodeBlockReady);
  controller.highlightCodeBlock(3, 10, QStringLiteral("```cpp\nold\n```"));
  QTRY_COMPARE(shared.requests.size(), 1);
  controller.highlightCodeBlock(3, 11, QStringLiteral("```cpp\ncurrent\n```"));
  // Cancelled active rendering must drain before its replacement can execute.
  QCoreApplication::processEvents();
  QCOMPARE(shared.requests.size(), 1);
  shared.session->complete(shared.requests.at(0), true, QStringLiteral("stale HTML"), {});
  QCOMPARE(highlighted.size(), 0);
  QTRY_COMPARE(shared.requests.size(), 2);
  shared.session->complete(shared.requests.at(1), true, QStringLiteral("current HTML"), {});
  QCOMPARE(highlighted.size(), 1);
  QCOMPARE(highlighted.first().at(1).toULongLong(), quint64(11));
  controller.highlightCodeBlock(3, 9, QStringLiteral("```cpp\neven older\n```"));
  QCoreApplication::processEvents();
  QCOMPARE(shared.requests.size(), 2);
  QCOMPARE(highlighted.size(), 1);
}

void TestMarkdownWebJobController::ownerDestructionAndEndpointTeardown() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  int callbacks = 0;
  auto owner = std::make_unique<MarkdownWebJobController>(shared.services, m_ordinary);
  connect(owner.get(), &MarkdownWebJobController::htmlToMarkdownReady, this,
          [&]() { ++callbacks; });
  connect(owner.get(), &MarkdownWebJobController::operationFailed, this, [&]() { ++callbacks; });
  owner->htmlToMarkdown(1, 1, QStringLiteral("<b>Gone</b>"));
  QTRY_COMPARE(shared.requests.size(), 1);
  const auto oldId = shared.requests.first();
  owner.reset();
  shared.session->complete(oldId, true, QStringLiteral("**Late**"), {});
  QCOMPARE(callbacks, 0);

  MarkdownWebJobController live(shared.services, m_ordinary);
  QSignalSpy converted(&live, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&live, &MarkdownWebJobController::operationFailed);
  live.htmlToMarkdown(2, 2, QStringLiteral("<b>Retired</b>"));
  QTRY_COMPARE(shared.requests.size(), 2);
  shared.service.invalidate();
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(2));
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QVERIFY(converted.isEmpty());
}

void TestMarkdownWebJobController::protectedSessionsAreWindowScoped() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint firstFallback(m_private);
  Endpoint secondFallback(m_private);
  Endpoint reader(m_private);
  firstFallback.ready();
  secondFallback.ready();
  MarkdownWebJobController first(shared.services, m_private);
  MarkdownWebJobController second(shared.services, m_private);
  first.setProtectedSession(&firstFallback.session, QByteArrayLiteral("bundled first"));
  second.setProtectedSession(&secondFallback.session, QByteArrayLiteral("bundled second"));
  // A second window must not borrow the first window's protected session, even for the same note.
  second.setPreferredSession(&firstFallback.session);
  QSignalSpy firstConverted(&first, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy secondConverted(&second, &MarkdownWebJobController::htmlToMarkdownReady);
  first.htmlToMarkdown(1, 1, QStringLiteral("<b>First private</b>"));
  second.htmlToMarkdown(1, 1, QStringLiteral("<b>Second private</b>"));
  QTRY_COMPARE(firstFallback.requests.size(), 1);
  QTRY_COMPARE(secondFallback.requests.size(), 1);
  QSignalSpy release(&first, &MarkdownWebJobController::protectedSessionReleaseRequested);
  reader.ready();
  first.setPreferredSession(&reader.session);
  QCOMPARE(release.size(), 0);
  firstFallback.complete(0, QStringLiteral("**First private**"));
  QTRY_COMPARE(release.size(), 1);
  QCOMPARE(firstConverted.size(), 1);
  QCOMPARE(firstConverted.first().at(2).toString(), QStringLiteral("**First private**"));
  QVERIFY(secondConverted.isEmpty());
  secondFallback.complete(0, QStringLiteral("**Second private**"));
  QCOMPARE(secondConverted.size(), 1);
  QCOMPARE(secondConverted.first().at(2).toString(), QStringLiteral("**Second private**"));
  first.revoke();
  second.htmlToMarkdown(2, 2, QStringLiteral("<b>Still private</b>"));
  QTRY_COMPARE(secondFallback.requests.size(), 2);
  secondFallback.complete(1, QStringLiteral("**Still private**"));
  QCOMPARE(secondConverted.size(), 2);
  QCOMPARE(secondConverted.last().at(0).toULongLong(), quint64(2));
  QCOMPARE(secondConverted.last().at(2).toString(), QStringLiteral("**Still private**"));
  QCOMPARE(firstConverted.size(), 1);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::protectedLockCancelsBeforeReaderExistsAndCanResume() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint fallback(m_private);
  fallback.ready();
  MarkdownWebJobController controller(shared.services, m_private);
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller, [&]() {
    controller.setProtectedSession(&fallback.session, QByteArrayLiteral("bundled"));
  });
  QSignalSpy release(&controller, &MarkdownWebJobController::protectedSessionReleaseRequested);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Secret</b>"));
  QTRY_COMPARE(fallback.requests.size(), 1);
  QVERIFY(m_buffers->beginProtectedLocking());
  const auto restoreLock = qScopeGuard([&]() { m_buffers->cancelProtectedLocking(); });
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(1));
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QCOMPARE(release.size(), 1);
  QVERIFY(m_buffers->protectedBufferOperationsIdle(m_private.id()));
  fallback.complete(0, QStringLiteral("**Secret**"));
  QCOMPARE(failed.size(), 1);
  QVERIFY(converted.isEmpty());
  m_buffers->cancelProtectedLocking();
  controller.htmlToMarkdown(2, 2, QStringLiteral("<b>Resumed</b>"));
  QTRY_COMPARE(fallback.requests.size(), 2);
  fallback.complete(1, QStringLiteral("**Resumed**"));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(2));
  QCOMPARE(converted.first().at(2).toString(), QStringLiteral("**Resumed**"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::protectedFailureNeverUsesOrdinaryService() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController controller(shared.services, m_private);
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller,
          [&]() { controller.setProtectedSession(nullptr, {}); });
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Must stay private</b>"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(0).toString(), QStringLiteral("markdown.htmlToMarkdown"));
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(1));
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QVERIFY(converted.isEmpty());
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::protectedStylesUseSelectedBundledBytes() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint fallback(m_private);
  fallback.ready();
  MarkdownWebJobController controller(shared.services, m_private);
  const QByteArray css(".token.keyword { color: rgb(10, 20, 30); }");
  connect(&controller, &MarkdownWebJobController::protectedSessionRequested, &controller,
          [&]() { controller.setProtectedSession(&fallback.session, css); });
  int callbacks = 0;
  QVector<MarkdownWebJobController::CssRuleStyle> styles;
  controller.fetchHighlightStyles([&](const auto *p_rules) {
    ++callbacks;
    if (p_rules)
      styles = *p_rules;
  });
  QTRY_COMPARE(fallback.requests.size(), 1);
  QCOMPARE(fallback.requests.first().at(1).toString(), QStringLiteral("markdown.parseStyleSheet"));
  QCOMPARE(
      fallback.requests.first().at(2).toJsonObject().value(QStringLiteral("source")).toString(),
      QString::fromUtf8(css));
  fallback.session.complete(
      fallback.requests.first().at(0).toString(), true,
      QJsonArray{QJsonObject{{QStringLiteral("selector"), QStringLiteral(".token.keyword")},
                             {QStringLiteral("color"), QStringLiteral("rgb(10, 20, 30)")}}},
      {});
  QCOMPARE(callbacks, 1);
  QCOMPARE(styles.size(), 1);
  QCOMPARE(styles.first().toTextCharFormat().foreground().color(), QColor(10, 20, 30));
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::invalidBufferRejectsWork() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  const Buffer2 invalid;
  MarkdownWebJobController rejected(shared.services, invalid);
  QSignalSpy converted(&rejected, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&rejected, &MarkdownWebJobController::operationFailed);
  rejected.htmlToMarkdown(1, 1, QStringLiteral("<b>Invalid</b>"));
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(0).toString(), QStringLiteral("markdown.htmlToMarkdown"));
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  QVERIFY(converted.isEmpty());
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::emptyConversionIsDifferentFromFailure() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint reader(m_ordinary);
  reader.ready();
  MarkdownWebJobController controller(shared.services, m_ordinary);
  controller.setPreferredSession(&reader.session);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 9, QStringLiteral("<p></p>"));
  QTRY_COMPARE(reader.requests.size(), 1);
  reader.complete(0, QStringLiteral(""));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(converted.first().at(0).toULongLong(), quint64(1));
  QVERIFY(converted.first().at(2).toString().isEmpty());
  QVERIFY(failed.isEmpty());

  controller.htmlToMarkdown(2, 10, QStringLiteral("<p>Failure</p>"));
  QTRY_COMPARE(reader.requests.size(), 2);
  reader.session.complete(reader.requests.at(1).at(0).toString(), false, {},
                          QStringLiteral("Conversion unavailable"));
  QCOMPARE(converted.size(), 1);
  QCOMPARE(failed.size(), 1);
  QCOMPARE(failed.first().at(0).toString(), QStringLiteral("markdown.htmlToMarkdown"));
  QCOMPARE(failed.first().at(1).toULongLong(), quint64(2));
  QCOMPARE(failed.first().at(2).toULongLong(), quint64(10));
  QCOMPARE(failed.first().at(3).toString(), QStringLiteral("Conversion unavailable"));
}

void TestMarkdownWebJobController::bufferCloseRejectsLateCompletion() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  auto buffer = m_buffers->openVirtualBuffer(QStringLiteral("vx://web-job-lifetime"));
  QVERIFY(buffer.isValid());
  MarkdownWebJobController controller(shared.services, buffer);
  QSignalSpy converted(&controller, &MarkdownWebJobController::htmlToMarkdownReady);
  QSignalSpy failed(&controller, &MarkdownWebJobController::operationFailed);
  controller.htmlToMarkdown(1, 1, QStringLiteral("<b>Closing</b>"));
  QTRY_COMPARE(shared.requests.size(), 1);
  QVERIFY(m_buffers->closeBuffer(buffer.id()));
  ViewWindowCloseEvent event;
  event.bufferId = buffer.id();
  m_hooks.doAction(HookNames::ViewWindowAfterClose, event);
  QCOMPARE(failed.size(), 1);
  QVERIFY(!failed.first().at(3).toString().isEmpty());
  shared.complete(0, QStringLiteral("**Closing**"));
  QCOMPARE(failed.size(), 1);
  QVERIFY(converted.isEmpty());
}

void TestMarkdownWebJobController::collidingLocalIdsRemainOwnerScoped() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController first(shared.services, m_ordinary);
  MarkdownWebJobController second(shared.services, m_ordinary);
  QSignalSpy firstReady(&first, &MarkdownWebJobController::highlightCodeBlockReady);
  QSignalSpy secondReady(&second, &MarkdownWebJobController::highlightCodeBlockReady);
  first.highlightCodeBlock(7, 42, QStringLiteral("```cpp\nfirst\n```"));
  second.highlightCodeBlock(7, 42, QStringLiteral("```cpp\nsecond\n```"));
  QTRY_COMPARE(shared.requests.size(), 1);
  shared.session->complete(shared.requests.at(0), true, QStringLiteral("first highlight"), {});
  QCOMPARE(firstReady.size(), 1);
  QCOMPARE(secondReady.size(), 0);
  QTRY_COMPARE(shared.requests.size(), 2);
  QVERIFY(shared.requests.at(0) != shared.requests.at(1));
  shared.session->complete(shared.requests.at(1), true, QStringLiteral("second highlight"), {});
  QCOMPARE(firstReady.size(), 1);
  QCOMPARE(secondReady.size(), 1);
  QCOMPARE(firstReady.first().at(0).toInt(), 7);
  QCOMPARE(secondReady.first().at(0).toInt(), 7);
  QCOMPARE(firstReady.first().at(2).toString(), QStringLiteral("first highlight"));
  QCOMPARE(secondReady.first().at(2).toString(), QStringLiteral("second highlight"));
  QCOMPARE(shared.created, 1);
}

void TestMarkdownWebJobController::protectedPreviewRejectsActiveImages() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint fallback(m_private);
  fallback.ready();
  MarkdownWebJobController controller(shared.services, m_private);
  controller.setProtectedSession(&fallback.session, QByteArrayLiteral("bundled"));
  QSignalSpy previews(&controller, &MarkdownWebJobController::graphPreviewDataReady);
  QSignalSpy failures(&controller, &MarkdownWebJobController::operationFailed);
  controller.previewGraph(4, 20, QStringLiteral("mermaid"), QStringLiteral("graph TD; A-->B"), 1);
  QTRY_COMPARE(fallback.requests.size(), 1);
  fallback.session.complete(
      fallback.requests.at(0).at(0).toString(), true,
      QJsonObject{
          {QStringLiteral("format"), QStringLiteral("svg")},
          {QStringLiteral("data"),
           QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
                          "<script>alert(1)</script></svg>")},
          {QStringLiteral("base64"), false}},
      {});
  QCOMPARE(previews.size(), 0);
  QCOMPARE(failures.size(), 1);

  QImage image(2, 3, QImage::Format_ARGB32);
  image.fill(Qt::blue);
  QByteArray png;
  QBuffer bytes(&png);
  QVERIFY(bytes.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&bytes, "PNG"));
  controller.previewGraph(4, 21, QStringLiteral("mermaid"), QStringLiteral("graph TD; B-->C"), 2);
  QTRY_COMPARE(fallback.requests.size(), 2);
  fallback.session.complete(
      fallback.requests.at(1).at(0).toString(), true,
      QJsonObject{{QStringLiteral("format"), QStringLiteral("png")},
                  {QStringLiteral("data"), QString::fromLatin1(png.toBase64())},
                  {QStringLiteral("base64"), true},
                  {QStringLiteral("needScale"), true},
                  {QStringLiteral("logicalWidth"), 40},
                  {QStringLiteral("logicalHeight"), 20}},
      {});
  QCOMPARE(previews.size(), 1);
  const auto preview =
      qvariant_cast<MarkdownWebJobController::PreviewData>(previews.first().first());
  QCOMPARE(preview.m_id, quint64(4));
  QCOMPARE(preview.m_timeStamp, quint64(21));
  QCOMPARE(preview.m_logicalSize, QSize(40, 20));
  QCOMPARE(QImage::fromData(preview.m_data).size(), QSize(2, 3));
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::previewScopesAreStableAndGenerationBound() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController first(shared.services, m_ordinary);
  MarkdownWebJobController second(shared.services, m_ordinary);
  const QJsonObject image{
      {QStringLiteral("format"), QStringLiteral("svg")},
      {QStringLiteral("data"),
       QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"/>")}};
  first.previewMath(1, 8, QStringLiteral("macro declaration"), 1);
  QTRY_COMPARE(shared.requests.size(), 1);
  const auto firstScope = shared.arguments.at(0).value(QStringLiteral("scope")).toString();
  QVERIFY(!firstScope.isEmpty());
  shared.session->complete(shared.requests.at(0), true, image, {});
  first.previewGraph(2, 8, QStringLiteral("mermaid"), QStringLiteral("graph TD; A-->B"), 2);
  QTRY_COMPARE(shared.requests.size(), 2);
  QCOMPARE(shared.arguments.at(1).value(QStringLiteral("scope")).toString(), firstScope);
  shared.session->complete(shared.requests.at(1), true, image, {});
  second.previewMath(1, 8, QStringLiteral("other editor formula"), 1);
  QTRY_COMPARE(shared.requests.size(), 3);
  const auto secondScope = shared.arguments.at(2).value(QStringLiteral("scope")).toString();
  QVERIFY(!secondScope.isEmpty());
  QVERIFY(secondScope != firstScope);
  shared.session->complete(shared.requests.at(2), true, image, {});
  QVERIFY(shared.releasedScopes.isEmpty());

  first.invalidate();
  QTRY_COMPARE(shared.releasedScopes.size(), 1);
  QCOMPARE(shared.releasedScopes.first().keys(), QStringList{QStringLiteral("scope")});
  QCOMPARE(shared.releasedScopes.first().value(QStringLiteral("scope")).toString(), firstScope);
  first.previewMath(1, 9, QStringLiteral("new renderer generation"), 1);
  QTRY_COMPARE(shared.requests.size(), 4);
  const auto nextScope = shared.arguments.at(3).value(QStringLiteral("scope")).toString();
  QVERIFY(nextScope != firstScope);
  QVERIFY(nextScope != secondScope);
  shared.session->complete(shared.requests.at(3), true, image, {});
  first.revoke();
  QTRY_COMPARE(shared.releasedScopes.size(), 2);
  QCOMPARE(shared.releasedScopes.last().value(QStringLiteral("scope")).toString(), nextScope);
  QCOMPARE(shared.created, 1);
}

void TestMarkdownWebJobController::scopeCleanupSurvivesOwnerAndDrainsActiveWork() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  auto controller = std::make_unique<MarkdownWebJobController>(shared.services, m_ordinary);
  controller->previewMath(1, 4, QStringLiteral("pending scoped formula"), 1);
  QTRY_COMPARE(shared.requests.size(), 1);
  const auto scope = shared.arguments.first().value(QStringLiteral("scope")).toString();
  controller.reset();
  QCoreApplication::processEvents();
  QVERIFY(shared.releasedScopes.isEmpty());
  QVERIFY(!shared.session->isIdle());
  // Cleanup cannot run until the cancelled renderer reports its real terminal completion.
  shared.session->complete(shared.requests.first(), true, QJsonObject(), {});
  QTRY_COMPARE(shared.releasedScopes.size(), 1);
  QCOMPARE(shared.releasedScopes.first().value(QStringLiteral("scope")).toString(), scope);
  QVERIFY(shared.session->isIdle());
}

void TestMarkdownWebJobController::readyTracksEffectiveEndpointOnly() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint fallback(m_private);
  Endpoint reader(m_private);
  reader.ready();
  MarkdownWebJobController controller(shared.services, m_private);
  QSignalSpy ready(&controller, &MarkdownWebJobController::ready);
  controller.setProtectedSession(&fallback.session, QByteArrayLiteral("bundled"));
  QCOMPARE(ready.size(), 0);
  controller.setPreferredSession(&reader.session);
  QCOMPARE(ready.size(), 1);
  controller.setPreferredSession(&reader.session);
  QCOMPARE(ready.size(), 1);
  fallback.ready(); // A late cold fallback cannot invalidate a current reader's preview cache.
  QCOMPARE(ready.size(), 1);
  controller.invalidate();
  QCOMPARE(ready.size(), 1);
  controller.setPreferredSession(&reader.session);
  QCOMPARE(ready.size(), 2);
  QCOMPARE(shared.created, 0);
}

void TestMarkdownWebJobController::scopeCleanupReachesEveryObservedEndpoint() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  Endpoint reader(m_ordinary);
  MarkdownWebJobController controller(shared.services, m_ordinary);
  controller.previewMath(1, 6, QStringLiteral("declaration on shared"), 1);
  QTRY_COMPARE(shared.requests.size(), 1);
  const auto scope = shared.arguments.first().value(QStringLiteral("scope")).toString();
  reader.ready();
  controller.setPreferredSession(&reader.session);
  controller.previewMath(2, 6, QStringLiteral("replay on reader"), 1);
  QTRY_COMPARE(reader.requests.size(), 1);
  QCOMPARE(reader.requests.first().at(2).toJsonObject().value(QStringLiteral("scope")).toString(),
           scope);
  controller.invalidate();
  QVERIFY(shared.releasedScopes.isEmpty());
  QVERIFY(reader.releasedScopes.isEmpty());
  shared.session->complete(shared.requests.first(), true, QJsonObject(), {});
  reader.session.complete(reader.requests.first().first().toString(), true, QJsonObject(), {});
  QTRY_COMPARE(shared.releasedScopes.size(), 2); // Handoff, then generation retirement.
  QTRY_COMPARE(reader.releasedScopes.size(), 1);
  QCOMPARE(shared.releasedScopes.last().value(QStringLiteral("scope")).toString(), scope);
  QCOMPARE(reader.releasedScopes.first().value(QStringLiteral("scope")).toString(), scope);
}

void TestMarkdownWebJobController::individualCancellationPreservesOtherBlocks() {
  SharedRuntime shared(m_buffers.get(), &m_hooks);
  MarkdownWebJobController controller(shared.services, m_ordinary);
  QSignalSpy highlighted(&controller, &MarkdownWebJobController::highlightCodeBlockReady);
  controller.highlightCodeBlock(1, 9, QStringLiteral("```cpp\nold first\n```"));
  QTRY_COMPARE(shared.requests.size(), 1);
  controller.highlightCodeBlock(2, 9, QStringLiteral("```cpp\nsecond\n```"));
  controller.highlightCodeBlock(1, 10, QStringLiteral("```cpp\nnew first\n```"));
  shared.session->complete(shared.requests.at(0), true, QStringLiteral("cancelled old first"), {});
  QCOMPARE(highlighted.size(), 0);
  QTRY_COMPARE(shared.requests.size(), 2);
  shared.session->complete(shared.requests.at(1), true, QStringLiteral("second highlight"), {});
  QCOMPARE(highlighted.size(), 1);
  QCOMPARE(highlighted.first().at(0).toInt(), 2);
  QTRY_COMPARE(shared.requests.size(), 3);
  shared.session->complete(shared.requests.at(2), true, QStringLiteral("new first highlight"), {});
  QCOMPARE(highlighted.size(), 2);
  QCOMPARE(highlighted.last().at(0).toInt(), 1);
  QCOMPARE(highlighted.last().at(1).toULongLong(), quint64(10));
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
  QStandardPaths::setTestModeEnabled(true);
  WebEngineProfileService::registerProtectedScheme();
  QApplication app(argc, argv);
  tests::TestMarkdownWebJobController test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markdownwebjobcontroller.moc"
