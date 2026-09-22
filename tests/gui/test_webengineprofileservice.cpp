// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QWebEnginePage>

#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTcpServer>
#include <QWebEngineProfile>
#include <QtConcurrent/QtConcurrentRun>

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

} // namespace

class TestWebEngineProfileService : public QObject {
  Q_OBJECT

private slots:
  void profileStorageStaysUnderConfiguredRoot();
  void protectedMathReadingAndLivePreview();
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

void TestWebEngineProfileService::protectedMathReadingAndLivePreview() {
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
  const QByteArray firstText("Inline $x^2+y^2=z^2$.\n\n$$\n\\frac{1}{2}\n$$\n");
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
             return notebooks.unlockNotebookEncryption(notebook, password);
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
  // Revocation must precede page destruction, then profile destruction, buffer
  // close and finally context destruction, including on an assertion failure.
  const auto revoke = qScopeGuard([&]() { service.revokeProtectedProfile(profile.get()); });
  QHash<QString, QByteArray> resources;
  auto html = templates.protectedMarkdownViewerTemplate(protectedPage.token, protectedPage.nonce,
                                                        resources);
  QVERIFY(!html.isEmpty());
  // The generated options precede the first bundled script. Supply only the
  // bridge notifications needed by the real viewer; no worker, font, loader or
  // request policy is replaced. Deliberately conflict with the protected renderer.
  const auto scriptsStart =
      html.indexOf(QStringLiteral("<script nonce=\"%1\" src=").arg(protectedPage.nonce));
  QVERIFY(scriptsStart >= 0);
  html.insert(scriptsStart, QStringLiteral(R"JS(<script nonce="%1">
    window.vxOptions.mathRenderer = 'mathjax';
    window.qt = { webChannelTransport: { send() {} } };
    window.__renderCount = 0;
    window.__mathPreviews = [];
    window.vxMarkdownAdapter = {
      setReady() {},
      setWorkFinished() { ++window.__renderCount; },
      setHeadings() {},
      setCrossCopyTargets() {},
      setCurrentHeadingAnchor() {},
      setTopLineNumber() {},
      setPresentationState() {},
      setMathPreviewData(id, stamp, format, data, base64) {
        window.__mathPreviews.push({ format, data, base64 });
      }
    };
  </script>
)JS")
                                .arg(protectedPage.nonce));
  QVERIFY(service.setProtectedDocument(profile.get(), html, resources));
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.load(protectedPage.url);
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());
  QVERIFY(evaluateJavaScript(page, QStringLiteral("window.vxcore.initialized")).toBool());

  const auto render = [&](const QString &p_text) {
    const auto text =
        QString::fromUtf8(QJsonDocument(QJsonArray{p_text}).toJson(QJsonDocument::Compact));
    page.runJavaScript(QStringLiteral("window.vxcore.setMarkdownText(%1[0])").arg(text));
  };
  render(note.decode(note.peekContentRaw()));
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluateJavaScript(page, QStringLiteral("window.__renderCount")).toInt(), 1, 15000);
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

  // A second editor snapshot replaces the reading DOM, rather than appending
  // another math pass or leaving already-typeset content behind.
  const QByteArray secondText("Updated $e^{i\\pi}+1=0$.\n");
  QVERIFY(note.setContentRaw(secondText));
  render(note.decode(note.peekContentRaw()));
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluateJavaScript(page, QStringLiteral("window.__renderCount")).toInt(), 2, 10000);
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
  render(QStringLiteral(R"MD($\href{https://protected.invalid/link}{unsafe}$

$\includegraphics{https://protected.invalid/image.png}$

$\htmlClass{protected-injected}{unsafe}$
)MD"));
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluateJavaScript(page, QStringLiteral("window.__renderCount")).toInt(), 3, 10000);
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
    window.vxcore.previewMath(1, 1, '$x^2$');
    window.vxcore.previewMath(2, 2, '$\\frac{a}{b}$');
    return blocked && !root.querySelector('.katex, svg');
  })())JS"))
              .toBool());
  const auto previews = evaluateJavaScript(page, QStringLiteral("window.__mathPreviews")).toList();
  QCOMPARE(previews.size(), 2);
  const auto firstPreview = previews.at(0).toMap();
  const auto secondPreview = previews.at(1).toMap();
  QCOMPARE(firstPreview.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
  QVERIFY(firstPreview.value(QStringLiteral("base64")).toBool());
  const auto blockedImage =
      QByteArray::fromBase64(firstPreview.value(QStringLiteral("data")).toByteArray());
  QVERIFY(!QImage::fromData(blockedImage, "PNG").isNull());
  QCOMPARE(firstPreview.value(QStringLiteral("data")), secondPreview.value(QStringLiteral("data")));
}

} // namespace tests

int main(int argc, char **argv) {
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
