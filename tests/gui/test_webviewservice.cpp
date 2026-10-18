// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include <memory>
#include <utility>

#include <core/servicelocator.h>
#include <gui/services/webviewjobs.h>
#include <gui/services/webviewservice.h>

using namespace vnotex;

namespace tests {
namespace {

QVariant evaluate(QWebEnginePage &p_page, const QString &p_script) {
  auto result = std::make_shared<std::pair<bool, QVariant>>();
  p_page.runJavaScript(p_script, [result](const QVariant &p_value) {
    result->second = p_value;
    result->first = true;
  });
  if (!QTest::qWaitFor([result]() { return result->first; }, 10000)) {
    return {};
  }
  return result->second;
}

struct CompletionLog {
  QList<WebViewJobResult> results;

  WebViewJobSession::Completion callback() {
    return [this](const WebViewJobResult &p_result) { results.append(p_result); };
  }
};

struct PageFixture {
  QWebEngineProfile profile;
  QWebEngineView view;
  QWebEnginePage page{&profile};
  QWebChannel channel;
  WebViewJobSession session{&page, &channel};

  PageFixture() {
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.setAttribute(Qt::WA_ShowWithoutActivating);
    view.resize(640, 480);
    view.setPage(&page);
    page.setWebChannel(&channel);
    view.show();
  }
};

} // namespace

// This is an independent page's existing native object, not a job-result mock.
class PageCounter : public QObject {
  Q_OBJECT

public:
  int total = 0;

public slots:
  int add(int p_value) {
    total += p_value;
    return total;
  }
};

class TestWebViewService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void lazyServiceRunsIndependentPromisesAndRasterWork();
  void readiness_data();
  void readiness();
  void errorsReleaseTheSerialQueue();
  void cancellationDrainsActiveDomWork();
  void cancelOneJobPreservesSameOwner_data();
  void cancelOneJobPreservesSameOwner();
  void ownerDestructionDropsCallbacksAndCaptures();
  void invalidationWaitsForDrain();
  void reloadRetiresIdsAndRecovers();
  void failedLoadSettlesAndRecovers();
  void providerFailureSettlesAndRecovers_data();
  void providerFailureSettlesAndRecovers();
  void rendererFailureSettlesAndRecovers();
  void borrowedPageAndChannelSurvive();
  void endpointDestruction_data();
  void endpointDestruction();
  void serviceResetRetiresOnlyItsOwnHost();
  void completionCanDestroySession();

private:
  QString html(bool p_runtimeFirst = true, bool p_automatic = true) const;
  QString m_channelScript;
  QString m_jobsScript;
};

void TestWebViewService::initTestCase() {
  QFile channel(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
  QVERIFY(channel.open(QIODevice::ReadOnly));
  m_channelScript = QString::fromUtf8(channel.readAll());
  const auto path = QFINDTESTDATA("../../src/data/extra/web/js/webviewjobs.js");
  QVERIFY(!path.isEmpty());
  QFile jobs(path);
  QVERIFY(jobs.open(QIODevice::ReadOnly));
  m_jobsScript = QString::fromUtf8(jobs.readAll());
}

QString TestWebViewService::html(bool p_runtimeFirst, bool p_automatic) const {
  // A non-Markdown provider, using the production dispatcher and exactly one
  // actual WebChannel handshake. All work executes in Chromium, including DOM
  // cleanup and Promise rejection; runJavaScript is only a test control plane.
  return QStringLiteral("<!doctype html><html><body><main id='work'></main><script>") +
         m_channelScript + QStringLiteral("\n</script><script>\n") + m_jobsScript +
         QStringLiteral(R"JS(
</script><script>
window.finished = 0;
window.cancelledObserved = false;
window.channelSeen = false;
window.startedId = '';
window.releaseJob = null;
const root = document.getElementById('work');
vxWebJobs.registerHandler('compute.squares', async function (args) {
    const values = await Promise.resolve(args.values.map(value => value * value));
    return values.reduce((sum, value) => sum + value, 0);
});
vxWebJobs.registerHandler('compute.factorial', function (args) {
    let result = 1;
    for (let n = 2; n <= args.n; ++n) { result *= n; }
    return result;
});
vxWebJobs.registerHandler('compute.throw', function () {
    throw new Error('synchronous computation failed');
});
vxWebJobs.registerHandler('compute.reject', async function () {
    await Promise.resolve();
    throw new Error('asynchronous computation failed');
});
vxWebJobs.registerHandler('compute.circular', function () {
    const result = {};
    result.self = result;
    return result;
});
vxWebJobs.registerHandler('dom.hold', async function (args, context) {
    const element = document.createElement('div');
    element.id = context.id;
    element.textContent = String(args.n);
    root.appendChild(element);
    window.startedId = context.id;
    try {
        await new Promise(resolve => { window.releaseJob = resolve; });
        return args.n * args.n;
    } finally {
        window.cancelledObserved = context.cancelled;
        element.remove();
        window.releaseJob = null;
        ++window.finished;
    }
});
vxWebJobs.registerHandler('dom.inspect', function () {
    return { active: root.childElementCount, finished: window.finished,
             cancelled: window.cancelledObserved };
});
vxWebJobs.registerHandler('dom.raster', async function () {
    const canvas = document.createElement('canvas');
    canvas.width = 16;
    canvas.height = 12;
    root.appendChild(canvas);
    try {
        const context = canvas.getContext('2d');
        context.fillStyle = 'rgb(17, 34, 51)';
        context.fillRect(0, 0, canvas.width, canvas.height);
        const image = new Image();
        image.src = canvas.toDataURL('image/png');
        await image.decode();
        await new Promise(resolve => requestAnimationFrame(resolve));
        context.drawImage(image, 0, 0);
        return { pixel: Array.from(context.getImageData(0, 0, 1, 1).data),
                 width: image.naturalWidth, height: image.naturalHeight,
                 viewportWidth: window.innerWidth };
    } finally {
        canvas.remove();
    }
});
const runtimeFirst = %1;
const automatic = %2;
if (runtimeFirst) { vxWebJobs.setReady(); }
new QWebChannel(qt.webChannelTransport, function (channel) {
    window.pageChannel = channel;
    window.channelSeen = true;
    if (runtimeFirst) {
        window.finishReadiness = function () { vxWebJobs.bind(channel.objects.vxJobs); };
    } else {
        vxWebJobs.bind(channel.objects.vxJobs);
        window.finishReadiness = function () { vxWebJobs.setReady(); };
    }
    if (automatic) { window.finishReadiness(); }
});
</script></body></html>
)JS")
             .arg(p_runtimeFirst ? QStringLiteral("true") : QStringLiteral("false"),
                  p_automatic ? QStringLiteral("true") : QStringLiteral("false"));
}

void TestWebViewService::lazyServiceRunsIndependentPromisesAndRasterWork() {
  ServiceLocator services;
  QWebEngineProfile profile;
  QObject firstOwner;
  QObject secondOwner;
  CompletionLog first;
  CompletionLog second;
  CompletionLog raster;
  int bootstraps = 0;
  QPointer<QWebEnginePage> page;
  WebViewService service(services, &profile, [&](QWebEnginePage *p_page, QWebChannel *p_channel) {
    ++bootstraps;
    page = p_page;
    QCOMPARE(p_page->webChannel(), p_channel);
    QVERIFY(p_channel->registeredObjects().contains(QStringLiteral("vxJobs")));
    p_page->setHtml(html());
  });
  QCOMPARE(bootstraps, 0);
  auto session = service.sharedSession();
  QVERIFY(session);
  QCOMPARE(service.sharedSession(), session);
  QCOMPARE(bootstraps, 1);
  QSignalSpy requested(session, &WebViewJobSession::jobRequested);
  const auto firstId =
      session->submit(QStringLiteral("compute.squares"), {{"values", QJsonArray{2, 3, 5}}},
                      &firstOwner, first.callback());
  const auto secondId = session->submit(QStringLiteral("compute.factorial"), {{"n", 6}},
                                        &secondOwner, second.callback());
  QVERIFY(!firstId.isEmpty());
  QVERIFY(firstId != secondId);
  session->submit(QStringLiteral("dom.raster"), {}, &secondOwner, raster.callback());
  QTRY_COMPARE_WITH_TIMEOUT(first.results.size(), 1, 20000);
  QTRY_COMPARE_WITH_TIMEOUT(second.results.size(), 1, 20000);
  QTRY_COMPARE_WITH_TIMEOUT(raster.results.size(), 1, 20000);
  QCOMPARE(first.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(first.results.first().value.toInt(), 38);
  QCOMPARE(second.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(second.results.first().value.toInt(), 720);
  QCOMPARE(raster.results.first().status, WebViewJobResult::Status::Success);
  const auto image = raster.results.first().value.toObject();
  QCOMPARE(image.value(QStringLiteral("pixel")).toArray(), QJsonArray({17, 34, 51, 255}));
  QCOMPARE(image.value(QStringLiteral("width")).toInt(), 16);
  QCOMPARE(image.value(QStringLiteral("height")).toInt(), 12);
  QVERIFY(image.value(QStringLiteral("viewportWidth")).toInt() > 0);
  QCOMPARE(requested.size(), 3);
  QCOMPARE(requested.at(0).at(0).toString(), firstId);
  QCOMPARE(requested.at(1).at(0).toString(), secondId);
  QVERIFY(session->isIdle());
  QCOMPARE(bootstraps, 1);
}

void TestWebViewService::readiness_data() {
  QTest::addColumn<bool>("runtimeFirst");
  QTest::newRow("runtime-before-channel") << true;
  QTest::newRow("channel-before-runtime") << false;
}

void TestWebViewService::readiness() {
  QFETCH(bool, runtimeFirst);
  QObject owner;
  CompletionLog completed;
  PageFixture fixture;
  QSignalSpy readiness(&fixture.session, &WebViewJobSession::readyChanged);
  const auto initialGeneration = fixture.session.generation();
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                         completed.callback());
  QVERIFY(!fixture.session.isReady());
  QVERIFY(!fixture.session.isIdle());
  QSignalSpy loaded(&fixture.page, &QWebEnginePage::loadFinished);
  fixture.page.setHtml(html(runtimeFirst, false));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 20000);
  QVERIFY(loaded.first().first().toBool());
  QTRY_VERIFY_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.channelSeen")).toBool(),
                           10000);
  QCOMPARE(fixture.session.generation(), initialGeneration);
  QVERIFY(!fixture.session.isReady());
  QCOMPARE(completed.results.size(), 0);
  fixture.page.runJavaScript(QStringLiteral("window.finishReadiness()"));
  QTRY_COMPARE_WITH_TIMEOUT(completed.results.size(), 1, 10000);
  QCOMPARE(completed.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(completed.results.first().value.toInt(), 120);
  QCOMPARE(readiness.size(), 1);
  QVERIFY(readiness.first().first().toBool());
  fixture.page.runJavaScript(QStringLiteral("window.finishReadiness(); vxWebJobs.setReady()"));
  QCOMPARE(evaluate(fixture.page, QStringLiteral("6 * 7")).toInt(), 42);
  QCOMPARE(readiness.size(), 1);
}

void TestWebViewService::errorsReleaseTheSerialQueue() {
  QObject owner;
  CompletionLog completed;
  PageFixture fixture;
  fixture.page.setHtml(html());
  const QStringList operations{QStringLiteral("compute.throw"), QStringLiteral("compute.reject"),
                               QStringLiteral("compute.absent"), QStringLiteral("compute.circular"),
                               QStringLiteral("compute.factorial")};
  for (const auto &operation : operations) {
    fixture.session.submit(operation, {{"n", 4}}, &owner, completed.callback());
  }
  QTRY_COMPARE_WITH_TIMEOUT(completed.results.size(), operations.size(), 20000);
  for (int i = 0; i < 4; ++i) {
    QCOMPARE(completed.results.at(i).status, WebViewJobResult::Status::Error);
    QVERIFY(!completed.results.at(i).error.isEmpty());
  }
  QVERIFY(completed.results.at(0).error.contains(QStringLiteral("synchronous computation failed")));
  QVERIFY(
      completed.results.at(1).error.contains(QStringLiteral("asynchronous computation failed")));
  QVERIFY(completed.results.at(2).error.contains(QStringLiteral("Unknown web job operation")));
  QCOMPARE(completed.results.last().status, WebViewJobResult::Status::Success);
  QCOMPARE(completed.results.last().value.toInt(), 24);
  QVERIFY(fixture.session.isIdle());
}

void TestWebViewService::cancellationDrainsActiveDomWork() {
  QObject cancelledOwner;
  QObject otherOwner;
  CompletionLog cancelled;
  CompletionLog inspected;
  PageFixture fixture;
  fixture.page.setHtml(html());
  QSignalSpy requested(&fixture.session, &WebViewJobSession::jobRequested);
  const auto activeId = fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 7}},
                                               &cancelledOwner, cancelled.callback());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &cancelledOwner,
                         cancelled.callback());
  fixture.session.submit(QStringLiteral("dom.inspect"), {}, &otherOwner, inspected.callback());
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            activeId, 20000);
  fixture.session.cancelOwner(&cancelledOwner);
  QCOMPARE(cancelled.results.size(), 2);
  for (const auto &result : cancelled.results) {
    QCOMPARE(result.status, WebViewJobResult::Status::Cancelled);
  }
  fixture.session.cancelOwner(&cancelledOwner);
  QCOMPARE(cancelled.results.size(), 2);
  QVERIFY(!fixture.session.isIdle());
  QCOMPARE(requested.size(), 1);
  QCOMPARE(inspected.results.size(), 0);
  QCOMPARE(
      evaluate(fixture.page, QStringLiteral("document.getElementById('work').childElementCount"))
          .toInt(),
      1);
  fixture.page.runJavaScript(QStringLiteral("window.releaseJob()"));
  QTRY_COMPARE_WITH_TIMEOUT(inspected.results.size(), 1, 10000);
  const auto inspection = inspected.results.first().value.toObject();
  QCOMPARE(inspected.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(inspection.value(QStringLiteral("active")).toInt(), 0);
  QCOMPARE(inspection.value(QStringLiteral("finished")).toInt(), 1);
  QVERIFY(inspection.value(QStringLiteral("cancelled")).toBool());
  QCOMPARE(requested.size(), 2);
  QVERIFY(fixture.session.isIdle());
  fixture.session.complete(activeId, true, 999, {});
  QCOMPARE(cancelled.results.size(), 2);
  QCOMPARE(inspected.results.size(), 1);
}

void TestWebViewService::cancelOneJobPreservesSameOwner_data() {
  QTest::addColumn<bool>("cancelActive");
  QTest::newRow("cancel-active-job") << true;
  QTest::newRow("cancel-queued-job") << false;
}

void TestWebViewService::cancelOneJobPreservesSameOwner() {
  QFETCH(bool, cancelActive);
  QObject owner;
  CompletionLog active;
  CompletionLog queued;
  CompletionLog inspected;
  PageFixture fixture;
  fixture.page.setHtml(html());
  QSignalSpy requested(&fixture.session, &WebViewJobSession::jobRequested);
  const auto activeId =
      fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 7}}, &owner, active.callback());
  const auto queuedId = fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}},
                                               &owner, queued.callback());
  fixture.session.submit(QStringLiteral("dom.inspect"), {}, &owner, inspected.callback());
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            activeId, 20000);
  const auto cancelledId = cancelActive ? activeId : queuedId;
  auto &cancelled = cancelActive ? active : queued;
  auto &surviving = cancelActive ? queued : active;
  fixture.session.cancel(cancelledId);
  fixture.session.cancel(cancelledId);
  fixture.session.cancel({});
  fixture.session.cancel(QStringLiteral("nonexistent-job"));
  QCOMPARE(cancelled.results.size(), 1);
  QCOMPARE(cancelled.results.first().status, WebViewJobResult::Status::Cancelled);
  QCOMPARE(surviving.results.size(), 0);
  QCOMPARE(inspected.results.size(), 0);
  QCOMPARE(requested.size(), 1);
  QVERIFY(!fixture.session.isIdle());

  fixture.page.runJavaScript(QStringLiteral("window.releaseJob()"));
  QTRY_COMPARE_WITH_TIMEOUT(inspected.results.size(), 1, 10000);
  QCOMPARE(cancelled.results.size(), 1);
  QCOMPARE(surviving.results.size(), 1);
  QCOMPARE(surviving.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(surviving.results.first().value.toInt(), cancelActive ? 120 : 49);
  QCOMPARE(inspected.results.first().status, WebViewJobResult::Status::Success);
  const auto inspection = inspected.results.first().value.toObject();
  QCOMPARE(inspection.value(QStringLiteral("active")).toInt(), 0);
  QCOMPARE(inspection.value(QStringLiteral("finished")).toInt(), 1);
  QCOMPARE(inspection.value(QStringLiteral("cancelled")).toBool(), cancelActive);
  QCOMPARE(requested.size(), cancelActive ? 3 : 2);
  QVERIFY(fixture.session.isIdle());
}

void TestWebViewService::ownerDestructionDropsCallbacksAndCaptures() {
  auto owner = std::make_unique<QObject>();
  QObject otherOwner;
  CompletionLog inspected;
  int callbacks = 0;
  auto lifetime = std::make_shared<int>(42);
  std::weak_ptr<int> weakLifetime = lifetime;
  PageFixture fixture;
  fixture.page.setHtml(html());
  const auto id =
      fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 9}}, owner.get(),
                             [lifetime, &callbacks](const WebViewJobResult &) { ++callbacks; });
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}}, owner.get(),
                         [lifetime, &callbacks](const WebViewJobResult &) { ++callbacks; });
  fixture.session.submit(QStringLiteral("dom.inspect"), {}, &otherOwner, inspected.callback());
  lifetime.reset();
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            id, 20000);
  owner.reset();
  QVERIFY(weakLifetime.expired());
  QCOMPARE(callbacks, 0);
  QVERIFY(!fixture.session.isIdle());
  fixture.page.runJavaScript(QStringLiteral("window.releaseJob()"));
  QTRY_COMPARE_WITH_TIMEOUT(inspected.results.size(), 1, 10000);
  QCOMPARE(callbacks, 0);
  const auto inspection = inspected.results.first().value.toObject();
  QCOMPARE(inspection.value(QStringLiteral("active")).toInt(), 0);
  QVERIFY(inspection.value(QStringLiteral("cancelled")).toBool());
  QVERIFY(fixture.session.isIdle());
}

void TestWebViewService::invalidationWaitsForDrain() {
  QObject owner;
  CompletionLog cancelled;
  CompletionLog recovered;
  PageFixture fixture;
  fixture.page.setHtml(html());
  const auto activeId =
      fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 8}}, &owner, cancelled.callback());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                         cancelled.callback());
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            activeId, 20000);
  const auto generation = fixture.session.generation();
  fixture.session.invalidate();
  QCOMPARE(cancelled.results.size(), 2);
  QCOMPARE(cancelled.results.first().status, WebViewJobResult::Status::Cancelled);
  QVERIFY(!fixture.session.isReady());
  QVERIFY(!fixture.session.isIdle());
  QVERIFY(fixture.session.generation() != generation);
  fixture.session.setReady(generation);
  fixture.session.setReady(fixture.session.generation());
  QVERIFY(!fixture.session.isReady());
  fixture.page.runJavaScript(QStringLiteral("window.releaseJob()"));
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.isIdle(), 10000);
  QVERIFY(!fixture.session.isReady());
  fixture.page.setHtml(html());
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.isReady(), 20000);
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 3}}, &owner,
                         recovered.callback());
  QTRY_COMPARE_WITH_TIMEOUT(recovered.results.size(), 1, 10000);
  QCOMPARE(recovered.results.first().value.toInt(), 6);
  QCOMPARE(cancelled.results.size(), 2);
}

void TestWebViewService::reloadRetiresIdsAndRecovers() {
  QObject owner;
  CompletionLog cancelled;
  CompletionLog recovered;
  PageFixture fixture;
  fixture.page.setHtml(html());
  const auto oldId =
      fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 4}}, &owner, cancelled.callback());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                         cancelled.callback());
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            oldId, 20000);
  const auto generation = fixture.session.generation();
  fixture.page.setHtml(html());
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.generation() != generation, 10000);
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.isReady(), 20000);
  QCOMPARE(cancelled.results.size(), 2);
  for (const auto &result : cancelled.results) {
    QCOMPARE(result.status, WebViewJobResult::Status::Cancelled);
  }
  const auto newId =
      fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 11}}, &owner, recovered.callback());
  QVERIFY(newId != oldId);
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            newId, 10000);
  fixture.session.complete(oldId, true, 999, {});
  QVERIFY(!fixture.session.isIdle());
  QCOMPARE(recovered.results.size(), 0);
  fixture.page.runJavaScript(QStringLiteral("window.releaseJob()"));
  QTRY_COMPARE_WITH_TIMEOUT(recovered.results.size(), 1, 10000);
  QCOMPARE(recovered.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(recovered.results.first().value.toInt(), 121);
  QCOMPARE(cancelled.results.size(), 2);
}

void TestWebViewService::failedLoadSettlesAndRecovers() {
  QObject owner;
  CompletionLog failed;
  CompletionLog recovered;
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  PageFixture fixture;
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         failed.callback());
  QSignalSpy loaded(&fixture.page, &QWebEnginePage::loadFinished);
  fixture.page.load(QUrl::fromLocalFile(directory.filePath(QStringLiteral("absent.html"))));
  QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 20000);
  QVERIFY(!loaded.first().first().toBool());
  QCOMPARE(failed.results.size(), 1);
  QCOMPARE(failed.results.first().status, WebViewJobResult::Status::Error);
  QVERIFY(!fixture.session.isReady());
  QVERIFY(fixture.session.isIdle());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         failed.callback());
  QCOMPARE(failed.results.size(), 2);
  QCOMPARE(failed.results.last().status, WebViewJobResult::Status::Error);
  fixture.page.setHtml(html());
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.isReady(), 20000);
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         recovered.callback());
  QTRY_COMPARE_WITH_TIMEOUT(recovered.results.size(), 1, 10000);
  QCOMPARE(recovered.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(recovered.results.first().value.toInt(), 24);
}

void TestWebViewService::providerFailureSettlesAndRecovers_data() {
  QTest::addColumn<bool>("beforeInitialLoad");
  QTest::newRow("bootstrap-unavailable") << true;
  QTest::newRow("loaded-template-without-provider") << false;
}

void TestWebViewService::providerFailureSettlesAndRecovers() {
  QFETCH(bool, beforeInitialLoad);
  QObject owner;
  CompletionLog failed;
  CompletionLog recovered;
  PageFixture fixture;
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         failed.callback());
  fixture.session.submit(QStringLiteral("compute.squares"), {{"values", QJsonArray{2, 3}}}, &owner,
                         failed.callback());
  if (!beforeInitialLoad) {
    QSignalSpy loaded(&fixture.page, &QWebEnginePage::loadFinished);
    fixture.page.setHtml(QStringLiteral("<!doctype html><title>Missing job provider</title>"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 20000);
    QVERIFY(loaded.first().first().toBool());
    QCOMPARE(evaluate(fixture.page, QStringLiteral("typeof window.vxWebJobs")).toString(),
             QStringLiteral("undefined"));
  }
  QVERIFY(!fixture.session.isReady());
  QCOMPARE(failed.results.size(), 0);
  const auto generation = fixture.session.generation();
  const auto error = QStringLiteral("Required job provider is unavailable");
  fixture.session.fail(error);
  QCOMPARE(failed.results.size(), 2);
  for (const auto &result : failed.results) {
    QCOMPARE(result.status, WebViewJobResult::Status::Error);
    QCOMPARE(result.error, error);
  }
  QVERIFY(fixture.session.isIdle());
  QVERIFY(fixture.session.generation() != generation);
  fixture.session.setReady(generation);
  fixture.session.setReady(fixture.session.generation());
  QVERIFY(!fixture.session.isReady());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         failed.callback());
  QCOMPARE(failed.results.size(), 3);
  QCOMPARE(failed.results.last().status, WebViewJobResult::Status::Error);

  fixture.page.setHtml(html());
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.isReady(), 20000);
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                         recovered.callback());
  QTRY_COMPARE_WITH_TIMEOUT(recovered.results.size(), 1, 10000);
  QCOMPARE(recovered.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(recovered.results.first().value.toInt(), 120);
  QCOMPARE(failed.results.size(), 3);
}

void TestWebViewService::rendererFailureSettlesAndRecovers() {
  QObject owner;
  CompletionLog failed;
  CompletionLog recovered;
  PageFixture fixture;
  fixture.page.setHtml(html());
  const auto id =
      fixture.session.submit(QStringLiteral("dom.hold"), {{"n", 3}}, &owner, failed.callback());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         failed.callback());
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(fixture.page, QStringLiteral("window.startedId")).toString(),
                            id, 20000);
  QSignalSpy terminated(&fixture.page, &QWebEnginePage::renderProcessTerminated);
  // Chromium's built-in crash URL actually terminates the renderer. Do not
  // replace this with manually emitting the native lifecycle signal.
  fixture.page.load(QUrl(QStringLiteral("chrome://crash")));
  QTRY_VERIFY_WITH_TIMEOUT(!terminated.isEmpty(), 20000);
  QTRY_COMPARE_WITH_TIMEOUT(failed.results.size(), 2, 10000);
  for (const auto &result : failed.results) {
    // Some Qt versions begin navigation before the crash (Cancelled); others
    // report process death directly (Error). Neither may publish stale success.
    QVERIFY(result.status != WebViewJobResult::Status::Success);
    QVERIFY(!result.error.isEmpty());
  }
  QVERIFY(!fixture.session.isReady());
  QVERIFY(fixture.session.isIdle());
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         failed.callback());
  QCOMPARE(failed.results.size(), 3);
  QCOMPARE(failed.results.last().status, WebViewJobResult::Status::Error);
  fixture.page.setHtml(html());
  QTRY_VERIFY_WITH_TIMEOUT(fixture.session.isReady(), 20000);
  fixture.session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                         recovered.callback());
  QTRY_COMPARE_WITH_TIMEOUT(recovered.results.size(), 1, 10000);
  QCOMPARE(recovered.results.first().value.toInt(), 24);
  QCOMPARE(failed.results.size(), 3);
}

void TestWebViewService::borrowedPageAndChannelSurvive() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QWebChannel channel;
  PageCounter counter;
  channel.registerObject(QStringLiteral("counter"), &counter);
  page.setWebChannel(&channel);
  QPointer<QWebEnginePage> pageGuard(&page);
  QPointer<QWebChannel> channelGuard(&channel);
  QObject owner;
  CompletionLog completed;
  auto session = std::make_unique<WebViewJobSession>(&page, &channel);
  const auto oldId = session->submit(QStringLiteral("compute.squares"),
                                     {{"values", QJsonArray{4, 6}}}, &owner, completed.callback());
  page.setHtml(html());
  QTRY_COMPARE_WITH_TIMEOUT(completed.results.size(), 1, 20000);
  QCOMPARE(completed.results.first().value.toInt(), 52);
  QCOMPARE(page.webChannel(), &channel);
  page.runJavaScript(QStringLiteral("window.pageChannel.objects.counter.add(7)"));
  QTRY_COMPARE_WITH_TIMEOUT(counter.total, 7, 10000);
  session.reset();
  QVERIFY(pageGuard);
  QVERIFY(channelGuard);
  QCOMPARE(page.webChannel(), &channel);
  QVERIFY(!channel.registeredObjects().contains(QStringLiteral("vxJobs")));
  QCOMPARE(channel.registeredObjects().value(QStringLiteral("counter")), &counter);
  page.runJavaScript(QStringLiteral("window.pageChannel.objects.counter.add(5)"));
  QTRY_COMPARE_WITH_TIMEOUT(counter.total, 12, 10000);
  QCOMPARE(evaluate(page, QStringLiteral("document.getElementById('work').tagName")).toString(),
           QStringLiteral("MAIN"));

  // A new session is registered before a fresh handshake on this same page.
  session = std::make_unique<WebViewJobSession>(&page, &channel);
  const auto newId = session->submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                                     completed.callback());
  QVERIFY(newId != oldId);
  page.setHtml(html());
  QTRY_COMPARE_WITH_TIMEOUT(completed.results.size(), 2, 20000);
  QCOMPARE(completed.results.last().value.toInt(), 120);
  page.runJavaScript(QStringLiteral("window.pageChannel.objects.counter.add(3)"));
  QTRY_COMPARE_WITH_TIMEOUT(counter.total, 15, 10000);
}

void TestWebViewService::endpointDestruction_data() {
  QTest::addColumn<bool>("destroyPage");
  QTest::newRow("page") << true;
  QTest::newRow("channel") << false;
}

void TestWebViewService::endpointDestruction() {
  QFETCH(bool, destroyPage);
  QObject owner;
  CompletionLog failed;
  QWebEngineProfile profile;
  auto page = std::make_unique<QWebEnginePage>(&profile);
  auto channel = std::make_unique<QWebChannel>();
  page->setWebChannel(channel.get());
  WebViewJobSession session(page.get(), channel.get());
  page->setHtml(html());
  const auto id = session.submit(QStringLiteral("dom.hold"), {{"n", 3}}, &owner, failed.callback());
  session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner, failed.callback());
  QTRY_COMPARE_WITH_TIMEOUT(evaluate(*page, QStringLiteral("window.startedId")).toString(), id,
                            20000);
  if (destroyPage) {
    page.reset();
    QVERIFY(channel);
  } else {
    page->setWebChannel(nullptr);
    channel.reset();
    QCOMPARE(evaluate(*page, QStringLiteral("6 * 7")).toInt(), 42);
  }
  QCOMPARE(failed.results.size(), 2);
  for (const auto &result : failed.results) {
    QCOMPARE(result.status, WebViewJobResult::Status::Error);
  }
  QVERIFY(!session.isReady());
  QVERIFY(session.isIdle());
  session.submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner, failed.callback());
  QCOMPARE(failed.results.size(), 3);
  QCOMPARE(failed.results.last().status, WebViewJobResult::Status::Error);
}

void TestWebViewService::serviceResetRetiresOnlyItsOwnHost() {
  ServiceLocator services;
  QWebEngineProfile profile;
  QPointer<QWebEngineProfile> profileGuard(&profile);
  QObject owner;
  CompletionLog cancelled;
  CompletionLog recovered;
  int bootstraps = 0;
  QPointer<QWebEnginePage> page;
  QPointer<QWebChannel> channel;
  {
    WebViewService service(services, &profile, [&](QWebEnginePage *p_page, QWebChannel *p_channel) {
      ++bootstraps;
      page = p_page;
      channel = p_channel;
      p_page->setHtml(html());
    });
    QPointer<WebViewJobSession> oldSession(service.sharedSession());
    const auto oldId =
        oldSession->submit(QStringLiteral("dom.hold"), {{"n", 7}}, &owner, cancelled.callback());
    oldSession->submit(QStringLiteral("compute.factorial"), {{"n", 4}}, &owner,
                       cancelled.callback());
    QTRY_COMPARE_WITH_TIMEOUT(evaluate(*page, QStringLiteral("window.startedId")).toString(), oldId,
                              20000);
    service.invalidate();
    QVERIFY(!oldSession);
    QVERIFY(!page);
    QVERIFY(!channel);
    QVERIFY(profileGuard);
    QCOMPARE(bootstraps, 1);
    QCOMPARE(cancelled.results.size(), 2);
    for (const auto &result : cancelled.results) {
      QCOMPARE(result.status, WebViewJobResult::Status::Cancelled);
    }
    auto session = service.sharedSession();
    QCOMPARE(bootstraps, 2);
    const auto newId = session->submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                                       recovered.callback());
    QVERIFY(newId != oldId);
    QTRY_COMPARE_WITH_TIMEOUT(recovered.results.size(), 1, 20000);
    QCOMPARE(recovered.results.first().value.toInt(), 120);
  }
  QVERIFY(!page);
  QVERIFY(!channel);
  QVERIFY(profileGuard);
  QCOMPARE(cancelled.results.size(), 2);
}

void TestWebViewService::completionCanDestroySession() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QWebChannel channel;
  page.setWebChannel(&channel);
  QObject owner;
  CompletionLog completed;
  CompletionLog cancelled;
  auto session = std::make_unique<WebViewJobSession>(&page, &channel);
  session->submit(QStringLiteral("compute.factorial"), {{"n", 5}}, &owner,
                  [&](const WebViewJobResult &p_result) {
                    completed.results.append(p_result);
                    session.reset();
                  });
  session->submit(QStringLiteral("compute.factorial"), {{"n", 6}}, &owner, cancelled.callback());
  page.setHtml(html());
  QTRY_COMPARE_WITH_TIMEOUT(completed.results.size(), 1, 20000);
  QVERIFY(!session);
  QCOMPARE(completed.results.first().status, WebViewJobResult::Status::Success);
  QCOMPARE(completed.results.first().value.toInt(), 120);
  QCOMPARE(cancelled.results.size(), 1);
  QCOMPARE(cancelled.results.first().status, WebViewJobResult::Status::Cancelled);
  QCOMPARE(page.webChannel(), &channel);
}

} // namespace tests

int main(int argc, char *argv[]) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication application(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  tests::TestWebViewService test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_webviewservice.moc"
