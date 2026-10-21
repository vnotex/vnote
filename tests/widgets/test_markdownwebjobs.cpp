// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <cmath>
#include <memory>
#include <utility>

#include <core/servicelocator.h>
#include <gui/services/webviewjobs.h>
#include <gui/services/webviewservice.h>
#include <utils/markdownhtmlrenderer.h>
#include <utils/webutils.h>
#include <widgets/editors/markdownvieweradapter.h>

using namespace vnotex;

namespace tests {
namespace {

QString webDir() { return QStringLiteral(VNOTE_SRC_DIR) + QStringLiteral("/data/extra/web"); }

// These are shipped scripts, including the production channel handshake. Only
// the host's renderer options and theme are fixture data; no worker is replaced.
const QStringList &scripts() {
  static const QStringList files = {QStringLiteral("webviewjobs.js"),
                                    QStringLiteral("qwebchannel.js"),
                                    QStringLiteral("eventemitter.js"),
                                    QStringLiteral("vxcore.js"),
                                    QStringLiteral("utils.js"),
                                    QStringLiteral("nodelinemapper.js"),
                                    QStringLiteral("lrucache.js"),
                                    QStringLiteral("graphcache.js"),
                                    QStringLiteral("graphpreviewer.js"),
                                    QStringLiteral("markdownviewercore.js"),
                                    QStringLiteral("vxworker.js"),
                                    QStringLiteral("graphrenderer.js"),
                                    QStringLiteral("svg-to-image.js"),
                                    QStringLiteral("computed-style-to-inline-style.js"),
                                    QStringLiteral("imageviewer.js"),
                                    QStringLiteral("easyaccess.js"),
                                    QStringLiteral("crosscopy.js"),
                                    QStringLiteral("markdownviewer.js"),
                                    QStringLiteral("xss.min.js"),
                                    QStringLiteral("documentrenderer.js"),
                                    QStringLiteral("prism.js"),
                                    QStringLiteral("codeblockactions.js"),
                                    QStringLiteral("mermaid.js"),
                                    QStringLiteral("flowchart.js/raphael.min.js"),
                                    QStringLiteral("flowchart.js/flowchart.min.js"),
                                    QStringLiteral("flowchartjs.js"),
                                    QStringLiteral("wavedrom.js"),
                                    QStringLiteral("mathjax.js"),
                                    QStringLiteral("plantuml.js"),
                                    QStringLiteral("graphviz.js"),
                                    QStringLiteral("turndown/turndown.js"),
                                    QStringLiteral("turndown/turndown-plugin-gfm.js"),
                                    QStringLiteral("turndown.js"),
                                    QStringLiteral("mark.js/mark.min.js"),
                                    QStringLiteral("markjs.js"),
                                    QStringLiteral("markdownwebjobs.js")};
  return files;
}

QString runtimeHtml(const QColor &p_color, const QString &p_mathJaxScript) {
  QFile file(webDir() + QStringLiteral("/markdown-viewer-template.html"));
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  auto html = QString::fromUtf8(file.readAll());
  const QJsonObject options{{QStringLiteral("webGraphviz"), true},
                            {QStringLiteral("mathRenderer"), p_mathJaxScript.isEmpty()
                                                                 ? QStringLiteral("katex")
                                                                 : QStringLiteral("mathjax")},
                            {QStringLiteral("mathJaxScript"), p_mathJaxScript},
                            {QStringLiteral("scrollable"), true},
                            {QStringLiteral("bodyWidth"), 0},
                            {QStringLiteral("bodyHeight"), 0},
                            {QStringLiteral("headingFoldingEnabled"), true}};
  html.replace(
      QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"),
      WebUtils::translationScript() +
          QStringLiteral("window.vxOptions = %1;")
              .arg(QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact))));
  html.replace(QStringLiteral("/* VX_GLOBAL_STYLES_PLACEHOLDER */"),
               QStringLiteral("#vx-content {font:18px Arial;color:%1;}"
                              "#vx-inplace-preview {position:absolute;left:-100000px;top:0;}")
                   .arg(p_color.name()));
  QString tags;
  for (const auto &name : scripts()) {
    tags += QStringLiteral("<script src=\"%1\"></script>\n")
                .arg(QUrl::fromLocalFile(webDir() + QStringLiteral("/js/") + name)
                         .toString()
                         .toHtmlEscaped());
  }
  html.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"), tags);
  return html;
}

QVariant evaluate(QWebEnginePage *p_page, const QString &p_script) {
  const auto state = std::make_shared<std::pair<bool, QVariant>>();
  p_page->runJavaScript(p_script, [state](const QVariant &value) {
    state->first = true;
    state->second = value;
  });
  if (!QTest::qWaitFor([state]() { return state->first; }, 10000)) {
    return QVariant();
  }
  return state->second;
}

struct Result {
  QString id;
  bool finished = false;
  WebViewJobResult result;
};

std::shared_ptr<Result> submit(WebViewJobSession *p_session, const QString &p_operation,
                               const QJsonObject &p_args, QObject *p_owner) {
  const auto state = std::make_shared<Result>();
  state->id =
      p_session->submit(p_operation, p_args, p_owner, [state](const WebViewJobResult &result) {
        state->result = result;
        state->finished = true;
      });
  return state;
}

QJsonObject source(const QString &p_text) { return {{QStringLiteral("source"), p_text}}; }

QJsonObject preview(const QString &p_text, const QString &p_language = QString(), qreal p_zoom = 1,
                    qreal p_dpr = 1, qreal p_width = 800) {
  return {{QStringLiteral("source"), p_text},
          {QStringLiteral("language"), p_language},
          {QStringLiteral("zoom"), p_zoom},
          {QStringLiteral("devicePixelRatio"), p_dpr},
          {QStringLiteral("width"), p_width}};
}

QImage image(const QJsonValue &p_value) {
  const auto obj = p_value.toObject();
  const auto data = obj.value(QStringLiteral("data")).toString().toUtf8();
  return QImage::fromData(
      obj.value(QStringLiteral("base64")).toBool() ? QByteArray::fromBase64(data) : data,
      obj.value(QStringLiteral("format")).toString().toLatin1().constData());
}

int inkPixels(const QImage &p_image, const QColor &p_color = QColor()) {
  int count = 0;
  for (int y = 0; y < p_image.height(); ++y) {
    for (int x = 0; x < p_image.width(); ++x) {
      const auto color = p_image.pixelColor(x, y);
      const bool ink = color.red() < 245 || color.green() < 245 || color.blue() < 245;
      if (color.alpha() > 64 && ((!p_color.isValid() && ink) ||
                                 (p_color.isValid() && std::abs(color.red() - p_color.red()) <= 4 &&
                                  std::abs(color.green() - p_color.green()) <= 4 &&
                                  std::abs(color.blue() - p_color.blue()) <= 4))) {
        ++count;
      }
    }
  }
  return count;
}

struct Runtime {
  ServiceLocator services;
  QWebEngineProfile profile;
  std::unique_ptr<WebViewService> service;
  std::unique_ptr<QWebEngineView> reader;
  QPointer<QWebEnginePage> page;
  QPointer<MarkdownViewerAdapter> adapter;
  WebViewJobSession *session = nullptr;
  quint64 generation = 0;

  explicit Runtime(bool p_shared, const QColor &p_color = QColor(17, 85, 153),
                   const QUrl &p_base = QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                   const QString &p_mathJaxScript = QString()) {
    const auto bootstrap = [this, p_color, p_base, p_mathJaxScript](QWebEnginePage *page,
                                                                    QWebChannel *channel) {
      this->page = page;
      adapter = new MarkdownViewerAdapter(services, page);
      channel->registerObject(QStringLiteral("vxAdapter"), adapter);
      page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
      page->setHtml(runtimeHtml(p_color, p_mathJaxScript), p_base);
    };
    if (p_shared) {
      service = std::make_unique<WebViewService>(services, &profile, bootstrap);
      session = service->sharedSession();
    } else {
      reader = std::make_unique<QWebEngineView>();
      reader->setAttribute(Qt::WA_DontShowOnScreen);
      reader->resize(997, 701);
      auto *readerPage = new QWebEnginePage(&profile, reader.get());
      reader->setPage(readerPage);
      auto *channel = new QWebChannel(readerPage);
      readerPage->setWebChannel(channel);
      session = new WebViewJobSession(readerPage, channel, readerPage);
      bootstrap(readerPage, channel);
      reader->show();
    }
  }

  MarkdownHtmlResult renderDocument(const QString &p_source) {
    const auto result = MarkdownHtmlRenderer::render(p_source.toUtf8(), MarkdownHtmlOptions{});
    if (result.error.isEmpty()) {
      adapter->setRenderedHtml(QString::number(++generation), result.html);
    }
    return result;
  }
};

} // namespace

class TestMarkdownWebJobs : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void allOperations_data();
  void allOperations();
  void graphAlgorithms_data();
  void graphAlgorithms();
  void rasterGeometry_data();
  void rasterGeometry();
  void targetWidth();
  void mathKeepsIntrinsicSizeInNarrowHosts();
  void immutableThemeGenerations();
  void macroIsolation_data();
  void macroIsolation();
  void readerMathLifecycle_data();
  void readerMathLifecycle();
  void sourceFailuresAndCancellationDrain();
};

void TestMarkdownWebJobs::initTestCase() {
  for (const auto &file : scripts()) {
    QVERIFY2(QFile::exists(webDir() + QStringLiteral("/js/") + file), qPrintable(file));
  }
}

void TestMarkdownWebJobs::allOperations_data() {
  QTest::addColumn<bool>("shared");
  QTest::newRow("shared-empty-runtime") << true;
  QTest::newRow("borrowed-reader") << false;
}

void TestMarkdownWebJobs::allOperations() {
  QFETCH(bool, shared);
  QTemporaryDir base;
  QVERIFY(base.isValid());
  Runtime runtime(shared, QColor(17, 85, 153), QUrl::fromLocalFile(base.path() + QLatin1Char('/')));
  QObject ownerA, ownerB;
  QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
  // Submit before cold-start readiness. Distinct sources intentionally share all
  // local editor identity; only transport IDs distinguish their completions.
  const auto htmlA =
      submit(runtime.session, QStringLiteral("markdown.htmlToMarkdown"),
             source(QStringLiteral("<h2>Alpha</h2><p><strong>one</strong></p>")), &ownerA);
  const auto htmlB =
      submit(runtime.session, QStringLiteral("markdown.htmlToMarkdown"),
             source(QStringLiteral("<p><a href=\"../other.md#two\">Beta</a></p>"
                                   "<img src=\"images/local.png\" width=\"37\" height=\"19\" />")),
             &ownerB);
  const auto code =
      submit(runtime.session, QStringLiteral("markdown.highlightCode"),
             source(QStringLiteral("```javascript\nconst answer = 42;\n```")), &ownerA);
  const auto mathSource = submit(runtime.session, QStringLiteral("markdown.highlightMath"),
                                 source(QStringLiteral("$$\n\\frac{a}{b}\n+ c\n$$")), &ownerB);
  const auto styles =
      submit(runtime.session, QStringLiteral("markdown.parseStyleSheet"),
             source(QStringLiteral(".token.keyword {color:#115599;background-color:#eef1f4;"
                                   "font-weight:bold;font-style:italic;}")),
             &ownerA);
  const auto graph =
      submit(runtime.session, QStringLiteral("markdown.previewGraph"),
             preview(QStringLiteral("graph TD\n A[Alpha] --> B[Beta]"), QStringLiteral("mermaid")),
             &ownerA);
  const auto math = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                           preview(QStringLiteral("$\\frac{a^2+1}{\\sqrt{b}}$")), &ownerB);
  for (const auto &job : {htmlA, htmlB, code, mathSource, styles, graph, math}) {
    QTRY_VERIFY_WITH_TIMEOUT(job->finished, 30000);
    QCOMPARE(job->result.status, WebViewJobResult::Status::Success);
  }
  QVERIFY(htmlA->id != htmlB->id);
  QCOMPARE(htmlA->result.value.toString(), QStringLiteral("## Alpha\n\n**one**"));
  QCOMPARE(htmlB->result.value.toString(),
           QStringLiteral("[Beta](../other.md#two)\n\n<img src=\"images/local.png\" width=\"37\" "
                          "height=\"19\" />"));
  QVERIFY(code->result.value.toString().contains(
      QStringLiteral("<span class=\"token keyword\">const</span>")));
  const auto lines = mathSource->result.value.toString().split(QLatin1Char('\n'));
  QCOMPARE(lines.size(), 4);
  QVERIFY(lines.at(1).contains(QStringLiteral("token")));
  for (const auto &line : lines) {
    QCOMPARE(line.count(QStringLiteral("<span")), line.count(QStringLiteral("</span>")));
  }
  const auto rule = styles->result.value.toArray().at(0).toObject();
  QCOMPARE(rule.value(QStringLiteral("selector")).toString(), QStringLiteral(".token.keyword"));
  QCOMPARE(rule.value(QStringLiteral("color")).toString(), QStringLiteral("rgb(17, 85, 153)"));
  QCOMPARE(rule.value(QStringLiteral("backgroundColor")).toString(),
           QStringLiteral("rgb(238, 241, 244)"));
  QCOMPARE(rule.value(QStringLiteral("fontWeight")).toString(), QStringLiteral("bold"));
  QCOMPARE(rule.value(QStringLiteral("fontStyle")).toString(), QStringLiteral("italic"));
  QVERIFY(inkPixels(image(graph->result.value)) > 100);
  QVERIFY(inkPixels(image(math->result.value), QColor(17, 85, 153)) > 30);
  QCOMPARE(rendered.count(), 0);
  QCOMPARE(evaluate(runtime.page, QStringLiteral("vxcore.contentContainer.innerHTML")).toString(),
           QString());
  QCOMPARE(evaluate(runtime.page, QStringLiteral("document.baseURI")).toUrl(),
           QUrl::fromLocalFile(base.path() + QLatin1Char('/')));

  // Helper jobs must leave the native document bridge intact. Publish real native
  // HTML and exercise its task, code and graph markers through the shipped workers.
  const auto document = runtime.renderDocument(
      QStringLiteral("# Reader\n\n- [ ] action\n\nA visible paragraph.\n\n"
                     "```javascript\nconst answer = 42;\n```\n\n"
                     "```mermaid\ngraph TD\n A[Reader] --> B[Rendered]\n```\n"));
  QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
  QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
  QCOMPARE(document.headings.size(), 1);
  QCOMPARE(evaluate(runtime.page, QStringLiteral("document.querySelector('#vx-content h1').id"))
               .toString(),
           document.headings.first().anchor);
  QVERIFY(evaluate(runtime.page,
                   QStringLiteral("!!document.querySelector('#vx-content code .token.keyword')"))
              .toBool());
  QVERIFY(evaluate(runtime.page,
                   QStringLiteral("!!document.querySelector('#vx-content .vx-mermaid-graph svg')"))
              .toBool());
  const auto generation = runtime.adapter->documentGeneration();
  QCOMPARE(evaluate(runtime.page, QStringLiteral("vxcore.documentGeneration")).toString(),
           generation);
  QSignalSpy toggled(runtime.adapter, &MarkdownViewerAdapter::taskListItemToggleRequested);
  evaluate(runtime.page,
           QStringLiteral("document.querySelector('.task-list-item-checkbox').click()"));
  QTRY_COMPARE_WITH_TIMEOUT(toggled.count(), 1, 10000);
  QCOMPARE(toggled.at(0).at(0).toString(), generation);
  QCOMPARE(toggled.at(0).at(1).toInt(), 2);
  QVERIFY(toggled.at(0).at(2).toBool());

  // A separate conversion must produce its own result without replacing or rerendering
  // the live reader document, even when the reader session executes the helper job.
  const auto before =
      evaluate(runtime.page, QStringLiteral("vxcore.contentContainer.innerHTML")).toString();
  const auto other =
      submit(runtime.session, QStringLiteral("markdown.htmlToMarkdown"),
             source(QStringLiteral("<h2>Unrelated</h2><p><em>Helper</em></p>")), &ownerA);
  QTRY_VERIFY_WITH_TIMEOUT(other->finished, 10000);
  QCOMPARE(other->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(other->result.value.toString(), QStringLiteral("## Unrelated\n\n*Helper*"));
  QCOMPARE(evaluate(runtime.page, QStringLiteral("vxcore.contentContainer.innerHTML")).toString(),
           before);
  QCOMPARE(evaluate(runtime.page, QStringLiteral("vxcore.documentGeneration")).toString(),
           generation);
  QCOMPARE(rendered.count(), 1);
}

void TestMarkdownWebJobs::graphAlgorithms_data() {
  QTest::addColumn<QString>("language");
  QTest::addColumn<QString>("text");
  QTest::newRow("mermaid") << QStringLiteral("mermaid") << QStringLiteral("graph LR\n A --> B");
  QTest::newRow("flowchart") << QStringLiteral("flow")
                             << QStringLiteral("st=>start: Start\ne=>end: End\nst->e");
  QTest::newRow("wavedrom")
      << QStringLiteral("wavedrom")
      << QStringLiteral("{signal:[{name:'clk',wave:'p....'},{name:'data',wave:'01010'}]}");
  QTest::newRow("graphviz") << QStringLiteral("dot") << QStringLiteral("digraph { Alpha -> Beta }");
}

void TestMarkdownWebJobs::graphAlgorithms() {
  QFETCH(QString, language);
  QFETCH(QString, text);
  Runtime runtime(true);
  QObject owner;
  const auto job = submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                          preview(text, language), &owner);
  QTRY_VERIFY_WITH_TIMEOUT(job->finished, 30000);
  QCOMPARE(job->result.status, WebViewJobResult::Status::Success);
  QVERIFY2(inkPixels(image(job->result.value)) > 50, qPrintable(language));
  QCOMPARE(
      evaluate(runtime.page,
               QStringLiteral("document.getElementById('vx-inplace-preview').childElementCount"))
          .toInt(),
      0);
}

void TestMarkdownWebJobs::rasterGeometry_data() {
  QTest::addColumn<QString>("operation");
  QTest::addColumn<qreal>("zoom");
  QTest::addColumn<qreal>("dpr");
  for (const auto &operation :
       {QStringLiteral("markdown.previewGraph"), QStringLiteral("markdown.previewMath")}) {
    for (const auto &pair : {QPair<qreal, qreal>(1, 2), {2, 1}, {1.5, 1.25}}) {
      QTest::newRow(qPrintable(operation + QString::number(pair.first) + QLatin1Char('/') +
                               QString::number(pair.second)))
          << operation << pair.first << pair.second;
    }
  }
}

void TestMarkdownWebJobs::rasterGeometry() {
  QFETCH(QString, operation);
  QFETCH(qreal, zoom);
  QFETCH(qreal, dpr);
  Runtime runtime(false);
  QObject owner;
  const bool math = operation == QStringLiteral("markdown.previewMath");
  const auto text = math ? QStringLiteral("$\\frac{a^2}{b}+\\sqrt{c}$")
                         : QStringLiteral("graph LR\n A[Left] --> B[Right]");
  const auto normal =
      submit(runtime.session, operation, preview(text, QStringLiteral("mermaid")), &owner);
  const auto scaled = submit(runtime.session, operation,
                             preview(text, QStringLiteral("mermaid"), zoom, dpr), &owner);
  QTRY_VERIFY_WITH_TIMEOUT(normal->finished && scaled->finished, 30000);
  QCOMPARE(normal->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(scaled->result.status, WebViewJobResult::Status::Success);
  const auto a = normal->result.value.toObject();
  const auto b = scaled->result.value.toObject();
  QCOMPARE(b.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
  QVERIFY(!b.value(QStringLiteral("needScale")).toBool());
  const auto baseline = image(normal->result.value);
  const auto raster = image(scaled->result.value);
  QVERIFY(inkPixels(raster) > 100);
  for (const auto &dimension : {QStringLiteral("logicalWidth"), QStringLiteral("logicalHeight")}) {
    QVERIFY(a.value(dimension).toInt() > 0);
    QVERIFY(std::abs(b.value(dimension).toInt() - qRound(a.value(dimension).toInt() * zoom)) <= 1);
  }
  QVERIFY(std::abs(raster.width() - baseline.width() * zoom * dpr) <= 3);
  QVERIFY(std::abs(raster.height() - baseline.height() * zoom * dpr) <= 3);
  QCOMPARE(runtime.page->zoomFactor(), qreal(1));
}

void TestMarkdownWebJobs::mathKeepsIntrinsicSizeInNarrowHosts() {
  Runtime runtime(true);
  QObject hiddenEditor, visibleEditor;
  const auto formula = QStringLiteral("$x^3 + y^3$");
  const auto narrow = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                             preview(formula, QString(), 1, 1, 40), &hiddenEditor);
  const auto wide = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                           preview(formula, QString(), 1, 1, 800), &visibleEditor);
  QTRY_VERIFY_WITH_TIMEOUT(narrow->finished && wide->finished, 30000);
  QCOMPARE(narrow->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(wide->result.status, WebViewJobResult::Status::Success);
  const auto expected = image(wide->result.value);
  QVERIFY(inkPixels(expected) > 30);
  QCOMPARE(image(narrow->result.value), expected);
  const auto a = narrow->result.value.toObject();
  const auto b = wide->result.value.toObject();
  QCOMPARE(a.value(QStringLiteral("logicalWidth")), b.value(QStringLiteral("logicalWidth")));
  QCOMPARE(a.value(QStringLiteral("logicalHeight")), b.value(QStringLiteral("logicalHeight")));
}

void TestMarkdownWebJobs::targetWidth() {
  Runtime runtime(true);
  QObject owner;
  const auto text = QStringLiteral("gantt\n dateFormat YYYY-MM-DD\n section Work\n"
                                   " First :a, 2026-01-01, 3d\n Second :after a, 2d");
  const auto narrow = submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                             preview(text, QStringLiteral("mermaid"), 1, 1, 420), &owner);
  const auto wide = submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                           preview(text, QStringLiteral("mermaid"), 1, 1, 900), &owner);
  QTRY_VERIFY_WITH_TIMEOUT(narrow->finished && wide->finished, 30000);
  QCOMPARE(narrow->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(wide->result.status, WebViewJobResult::Status::Success);
  const int narrowWidth =
      narrow->result.value.toObject().value(QStringLiteral("logicalWidth")).toInt();
  const int wideWidth = wide->result.value.toObject().value(QStringLiteral("logicalWidth")).toInt();
  QVERIFY(narrowWidth > 0);
  QVERIFY(wideWidth > narrowWidth * 1.4);
  QVERIFY(inkPixels(image(narrow->result.value)) > 100);
  QVERIFY(inkPixels(image(wide->result.value)) > 100);
}

void TestMarkdownWebJobs::immutableThemeGenerations() {
  const QColor firstColor(17, 85, 153), secondColor(180, 40, 60);
  Runtime first(true, firstColor), second(true, secondColor);
  QObject ownerA, ownerB;
  const auto a = submit(first.session, QStringLiteral("markdown.previewMath"),
                        preview(QStringLiteral("$x^2+y^2=z^2$")), &ownerA);
  const auto b = submit(second.session, QStringLiteral("markdown.previewMath"),
                        preview(QStringLiteral("$x^2+y^2=z^2$")), &ownerB);
  QTRY_VERIFY_WITH_TIMEOUT(a->finished && b->finished, 30000);
  QCOMPARE(a->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(b->result.status, WebViewJobResult::Status::Success);
  const auto firstImage = image(a->result.value), secondImage = image(b->result.value);
  QCOMPARE(firstImage.size(), secondImage.size());
  QVERIFY(inkPixels(firstImage, firstColor) > 100);
  QVERIFY(inkPixels(secondImage, secondColor) > 100);
  QCOMPARE(inkPixels(firstImage, secondColor), 0);
  QCOMPARE(inkPixels(secondImage, firstColor), 0);
}

void TestMarkdownWebJobs::macroIsolation_data() {
  QTest::addColumn<bool>("mathJax");
  QTest::addColumn<QString>("definition");
  QTest::newRow("katex-newcommand") << false << QStringLiteral("\\newcommand{\\vxjobmacro}{ABCD}");
  QTest::newRow("katex-gdef") << false << QStringLiteral("\\gdef\\vxjobmacro{ABCD}");
  QTest::newRow("mathjax-newcommand") << true << QStringLiteral("\\newcommand{\\vxjobmacro}{ABCD}");
  QTest::newRow("mathjax-def") << true << QStringLiteral("\\def\\vxjobmacro{ABCD}");
}

void TestMarkdownWebJobs::macroIsolation() {
  QFETCH(bool, mathJax);
  QFETCH(QString, definition);
  QString mathJaxScript;
  if (mathJax) {
    const auto path = QStringLiteral(VNOTE_TEST_MATHJAX_SCRIPT);
    QVERIFY2(QFile::exists(path), qPrintable(path));
    mathJaxScript = QUrl::fromLocalFile(path).toString();
  }
  Runtime runtime(false, QColor(17, 85, 153), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                  mathJaxScript);
  QString readerMacro;
  if (mathJax) {
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    const auto document =
        runtime.renderDocument(QStringLiteral("$\\newcommand{\\vxreadermacro}{R}\\vxreadermacro$"));
    QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    readerMacro =
        evaluate(runtime.page, QStringLiteral("MathJax.tex2mml('\\\\vxreadermacro')")).toString();
    QVERIFY(readerMacro.contains(QStringLiteral(">R</mi>")));
  }
  QObject ownerA, ownerB;
  const auto scoped = [](const QString &text, const QString &scope) {
    auto args = preview(text);
    args.insert(QStringLiteral("scope"), scope);
    return args;
  };
  const auto scopeA = QStringLiteral("editor-a-generation-1");
  const auto scopeB = QStringLiteral("editor-b-generation-1");
  // The undefined-command image is the reference. A definition in another
  // client's request must neither alter it nor disable definitions within a job.
  const auto before = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                             scoped(QStringLiteral("$\\vxjobmacro$"), scopeB), &ownerB);
  const auto defined = submit(
      runtime.session, QStringLiteral("markdown.previewMath"),
      scoped(QLatin1Char('$') + definition + QStringLiteral("\\vxjobmacro$"), scopeA), &ownerA);
  const auto literal = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                              scoped(QStringLiteral("$ABCD$"), scopeA), &ownerA);
  const auto after = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                            scoped(QStringLiteral("$\\vxjobmacro$"), scopeB), &ownerB);
  const auto continuation = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                   scoped(QStringLiteral("$\\vxjobmacro$"), scopeA), &ownerA);
  for (const auto &job : {before, defined, literal, after, continuation}) {
    QTRY_VERIFY_WITH_TIMEOUT(job->finished, 30000);
    QCOMPARE(job->result.status, WebViewJobResult::Status::Success);
    QVERIFY(!image(job->result.value).isNull());
  }
  QCOMPARE(image(defined->result.value), image(literal->result.value));
  QCOMPARE(image(before->result.value), image(after->result.value));
  QVERIFY(image(defined->result.value) != image(after->result.value));
  // MathJax declarations persist across this editor's ordered requests. KaTeX
  // retains its existing per-expression macro environment.
  QCOMPARE(image(continuation->result.value),
           image(mathJax ? literal->result.value : before->result.value));
  if (mathJax) {
    const auto overwrite = submit(
        runtime.session, QStringLiteral("markdown.previewMath"),
        scoped(QStringLiteral("$\\def\\vxreadermacro{ZZZZ}\\vxreadermacro$"), scopeA), &ownerA);
    QTRY_VERIFY_WITH_TIMEOUT(overwrite->finished, 30000);
    QCOMPARE(overwrite->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("MathJax.tex2mml('\\\\vxreadermacro')")).toString(),
        readerMacro);
  }
  const auto survivingDefinition =
      submit(runtime.session, QStringLiteral("markdown.previewMath"),
             scoped((mathJax ? QStringLiteral("$\\def") : QStringLiteral("$\\gdef")) +
                        QStringLiteral("\\vxjobmacro{EFGH}\\vxjobmacro$"), scopeB), &ownerB);
  QTRY_VERIFY_WITH_TIMEOUT(survivingDefinition->finished, 30000);
  QCOMPARE(survivingDefinition->result.status, WebViewJobResult::Status::Success);
  const auto released = submit(runtime.session, QStringLiteral("markdown.releaseScope"),
                               {{QStringLiteral("scope"), scopeA}}, &ownerA);
  const auto reset = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                            scoped(QStringLiteral("$\\vxjobmacro$"), scopeA), &ownerA);
  QTRY_VERIFY_WITH_TIMEOUT(released->finished && reset->finished, 30000);
  QCOMPARE(released->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(reset->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(image(reset->result.value), image(before->result.value));
  const auto survivingContinuation =
      submit(runtime.session, QStringLiteral("markdown.previewMath"),
             scoped(QStringLiteral("$\\vxjobmacro$"), scopeB), &ownerB);
  QTRY_VERIFY_WITH_TIMEOUT(survivingContinuation->finished, 30000);
  QCOMPARE(survivingContinuation->result.status, WebViewJobResult::Status::Success);
  QCOMPARE(image(survivingContinuation->result.value),
           image(mathJax ? survivingDefinition->result.value : before->result.value));
  if (mathJax) {
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    const auto document = runtime.renderDocument(QStringLiteral("$\\vxreadermacro$"));
    QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QVERIFY(evaluate(
                runtime.page,
                QStringLiteral("!!document.querySelector('#vx-content mjx-container svg') && "
                               "!document.querySelector('#vx-content [data-mml-node=\"merror\"]')"))
                .toBool());
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("MathJax.tex2mml('\\\\vxreadermacro')")).toString(),
        readerMacro);
  }
}

void TestMarkdownWebJobs::readerMathLifecycle_data() {
  QTest::addColumn<bool>("mathJax");
  QTest::newRow("mathjax") << true;
  QTest::newRow("katex") << false;
}

void TestMarkdownWebJobs::readerMathLifecycle() {
  QFETCH(bool, mathJax);
  const auto script = mathJax ? QUrl::fromLocalFile(QStringLiteral(VNOTE_TEST_MATHJAX_SCRIPT)).toString()
                             : QString();
  Runtime runtime(false, QColor(17, 85, 153), QUrl::fromLocalFile(webDir() + QLatin1Char('/')), script);
  QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
  const auto text = QStringLiteral(
      "# Math\n\nInline $a_1^2+b_1^2=c_1^2$ and $\\sqrt{2}$.\n\n"
      "$$\n\\int_0^1 x\\,dx=\\frac{1}{2}\n$$\n\n"
      "```mathjax\n$$\n\\sum_{n=1}^{5}n=15\n$$\n```\n");
  const QStringList expectedSources{QStringLiteral("a_1^2+b_1^2=c_1^2"),
                                    QStringLiteral("\\sqrt{2}"),
                                    QStringLiteral("\\int_0^1 x\\,dx=\\frac{1}{2}"),
                                    QStringLiteral("\\sum_{n=1}^{5}n=15")};
  int passes = 0;
  // Plain and empty replacements both retire the old document; subsequent math must still work.
  for (const auto &replacement : {QStringLiteral("# Plain\n\nNo formulas"), QString()}) {
    for (int repeat = 0; repeat < 3; ++repeat) {
      const auto document = runtime.renderDocument(text);
      QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
      ++passes;
      QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), passes, 30000);
      const auto result = evaluate(runtime.page, QStringLiteral(R"JS(
JSON.stringify((() => {
  const root = vxcore.contentContainer;
  const doc = window.MathJax && MathJax.startup.document;
  const items = doc ? Array.from(doc.math) : [];
  return {
    sources: doc ? items.map(item => item.math.trim())
      : Array.from(root.querySelectorAll('annotation[encoding="application/x-tex"]'),
                   node => node.textContent.trim()),
    detached: items.filter(item => !item.typesetRoot.isConnected).length,
    formulas: root.querySelectorAll('mjx-container, .katex').length,
    errors: root.querySelectorAll('[data-mml-node="merror"], .katex-error').length
  };
})())
)JS"));
      const auto state = QJsonDocument::fromJson(result.toString().toUtf8()).object();
      QCOMPARE(state.value(QStringLiteral("sources")).toArray().toVariantList(),
               QJsonArray::fromStringList(expectedSources).toVariantList());
      QCOMPARE(state.value(QStringLiteral("detached")).toInt(-1), 0);
      QCOMPARE(state.value(QStringLiteral("formulas")).toInt(-1), 4);
      QCOMPARE(state.value(QStringLiteral("errors")).toInt(-1), 0);
      if (mathJax) {
        // Select a real MathItem through its installed menu listener, not a fixture-held DOM ref.
        QVERIFY(evaluate(runtime.page, QStringLiteral(R"JS(
(() => {
  document.querySelector('mjx-container').dispatchEvent(
    new MouseEvent('contextmenu', {bubbles:true, cancelable:true, clientX:20, clientY:20}));
  return !!MathJax.startup.document.menu.menu.mathItem;
})()
)JS")).toBool());
      }
    }
    const auto document = runtime.renderDocument(replacement);
    QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
    ++passes;
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), passes, 30000);
    const auto cleared = evaluate(runtime.page, QStringLiteral(R"JS(
JSON.stringify((() => {
  const doc = window.MathJax && MathJax.startup.document;
  if (!doc) return {formulas:document.querySelectorAll('#vx-content .katex').length};
  const output = doc.outputJax;
  const menu = doc.menu.menu;
  const refs = [output.math && output.math.typesetRoot, output.container,
                output.fontCache && output.fontCache.defs,
                menu.mathItem && menu.mathItem.typesetRoot, menu.anchor, menu.store.active]
    .concat(doc.options.elements || [], Object.values(menu.store.store));
  return {
    formulas:document.querySelectorAll('#vx-content mjx-container').length,
    tracked:Array.from(doc.math).length,
    menuItems:Object.keys(menu.store.store).length,
    menuHandlers:Object.keys(menu.store.attrMap).length,
    retained:refs.filter(node => node && !node.isConnected && node.parentNode).length,
    selected:!!menu.mathItem
  };
})())
)JS"));
    const auto state = QJsonDocument::fromJson(cleared.toString().toUtf8()).object();
    QCOMPARE(state.value(QStringLiteral("formulas")).toInt(-1), 0);
    const auto readerText =
        evaluate(runtime.page, QStringLiteral("vxcore.contentContainer.textContent")).toString();
    if (replacement.isEmpty()) {
      QVERIFY(readerText.isEmpty());
      QCOMPARE(evaluate(runtime.page, QStringLiteral("vxcore.contentContainer.childElementCount"))
                   .toInt(),
               0);
    } else {
      QVERIFY(readerText.contains(QStringLiteral("No formulas")));
      QVERIFY(!readerText.contains(QStringLiteral("Inline")));
    }
    if (mathJax) {
      QCOMPARE(state.value(QStringLiteral("tracked")).toInt(-1), 0);
      QCOMPARE(state.value(QStringLiteral("menuItems")).toInt(-1), 0);
      QCOMPARE(state.value(QStringLiteral("menuHandlers")).toInt(-1), 0);
      QCOMPARE(state.value(QStringLiteral("retained")).toInt(-1), 0);
      QVERIFY(!state.value(QStringLiteral("selected")).toBool(true));
    }
  }
}

void TestMarkdownWebJobs::sourceFailuresAndCancellationDrain() {
  Runtime runtime(true);
  QObject failedOwner, nextOwner;
  const auto badGraph =
      submit(runtime.session, QStringLiteral("markdown.previewGraph"),
             preview(QStringLiteral("graph {"), QStringLiteral("dot")), &failedOwner);
  const auto badMermaid =
      submit(runtime.session, QStringLiteral("markdown.previewGraph"),
             preview(QStringLiteral("not a diagram"), QStringLiteral("mermaid")), &failedOwner);
  const auto badWave =
      submit(runtime.session, QStringLiteral("markdown.previewGraph"),
             preview(QStringLiteral("{"), QStringLiteral("wavedrom")), &failedOwner);
  const auto badFlow =
      submit(runtime.session, QStringLiteral("markdown.previewGraph"),
             preview(QStringLiteral("start->missing"), QStringLiteral("flow")), &failedOwner);
  const auto badMath = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                              preview(QStringLiteral("not dollar-delimited math")), &failedOwner);
  const auto next = submit(
      runtime.session, QStringLiteral("markdown.previewGraph"),
      preview(QStringLiteral("digraph { Recovery -> Alive }"), QStringLiteral("dot")), &nextOwner);
  for (const auto &job : {badGraph, badMermaid, badWave, badFlow, badMath}) {
    QTRY_VERIFY_WITH_TIMEOUT(job->finished, 30000);
    QCOMPARE(job->result.status, WebViewJobResult::Status::Error);
    QVERIFY(!job->result.error.isEmpty());
  }
  QTRY_VERIFY_WITH_TIMEOUT(next->finished, 30000);
  QCOMPARE(next->result.status, WebViewJobResult::Status::Success);
  QVERIFY(next->result.value.toObject()
              .value(QStringLiteral("data"))
              .toString()
              .contains(QStringLiteral("Recovery")));

  QSignalSpy dispatched(runtime.session, &WebViewJobSession::jobRequested);
  const auto cancelled = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                preview(QStringLiteral("$\\frac{a}{b}$")), &failedOwner);
  // Cancel after native dispatch, not a merely queued request. The real math
  // handler must drain before another source can reuse the runtime.
  QTRY_COMPARE_WITH_TIMEOUT(dispatched.count(), 1, 10000);
  runtime.session->cancelOwner(&failedOwner);
  QTRY_VERIFY_WITH_TIMEOUT(cancelled->finished, 10000);
  QCOMPARE(cancelled->result.status, WebViewJobResult::Status::Cancelled);
  const auto after = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                            preview(QStringLiteral("$\\sqrt{c}+d$")), &nextOwner);
  QTRY_VERIFY_WITH_TIMEOUT(after->finished, 30000);
  QCOMPARE(after->result.status, WebViewJobResult::Status::Success);
  QVERIFY(inkPixels(image(after->result.value)) > 30);
  QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  QCOMPARE(
      evaluate(runtime.page,
               QStringLiteral("document.getElementById('vx-inplace-preview').childElementCount"))
          .toInt(),
      0);
}

} // namespace tests

int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  tests::TestMarkdownWebJobs test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markdownwebjobs.moc"
