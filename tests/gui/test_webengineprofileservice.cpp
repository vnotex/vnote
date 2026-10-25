// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSemaphore>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QWebChannel>
#include <QWebEnginePage>

#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTcpServer>
#include <QWebEngineProfile>
#include <QtConcurrent/QtConcurrentRun>

#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <vxcore/vxcore.h>

#include <memory>
#include <utility>

#include <gui/services/webengineprofileservice.h>
#include <gui/services/webviewjobs.h>
#include <gui/utils/imageutils.h>
#include <utils/markdownhtmlrenderer.h>
#include <vtextedit/typstfragmentrenderer.h>
#include <vtextedit/typstruntime.h>
#include <widgets/editors/markdownvieweradapter.h>

using namespace vnotex;

namespace tests {

namespace {

QVariant evaluateJavaScript(QWebEnginePage &p_page, const QString &p_script) {
  const auto state = std::make_shared<std::pair<bool, QVariant>>(false, QVariant());
  p_page.runJavaScript(p_script, [state](const QVariant &p_value) {
    state->second = p_value;
    state->first = true;
  });
  if (!QTest::qWaitFor([state]() { return state->first; }, 10000)) {
    return QVariant();
  }
  return state->second;
}

// A real encrypted profile/channel. The capability is never replaced with a test predicate.
struct ProtectedMathPage {
  WebEngineProfileService &service;
  WebEngineProfileService::ProtectedPage grant;
  std::unique_ptr<QWebEngineProfile> profile;
  std::unique_ptr<QWebEnginePage> page;
  MarkdownViewerAdapter *adapter = nullptr;
  WebViewJobSession *jobs = nullptr;
  quint64 generation = 0;

  ProtectedMathPage(WebEngineProfileService &p_service, const Buffer2 &p_note,
                    ServiceLocator &p_services)
      : service(p_service), grant(service.createProtectedProfile(p_note)), profile(grant.profile) {
    if (!profile)
      return;
    page = std::make_unique<QWebEnginePage>(profile.get());
    auto *channel = new QWebChannel(page.get());
    adapter = new MarkdownViewerAdapter(p_services, page.get());
    adapter->setProtectedView(true);
    adapter->setProtectedTypstAuthorization(grant.isAuthorized);
    QObject::connect(&service, &WebEngineProfileService::protectedProfileRevoked, adapter,
                     [target = adapter, identity = profile.get()](QWebEngineProfile *p_profile) {
                       if (p_profile == identity)
                         target->revokeProtectedTypst();
                     });
    QObject::connect(profile.get(), &QObject::destroyed, adapter,
                     &MarkdownViewerAdapter::revokeProtectedTypst);
    QObject::connect(&service, &QObject::destroyed, adapter,
                     &MarkdownViewerAdapter::revokeProtectedTypst);
    channel->registerObject(QStringLiteral("vxAdapter"), adapter);
    page->setWebChannel(channel);
    jobs = new WebViewJobSession(page.get(), channel, page.get());
  }

  ~ProtectedMathPage() {
    if (profile)
      service.revokeProtectedProfile(profile.get());
    // Pages/adapter/jobs are destroyed before the borrowed profile.
    page.reset();
  }

  bool load(HtmlTemplateService &p_templates, bool p_dark,
            const QString &p_renderer = QStringLiteral("typst")) {
    if (!profile)
      return false;
    QHash<QString, QByteArray> resources;
    const auto html = p_templates.protectedMarkdownViewerTemplate(
        grant.token, grant.nonce, p_dark ? QStringLiteral("vscode-dark") : QStringLiteral("pure"),
        p_dark, p_renderer, resources);
    if (html.isEmpty() || !service.setProtectedDocument(profile.get(), html, resources))
      return false;
    page->load(grant.url);
    return QTest::qWaitFor([&] { return jobs->isReady() && adapter->isReady(); }, 30000);
  }

  bool render(const QByteArray &p_source) {
    const auto result = MarkdownHtmlRenderer::render(p_source, {});
    if (!result.error.isEmpty())
      return false;
    adapter->setRenderedHtml(QString::number(++generation), result.html);
    return QTest::qWaitFor([&] { return adapter->isDocumentReady(); }, 30000);
  }
};

// Existing runtime-lane gate pattern: no sleeps or pathological Typst programs.
class ProtectedMathGate {
public:
  explicit ProtectedMathGate(vte::TypstRuntime &p_runtime) : state(std::make_shared<State>()) {
    for (const auto *name : {"typst-compile-0", "typst-compile-1"}) {
      auto *thread = p_runtime.findChild<QThread *>(QLatin1String(name));
      auto *worker = thread ? thread->property("worker").value<QObject *>() : nullptr;
      if (worker && QMetaObject::invokeMethod(
                        worker,
                        [held = state] {
                          held->entered.release();
                          held->resume.acquire();
                        },
                        Qt::QueuedConnection))
        ++scheduled;
    }
  }
  ~ProtectedMathGate() { release(); }
  bool entered() { return scheduled == 2 && state->entered.tryAcquire(2, 10000); }
  void release() {
    if (scheduled) {
      state->resume.release(scheduled);
      scheduled = 0;
    }
  }

private:
  struct State {
    QSemaphore entered;
    QSemaphore resume;
  };
  std::shared_ptr<State> state;
  int scheduled = 0;
};

class RevokeMathOnDelivery : public QObject {
public:
  QObject *renderer = nullptr;
  std::function<void()> revoke;

protected:
  bool eventFilter(QObject *p_object, QEvent *p_event) override {
    if (revoke && p_event->type() == QEvent::MetaCall && p_object->parent() == renderer &&
        QByteArray(p_object->metaObject()->className()) == "vte::TypstRuntimeSession") {
      auto callback = std::exchange(revoke, {});
      callback();
    }
    return false;
  }
};

QImage protectedPreviewImage(const WebViewJobResult &p_result) {
  if (p_result.status != WebViewJobResult::Status::Success)
    return {};
  const auto object = p_result.value.toObject();
  auto bytes = object.value(QStringLiteral("data")).toString().toUtf8();
  if (object.value(QStringLiteral("base64")).toBool())
    bytes = QByteArray::fromBase64(bytes);
  QByteArray png, mime;
  if (!ImageUtils::protectedImageData(bytes, png, mime) || mime != "image/png")
    return {};
  return QImage::fromData(png, "PNG");
}

int pixelsMatching(const QImage &p_image, const QColor &p_expected) {
  int count = 0;
  for (int y = 0; y < p_image.height(); ++y) {
    for (int x = 0; x < p_image.width(); ++x) {
      const auto color = p_image.pixelColor(x, y);
      if (qAbs(color.red() - p_expected.red()) <= 4 &&
          qAbs(color.green() - p_expected.green()) <= 4 &&
          qAbs(color.blue() - p_expected.blue()) <= 4 &&
          qAbs(color.alpha() - p_expected.alpha()) <= 3)
        ++count;
    }
  }
  return count;
}
} // namespace

class TestWebEngineProfileService : public QObject {
  Q_OBJECT

private slots:
  void profileStorageStaysUnderConfiguredRoot();
  void protectedMathReadingAndLivePreview_data();
  void protectedMathReadingAndLivePreview();
  void protectedTypstMath_data();
  void protectedTypstMath();
};

void TestWebEngineProfileService::profileStorageStaysUnderConfiguredRoot() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const auto root = QDir(temp.path()).filePath(QStringLiteral("VNote"));
  const auto defaultStorage =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath(QStringLiteral("QtWebEngine/vnote"));
  QVERIFY(!QFileInfo::exists(defaultStorage));

  // Reopen the same profile to prove that avoiding Qt's default path does not
  // silently turn persistent browser storage into an off-the-record profile.
  for (int pass = 0; pass < 2; ++pass) {
    WebEngineProfileService service(root, nullptr, nullptr);
    QVariant result;
    bool finished = false;
    QWebEnginePage page(service.profile());
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(QStringLiteral("<!doctype html><title>Storage probe</title>"),
                 QUrl(QStringLiteral("https://storage.vnote.test/")));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
    QVERIFY(loaded.at(0).at(0).toBool());
    const auto script =
        pass == 0 ? QStringLiteral("localStorage.setItem('vnote-storage-probe', "
                                   "'persisted'); localStorage.getItem('vnote-storage-probe')")
                  : QStringLiteral("localStorage.getItem('vnote-storage-probe')");
    page.runJavaScript(script, [&result, &finished](const QVariant &p_result) {
      result = p_result;
      finished = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QCOMPARE(result.toString(), QStringLiteral("persisted"));
    QVERIFY2(!QFileInfo::exists(defaultStorage),
             "profile initialization created storage outside the configured VNote root");
  }
  QVERIFY(!QFileInfo::exists(defaultStorage));
}

void TestWebEngineProfileService::protectedMathReadingAndLivePreview_data() {
  QTest::addColumn<QString>("themeName");
  QTest::addColumn<bool>("darkTheme");
  QTest::addColumn<QString>("background");
  QTest::addColumn<QString>("foreground");
  QTest::addColumn<QString>("linkColor");
  QTest::addColumn<QString>("keywordColor");
  QTest::newRow("bundled-dark") << QStringLiteral("vscode-dark") << true
                                << QStringLiteral("rgb(30, 30, 30)")
                                << QStringLiteral("rgb(212, 212, 212)")
                                << QStringLiteral("rgb(119, 201, 255)")
                                << QStringLiteral("rgb(86, 156, 214)");
  QTest::newRow("bundled-light") << QStringLiteral("pure") << false
                                 << QStringLiteral("rgb(255, 255, 255)")
                                 << QStringLiteral("rgb(34, 34, 34)")
                                 << QStringLiteral("rgb(0, 153, 255)")
                                 << QStringLiteral("rgb(0, 119, 170)");
  QTest::newRow("custom-dark") << QStringLiteral("custom-dark") << true
                               << QStringLiteral("rgb(30, 30, 30)")
                               << QStringLiteral("rgb(212, 212, 212)")
                               << QStringLiteral("rgb(119, 201, 255)")
                               << QStringLiteral("rgb(86, 156, 214)");
  QTest::newRow("native-dark") << QStringLiteral("native") << true
                               << QStringLiteral("rgb(30, 30, 30)")
                               << QStringLiteral("rgb(212, 212, 212)")
                               << QStringLiteral("rgb(119, 201, 255)")
                               << QStringLiteral("rgb(86, 156, 214)");
  QTest::newRow("path-is-not-a-theme")
      << QStringLiteral("../themes/pure") << true << QStringLiteral("rgb(30, 30, 30)")
      << QStringLiteral("rgb(212, 212, 212)") << QStringLiteral("rgb(119, 201, 255)")
      << QStringLiteral("rgb(86, 156, 214)");
}

void TestWebEngineProfileService::protectedMathReadingAndLivePreview() {
  QFETCH(QString, themeName);
  QFETCH(bool, darkTheme);
  QFETCH(QString, background);
  QFETCH(QString, foreground);
  QFETCH(QString, linkColor);
  QFETCH(QString, keywordColor);
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY2(QFileInfo::exists(rcc), qPrintable(rcc));
  const auto appSearchPaths = QDir::searchPaths(QStringLiteral("app"));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  const auto restoreSearchPaths =
      qScopeGuard([&]() { QDir::setSearchPaths(QStringLiteral("app"), appSearchPaths); });

  vxcore_set_test_mode(1);
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &context), VXCORE_OK);
  const auto destroyContext = qScopeGuard([&]() { vxcore_context_destroy(context); });
  SyncWorkQueueManager queues;
  NotebookIoGate gate;
  HookManager hooks;
  NotebookCoreService notebooks(context);
  notebooks.setHookManager(&hooks);
  notebooks.setNotebookIoGate(&gate);
  const auto notebook = notebooks.createNotebook(temp.filePath(QStringLiteral("notebook")),
                                                 QStringLiteral("{\"name\":\"Protected math\"}"),
                                                 NotebookType::Bundled);
  QVERIFY(!notebook.isEmpty());
  BufferService buffers(context, &hooks, &gate, AutoSavePolicy::None);
  // Notebook-close hooks still need the live BufferService.
  const auto closeNotebook = qScopeGuard([&]() { notebooks.closeNotebook(notebook); });
  const QByteArray password("protected-math-test-password");
  const QByteArray firstText("Inline $x^2+y^2=z^2$.\n\n$$\n\\frac{1}{2}\n$$\n\n"
                             "[Link](https://protected.invalid/)\n\n"
                             "```javascript\nconst answer = 42;\n```\n");
  QString noteId;
  QCOMPARE(QtConcurrent::run([&]() -> VxCoreError {
             auto setup = notebooks.prepareNotebookEncryption(notebook, QString(), password);
             if (!setup.isValid()) {
               return setup.m_error;
             }
             auto maintenance = queues.tryAcquireMaintenance({notebook});
             if (!maintenance.isValid()) {
               return VXCORE_ERR_INVALID_STATE;
             }
             NotebookIoGate::ScopedLock lock(gate, notebook);
             const auto error = notebooks.commitNotebookEncryption(setup);
             return error == VXCORE_OK ? notebooks.createEncryptedNote(
                                             notebook, QString(), QStringLiteral("private.md"),
                                             QStringLiteral("markdown"), firstText, &noteId)
                                       : error;
           }).result(),
           VXCORE_OK);
  QCOMPARE(notebooks.lockAllEncryption(), VXCORE_OK);
  QCOMPARE(QtConcurrent::run([&]() {
             return notebooks.unlockNoteEncryption(notebook, QStringLiteral("private.md.vne"),
                                                   password);
           }).result(),
           VXCORE_OK);
  auto note = buffers.openBufferByNodeId(noteId);
  QVERIFY(note.isValid());
  QVERIFY(note.isEncrypted());
  const auto closeNote = qScopeGuard([&]() {
    buffers.closeBuffer(note.id());
    notebooks.lockAllEncryption();
  });
  QCOMPARE(note.getContentRaw(), firstText);

  HtmlTemplateService templates(nullptr);
  WebEngineProfileService service(temp.filePath(QStringLiteral("profile")), nullptr, nullptr);
  const auto protectedPage = service.createProtectedProfile(note);
  QVERIFY(protectedPage.profile);
  std::unique_ptr<QWebEngineProfile> profile(protectedPage.profile);
  QWebEnginePage page(profile.get());
  QWebChannel channel;
  MarkdownViewerAdapter adapter;
  adapter.setProtectedView(true);
  channel.registerObject(QStringLiteral("vxAdapter"), &adapter);
  page.setWebChannel(&channel);
  WebViewJobSession jobs(&page, &channel);
  QSignalSpy rendered(&adapter, &MarkdownViewerAdapter::workFinished);
  // Revocation must precede page destruction, then profile destruction, buffer
  // close and finally context destruction, including on an assertion failure.
  const auto revoke = qScopeGuard([&]() { service.revokeProtectedProfile(profile.get()); });
  QHash<QString, QByteArray> resources;
  auto html =
      templates.protectedMarkdownViewerTemplate(protectedPage.token, protectedPage.nonce, themeName,
                                                darkTheme, QStringLiteral("mathjax"), resources);
  QVERIFY(!html.isEmpty());
  // Deliberately conflict with the protected renderer. The actual document
  // adapter and generic jobs share the page's single native/JS channel.
  const auto scriptsStart =
      html.indexOf(QStringLiteral("<script nonce=\"%1\" src=").arg(protectedPage.nonce));
  QVERIFY(scriptsStart >= 0);
  const auto bridge = QStringLiteral(R"JS(<script nonce="%1">
    window.vxOptions.mathRenderer = 'mathjax';
  </script>
)JS");
  html.insert(scriptsStart, bridge.arg(protectedPage.nonce));
  QVERIFY(service.setProtectedDocument(profile.get(), html, resources));
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.load(protectedPage.url);
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 30000);
  QVERIFY(loaded.at(0).at(0).toBool());
  QTRY_VERIFY_WITH_TIMEOUT(jobs.isReady() && adapter.isReady(), 10000);

  quint64 renderGeneration = 0;
  MarkdownHtmlOptions renderOptions;
  renderOptions.autoBreaks = false;
  const auto render = [&](MarkdownViewerAdapter &p_adapter, const QString &p_text) {
    const auto result = MarkdownHtmlRenderer::render(p_text.toUtf8(), renderOptions);
    if (result.error.isEmpty()) {
      p_adapter.setRenderedHtml(QStringLiteral("protected-math-%1").arg(++renderGeneration),
                                result.html);
    }
    return result.error;
  };
  auto renderError = render(adapter, note.decode(note.peekContentRaw()));
  QVERIFY2(renderError.isEmpty(), qPrintable(renderError));
  QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 15000);
  const auto initial = evaluateJavaScript(page, QStringLiteral(R"JS((() => {
    const root = document.getElementById('vx-content');
    const maths = Array.from(root.querySelectorAll('.katex'));
    const faces = Array.from(document.fonts);
    return {
      inline: maths.filter(node => !node.closest('.katex-display')).length,
      display: root.querySelectorAll('.katex-display .katex').length,
      formulas: Array.from(root.querySelectorAll('annotation[encoding="application/x-tex"]'))
        .map(node => node.textContent.trim()).join('\n'),
      styled: maths.length > 0 && getComputedStyle(maths[0]).fontFamily.includes('KaTeX_Main'),
      fontsLoaded: ['KaTeX_Main', 'KaTeX_Math'].every(name => faces.some(face =>
        face.family.replace(/["']/g, '') === name && face.status === 'loaded')),
      usedMathJax: typeof window.MathJax !== 'undefined'
    };
  })())JS"))
                           .toMap();
  QCOMPARE(initial.value(QStringLiteral("inline")).toInt(), 1);
  QCOMPARE(initial.value(QStringLiteral("display")).toInt(), 1);
  QCOMPARE(initial.value(QStringLiteral("formulas")).toString(),
           QStringLiteral("x^2+y^2=z^2\n\\frac{1}{2}"));
  QVERIFY(initial.value(QStringLiteral("styled")).toBool());
  QVERIFY(initial.value(QStringLiteral("fontsLoaded")).toBool());
  QVERIFY(!initial.value(QStringLiteral("usedMathJax")).toBool());
  QTRY_VERIFY_WITH_TIMEOUT(
      evaluateJavaScript(
          page, QStringLiteral("!!document.querySelector('#vx-content pre .token.keyword')"))
          .toBool(),
      10000);
  const auto colors = evaluateJavaScript(page, QStringLiteral(R"JS((() => {
    const root = document.getElementById('vx-content');
    return {
      background: getComputedStyle(document.body).backgroundColor,
      foreground: getComputedStyle(root.querySelector('p')).color,
      link: getComputedStyle(root.querySelector('a')).color,
      keyword: getComputedStyle(root.querySelector('pre .token.keyword')).color
    };
  })())JS"))
                          .toMap();
  QCOMPARE(colors.value(QStringLiteral("background")).toString(), background);
  QCOMPARE(colors.value(QStringLiteral("foreground")).toString(), foreground);
  QCOMPARE(colors.value(QStringLiteral("link")).toString(), linkColor);
  QCOMPARE(colors.value(QStringLiteral("keyword")).toString(), keywordColor);

  // A second editor snapshot replaces the reading DOM, rather than appending
  // another math pass or leaving already-typeset content behind.
  const QByteArray secondText("Updated $e^{i\\pi}+1=0$.\n");
  QVERIFY(note.setContentRaw(secondText));
  renderError = render(adapter, note.decode(note.peekContentRaw()));
  QVERIFY2(renderError.isEmpty(), qPrintable(renderError));
  QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 2, 10000);
  QVERIFY(evaluateJavaScript(page, QStringLiteral(R"JS((() => {
    const root = document.getElementById('vx-content');
    const formulas = root.querySelectorAll('annotation[encoding="application/x-tex"]');
    return root.querySelectorAll('.katex').length === 1 &&
      root.querySelectorAll('.katex-display').length === 0 && formulas.length === 1 &&
      formulas[0].textContent === 'e^{i\\pi}+1=0' && !root.textContent.includes('Inline');
  })())JS"))
              .toBool());

  // These commands would create active DOM with trust:true, after Markdown's
  // sanitizer has finished. Exercise the shipped KaTeX trust boundary itself.
  renderError = render(adapter, QStringLiteral(R"MD($\href{https://protected.invalid/link}{unsafe}$

$\includegraphics{https://protected.invalid/image.png}$

$\htmlClass{protected-injected}{unsafe}$
)MD"));
  QVERIFY2(renderError.isEmpty(), qPrintable(renderError));
  QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 3, 10000);
  QVERIFY(evaluateJavaScript(page, QStringLiteral(R"JS((() => {
    const root = document.getElementById('vx-content');
    return root.querySelectorAll('.katex').length === 3 &&
      !root.querySelector('a[href], img, iframe, script, .protected-injected');
  })())JS"))
              .toBool());

  // Same external origin, with a live TCP listener: failure cannot be explained
  // by DNS or an unreachable server. The script has the nonce, so its rejection
  // specifically exercises the profile interceptor, not only script-src.
  QTcpServer external;
  QVERIFY(external.listen(QHostAddress::LocalHost));
  QSignalSpy externalConnections(&external, &QTcpServer::newConnection);
  const auto origin = QStringLiteral("http://127.0.0.1:%1/").arg(external.serverPort());
  const auto originJson =
      QString::fromUtf8(QJsonDocument(QJsonArray{origin}).toJson(QJsonDocument::Compact));
  page.runJavaScript(QStringLiteral(R"JS((() => {
    const origin = %1[0];
    window.__requests = { image: 'pending', script: 'pending', fetch: 'pending', violations: [] };
    document.addEventListener('securitypolicyviolation', event => {
      if (event.blockedURI.startsWith(origin.slice(0, -1))) {
        window.__requests.violations.push(event.effectiveDirective);
      }
    });
    const image = document.createElement('img');
    image.onload = () => { window.__requests.image = 'loaded'; };
    image.onerror = () => { window.__requests.image = 'blocked'; };
    image.src = origin + 'image.png';
    document.body.appendChild(image);
    const script = document.createElement('script');
    script.nonce = document.querySelector('script[nonce]').nonce;
    script.onload = () => { window.__requests.script = 'loaded'; };
    script.onerror = () => { window.__requests.script = 'blocked'; };
    script.src = origin + 'script.js';
    document.head.appendChild(script);
    fetch(origin + 'fetch').then(() => { window.__requests.fetch = 'loaded'; },
      () => { window.__requests.fetch = 'blocked'; });
  })())JS")
                         .arg(originJson));
  QTRY_VERIFY_WITH_TIMEOUT(evaluateJavaScript(page, QStringLiteral(R"JS(
    window.__requests && ['image', 'script', 'fetch'].every(
      kind => window.__requests[kind] === 'blocked') &&
      window.__requests.violations.includes('img-src') &&
      window.__requests.violations.includes('connect-src')
  )JS"))
                               .toBool(),
                           10000);
  QCOMPARE(externalConnections.count(), 0);

  // Reading/live preview permission must not enable either in-place entrypoint.
  QVERIFY(evaluateJavaScript(page, QStringLiteral(R"JS((() => {
    const root = document.getElementById('vx-inplace-preview');
    let blocked = false;
    window.vxcore.getWorker('math').renderText(root, '$x^2$', node => { blocked = node === null; });
    return blocked && !root.querySelector('.katex, svg');
  })())JS"))
              .toBool());
  const auto previews = std::make_shared<QVector<WebViewJobResult>>();
  const auto completed = [previews](const WebViewJobResult &result) { previews->append(result); };
  jobs.submit(QStringLiteral("markdown.previewMath"),
              {{QStringLiteral("source"), QStringLiteral("$x^2$")}}, this, completed);
  jobs.submit(QStringLiteral("markdown.previewMath"),
              {{QStringLiteral("source"), QStringLiteral("$\\frac{a}{b}$")}}, this, completed);
  jobs.submit(QStringLiteral("markdown.previewGraph"),
              {{QStringLiteral("source"), QStringLiteral("@startuml\nA -> B\n@enduml")},
               {QStringLiteral("language"), QStringLiteral("plantuml")}},
              this, completed);
  jobs.submit(QStringLiteral("markdown.previewGraph"),
              {{QStringLiteral("source"), QStringLiteral("$x^2$")},
               {QStringLiteral("language"), QStringLiteral("mathjax")}},
              this, completed);
  QTRY_COMPARE_WITH_TIMEOUT(previews->size(), 4, 10000);
  QCOMPARE(previews->at(0).status, WebViewJobResult::Status::Success);
  QCOMPARE(previews->at(1).status, WebViewJobResult::Status::Success);
  const auto firstPreview = previews->at(0).value.toObject().toVariantMap();
  const auto secondPreview = previews->at(1).value.toObject().toVariantMap();
  QCOMPARE(firstPreview.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
  QVERIFY(firstPreview.value(QStringLiteral("base64")).toBool());
  const auto blockedImage =
      QByteArray::fromBase64(firstPreview.value(QStringLiteral("data")).toByteArray());
  QVERIFY(!QImage::fromData(blockedImage, "PNG").isNull());
  QCOMPARE(firstPreview.value(QStringLiteral("data")), secondPreview.value(QStringLiteral("data")));
  for (int i = 2; i < previews->size(); ++i) {
    QCOMPARE(previews->at(i).status, WebViewJobResult::Status::Success);
    const auto result = previews->at(i).value.toObject();
    QCOMPARE(result.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
    QVERIFY(!QImage::fromData(
                 QByteArray::fromBase64(result.value(QStringLiteral("data")).toString().toLatin1()),
                 "PNG")
                 .isNull());
  }
  QCOMPARE(
      evaluateJavaScript(
          page, QStringLiteral("document.getElementById('vx-inplace-preview').childElementCount"))
          .toInt(),
      0);
  QCOMPARE(externalConnections.count(), 0);

  // Theme changes need a fresh generation, never a republish under pending jobs.
  QVERIFY(!service.setProtectedDocument(profile.get(), html, resources));
  service.revokeProtectedProfile(profile.get());
  QSignalSpy retiredLoad(&page, &QWebEnginePage::loadFinished);
  page.load(protectedPage.url);
  QTRY_COMPARE_WITH_TIMEOUT(retiredLoad.count(), 1, 10000);
  QVERIFY(!retiredLoad.at(0).at(0).toBool());

  const auto replacement = service.createProtectedProfile(note);
  QVERIFY(replacement.profile);
  std::unique_ptr<QWebEngineProfile> replacementProfile(replacement.profile);
  QWebEnginePage replacementPage(replacementProfile.get());
  QWebChannel replacementChannel;
  MarkdownViewerAdapter replacementAdapter;
  replacementAdapter.setProtectedView(true);
  replacementChannel.registerObject(QStringLiteral("vxAdapter"), &replacementAdapter);
  replacementPage.setWebChannel(&replacementChannel);
  WebViewJobSession replacementJobs(&replacementPage, &replacementChannel);
  QSignalSpy replacementRendered(&replacementAdapter, &MarkdownViewerAdapter::workFinished);
  const auto revokeReplacement =
      qScopeGuard([&]() { service.revokeProtectedProfile(replacementProfile.get()); });
  auto replacementHtml = templates.protectedMarkdownViewerTemplate(
      replacement.token, replacement.nonce,
      darkTheme ? QStringLiteral("pure") : QStringLiteral("vscode-dark"), !darkTheme,
      QStringLiteral("mathjax"), resources);
  const auto replacementScripts =
      replacementHtml.indexOf(QStringLiteral("<script nonce=\"%1\" src=").arg(replacement.nonce));
  QVERIFY(replacementScripts >= 0);
  replacementHtml.insert(replacementScripts, bridge.arg(replacement.nonce));
  QVERIFY(service.setProtectedDocument(replacement.profile, replacementHtml, resources));
  QSignalSpy replacementLoaded(&replacementPage, &QWebEnginePage::loadFinished);
  replacementPage.load(replacement.url);
  QTRY_COMPARE_WITH_TIMEOUT(replacementLoaded.count(), 1, 30000);
  QVERIFY(replacementLoaded.at(0).at(0).toBool());
  QTRY_VERIFY_WITH_TIMEOUT(replacementJobs.isReady() && replacementAdapter.isReady(), 10000);
  renderError = render(replacementAdapter, QStringLiteral("Updated $x^2$"));
  QVERIFY2(renderError.isEmpty(), qPrintable(renderError));
  QTRY_COMPARE_WITH_TIMEOUT(replacementRendered.count(), 1, 15000);
  QCOMPARE(evaluateJavaScript(replacementPage,
                              QStringLiteral("getComputedStyle(document.body).backgroundColor"))
               .toString(),
           darkTheme ? QStringLiteral("rgb(255, 255, 255)") : QStringLiteral("rgb(30, 30, 30)"));
  QVERIFY(
      evaluateJavaScript(
          replacementPage,
          QStringLiteral("document.getElementById('vx-content').textContent.includes('Updated') && "
                         "!!document.querySelector('#vx-content .katex')"))
          .toBool());
}

void TestWebEngineProfileService::protectedTypstMath_data() {
  QTest::addColumn<bool>("dark");
  QTest::newRow("light") << false;
  QTest::newRow("dark") << true;
}

void TestWebEngineProfileService::protectedTypstMath() {
  QFETCH(bool, dark);
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QFileInfo bundle(QStringLiteral(VNOTE_TEST_EXTRA_RCC));
  QVERIFY(bundle.isFile());
  const auto oldPaths = QDir::searchPaths(QStringLiteral("app"));
  const auto restorePaths =
      qScopeGuard([&] { QDir::setSearchPaths(QStringLiteral("app"), oldPaths); });
  QDir::setSearchPaths(QStringLiteral("app"), {bundle.absolutePath()});
  vxcore_set_test_mode(1);
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &context), VXCORE_OK);
  const auto destroyContext = qScopeGuard([&] { vxcore_context_destroy(context); });
  SyncWorkQueueManager queues;
  NotebookIoGate gate;
  HookManager hooks;
  NotebookCoreService notebooks(context);
  notebooks.setHookManager(&hooks);
  notebooks.setNotebookIoGate(&gate);
  const auto notebook = notebooks.createNotebook(temp.filePath(QStringLiteral("notebook")),
                                                 QStringLiteral("{\"name\":\"Protected Typst\"}"),
                                                 NotebookType::Bundled);
  QVERIFY(!notebook.isEmpty());
  BufferService buffers(context, &hooks, &gate, AutoSavePolicy::None);
  const auto closeNotebook = qScopeGuard([&] { notebooks.closeNotebook(notebook); });
  const QByteArray password("protected-typst-test-password");
  const QByteArray marker("ProtectedTypstPlaintextSentinel");
  const QByteArray source =
      "# " + marker +
      "\n\nInline $frac(a^2 + b^2, sqrt(c))$ after.\n\n"
      "$$sum_(i=1)^n i$$\n\n```mathjax\n$$sqrt(x)$$\n```\n\nInvalid $frac($\n";
  QString noteId, siblingId;
  QCOMPARE(QtConcurrent::run([&]() -> VxCoreError {
             auto setup = notebooks.prepareNotebookEncryption(notebook, QString(), password);
             if (!setup.isValid())
               return setup.m_error;
             auto maintenance = queues.tryAcquireMaintenance({notebook});
             if (!maintenance.isValid())
               return VXCORE_ERR_INVALID_STATE;
             NotebookIoGate::ScopedLock lock(gate, notebook);
             auto error = notebooks.commitNotebookEncryption(setup);
             if (error != VXCORE_OK)
               return error;
             error =
                 notebooks.createEncryptedNote(notebook, QString(), QStringLiteral("private.md"),
                                               QStringLiteral("markdown"), source, &noteId);
             return error == VXCORE_OK
                        ? notebooks.createEncryptedNote(notebook, QString(),
                                                        QStringLiteral("sibling.md"),
                                                        QStringLiteral("markdown"),
                                                        QByteArray("Sibling $sqrt(z)$"), &siblingId)
                        : error;
           }).result(),
           VXCORE_OK);
  QCOMPARE(notebooks.lockAllEncryption(), VXCORE_OK);
  for (const auto *name : {"private.md.vne", "sibling.md.vne"}) {
    QCOMPARE(QtConcurrent::run([&] {
               return notebooks.unlockNoteEncryption(notebook, QLatin1String(name), password);
             }).result(),
             VXCORE_OK);
  }
  auto note = buffers.openBufferByNodeId(noteId);
  auto sibling = buffers.openBufferByNodeId(siblingId);
  QVERIFY(note.isEncrypted() && sibling.isEncrypted());
  const auto closeNotes = qScopeGuard([&] {
    buffers.cancelProtectedLocking();
    buffers.closeBuffer(note.id());
    buffers.closeBuffer(sibling.id());
    notebooks.lockAllEncryption();
  });
  vte::TypstRuntime runtime;
  QVERIFY(runtime.isAvailable());
  ServiceLocator services;
  services.registerService(&runtime);
  HtmlTemplateService templates(nullptr);
  WebEngineProfileService profiles(temp.filePath(QStringLiteral("profiles")), nullptr, nullptr);
  ProtectedMathPage first(profiles, note, services), second(profiles, sibling, services);
  QVERIFY(first.profile && second.profile);
  QVERIFY(first.profile->isOffTheRecord() && second.profile->isOffTheRecord());
  QCOMPARE(first.profile->httpCacheType(), QWebEngineProfile::NoCache);
  QVERIFY(first.grant.isAuthorized() && second.grant.isAuthorized());
  QVERIFY(first.grant.token != second.grant.token);
  QVERIFY(first.load(templates, dark) && second.load(templates, dark));
  QVERIFY(first.render(note.getContentRaw()));
  QVERIFY(second.render(sibling.getContentRaw()));
  const auto state = evaluateJavaScript(*first.page, QStringLiteral(R"JS((() => {
    const root = vxcore.contentContainer;
    const svg = Array.from(root.querySelectorAll('svg.typst-math'));
    return {renderer:vxOptions.mathRenderer, count:svg.length, workers:vxcore.numOfOngoingWorkers,
      other:!!window.katex || !!window.MathJax,
      valid:svg.every(node => node.viewBox.baseVal.width > 0 && node.viewBox.baseVal.width < 200 &&
        node.viewBox.baseVal.height > 0 && node.viewBox.baseVal.height < 200),
      failure:Array.from(root.querySelectorAll('.tex-to-render[title]')).some(node =>
        node.textContent.includes('frac(') && !node.querySelector('svg')),
      unsafe:!!root.querySelector('svg image, svg script, svg foreignObject, svg style')};
  })())JS"))
                         .toMap();
  QCOMPARE(state.value(QStringLiteral("renderer")).toString(), QStringLiteral("typst"));
  QCOMPARE(state.value(QStringLiteral("count")).toInt(), 3);
  QCOMPARE(state.value(QStringLiteral("workers"), -1).toInt(), 0);
  QVERIFY(state.value(QStringLiteral("valid")).toBool());
  QVERIFY(state.value(QStringLiteral("failure")).toBool());
  QVERIFY(!state.value(QStringLiteral("unsafe")).toBool());
  QVERIFY(!state.value(QStringLiteral("other")).toBool());

  // Source updates never append a second reader pass or fall back to LaTeX.
  QVERIFY(note.setContentRaw(QByteArray("Changed $sqrt(y)$")));
  QVERIFY(first.grant.isAuthorized());
  QVERIFY(first.render(note.getContentRaw()));
  QCOMPARE(evaluateJavaScript(*first.page,
                              QStringLiteral("Array.from(document.querySelectorAll('svg.typst-math'"
                                             "), n => n.getAttribute('aria-label')).join(',')"))
               .toString(),
           QStringLiteral("sqrt(y)"));
  QVERIFY(first.render(
      QByteArray("$#read(\"private.txt\")$\n\n$#import \"@preview/mitex:0.2.4\": *$\n\n"
                 "$#link(\"https://protected.invalid/\")[x]$")));
  QCOMPARE(evaluateJavaScript(*first.page,
                              QStringLiteral("document.querySelectorAll('svg.typst-math').length"))
               .toInt(),
           0);
  QVERIFY(note.setContentRaw(source));
  QVERIFY(first.render(note.getContentRaw()));

  QTcpServer external;
  QVERIFY(external.listen(QHostAddress::LocalHost));
  QSignalSpy outbound(&external, &QTcpServer::newConnection);
  const auto origin = QStringLiteral("http://127.0.0.1:%1/probe").arg(external.serverPort());
  evaluateJavaScript(*first.page, QStringLiteral(R"JS((() => {
    window.typstNetwork = 0;
    fetch('%1').then(() => {typstNetwork = -10;}, () => {++typstNetwork;});
    const image = new Image(); image.onload = () => {typstNetwork = -10;};
    image.onerror = () => {++typstNetwork;}; image.src = '%1';
    document.body.appendChild(image);
  })())JS")
                                      .arg(origin));
  QTRY_COMPARE_WITH_TIMEOUT(evaluateJavaScript(*first.page, QStringLiteral("typstNetwork")).toInt(),
                            2, 10000);
  QCOMPARE(outbound.count(), 0);

  const auto arguments = [](const QString &p_source, const QString &p_scope, bool p_graph = false) {
    QJsonObject args{{QStringLiteral("source"), p_source},
                     {QStringLiteral("scope"), p_scope},
                     {QStringLiteral("zoom"), 1.5},
                     {QStringLiteral("devicePixelRatio"), 2},
                     {QStringLiteral("width"), 96}};
    if (p_graph)
      args.insert(QStringLiteral("language"), QStringLiteral("mathjax"));
    return args;
  };
  // Full Typst fences are independent of protected MathJax -> KaTeX preference.
  ProtectedMathPage blocks(profiles, note, services);
  QVERIFY(blocks.load(templates, dark, QStringLiteral("mathjax")));
  const QString program = QStringLiteral(
      "#set page(width: 100pt, height: 60pt, margin: 8pt, fill: none)\n"
      "#let hidden = \"HiddenTypstProgramSentinel\"\n"
      "#rect(width: 20pt, height: 12pt, fill: rgb(\"#247ac8\"), stroke: none)\n"
      "#pagebreak()\n"
      "#link(\"https://protected.invalid/typst\")[Second] #context counter(page).display()\n"
      "#rect(width: 20pt, height: 12pt, fill: rgb(\"#c84a24\"), stroke: none)");
  QVERIFY(blocks.render(
      (QStringLiteral("Before\n\n```typst\n") + program + QStringLiteral("\n```\n\nAfter"))
          .toUtf8()));
  const auto blockState = evaluateJavaScript(*blocks.page, QStringLiteral(R"JS((() => {
    const root = vxcore.contentContainer;
    const svg = root.querySelector('svg.typst-block');
    return {renderer:vxOptions.mathRenderer, count:root.querySelectorAll('svg.typst-block').length,
      width:svg && svg.viewBox.baseVal.width, height:svg && svg.viewBox.baseVal.height,
      safe:!!svg && !svg.querySelector('a, image, foreignObject, script') &&
        !svg.outerHTML.includes('HiddenTypstProgramSentinel') &&
        !svg.outerHTML.includes('https://protected.invalid'),
      source:root.textContent, other:!!window.katex || !!window.MathJax,
      workers:vxcore.numOfOngoingWorkers};
  })())JS"))
                              .toMap();
  QCOMPARE(blockState.value(QStringLiteral("renderer")).toString(), QStringLiteral("katex"));
  QCOMPARE(blockState.value(QStringLiteral("count")).toInt(), 1);
  QVERIFY(qAbs(blockState.value(QStringLiteral("width")).toDouble() - 100) < 0.01);
  QVERIFY(qAbs(blockState.value(QStringLiteral("height")).toDouble() - 128) < 0.01);
  QVERIFY(blockState.value(QStringLiteral("safe")).toBool());
  QVERIFY(!blockState.value(QStringLiteral("other")).toBool());
  QVERIFY(blockState.value(QStringLiteral("source")).toString().contains(QStringLiteral("Before")));
  QVERIFY(blockState.value(QStringLiteral("source")).toString().contains(QStringLiteral("After")));
  QCOMPARE(blockState.value(QStringLiteral("workers"), -1).toInt(), 0);
  auto blockArgs = arguments(program, QStringLiteral("markup"));
  blockArgs.insert(QStringLiteral("language"), QStringLiteral("typst"));
  QVector<WebViewJobResult> blockResults;
  blocks.jobs->submit(QStringLiteral("markdown.previewGraph"), blockArgs, this,
                      [&](const WebViewJobResult &r) { blockResults.append(r); });
  QTRY_COMPARE_WITH_TIMEOUT(blockResults.size(), 1, 30000);
  const auto blockImage = protectedPreviewImage(blockResults.first());
  QVERIFY(!blockImage.isNull());
  QVERIFY(pixelsMatching(blockImage, QColor(36, 122, 200)) > 100);
  QVERIFY(pixelsMatching(blockImage, QColor(200, 74, 36)) > 100);
  {
    ProtectedMathGate lanes(runtime);
    QVERIFY(lanes.entered());
    blocks.jobs->submit(QStringLiteral("markdown.previewGraph"), blockArgs, this,
                        [&](const WebViewJobResult &r) { blockResults.append(r); });
    QTRY_VERIFY_WITH_TIMEOUT(
        blocks.adapter->findChild<vte::TypstFragmentRenderer *>()->isRendering(), 10000);
    profiles.revokeProtectedProfile(blocks.profile.get());
    QTRY_COMPARE_WITH_TIMEOUT(blockResults.size(), 2, 10000);
    QVERIFY(blockResults.last().status != WebViewJobResult::Status::Success);
    QVERIFY(!blocks.grant.isAuthorized());
    QVERIFY(first.grant.isAuthorized() && second.grant.isAuthorized());
  }

  evaluateJavaScript(
      *first.page,
      QStringLiteral("vxcore.contentContainer.style.color = 'rgba(31,83,157,0.6784313725490196)'"));
  QVector<WebViewJobResult> results;
  const auto complete = [&](const WebViewJobResult &p_result) { results.append(p_result); };
  first.jobs->submit(QStringLiteral("markdown.previewMath"),
                     arguments(QStringLiteral("$frac(a^2, sqrt(b))$"), QStringLiteral("inline")),
                     this, complete);
  first.jobs->submit(QStringLiteral("markdown.previewGraph"),
                     arguments(QStringLiteral("$$sum_(i=1)^n i$$"), QStringLiteral("fence"), true),
                     this, complete);
  QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 30000);
  for (const auto &result : results) {
    QCOMPARE(result.status, WebViewJobResult::Status::Success);
    const auto object = result.value.toObject();
    QCOMPARE(object.value(QStringLiteral("format")).toString(), QStringLiteral("svg"));
    const auto image = protectedPreviewImage(result);
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 10 && image.width() < 300);
    QVERIFY(image.height() > 10 && image.height() < 300);
    QVERIFY(pixelsMatching(image, QColor(31, 83, 157, 173)) > 10);
    QVERIFY(qAbs(image.width() - object.value(QStringLiteral("logicalWidth")).toInt() * 2) <= 2);
    QVERIFY(qAbs(image.height() - object.value(QStringLiteral("logicalHeight")).toInt() * 2) <= 2);
  }
  QCOMPARE(evaluateJavaScript(
               *first.page,
               QStringLiteral("document.getElementById('vx-inplace-preview').childElementCount"))
               .toInt(),
           0);
  QCOMPARE(outbound.count(), 0);

  // Real queued native work is retired immediately; the other note's profile survives.
  {
    ProtectedMathGate lanes(runtime);
    QVERIFY(lanes.entered());
    QVector<WebViewJobResult> retired, live;
    first.jobs->submit(QStringLiteral("markdown.previewMath"),
                       arguments(QStringLiteral("$sqrt(x)$"), QStringLiteral("pending")), this,
                       [&](const WebViewJobResult &r) { retired.append(r); });
    second.jobs->submit(QStringLiteral("markdown.previewMath"),
                        arguments(QStringLiteral("$sqrt(z)$"), QStringLiteral("pending")), this,
                        [&](const WebViewJobResult &r) { live.append(r); });
    QTRY_VERIFY_WITH_TIMEOUT(
        first.adapter->findChild<vte::TypstFragmentRenderer *>()->isRendering(), 10000);
    profiles.revokeProtectedProfile(first.profile.get());
    QVERIFY(!first.grant.isAuthorized());
    QVERIFY(second.grant.isAuthorized());
    QTRY_COMPARE_WITH_TIMEOUT(retired.size(), 1, 10000);
    QVERIFY(retired.first().status != WebViewJobResult::Status::Success);
    QVERIFY(retired.first().value.toObject().value(QStringLiteral("data")).toString().isEmpty());
    lanes.release();
    QTRY_COMPARE_WITH_TIMEOUT(live.size(), 1, 30000);
    QVERIFY(!protectedPreviewImage(live.first()).isNull());
    QTRY_VERIFY_WITH_TIMEOUT(first.jobs->isIdle() && second.jobs->isIdle(), 10000);
    QCOMPARE(retired.size(), 1);
  }
  // A retired profile cannot be revived by rebinding its old callback.
  first.adapter->setProtectedTypstAuthorization(first.grant.isAuthorized);
  QSignalSpy denied(first.adapter, &MarkdownViewerAdapter::typstReady);
  first.adapter->renderTypstMath(QStringLiteral("retired"), QStringLiteral("preview:retired"),
                                 QStringLiteral("sqrt(x)"), false, QStringLiteral("#1f539dff"));
  QCOMPARE(denied.count(), 1);
  QVERIFY(!denied.first().at(1).toJsonObject().value(QStringLiteral("error")).toString().isEmpty());

  ProtectedMathPage replacement(profiles, note, services);
  QVERIFY(replacement.load(templates, !dark));
  QVERIFY(replacement.render(QByteArray("Fresh $sqrt(y)$")));
  QVERIFY(
      buffers.protectedOperationsIdle()); // Capability checks do not retain a native-work lease.
  QVERIFY(buffers.beginProtectedLocking());
  QVERIFY(!replacement.grant.isAuthorized());
  QSignalSpy locked(replacement.adapter, &MarkdownViewerAdapter::typstReady);
  replacement.adapter->renderTypstMath(QStringLiteral("locked"), QStringLiteral("preview:locked"),
                                       QStringLiteral("sqrt(x)"), false,
                                       QStringLiteral("#1f539dff"));
  QCOMPARE(locked.count(), 1);
  QVERIFY(locked.first().at(1).toJsonObject().value(QStringLiteral("svg")).toString().isEmpty());
  buffers.cancelProtectedLocking();
  QVERIFY(replacement.grant.isAuthorized());

  // Revoke after real native output exists but before its GUI delivery/status acknowledgement.
  {
    ProtectedMathGate lanes(runtime);
    QVERIFY(lanes.entered());
    QSignalSpy completed(replacement.adapter, &MarkdownViewerAdapter::typstReady);
    replacement.adapter->renderTypstMath(
        QStringLiteral("in-flight"), QStringLiteral("preview:flight"),
        QStringLiteral("frac(a^2, sqrt(b))"), false, QStringLiteral("#1f539dff"));
    RevokeMathOnDelivery observer;
    observer.renderer = replacement.adapter->findChild<vte::TypstFragmentRenderer *>();
    QVERIFY(observer.renderer);
    bool revoked = false;
    observer.revoke = [&] {
      revoked = true;
      profiles.revokeProtectedProfile(replacement.profile.get());
    };
    qApp->installEventFilter(&observer);
    const auto removeObserver = qScopeGuard([&] { qApp->removeEventFilter(&observer); });
    lanes.release();
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 30000);
    QVERIFY(revoked);
    QVERIFY(
        completed.first().at(1).toJsonObject().value(QStringLiteral("svg")).toString().isEmpty());
    QVERIFY(!replacement.grant.isAuthorized());
  }
  QVERIFY(!first.grant.isAuthorized());
  QVERIFY(second.grant.isAuthorized());
  QCOMPARE(outbound.count(), 0);
  const auto unicodeMarker = QString::fromUtf8(marker);
  const QByteArray utf16Marker(reinterpret_cast<const char *>(unicodeMarker.utf16()),
                               unicodeMarker.size() * int(sizeof(char16_t)));
  QDirIterator files(temp.path(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (files.hasNext()) {
    QFile file(files.next());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY2(!bytes.contains(marker) && !bytes.contains(utf16Marker), qPrintable(file.fileName()));
  }
}

} // namespace tests

int main(int argc, char **argv) {
  // vxcore test mode uses the OS temporary root. Give this process its own
  // root instead of sharing notebook/session state with concurrently running suites.
  QTemporaryDir runtimeRoot;
  if (!runtimeRoot.isValid())
    return 1;
  QHash<QByteArray, QByteArray> oldEnvironment;
  for (const auto *key : {"TEMP", "TMP", "TMPDIR"}) {
    oldEnvironment.insert(key, qgetenv(key));
    qputenv(key, runtimeRoot.path().toUtf8());
  }
  const auto restoreEnvironment = qScopeGuard([&] {
    for (auto it = oldEnvironment.cbegin(); it != oldEnvironment.cend(); ++it) {
      if (it.value().isNull())
        qunsetenv(it.key());
      else
        qputenv(it.key(), it.value());
    }
  });
  WebEngineProfileService::registerProtectedScheme();
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
  int result;
  QString dataPath;
  QString cachePath;
  {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VNoteProfileTest-") +
                            QUuid::createUuid().toString(QUuid::WithoutBraces));
    app.setApplicationName(QStringLiteral("VNote"));
    dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    tests::TestWebEngineProfileService test;
    result = QTest::qExec(&test, argc, argv);
  }
  // Only this process's unique test identity, after all Chromium objects exit.
  QDir(dataPath).removeRecursively();
  QDir(cachePath).removeRecursively();
  QDir().rmdir(QFileInfo(dataPath).absolutePath());
  QDir().rmdir(QFileInfo(cachePath).absolutePath());
  return result;
}
#include "test_webengineprofileservice.moc"
