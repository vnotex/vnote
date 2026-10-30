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
#include <QSemaphore>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QXmlStreamReader>

#include <vtextedit/typstfragmentrenderer.h>
#include <vtextedit/typstruntime.h>

#include <cmath>
#include <memory>
#include <utility>

#include <core/servicelocator.h>
#include <gui/services/webviewjobs.h>
#include <gui/services/webviewservice.h>
#include <gui/utils/imageutils.h>
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
                                    QStringLiteral("typst.js"),
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

QString runtimeHtml(const QColor &p_color, const QString &p_mathJaxScript,
                    const QString &p_renderer = QString(), bool p_protected = false) {
  QFile file(webDir() + QStringLiteral("/markdown-viewer-template.html"));
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  auto html = QString::fromUtf8(file.readAll());
  const QJsonObject options{{QStringLiteral("webGraphviz"), true},
                            {QStringLiteral("mathRenderer"), !p_renderer.isEmpty() ? p_renderer
                                                             : p_mathJaxScript.isEmpty()
                                                                 ? QStringLiteral("katex")
                                                                 : QStringLiteral("mathjax")},
                            {QStringLiteral("protectedView"), p_protected},
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
  if (p_protected) {
    // Production protected templates preload CSS; their math worker never uses CSS XHR.
    tags += QStringLiteral("<link rel=\"stylesheet\" href=\"%1\">\n")
                .arg(QUrl::fromLocalFile(webDir() + QStringLiteral("/js/katex/katex.min.css"))
                         .toString()
                         .toHtmlEscaped());
  }
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
  // Declared first: pages, services and their registry all die before native workers.
  std::unique_ptr<vte::TypstRuntime> typstRuntime;
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
                   const QString &p_mathJaxScript = QString(),
                   const QString &p_renderer = QString(), bool p_protected = false,
                   bool p_runtimeAvailable = true) {
    if (p_runtimeAvailable) {
      typstRuntime = std::make_unique<vte::TypstRuntime>();
      services.registerService<vte::TypstRuntime>(typstRuntime.get());
    }
    const auto bootstrap = [this, p_color, p_base, p_mathJaxScript, p_renderer,
                            p_protected](QWebEnginePage *page, QWebChannel *channel) {
      this->page = page;
      adapter = new MarkdownViewerAdapter(services, page);
      adapter->setProtectedView(p_protected);
      channel->registerObject(QStringLiteral("vxAdapter"), adapter);
      page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
      page->setHtml(runtimeHtml(p_color, p_mathJaxScript, p_renderer, p_protected), p_base);
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

// The approved mixed-placement corpus goes through cmark, not hand-authored math HTML.
QString typstFixture() {
  return QStringLiteral(R"MD(# Typst math
Inline: $frac(a^2 + b^2, sqrt(c))$ after.

Before $$sum_(i=1)^n i$$ after.

$$
integral_0^1 x^2 dif x = 1/3
$$

| Mode | Formula |
| --- | --- |
| Inline | $sqrt(x^2 + y^2)$ |
| Display style | $$sum_(i=1)^n i$$ |

Escaped \$ stays text; `$sqrt(x)$` stays code.

Invalid: $frac($

```mathjax
$$frac(1, 2)$$
```
)MD");
}

QJsonObject evaluateObject(QWebEnginePage *p_page, const QString &p_expression) {
  return QJsonDocument::fromJson(
             evaluate(p_page, QStringLiteral("JSON.stringify(%1)").arg(p_expression))
                 .toString()
                 .toUtf8())
      .object();
}

// Observe genuine asynchronous browser operations without replacing renderer callbacks.
QJsonObject awaitObject(QWebEnginePage *p_page, const QString &p_expression) {
  evaluate(
      p_page,
      QStringLiteral("window.typstTestResult = null; Promise.resolve(%1).then("
                     "value => {window.typstTestResult = JSON.stringify(value);}, "
                     "error => {window.typstTestResult = JSON.stringify({error:String(error)});});")
          .arg(p_expression));
  QString result;
  if (!QTest::qWaitFor(
          [&] {
            result = evaluate(p_page, QStringLiteral("window.typstTestResult")).toString();
            return !result.isEmpty();
          },
          30000)) {
    return {{QStringLiteral("error"), QStringLiteral("Browser operation did not settle")}};
  }
  return QJsonDocument::fromJson(result.toUtf8()).object();
}

QImage dataImage(const QJsonValue &p_value) {
  const auto url = p_value.toString().toLatin1();
  if (!url.startsWith("data:image/png;base64,"))
    return QImage();
  return QImage::fromData(QByteArray::fromBase64(url.mid(url.indexOf(',') + 1)), "PNG");
}

bool transparentBorder(const QImage &p_image) {
  if (p_image.isNull())
    return false;
  for (int x = 0; x < p_image.width(); ++x) {
    if (p_image.pixelColor(x, 0).alpha() || p_image.pixelColor(x, p_image.height() - 1).alpha())
      return false;
  }
  for (int y = 0; y < p_image.height(); ++y) {
    if (p_image.pixelColor(0, y).alpha() || p_image.pixelColor(p_image.width() - 1, y).alpha())
      return false;
  }
  return true;
}

int rgbaPixels(const QImage &p_image, const QColor &p_expected) {
  int count = 0;
  for (int y = 0; y < p_image.height(); ++y) {
    for (int x = 0; x < p_image.width(); ++x) {
      const auto color = p_image.pixelColor(x, y);
      if (std::abs(color.red() - p_expected.red()) <= 4 &&
          std::abs(color.green() - p_expected.green()) <= 4 &&
          std::abs(color.blue() - p_expected.blue()) <= 4 &&
          std::abs(color.alpha() - p_expected.alpha()) <= 2)
        ++count;
    }
  }
  return count;
}

// Clipboard computed styles round transform coordinates. Compare painted pixels
// with a small antialias allowance, not byte-identical SVG rasterization paths.
bool sameMathInk(const QImage &p_actual, const QImage &p_expected) {
  if (p_actual.isNull() || p_actual.size() != p_expected.size())
    return false;
  quint64 difference = 0;
  for (int y = 0; y < p_actual.height(); ++y) {
    for (int x = 0; x < p_actual.width(); ++x) {
      const auto actual = p_actual.pixelColor(x, y);
      const auto expected = p_expected.pixelColor(x, y);
      const int aa = actual.alpha(), ea = expected.alpha();
      for (const int delta :
           {std::abs(aa - ea), std::abs(actual.red() * aa / 255 - expected.red() * ea / 255),
            std::abs(actual.green() * aa / 255 - expected.green() * ea / 255),
            std::abs(actual.blue() * aa / 255 - expected.blue() * ea / 255)}) {
        if (delta > 12)
          return false;
        difference += delta;
      }
    }
  }
  return difference <= quint64(p_actual.width()) * p_actual.height() / 5;
}

QString typstBlockMarkup() {
  return QStringLiteral("#let label = [*Markup*]\n= #label\n"
                        "#rect(width: 36pt, height: 18pt)\n\n"
                        "#link(\"https://example.org\")[Visible link]\n");
}

QString typstBlockPages() {
  return QStringLiteral("#set page(width: 120pt, height: 80pt)\n"
                        "#rect(width: 40pt, height: 20pt, fill: rgb(\"#1F539D\"))\n"
                        "#pagebreak()\n"
                        "#rect(width: 40pt, height: 20pt, fill: rgb(\"#A12F59\"))\n");
}

QString typstBlockFence(const QString &p_source) {
  return QStringLiteral("```typst\n") + p_source + QStringLiteral("\n```\n");
}

QJsonObject typstBlockPreview(const QString &p_source, const QString &p_scope) {
  auto args = preview(p_source, QStringLiteral("typst"));
  args.insert(QStringLiteral("scope"), p_scope);
  return args;
}

QJsonObject typstDom(QWebEnginePage *p_page, bool p_blocks = false) {
  return evaluateObject(
      p_page,
      QStringLiteral(R"JS((() => {
    const root = vxcore.contentContainer;
    const roots = Array.from(root.querySelectorAll('svg.%1'));
    const globalIds = new Set();
    let localReferences = 0, valid = true;
    const svgs = roots.map(svg => {
      const nodes = [svg, ...svg.querySelectorAll('*')];
      const ids = new Set(nodes.filter(node => node.id).map(node => node.id));
      for (const node of nodes) {
        if (node.id) {
          if (globalIds.has(node.id)) valid = false;
          globalIds.add(node.id);
        }
        for (const attribute of node.attributes) {
          if (attribute.localName === 'href') {
            ++localReferences;
            if (!attribute.value.startsWith('#') || !ids.has(attribute.value.slice(1))) valid = false;
          }
          if (attribute.value.startsWith('url(#') && attribute.value.endsWith(')')) {
            ++localReferences;
            if (!ids.has(attribute.value.slice(5, -1))) valid = false;
          }
        }
      }
      const box = svg.getBoundingClientRect();
      return {id:svg.id, source:svg.getAttribute('aria-label'), title:svg.querySelector('title')?.textContent,
        svg:new XMLSerializer().serializeToString(svg), width:box.width, height:box.height,
        align:parseFloat(getComputedStyle(svg).verticalAlign),
        fontSize:parseFloat(getComputedStyle(svg).fontSize),
        pages:Number(svg.getAttribute('data-typst-pages')),
        sourceLine:svg.parentElement.getAttribute('data-source-line'),
        sourceLineEnd:svg.parentElement.getAttribute('data-source-line-end'),
        sourceClass:svg.parentElement.classList.contains('source-line'),
        viewWidth:svg.viewBox.baseVal.width, viewHeight:svg.viewBox.baseVal.height};
    });
    return {svgs, valid:valid && roots.every(svg => !!svg.id), localReferences,
      workers:vxcore.numOfOngoingWorkers, text:root.textContent,
      rows:root.querySelectorAll('table tbody tr').length,
      cells:root.querySelectorAll('table td').length,
      tableMath:root.querySelectorAll('table svg.typst-math').length,
      code:Array.from(root.querySelectorAll('code'), node => node.textContent),
      failures:Array.from(root.querySelectorAll('%2')).filter(node => !node.querySelector('svg'))
        .map(node => ({text:node.textContent, title:node.getAttribute('title')})),
      otherEngines:!!window.katex || !!window.MathJax,
      unsafe:!!root.querySelector('svg script, svg image, svg foreignObject, svg style')};
  })())JS")
          .arg(p_blocks ? QStringLiteral("typst-block") : QStringLiteral("typst-math"),
               p_blocks ? QStringLiteral("code.lang-typst") : QStringLiteral(".tex-to-render")));
}

// Decode self-contained SVG with Chromium's real image/canvas path, independently
// of GraphPreviewer and export. Native symbols are deliberately left unmodified in
// the reference input, so this also detects sanitizer-induced glyph clipping.
QJsonObject rasterSvgs(QWebEnginePage *p_page, const QJsonArray &p_svgs) {
  const auto items = QString::fromUtf8(QJsonDocument(p_svgs).toJson(QJsonDocument::Compact));
  return awaitObject(p_page, QStringLiteral(R"JS((async () => {
    const images = [];
    for (const item of %1) {
      const doc = new DOMParser().parseFromString(item.svg, 'image/svg+xml');
      if (doc.querySelector('parsererror') || doc.documentElement.localName !== 'svg') throw Error('Invalid SVG');
      const svg = doc.documentElement;
      // Match the vector viewport, not a low-resolution decoded bitmap enlarged afterward.
      const width = Math.ceil(item.width * 2), height = Math.ceil(item.height * 2);
      svg.setAttribute('width', width + 'px'); svg.setAttribute('height', height + 'px');
      svg.style.width = width + 'px'; svg.style.height = height + 'px';
      const url = URL.createObjectURL(new Blob([new XMLSerializer().serializeToString(svg)],
        {type:'image/svg+xml'}));
      try {
        const image = new Image();
        await new Promise((resolve, reject) => {image.onload = resolve; image.onerror = reject; image.src = url;});
        const canvas = document.createElement('canvas');
        canvas.width = width; canvas.height = height;
        canvas.getContext('2d').drawImage(image, 0, 0);
        images.push(canvas.toDataURL('image/png'));
      } finally {URL.revokeObjectURL(url);}
    }
    return {images};
  })())JS")
                                 .arg(items));
}

QJsonObject scopedPreview(const QString &p_source, const QString &p_scope) {
  auto args = preview(p_source);
  args.insert(QStringLiteral("scope"), p_scope);
  return args;
}

// Same real queue-boundary gate as test_typsteditor: no sleeps, fake replies or
// pathological formulas. RAII unblocks workers even when a QVERIFY returns early.
class MathRuntimeGate {
public:
  explicit MathRuntimeGate(vte::TypstRuntime &p_runtime) : state(std::make_shared<State>()) {
    for (const auto &name :
         {QStringLiteral("typst-compile-0"), QStringLiteral("typst-compile-1")}) {
      auto *thread = p_runtime.findChild<QThread *>(name);
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
  ~MathRuntimeGate() { release(); }
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
  const std::shared_ptr<State> state;
  int scheduled = 0;
};

bool nativeMathPending(const Runtime &p_runtime) {
  const auto *renderer = p_runtime.adapter->findChild<vte::TypstFragmentRenderer *>();
  return renderer && renderer->isRendering();
}

} // namespace

class TestMarkdownWebJobs : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void htmlCodeBlocks();
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
  void typstReaderAndPreviews();
  void typstCancellationAndIsolation();
  void typstExportAndCrossCopy();
  void typstProtectedAndUnavailable();
  void typstAdapterBoundary();
  void typstProtectedSvgRaster();
  void typstBlocksReaderAndPreviews();
  void typstBlocksCancellationAndIsolation();
  void typstBlocksExportAndCrossCopy();
};

void TestMarkdownWebJobs::typstAdapterBoundary() {
  vte::TypstRuntime runtime;
  QVERIFY(runtime.isAvailable());
  ServiceLocator services;
  services.registerService<vte::TypstRuntime>(&runtime);
  const QString color = QStringLiteral("#13579B80");
  const QString formula = QStringLiteral("sum_(i=1)^n i");
  const QString scope = QStringLiteral("preview:editor-a");
  const auto countId = [](const QSignalSpy &p_spy, const QString &p_id) {
    int count = 0;
    for (const auto &reply : p_spy) {
      if (reply.at(0).toString() == p_id)
        ++count;
    }
    return count;
  };
  const auto resultFor = [](const QSignalSpy &p_spy, const QString &p_id) {
    for (const auto &reply : p_spy) {
      if (reply.at(0).toString() == p_id)
        return reply.at(1).toJsonObject();
    }
    return QJsonObject();
  };
  const auto failed = [](const QJsonObject &p_result) {
    return p_result.size() == 6 && p_result.value(QStringLiteral("pageCount")).toInt(-1) == 0 &&
           p_result.value(QStringLiteral("svg")).isString() &&
           p_result.value(QStringLiteral("svg")).toString().isEmpty() &&
           p_result.value(QStringLiteral("widthPt")).isDouble() &&
           p_result.value(QStringLiteral("widthPt")).toDouble() == 0 &&
           p_result.value(QStringLiteral("heightPt")).isDouble() &&
           p_result.value(QStringLiteral("heightPt")).toDouble() == 0 &&
           p_result.value(QStringLiteral("baselinePt")).isDouble() &&
           p_result.value(QStringLiteral("baselinePt")).toDouble() == 0 &&
           !p_result.value(QStringLiteral("error")).toString().isEmpty();
  };
  const auto succeeded = [](const QJsonObject &p_result) {
    const auto width = p_result.value(QStringLiteral("widthPt")).toDouble();
    const auto height = p_result.value(QStringLiteral("heightPt")).toDouble();
    return p_result.size() == 6 && p_result.value(QStringLiteral("pageCount")).toInt() == 1 &&
           p_result.value(QStringLiteral("error")).isString() &&
           p_result.value(QStringLiteral("error")).toString().isEmpty() &&
           !p_result.value(QStringLiteral("svg")).toString().isEmpty() &&
           p_result.value(QStringLiteral("svg")).toString().toUtf8().size() <= 8 * 1024 * 1024 &&
           std::isfinite(width) && width > 0 && width <= 16384 && std::isfinite(height) &&
           height > 0 && height <= 16384 &&
           p_result.value(QStringLiteral("baselinePt")).isDouble() &&
           std::isfinite(p_result.value(QStringLiteral("baselinePt")).toDouble());
  };

  {
    MarkdownViewerAdapter adapter(services);
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstReady);
    QVERIFY(replies.isValid());
    adapter.renderTypstMath(QStringLiteral("text"), scope, formula, false, color);
    // Even an invalid duplicate must not steal the first caller's completion.
    adapter.renderTypstMath(QStringLiteral("text"), QString(), QString(), true, QString());
    adapter.renderTypstMath(QStringLiteral("display"), scope, formula, true, color);
    QCOMPARE(replies.count(), 0);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 2; }, 30000));
    QCOMPARE(countId(replies, QStringLiteral("text")), 1);
    QCOMPARE(replies.at(0).at(0).toString(), QStringLiteral("text"));
    QCOMPARE(replies.at(1).at(0).toString(), QStringLiteral("display"));
    const auto text = resultFor(replies, QStringLiteral("text"));
    const auto display = resultFor(replies, QStringLiteral("display"));
    QVERIFY2(succeeded(text), qPrintable(text.value(QStringLiteral("error")).toString()));
    QVERIFY2(succeeded(display), qPrintable(display.value(QStringLiteral("error")).toString()));
    QVERIFY(display.value(QStringLiteral("heightPt")).toDouble() >
            text.value(QStringLiteral("heightPt")).toDouble());
    // Inspect the real SVG XML: QtSvg cannot rasterize CSS eight-digit alpha colors.
    QXmlStreamReader xml(text.value(QStringLiteral("svg")).toString());
    bool root = false;
    bool paint = false;
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement())
        continue;
      if (!root) {
        QCOMPARE(xml.name().toString(), QStringLiteral("svg"));
        root = true;
      }
      for (const auto &attribute : xml.attributes()) {
        if ((attribute.name() == QLatin1String("fill") ||
             attribute.name() == QLatin1String("stroke")) &&
            attribute.value().toString().compare(color, Qt::CaseInsensitive) == 0) {
          paint = true;
        }
      }
    }
    QVERIFY(!xml.hasError());
    QVERIFY(root);
    QVERIFY(paint); // Distinguishes RGBA from Qt's ARGB interpretation, including alpha.
  }

  {
    ServiceLocator missingRuntime;
    MarkdownViewerAdapter missingServices;
    MarkdownViewerAdapter unavailable(missingRuntime);
    MarkdownViewerAdapter protectedAdapter(services);
    protectedAdapter.setProtectedView(true);
    for (auto *adapter : {&missingServices, &unavailable, &protectedAdapter}) {
      QSignalSpy replies(adapter, &MarkdownViewerAdapter::typstReady);
      adapter->renderTypstMath(QStringLiteral("unavailable"), scope, formula, false, color);
      QVERIFY(QTest::qWaitFor([&] { return replies.count() == 1; }, 30000));
      QVERIFY(failed(replies.first().at(1).toJsonObject()));
    }
  }

  {
    MarkdownViewerAdapter adapter(services);
    adapter.setRenderedHtml(QStringLiteral("7"), QStringLiteral("<p>reader</p>"));
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstReady);
    struct Invalid {
      QString id;
      QString scope;
      QString source;
      QString color;
    };
    const QList<Invalid> invalid = {
        {QString(), scope, formula, color},
        {QString(129, QLatin1Char('i')), scope, formula, color},
        {QStringLiteral("empty-scope"), QString(), formula, color},
        {QStringLiteral("long-scope"), QStringLiteral("preview:") + QString(249, QLatin1Char('s')),
         formula, color},
        {QStringLiteral("unknown-scope"), QStringLiteral("editor:a"), formula, color},
        {QStringLiteral("empty-preview"), QStringLiteral("preview:"), formula, color},
        {QStringLiteral("empty-reader"), QStringLiteral("reader:"), formula, color},
        {QStringLiteral("stale-reader"), QStringLiteral("reader:6"), formula, color},
        {QStringLiteral("future-reader"), QStringLiteral("reader:8"), formula, color},
        {QStringLiteral("padded-reader"), QStringLiteral("reader:07"), formula, color},
        {QStringLiteral("empty-color"), scope, formula, QString()},
        {QStringLiteral("named-color"), scope, formula, QStringLiteral("red")},
        {QStringLiteral("rgb-color"), scope, formula, QStringLiteral("#13579B")},
        {QStringLiteral("invalid-color"), scope, formula, QStringLiteral("#13579BG0")},
        {QStringLiteral("blank"), scope, QStringLiteral(" \r\n\t"), color},
        {QStringLiteral("nul"), scope, QStringLiteral("x") + QChar(0), color},
        {QStringLiteral("surrogate"), scope, QString(QChar(0xd800)), color},
        {QStringLiteral("large"), scope, QString(65537, QLatin1Char('x')), color},
        {QStringLiteral("utf8-large"), scope, QString(32769, QChar(0x03b1)), color}};
    for (const auto &request : invalid) {
      adapter.renderTypstMath(request.id, request.scope, request.source, false, request.color);
    }
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == invalid.size(); }, 30000));
    for (const auto &request : invalid) {
      QCOMPARE(countId(replies, request.id), 1);
      QVERIFY2(failed(resultFor(replies, request.id)), qPrintable(request.id));
    }
    const QString maxId(128, QLatin1Char('i'));
    const QString maxScope = QStringLiteral("preview:") + QString(248, QLatin1Char('s'));
    const QString maxSource = QStringLiteral("x") + QString(65535, QLatin1Char(' '));
    adapter.renderTypstMath(maxId, maxScope, maxSource, false, color);
    adapter.renderTypstMath(QStringLiteral("current-reader"), QStringLiteral("reader:7"), formula,
                            true, color);
    adapter.renderTypstMath(QStringLiteral("syntax"), scope, QStringLiteral("frac("), false, color);
    adapter.renderTypstMath(QStringLiteral("after-error"), scope, QStringLiteral("sqrt(x)"), false,
                            color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == invalid.size() + 4; }, 30000));
    QVERIFY(succeeded(resultFor(replies, maxId)));
    QVERIFY(succeeded(resultFor(replies, QStringLiteral("current-reader"))));
    QVERIFY(failed(resultFor(replies, QStringLiteral("syntax"))));
    QVERIFY(succeeded(resultFor(replies, QStringLiteral("after-error"))));
  }

  {
    MarkdownViewerAdapter adapter(services);
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstReady);
    const QString fullSource = QStringLiteral("x") + QString(65535, QLatin1Char(' '));
    const QString keep = QStringLiteral("preview:keep");
    const QString retire = QStringLiteral("preview:retire");
    QStringList expectedOrder{QStringLiteral("active")};
    adapter.renderTypstMath(expectedOrder.first(), keep, formula, false, color);
    // No event processing: exactly one active and 32 waiting, exactly 2MiB queued UTF-8.
    for (int i = 0; i < 32; ++i) {
      const auto id = QStringLiteral("queued-%1").arg(i);
      adapter.renderTypstMath(id, i % 2 ? keep : retire, fullSource, false, color);
      if (i % 2)
        expectedOrder.append(id);
    }
    adapter.renderTypstMath(QStringLiteral("overflow"), keep, QStringLiteral("x"), false, color);
    // A duplicate waiting ID also cannot consume capacity or receive a failure.
    adapter.renderTypstMath(QStringLiteral("queued-1"), keep, formula, true, color);
    adapter.cancelTypst(QStringLiteral("preview:not-owned"));
    adapter.cancelTypst(retire);
    adapter.cancelTypst(retire); // Idempotent, no second cancellations.
    for (int i = 0; i < 16; ++i) {
      const auto id = QStringLiteral("refill-%1").arg(i);
      adapter.renderTypstMath(id, keep, fullSource, false, color);
      expectedOrder.append(id);
    }
    adapter.renderTypstMath(QStringLiteral("refill-overflow"), keep, formula, false, color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 51; }, 30000));
    QVERIFY(failed(resultFor(replies, QStringLiteral("overflow"))));
    QVERIFY(failed(resultFor(replies, QStringLiteral("refill-overflow"))));
    for (int i = 0; i < 32; ++i) {
      const auto id = QStringLiteral("queued-%1").arg(i);
      QCOMPARE(countId(replies, id), 1);
      if (!(i % 2))
        QVERIFY(failed(resultFor(replies, id)));
    }
    QStringList actualOrder;
    for (const auto &reply : replies) {
      if (succeeded(reply.at(1).toJsonObject()))
        actualOrder.append(reply.at(0).toString());
    }
    QCOMPARE(actualOrder, expectedOrder);
    // Completion as well as cancellation must release the queued byte accounting.
    for (int i = 0; i < 33; ++i) {
      adapter.renderTypstMath(QStringLiteral("after-drain-%1").arg(i), keep, fullSource, false,
                              color);
    }
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 84; }, 30000));
    for (int i = 0; i < 33; ++i) {
      QVERIFY(succeeded(resultFor(replies, QStringLiteral("after-drain-%1").arg(i))));
    }
  }

  {
    MarkdownViewerAdapter adapter(services);
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstReady);
    adapter.setRenderedHtml(QStringLiteral("1"), QStringLiteral("<p>old</p>"));
    adapter.renderTypstMath(QStringLiteral("old-active"), QStringLiteral("reader:1"), formula,
                            false, color);
    adapter.renderTypstMath(QStringLiteral("old-queued"), QStringLiteral("reader:1"), formula,
                            false, color);
    adapter.renderTypstMath(QStringLiteral("sibling"), scope, formula, false, color);
    adapter.setRenderedHtml(QStringLiteral("2"), QStringLiteral("<p>new</p>"));
    adapter.renderTypstMath(QStringLiteral("stale"), QStringLiteral("reader:1"), formula, false,
                            color);
    adapter.renderTypstMath(QStringLiteral("new-reader"), QStringLiteral("reader:2"), formula,
                            false, color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 5; }, 30000));
    for (const auto &id :
         {QStringLiteral("old-active"), QStringLiteral("old-queued"), QStringLiteral("stale")}) {
      QCOMPARE(countId(replies, id), 1);
      QVERIFY(failed(resultFor(replies, id)));
    }
    QVERIFY(succeeded(resultFor(replies, QStringLiteral("sibling"))));
    QVERIFY(succeeded(resultFor(replies, QStringLiteral("new-reader"))));
    // Conversely, replacing a reader must not cancel an already-active preview.
    adapter.renderTypstMath(QStringLiteral("active-preview"), scope, formula, false, color);
    adapter.renderTypstMath(QStringLiteral("queued-reader"), QStringLiteral("reader:2"), formula,
                            false, color);
    adapter.setRenderedHtml(QStringLiteral("3"), QStringLiteral("<p>third</p>"));
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 7; }, 30000));
    QVERIFY(succeeded(resultFor(replies, QStringLiteral("active-preview"))));
    QVERIFY(failed(resultFor(replies, QStringLiteral("queued-reader"))));
  }

  for (const bool protect : {false, true}) {
    MarkdownViewerAdapter adapter(services);
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstReady);
    adapter.setRenderedHtml(QStringLiteral("1"), QStringLiteral("<p>reader</p>"));
    adapter.renderTypstMath(QStringLiteral("retired-active"), scope, formula, false, color);
    adapter.renderTypstMath(QStringLiteral("retired-queued"), QStringLiteral("preview:other"),
                            formula, false, color);
    adapter.renderTypstMath(QStringLiteral("retired-reader"), QStringLiteral("reader:1"), formula,
                            false, color);
    if (protect)
      adapter.setProtectedView(true);
    else
      adapter.reset();
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 3; }, 30000));
    for (const auto &reply : replies)
      QVERIFY(failed(reply.at(1).toJsonObject()));
    if (protect) {
      adapter.renderTypstMath(QStringLiteral("still-protected"), scope, formula, false, color);
      QVERIFY(QTest::qWaitFor([&] { return replies.count() == 4; }, 30000));
      QVERIFY(failed(resultFor(replies, QStringLiteral("still-protected"))));
      adapter.setProtectedView(false);
    } else {
      adapter.renderTypstMath(QStringLiteral("reader-after-reset"), QStringLiteral("reader:1"),
                              formula, false, color);
      QVERIFY(QTest::qWaitFor([&] { return replies.count() == 4; }, 30000));
      QVERIFY(failed(resultFor(replies, QStringLiteral("reader-after-reset"))));
    }
    adapter.renderTypstMath(QStringLiteral("retired-active"), scope, QStringLiteral("sqrt(y)"),
                            false, color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 5; }, 30000));
    QCOMPARE(countId(replies, QStringLiteral("retired-active")), 2);
    QVERIFY(succeeded(replies.last().at(1).toJsonObject()));
  }

  for (const bool cancel : {false, true}) {
    MarkdownViewerAdapter adapter(services);
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstReady);
    bool replaced = false;
    connect(&adapter, &MarkdownViewerAdapter::typstReady, &adapter,
            [&](const QString &id, const QJsonObject &) {
              if (id != QStringLiteral("reentrant") || replaced)
                return;
              replaced = true;
              adapter.renderTypstMath(id, scope, QStringLiteral("sqrt(y)"), true, color);
            });
    adapter.renderTypstMath(QStringLiteral("reentrant"), scope, formula, false, color);
    adapter.renderTypstMath(QStringLiteral("other-owner"), QStringLiteral("preview:other"), formula,
                            false, color);
    if (cancel)
      adapter.cancelTypst(scope);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 3; }, 30000));
    QCOMPARE(countId(replies, QStringLiteral("reentrant")), 2);
    QVERIFY(replaced);
    QVERIFY(cancel ? failed(replies.first().at(1).toJsonObject())
                   : succeeded(replies.first().at(1).toJsonObject()));
    QVERIFY(succeeded(resultFor(replies, QStringLiteral("other-owner"))));
    QVERIFY(succeeded(replies.last().at(1).toJsonObject()));
    QCOMPARE(replies.last().at(0).toString(), QStringLiteral("reentrant"));
  }

  for (const bool cancel : {false, true}) {
    auto adapter = std::make_unique<MarkdownViewerAdapter>(services);
    QPointer<MarkdownViewerAdapter> guarded(adapter.get());
    int completions = 0;
    QJsonObject result;
    connect(adapter.get(), &MarkdownViewerAdapter::typstReady, this,
            [&](const QString &, const QJsonObject &p_result) {
              ++completions;
              result = p_result;
              adapter.reset();
            });
    adapter->renderTypstMath(QStringLiteral("delete-on-result"), scope, formula, false, color);
    if (cancel)
      adapter->cancelTypst(scope);
    QVERIFY(QTest::qWaitFor([&] { return guarded.isNull(); }, 30000));
    QCOMPARE(completions, 1);
    QVERIFY(cancel ? failed(result) : succeeded(result));
    // A surviving adapter can finish after destruction, with suppressed callbacks harmless.
    MarkdownViewerAdapter survivor(services);
    QSignalSpy replies(&survivor, &MarkdownViewerAdapter::typstReady);
    survivor.renderTypstMath(QStringLiteral("survivor"), scope, formula, false, color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 1; }, 30000));
    QVERIFY(succeeded(replies.first().at(1).toJsonObject()));
    QCOMPARE(completions, 1);
  }

  {
    int completions = 0;
    auto adapter = std::make_unique<MarkdownViewerAdapter>(services);
    connect(adapter.get(), &MarkdownViewerAdapter::typstReady, this,
            [&](const QString &, const QJsonObject &) { ++completions; });
    adapter->renderTypstMath(QStringLiteral("destroy-active"), scope, formula, false, color);
    adapter->renderTypstMath(QStringLiteral("destroy-queued"), scope, formula, false, color);
    adapter.reset();
    QCOMPARE(completions, 0);
    MarkdownViewerAdapter survivor(services);
    QSignalSpy replies(&survivor, &MarkdownViewerAdapter::typstReady);
    survivor.renderTypstMath(QStringLiteral("after-destruction"), scope, formula, false, color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 1; }, 30000));
    QVERIFY(succeeded(replies.first().at(1).toJsonObject()));
    QCOMPARE(completions, 0);
  }
}

void TestMarkdownWebJobs::initTestCase() {
  for (const auto &file : scripts()) {
    QVERIFY2(QFile::exists(webDir() + QStringLiteral("/js/") + file), qPrintable(file));
  }
}

void TestMarkdownWebJobs::htmlCodeBlocks() {
  struct Case {
    const char *name;
    QString html;
    QString markdown;
  };
  const Case cases[] = {
      {"gutter-is-not-language",
       QStringLiteral(
           "<div class=\"code-block\"><div class=\"line-numbers\">1</div><pre>x</pre></div>"),
       QStringLiteral("```\nx\n```")},
      {"nested-wrapper-language",
       QStringLiteral("<div class=\"code-block\"><div "
                      "class=\"highlight-source-python\"><pre>x</pre></div></div>"),
       QStringLiteral("```python\nx\n```")},
      {"reported-highlighted-pre",
       QStringLiteral(
           "<div class=\"md-code-block md-code-block-light\"><div "
           "class=\"md-code-block-banner-wrap\"><div class=\"md-code-block-banner "
           "md-code-block-banner-lite\"><div class=\"_121d384\"><div class=\"d2a24f03\"><span "
           "class=\"d813de27\">c</span></div><div class=\"d2a24f03 _246a029\"><div "
           "class=\"efa13877\"><div role=\"button\" class=\"ds-button\" tabindex=\"0\"><svg><path "
           "/></svg><span class=\"code-info-button-text\">复制</span></div><div role=\"button\" "
           "class=\"ds-button\" tabindex=\"0\"><svg><path /></svg><span "
           "class=\"code-info-button-text\">下载</span></div></div></div></div></div></"
           "div><pre><span><span class=\"token keyword\">char</span> thread_name<span "
           "class=\"token punctuation\">[</span><span class=\"token number\">16</span><span "
           "class=\"token punctuation\">]</span><span class=\"token "
           "punctuation\">;</span></span>\n<span><span class=\"token "
           "function\">pthread_getname_np</span><span class=\"token punctuation\">(</span><span "
           "class=\"token function\">pthread_self</span><span class=\"token "
           "punctuation\">(</span><span class=\"token punctuation\">)</span><span class=\"token "
           "punctuation\">,</span> thread_name<span class=\"token punctuation\">,</span> <span "
           "class=\"token keyword\">sizeof</span><span class=\"token "
           "punctuation\">(</span>thread_name<span class=\"token punctuation\">)</span><span "
           "class=\"token punctuation\">)</span><span class=\"token "
           "punctuation\">;</span></span>\n<span><span class=\"token function\">printf</span><span "
           "class=\"token punctuation\">(</span><span class=\"token string\">\"Current thread "
           "name: %s\\n\"</span><span class=\"token punctuation\">,</span> thread_name<span "
           "class=\"token punctuation\">)</span><span class=\"token "
           "punctuation\">;</span></span></pre><svg class=\"_9bc997d _33882ae\"><path /></svg><svg "
           "class=\"_9bc997d _28d7e84\"><path /></svg></div>"),
       QStringLiteral(
           "```c\nchar thread_name[16];\npthread_getname_np(pthread_self(), thread_name, "
           "sizeof(thread_name));\nprintf(\"Current thread name: %s\\n\", thread_name);\n```")},
      {"bare-pre-token-spans",
       QStringLiteral(
           "<pre><span>\tif (a_b[0] &lt; 2) {</span>\n<span>    run(\"\\n\");</span>\n}</pre>"),
       QStringLiteral("```\n\tif (a_b[0] < 2) {\n    run(\"\\n\");\n}\n```")},
      {"canonical-pre-code",
       QStringLiteral("<pre><code class=\"language-js\">const x = 1;\n</code></pre>"),
       QStringLiteral("```js\nconst x = 1;\n```")},
      {"class-only-code-block",
       QStringLiteral("<div class=\"code-block language-python\"><span>if ready:</span>\n<span>    "
                      "print(\"ready\")</span></div>"),
       QStringLiteral("```python\nif ready:\n    print(\"ready\")\n```")},
      {"language-with-token-evidence",
       QStringLiteral("<div class=\"language-python\"><span class=\"token keyword\">if</span> "
                      "ready:\n    pass</div>"),
       QStringLiteral("```python\nif ready:\n    pass\n```")},
      {"language-prose-is-not-code",
       QStringLiteral("<div class=\"language-en\">Ordinary English text</div>"),
       QStringLiteral("Ordinary English text")},
      {"class-substring-is-not-code",
       QStringLiteral("<div class=\"decode-block\">Normal <strong>text</strong></div>"),
       QStringLiteral("Normal **text**")},
      {"highlight-prose-is-not-code",
       QStringLiteral("<div class=\"highlight\"><p>Important <strong>text</strong></p></div>"),
       QStringLiteral("Important **text**")},
      {"inline-code-is-not-block", QStringLiteral("<p>Use <code>a_b</code> here</p>"),
       QStringLiteral("Use `a_b` here")},
      {"div-lines-and-empty-line",
       QStringLiteral(
           "<div class=\"sourceCode\"><div>one</div>\n<div><br></div>\n<div>  three</div></div>"),
       QStringLiteral("```\none\n\n  three\n```")},
      {"br-lines", QStringLiteral("<div class=\"codeblock\">a<br>  b<br><br>c</div>"),
       QStringLiteral("```\na\n  b\n\nc\n```")},
      {"language-precedence",
       QStringLiteral("<div class=\"code-block language-c\"><pre data-language=\"python\"><code "
                      "class=\"language-js\">value</code></pre></div>"),
       QStringLiteral("```js\nvalue\n```")},
      {"highlighter-wrapper",
       QStringLiteral("<div class=\"highlight highlight-source-c\">\n<pre>int a_b[2];</pre></div>"),
       QStringLiteral("```c\nint a_b[2];\n```")},
      {"nested-wrappers-once",
       QStringLiteral(
           "<div class=\"code-block\"><div class=\"highlight\"><pre>a_b</pre></div></div>"),
       QStringLiteral("```\na_b\n```")},
      {"embedded-indented-fences", QStringLiteral("<pre>  ```\n````\n</pre>"),
       QStringLiteral("`````\n  ```\n````\n`````")},
      {"whitespace-only-code", QStringLiteral("<pre> \t\n\n</pre>"),
       QStringLiteral("```\n \t\n\n```")},
      {"mixed-container-keeps-prose",
       QStringLiteral("<div class=\"highlight-source-c\"><pre>a_b</pre><p>Keep "
                      "<strong>this</strong></p></div>"),
       QStringLiteral("```\na_b\n```\n\nKeep **this**")},
      {"multiple-snippets-stay-separate",
       QStringLiteral("<div class=\"code-block\"><pre>one</pre><pre>two</pre></div>"),
       QStringLiteral("```\none\n```\n\n```\ntwo\n```")},
      {"outside-controls-preserved", QStringLiteral("<div role=\"button\">Copy</div><pre>x</pre>"),
       QStringLiteral("Copy\n\n```\nx\n```")},
      {"literal-closing-tags",
       QStringLiteral(
           "<div class=\"code-block language-html\">&lt;/pre&gt;\n&lt;!-- comment --&gt;</div>"),
       QStringLiteral("```html\n</pre>\n<!-- comment -->\n```")},
      {"generic-language-banner",
       QStringLiteral("<div class=\"code-block\"><div "
                      "class=\"code-header\"><span>Python</span><button>Copy</button></div><pre>if "
                      "ready:\n    pass</pre></div>"),
       QStringLiteral("```python\nif ready:\n    pass\n```")},
      {"syntax-token-not-chrome",
       QStringLiteral(
           "<div class=\"codeblock\"><span class=\"token header\">header</span> = 1</div>"),
       QStringLiteral("```\nheader = 1\n```")},
      {"weak-code-class-prose", QStringLiteral("<div class=\"code\">Ordinary prose</div>"),
       QStringLiteral("Ordinary prose")},
      {"empty-code-block", QStringLiteral("<pre></pre>"), QStringLiteral("```\n\n```")},
      {"invalid-language-token", QStringLiteral("<pre data-language=\"c&#10;```\">x</pre>"),
       QStringLiteral("```\nx\n```")}};
  // One real runtime covers normalization and the complete production converter.
  Runtime runtime(true, QColor(17, 85, 153), QUrl::fromLocalFile(webDir() + QLatin1Char('/')));
  QObject owner;
  for (const auto &entry : cases) {
    const auto job = submit(runtime.session, QStringLiteral("markdown.htmlToMarkdown"),
                            source(entry.html), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(job->finished, 10000);
    QVERIFY2(job->result.status == WebViewJobResult::Status::Success, entry.name);
    QVERIFY2(job->result.value.toString() == entry.markdown,
             qPrintable(QStringLiteral("%1\nExpected: %2\nActual: %3")
                            .arg(QString::fromUtf8(entry.name), entry.markdown,
                                 job->result.value.toString())));
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
                        QStringLiteral("\\vxjobmacro{EFGH}\\vxjobmacro$"),
                    scopeB),
             &ownerB);
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
  const auto script =
      mathJax ? QUrl::fromLocalFile(QStringLiteral(VNOTE_TEST_MATHJAX_SCRIPT)).toString()
              : QString();
  Runtime runtime(false, QColor(17, 85, 153), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                  script);
  QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
  const auto text = QStringLiteral("# Math\n\nInline $a_1^2+b_1^2=c_1^2$ and $\\sqrt{2}$.\n\n"
                                   "$$\n\\int_0^1 x\\,dx=\\frac{1}{2}\n$$\n\n"
                                   "```mathjax\n$$\n\\sum_{n=1}^{5}n=15\n$$\n```\n");
  const QStringList expectedSources{
      QStringLiteral("a_1^2+b_1^2=c_1^2"), QStringLiteral("\\sqrt{2}"),
      QStringLiteral("\\int_0^1 x\\,dx=\\frac{1}{2}"), QStringLiteral("\\sum_{n=1}^{5}n=15")};
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
)JS"))
                    .toBool());
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

void TestMarkdownWebJobs::typstReaderAndPreviews() {
  const QColor foreground(31, 83, 157);
  for (const bool shared : {true, false}) {
    Runtime runtime(shared, foreground, QUrl::fromLocalFile(webDir() + QLatin1Char('/')), QString(),
                    QStringLiteral("typst"));
    QVERIFY(runtime.typstRuntime->isAvailable());
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    const auto document = runtime.renderDocument(typstFixture());
    QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QVERIFY(runtime.adapter->isDocumentReady());
    QCOMPARE(native.count(), 7);
    const auto dom = typstDom(runtime.page);
    const auto svgs = dom.value(QStringLiteral("svgs")).toArray();
    QCOMPARE(svgs.size(), 6);
    QVERIFY(dom.value(QStringLiteral("valid")).toBool());
    QVERIFY(dom.value(QStringLiteral("localReferences")).toInt() > 6);
    QVERIFY(!dom.value(QStringLiteral("unsafe")).toBool());
    QVERIFY(!dom.value(QStringLiteral("otherEngines")).toBool());
    QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
    QCOMPARE(dom.value(QStringLiteral("rows")).toInt(), 2);
    QCOMPARE(dom.value(QStringLiteral("cells")).toInt(), 4);
    QCOMPARE(dom.value(QStringLiteral("tableMath")).toInt(), 2);
    QVERIFY(dom.value(QStringLiteral("text"))
                .toString()
                .contains(QStringLiteral("Escaped $ stays text;")));
    QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("Before ")));
    QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral(" after.")));
    QVERIFY(dom.value(QStringLiteral("code")).toArray().contains(QStringLiteral("$sqrt(x)$")));
    const auto failures = dom.value(QStringLiteral("failures")).toArray();
    QCOMPARE(failures.size(), 1);
    QVERIFY(failures.first()
                .toObject()
                .value(QStringLiteral("text"))
                .toString()
                .contains(QStringLiteral("frac(")));
    QVERIFY(!failures.first().toObject().value(QStringLiteral("title")).toString().isEmpty());
    const QStringList sources{QStringLiteral("frac(a^2 + b^2, sqrt(c))"),
                              QStringLiteral("sum_(i=1)^n i"),
                              QStringLiteral("integral_0^1 x^2 dif x = 1/3"),
                              QStringLiteral("sqrt(x^2 + y^2)"),
                              QStringLiteral("sum_(i=1)^n i"),
                              QStringLiteral("frac(1, 2)")};
    QJsonArray originals;
    int index = 0;
    for (const auto &reply : native) {
      const auto result = reply.at(1).toJsonObject();
      if (!result.value(QStringLiteral("error")).toString().isEmpty())
        continue;
      QVERIFY(index < svgs.size());
      const auto svg = svgs.at(index).toObject();
      QCOMPARE(svg.value(QStringLiteral("source")).toString().trimmed(), sources.at(index));
      QCOMPARE(svg.value(QStringLiteral("title")).toString().trimmed(), sources.at(index));
      const double scale = svg.value(QStringLiteral("fontSize")).toDouble() / 12;
      for (const auto &dimension : {QStringLiteral("width"), QStringLiteral("height")}) {
        const double actual = svg.value(dimension).toDouble();
        const double expected = result.value(dimension + QStringLiteral("Pt")).toDouble() * scale;
        QVERIFY(actual > 0 && actual < 700);
        QVERIFY(std::abs(actual - expected) < 0.05);
      }
      QVERIFY(std::abs(svg.value(QStringLiteral("viewWidth")).toDouble() -
                       result.value(QStringLiteral("widthPt")).toDouble()) < 0.01);
      QVERIFY(std::abs(svg.value(QStringLiteral("viewHeight")).toDouble() -
                       result.value(QStringLiteral("heightPt")).toDouble()) < 0.01);
      const double align = (result.value(QStringLiteral("baselinePt")).toDouble() -
                            result.value(QStringLiteral("heightPt")).toDouble()) *
                           scale;
      QVERIFY(std::abs(svg.value(QStringLiteral("align")).toDouble() - align) < 0.05);
      originals.append(
          QJsonObject{{QStringLiteral("svg"), result.value(QStringLiteral("svg"))},
                      {QStringLiteral("width"), svg.value(QStringLiteral("width"))},
                      {QStringLiteral("height"), svg.value(QStringLiteral("height"))}});
      ++index;
    }
    QCOMPARE(index, 6);
    const auto liveRaster = rasterSvgs(runtime.page, svgs);
    const auto originalRaster = rasterSvgs(runtime.page, originals);
    QVERIFY2(!liveRaster.contains(QStringLiteral("error")),
             qPrintable(liveRaster.value(QStringLiteral("error")).toString()));
    QVERIFY2(!originalRaster.contains(QStringLiteral("error")),
             qPrintable(originalRaster.value(QStringLiteral("error")).toString()));
    const auto liveImages = liveRaster.value(QStringLiteral("images")).toArray();
    const auto originalImages = originalRaster.value(QStringLiteral("images")).toArray();
    QCOMPARE(liveImages.size(), 6);
    QCOMPARE(originalImages.size(), 6);
    for (int i = 0; i < liveImages.size(); ++i) {
      const auto raster = dataImage(liveImages.at(i));
      QVERIFY(inkPixels(raster, foreground) > 30);
      QVERIFY(transparentBorder(raster));
      QCOMPARE(raster, dataImage(originalImages.at(i)));
    }

    // CSS, not QColor::name(), carries alpha into the actual browser/native round trip.
    evaluate(runtime.page,
             QStringLiteral("vxcore.contentContainer.style.color = 'rgba(31,83,157,%1)'")
                 .arg(QString::number(173.0 / 255, 'g', 16)));
    QVERIFY(
        evaluate(runtime.page, QStringLiteral("getComputedStyle(vxcore.contentContainer).color"))
            .toString()
            .startsWith(QStringLiteral("rgba(31, 83, 157,")));
    const auto fraction = QStringLiteral("$frac(a^2, sqrt(b))$");
    const auto normal = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                               scopedPreview(fraction, QStringLiteral("geometry")), &owner);
    const auto text =
        submit(runtime.session, QStringLiteral("markdown.previewMath"),
               scopedPreview(QStringLiteral("$sum_(i=1)^n i$"), QStringLiteral("text")), &owner);
    const auto display = submit(
        runtime.session, QStringLiteral("markdown.previewMath"),
        scopedPreview(QStringLiteral("$$sum_(i=1)^n i$$"), QStringLiteral("display")), &owner);
    for (const auto &job : {normal, text, display}) {
      QTRY_VERIFY_WITH_TIMEOUT(job->finished, 30000);
      QCOMPARE(job->result.status, WebViewJobResult::Status::Success);
      const auto raster = image(job->result.value);
      QVERIFY(rgbaPixels(raster, QColor(31, 83, 157, 173)) > 20);
      QVERIFY(transparentBorder(raster));
      QVERIFY(raster.width() < 400 && raster.height() < 200);
    }
    QVERIFY(image(display->result.value).height() > image(text->result.value).height());
    const auto baseline = image(normal->result.value);
    const auto baselineData = normal->result.value.toObject();
    for (const auto &pair : {QPair<qreal, qreal>(1, 2), {2, 1}, {1.5, 1.25}, {1, 1}}) {
      auto args = preview(fraction, QString(), pair.first, pair.second, 96);
      args.insert(QStringLiteral("scope"), QStringLiteral("narrow"));
      const auto scaled =
          submit(runtime.session, QStringLiteral("markdown.previewMath"), args, &owner);
      QTRY_VERIFY_WITH_TIMEOUT(scaled->finished, 30000);
      QCOMPARE(scaled->result.status, WebViewJobResult::Status::Success);
      const auto raster = image(scaled->result.value);
      const auto data = scaled->result.value.toObject();
      QVERIFY(rgbaPixels(raster, QColor(31, 83, 157, 173)) > 20);
      QCOMPARE(data.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
      QVERIFY(!data.value(QStringLiteral("needScale")).toBool());
      for (const auto &dimension :
           {QStringLiteral("logicalWidth"), QStringLiteral("logicalHeight")}) {
        QVERIFY(std::abs(data.value(dimension).toInt() -
                         qRound(baselineData.value(dimension).toInt() * pair.first)) <= 1);
      }
      QVERIFY(std::abs(raster.width() - baseline.width() * pair.first * pair.second) <= 3);
      QVERIFY(std::abs(raster.height() - baseline.height() * pair.first * pair.second) <= 3);
      if (pair.first == 1 && pair.second == 1)
        QCOMPARE(raster, baseline);
    }
    if (runtime.reader)
      runtime.reader->hide();
    const auto hidden = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                               scopedPreview(fraction, QStringLiteral("hidden")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(hidden->finished, 30000);
    QCOMPARE(hidden->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(image(hidden->result.value), baseline);
    evaluate(runtime.page, QStringLiteral("vxcore.contentContainer.style.fontSize = '24px'"));
    const auto larger = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                               scopedPreview(fraction, QStringLiteral("larger-font")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(larger->finished, 30000);
    QCOMPARE(larger->result.status, WebViewJobResult::Status::Success);
    QVERIFY(std::abs(image(larger->result.value).width() - baseline.width() * 4.0 / 3) <= 2);
    QVERIFY(std::abs(image(larger->result.value).height() - baseline.height() * 4.0 / 3) <= 2);
    const auto highlight = submit(runtime.session, QStringLiteral("markdown.highlightMath"),
                                  source(QStringLiteral("$$sum_(i=1)^n i$$")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(highlight->finished, 10000);
    QCOMPARE(highlight->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(highlight->result.value.toString(), QString());
    for (const auto &invalid : {QStringLiteral("$frac($"), QStringLiteral("$\\frac{a}{b}$")}) {
      const auto failed = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                 scopedPreview(invalid, QStringLiteral("recovery")), &owner);
      const auto recovered = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                    scopedPreview(fraction, QStringLiteral("recovery")), &owner);
      QTRY_VERIFY_WITH_TIMEOUT(failed->finished && recovered->finished, 30000);
      QCOMPARE(failed->result.status, WebViewJobResult::Status::Error);
      QVERIFY(!failed->result.error.isEmpty());
      QCOMPARE(recovered->result.status, WebViewJobResult::Status::Success);
      QCOMPARE(image(recovered->result.value), image(larger->result.value));
      QVERIFY(!typstDom(runtime.page).value(QStringLiteral("otherEngines")).toBool());
    }
    QCOMPARE(rendered.count(), 1); // Previews never finish or replace the reader's pass.
    QCOMPARE(typstDom(runtime.page).value(QStringLiteral("svgs")).toArray().size(), 6);
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
    QCOMPARE(runtime.page->zoomFactor(), qreal(1));
  }
}

void TestMarkdownWebJobs::typstCancellationAndIsolation() {
  const auto formula = QStringLiteral("$sum_(i=1)^n i$");
  {
    Runtime runtime(true, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("typst"));
    QObject retiring, siblingOwner;
    MathRuntimeGate gate(*runtime.typstRuntime);
    QVERIFY(gate.entered());
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    const auto cancelled = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                  scopedPreview(formula, QStringLiteral("retire")), &retiring);
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QCOMPARE(native.count(), 0); // Work really reached the native facade and is still pending.
    const auto sibling = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                scopedPreview(formula, QStringLiteral("sibling")), &siblingOwner);
    runtime.session->cancelOwner(&retiring);
    QTRY_VERIFY_WITH_TIMEOUT(cancelled->finished, 10000);
    QCOMPARE(cancelled->result.status, WebViewJobResult::Status::Cancelled);
    // Scope release is the worker's real lifecycle API. It must settle the live
    // renderText promise, allowing the generic cancelled job to drain before B.
    evaluate(runtime.page,
             QStringLiteral("vxcore.getWorker('typst').cancelScope('preview:retire')"));
    QTRY_COMPARE_WITH_TIMEOUT(native.count(), 1, 10000);
    QVERIFY(
        !native.first().at(1).toJsonObject().value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QVERIFY(!sibling->finished);
    gate.release();
    QTRY_VERIFY_WITH_TIMEOUT(sibling->finished, 30000);
    QCOMPARE(sibling->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(sibling->result.value), QColor(31, 83, 157)) > 30);
    const auto released = submit(runtime.session, QStringLiteral("markdown.releaseScope"),
                                 {{QStringLiteral("scope"), QStringLiteral("retire")}}, &retiring);
    const auto reused = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                               scopedPreview(formula, QStringLiteral("retire")), &retiring);
    QTRY_VERIFY_WITH_TIMEOUT(released->finished && reused->finished, 30000);
    QCOMPARE(released->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(reused->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(image(reused->result.value), image(sibling->result.value));
    QCOMPARE(native.count(), 3);
    QSet<QString> ids;
    for (const auto &reply : native)
      ids.insert(reply.at(0).toString());
    QCOMPARE(ids.size(), 3);
    QCOMPARE(cancelled->result.status, WebViewJobResult::Status::Cancelled);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
  }

  {
    Runtime runtime(false, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("typst"));
    QObject siblingOwner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QVERIFY(runtime.renderDocument(QStringLiteral("# Ready")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    rendered.clear();
    MathRuntimeGate gate(*runtime.typstRuntime);
    QVERIFY(gate.entered());
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    QVERIFY(runtime.renderDocument(QStringLiteral("Old $sqrt(x)$ document")).error.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QCOMPARE(native.count(), 0);
    const auto sibling =
        submit(runtime.session, QStringLiteral("markdown.previewMath"),
               scopedPreview(formula, QStringLiteral("surviving-editor")), &siblingOwner);
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        1, 10000);
    QVERIFY(runtime.renderDocument(QStringLiteral("Current $sqrt(y)$ document")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("vxcore.documentGeneration")).toString(),
        runtime.adapter->documentGeneration(), 10000);
    QCOMPARE(rendered.count(), 0);
    gate.release();
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(sibling->finished, 30000);
    QCOMPARE(sibling->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(sibling->result.value)) > 30);
    const auto dom = typstDom(runtime.page);
    QCOMPARE(dom.value(QStringLiteral("svgs")).toArray().size(), 1);
    QCOMPARE(dom.value(QStringLiteral("svgs"))
                 .toArray()
                 .first()
                 .toObject()
                 .value(QStringLiteral("source"))
                 .toString(),
             QStringLiteral("sqrt(y)"));
    QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("Current")));
    QVERIFY(!dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("Old")));
    QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
    QCOMPARE(native.count(), 3);
    int successes = 0, failures = 0;
    for (const auto &reply : native) {
      if (reply.at(1).toJsonObject().value(QStringLiteral("error")).toString().isEmpty())
        ++successes;
      else
        ++failures;
    }
    QCOMPARE(successes, 2);
    QCOMPARE(failures, 1);
    // Empty and no-math passes also complete; request counters never reset with content.
    QVERIFY(runtime.renderDocument(QString()).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 2, 10000);
    QVERIFY(runtime.renderDocument(QStringLiteral("Plain without math")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 3, 10000);
    QVERIFY(runtime.renderDocument(QStringLiteral("$sqrt(y)$ $sqrt(y)$")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 4, 30000);
    const auto repeated = typstDom(runtime.page);
    QVERIFY(repeated.value(QStringLiteral("valid")).toBool());
    QCOMPARE(repeated.value(QStringLiteral("svgs")).toArray().size(), 2);
    QSet<QString> ids;
    for (const auto &reply : native)
      ids.insert(reply.at(0).toString());
    QCOMPARE(ids.size(), native.count());
  }

  for (const bool protect : {false, true}) {
    Runtime runtime(false, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("typst"));
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QVERIFY(runtime.renderDocument(QStringLiteral("Ready")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    MathRuntimeGate gate(*runtime.typstRuntime);
    QVERIFY(gate.entered());
    const auto pending = submit(runtime.session, QStringLiteral("markdown.previewMath"),
                                scopedPreview(formula, QStringLiteral("live-preview")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QVERIFY(runtime.renderDocument(QStringLiteral("Pending $sqrt(x)$ reader")).error.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(runtime.page,
                 QStringLiteral("vxcore.ongoingWorkers.has('math') && "
                                "vxcore.contentContainer.textContent.includes('Pending')"))
            .toBool(),
        10000);
    if (protect)
      runtime.adapter->setProtectedView(true);
    else
      runtime.adapter->reset();
    // Neither settlement may depend on the outstanding native callback returning.
    QTRY_VERIFY_WITH_TIMEOUT(pending->finished, 10000);
    QCOMPARE(pending->result.status, WebViewJobResult::Status::Error);
    QVERIFY(!pending->result.error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
    QCOMPARE(typstDom(runtime.page).value(QStringLiteral("svgs")).toArray().size(), 0);
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
    gate.release();
    if (protect)
      runtime.adapter->setProtectedView(false);
    const auto recovery =
        submit(runtime.session, QStringLiteral("markdown.previewMath"),
               scopedPreview(QStringLiteral("$sqrt(y)$"), QStringLiteral("live-preview")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(recovery->finished, 30000);
    QCOMPARE(recovery->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(recovery->result.value)) > 20);
    QCOMPARE(typstDom(runtime.page).value(QStringLiteral("svgs")).toArray().size(), 0);
    QCOMPARE(pending->result.status, WebViewJobResult::Status::Error);
  }
}

void TestMarkdownWebJobs::typstExportAndCrossCopy() {
  const QColor foreground(31, 83, 157);
  Runtime runtime(false, foreground, QUrl::fromLocalFile(webDir() + QLatin1Char('/')), QString(),
                  QStringLiteral("typst"));
  QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
  QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
  QSignalSpy saved(runtime.adapter, &MarkdownViewerAdapter::contentReady);
  QSignalSpy copied(runtime.adapter, &MarkdownViewerAdapter::crossCopyReady);
  QSignalSpy exported(runtime.adapter, &MarkdownViewerAdapter::pdfRenderReady);
  QVERIFY(runtime.renderDocument(typstFixture()).error.isEmpty());
  QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
  const auto live = typstDom(runtime.page).value(QStringLiteral("svgs")).toArray();
  QCOMPARE(live.size(), 6);
  QJsonArray originalSvg;
  for (const auto &reply : native) {
    const auto result = reply.at(1).toJsonObject();
    if (result.value(QStringLiteral("error")).toString().isEmpty()) {
      originalSvg.append(result.value(QStringLiteral("svg")));
    }
  }
  QCOMPARE(originalSvg.size(), 6);
  runtime.adapter->saveContent();
  QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
  const auto html = saved.first().at(2).toString();
  QVERIFY(!html.isEmpty());
  const auto jsonHtml = [](const QString &p_html) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("html"), p_html}})
                                 .toJson(QJsonDocument::Compact));
  };
  const auto serialized = evaluateObject(runtime.page, QStringLiteral(R"JS((() => {
    const doc = new DOMParser().parseFromString(%1.html, 'text/html');
    return {svgs:Array.from(doc.querySelectorAll('svg.typst-math'), svg => new XMLSerializer().serializeToString(svg)),
      images:doc.querySelectorAll('img').length, tableMath:doc.querySelectorAll('table svg.typst-math').length,
      external:Array.from(doc.querySelectorAll('svg *')).some(node => Array.from(node.attributes).some(attribute =>
        attribute.localName === 'href' && !attribute.value.startsWith('#'))),
      unsafe:!!doc.querySelector('svg script, svg image, svg foreignObject, svg style')};
  })())JS")
                                                           .arg(jsonHtml(html)));
  QCOMPARE(serialized.value(QStringLiteral("svgs")).toArray().size(), 6);
  QCOMPARE(serialized.value(QStringLiteral("images")).toInt(-1), 0);
  QCOMPARE(serialized.value(QStringLiteral("tableMath")).toInt(), 2);
  QVERIFY(!serialized.value(QStringLiteral("external")).toBool());
  QVERIFY(!serialized.value(QStringLiteral("unsafe")).toBool());
  QJsonArray standalone;
  for (int i = 0; i < live.size(); ++i) {
    auto svg = live.at(i).toObject();
    svg.insert(QStringLiteral("svg"), serialized.value(QStringLiteral("svgs")).toArray().at(i));
    standalone.append(svg);
  }
  const auto standaloneRaster = rasterSvgs(runtime.page, standalone);
  QVERIFY2(!standaloneRaster.contains(QStringLiteral("error")),
           qPrintable(standaloneRaster.value(QStringLiteral("error")).toString()));
  QCOMPARE(standaloneRaster.value(QStringLiteral("images")).toArray().size(), 6);
  for (const auto &url : standaloneRaster.value(QStringLiteral("images")).toArray()) {
    QVERIFY(inkPixels(dataImage(url), foreground) > 30);
  }

  const auto imagesInHtml = [&](const QString &p_html) {
    return evaluateObject(runtime.page, QStringLiteral(R"JS((() => {
      const doc = new DOMParser().parseFromString(%1.html, 'text/html');
      return {svgs:doc.querySelectorAll('svg').length,
        images:Array.from(doc.querySelectorAll('img'), img => {
          const align = img.style.verticalAlign;
          return {src:img.src, width:parseFloat(img.style.width), height:parseFloat(img.style.height),
            align:parseFloat(align) * (align.endsWith('em') ? 18 : 1)};
        }), rows:doc.querySelectorAll('table tbody tr').length,
        tableImages:doc.querySelectorAll('table img').length,
        failed:Array.from(doc.querySelectorAll('.tex-to-render')).some(node =>
          node.textContent.includes('frac(') && !!node.getAttribute('title'))};
    })())JS")
                                            .arg(jsonHtml(p_html)));
  };

  // Exercise the actual native -> shipped CrossCopy -> native result route, not a
  // hand-picked conversion helper. Copying the table must produce only its two formulas.
  for (const bool tableOnly : {false, true}) {
    const auto fragment =
        tableOnly
            ? evaluate(runtime.page,
                       QStringLiteral("vxcore.contentContainer.querySelector('table').outerHTML"))
                  .toString()
            : html;
    const int count = tableOnly ? 2 : 6;
    const int offset = tableOnly ? 3 : 0;
    const quint64 id = tableOnly ? 102 : 101;
    runtime.adapter->crossCopyRequested(id, 7, QStringLiteral("Evernote"),
                                        QUrl::fromLocalFile(webDir() + QLatin1Char('/')).toString(),
                                        fragment);
    QTRY_COMPARE_WITH_TIMEOUT(copied.count(), tableOnly ? 2 : 1, 30000);
    QCOMPARE(copied.last().at(0).toULongLong(), id);
    QCOMPARE(copied.last().at(1).toULongLong(), quint64(7));
    const auto result = imagesInHtml(copied.last().at(2).toString());
    const auto images = result.value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), count);
    QCOMPARE(result.value(QStringLiteral("svgs")).toInt(-1), 0);
    QCOMPARE(result.value(QStringLiteral("tableImages")).toInt(), 2);
    QCOMPARE(result.value(QStringLiteral("rows")).toInt(), 2);
    if (!tableOnly)
      QVERIFY(result.value(QStringLiteral("failed")).toBool());
    QJsonArray references;
    for (int i = 0; i < count; ++i) {
      const auto png = images.at(i).toObject();
      const auto raster = dataImage(png.value(QStringLiteral("src")));
      QVERIFY(inkPixels(raster, foreground) > 30);
      QVERIFY(transparentBorder(raster));
      const auto svg = live.at(i + offset).toObject();
      QVERIFY(std::abs(png.value(QStringLiteral("width")).toDouble() -
                       svg.value(QStringLiteral("width")).toDouble()) < 0.05);
      QVERIFY(std::abs(png.value(QStringLiteral("height")).toDouble() -
                       svg.value(QStringLiteral("height")).toDouble()) < 0.05);
      QVERIFY(std::abs(png.value(QStringLiteral("align")).toDouble() -
                       svg.value(QStringLiteral("align")).toDouble()) < 0.05);
      references.append(QJsonObject{{QStringLiteral("svg"), originalSvg.at(i + offset)},
                                    {QStringLiteral("width"), raster.width() / 2.0},
                                    {QStringLiteral("height"), raster.height() / 2.0}});
    }
    const auto reference = rasterSvgs(runtime.page, references);
    QVERIFY2(!reference.contains(QStringLiteral("error")),
             qPrintable(reference.value(QStringLiteral("error")).toString()));
    const auto referenceImages = reference.value(QStringLiteral("images")).toArray();
    QCOMPARE(referenceImages.size(), count);
    for (int i = 0; i < count; ++i) {
      const auto actual = dataImage(images.at(i).toObject().value(QStringLiteral("src")));
      const auto expected = dataImage(referenceImages.at(i));
      QVERIFY2(sameMathInk(actual, expected),
               qPrintable(QStringLiteral("Copied formula %1: %2x%3/%4 versus %5x%6/%7")
                              .arg(i)
                              .arg(actual.width())
                              .arg(actual.height())
                              .arg(actual.format())
                              .arg(expected.width())
                              .arg(expected.height())
                              .arg(expected.format())));
    }
    QCOMPARE(typstDom(runtime.page).value(QStringLiteral("svgs")).toArray().size(), 6);
  }

  evaluate(runtime.page, QStringLiteral("vxcore.prepareForExport({rasterizeMath:true})"));
  QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 30000);
  runtime.adapter->saveContent();
  QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, 10000);
  const auto result = imagesInHtml(saved.last().at(2).toString());
  const auto images = result.value(QStringLiteral("images")).toArray();
  QCOMPARE(images.size(), 6);
  QCOMPARE(result.value(QStringLiteral("svgs")).toInt(-1), 0);
  QCOMPARE(result.value(QStringLiteral("tableImages")).toInt(), 2);
  QCOMPARE(result.value(QStringLiteral("rows")).toInt(), 2);
  QVERIFY(result.value(QStringLiteral("failed")).toBool());
  QJsonArray references;
  for (int i = 0; i < images.size(); ++i) {
    const auto png = images.at(i).toObject();
    const auto raster = dataImage(png.value(QStringLiteral("src")));
    QVERIFY(inkPixels(raster, foreground) > 30);
    QVERIFY(transparentBorder(raster));
    const auto svg = live.at(i).toObject();
    QCOMPARE(png.value(QStringLiteral("width")).toInt(),
             static_cast<int>(std::ceil(svg.value(QStringLiteral("width")).toDouble())));
    QCOMPARE(png.value(QStringLiteral("height")).toInt(),
             static_cast<int>(std::ceil(svg.value(QStringLiteral("height")).toDouble())));
    QVERIFY(std::abs(png.value(QStringLiteral("align")).toDouble() -
                     svg.value(QStringLiteral("align")).toDouble()) < 0.05);
    references.append(QJsonObject{{QStringLiteral("svg"), originalSvg.at(i)},
                                  {QStringLiteral("width"), raster.width() / 2.0},
                                  {QStringLiteral("height"), raster.height() / 2.0}});
  }
  const auto reference = rasterSvgs(runtime.page, references);
  QVERIFY2(!reference.contains(QStringLiteral("error")),
           qPrintable(reference.value(QStringLiteral("error")).toString()));
  const auto referenceImages = reference.value(QStringLiteral("images")).toArray();
  QCOMPARE(referenceImages.size(), 6);
  for (int i = 0; i < images.size(); ++i) {
    QCOMPARE(dataImage(images.at(i).toObject().value(QStringLiteral("src"))),
             dataImage(referenceImages.at(i)));
  }
  // Per-image counters drain on a repeated export with no SVGs remaining, too.
  evaluate(runtime.page, QStringLiteral("vxcore.prepareForExport({rasterizeMath:true})"));
  QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 2, 10000);
  QCOMPARE(evaluate(runtime.page,
                    QStringLiteral(
                        "vxcore.contentContainer.querySelectorAll('img[data-math-png]').length"))
               .toInt(),
           6);
  QCOMPARE(rendered.count(), 1);
}

void TestMarkdownWebJobs::typstProtectedSvgRaster() {
  // Isolate the SVG interchange using real native output, not a fabricated reply
  // or an always-authorized adapter. This fixture grants no protected-note lease.
  Runtime runtime(false, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                  QString(), QStringLiteral("typst"), true);
  QVERIFY(runtime.typstRuntime->isAvailable());
  QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
  QVERIFY(runtime.renderDocument(QStringLiteral("Raster boundary")).error.isEmpty());
  QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
  vte::TypstFragmentRenderer renderer(runtime.typstRuntime.get());
  QSignalSpy finished(&renderer, &vte::TypstFragmentRenderer::finished);
  const QStringList formulas{
      QStringLiteral("frac(a^2, sqrt(b))"),
      QStringLiteral("#rect(width: 36pt, height: 18pt, "
                     "fill: gradient.linear(rgb(\"#13579B80\"), rgb(\"#B5371780\")), "
                     "stroke: (paint: rgb(\"#2A963CC0\"), thickness: 2pt))")};
  for (int formula = 0; formula < formulas.size(); ++formula) {
    finished.clear();
    QVERIFY(renderer.renderMathToSvg(formulas.at(formula), false, QColor(31, 83, 157, 173)));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    const auto native = qvariant_cast<vte::TypstFragmentRenderer::Result>(finished.first().first());
    QVERIFY2(native.isSuccess(), qPrintable(native.m_error));
    const QJsonObject reply{{QStringLiteral("svg"), QString::fromUtf8(native.m_svg)},
                            {QStringLiteral("widthPt"), native.m_sizePt.width()},
                            {QStringLiteral("heightPt"), native.m_sizePt.height()},
                            {QStringLiteral("baselinePt"), native.m_baselinePt},
                            {QStringLiteral("pageCount"), native.m_pageCount},
                            {QStringLiteral("error"), QString()}};
    const auto serialized = QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact));
    for (const bool graph : {false, true}) {
      for (const double opacity : {1.0, 0.5}) {
        const auto raster = awaitObject(
            runtime.page, QStringLiteral(R"JS((async () => {
          const native = %1;
          const opacity = %2;
          const graph = %3;
          const document = new DOMParser().parseFromString(native.svg, 'image/svg+xml');
          const paints = [['fill', 'fill-opacity'], ['stroke', 'stroke-opacity'],
            ['stop-color', 'stop-opacity']];
          const originals = [document.documentElement, ...document.querySelectorAll('svg *')];
          const expected = [];
          for (const [index, node] of originals.entries()) {
            for (const [paint, channel] of paints) {
              const match = /^#([\da-f]{6})([\da-f]{2})$/i.exec(node.getAttribute(paint) || '');
              if (!match) continue;
              const existing = Number(node.getAttribute(channel) || 1);
              // Exercise multiplication of an existing independent channel opacity
              // on real native glyph, stroke and gradient geometry.
              node.setAttribute(channel, existing * opacity);
              expected.push({index, paint, channel, color:'#' + match[1],
                alpha:parseInt(match[2], 16) / 255 * existing * opacity});
            }
          }
          native.svg = new XMLSerializer().serializeToString(document.documentElement);
          const worker = vxcore.getWorker('typst');
          const svg = worker.createSvg(native, 'protected-raster', 'source-only sentinel', 18, false, 'math');
          const nodes = [svg, ...svg.querySelectorAll('*')].filter(node => node.localName !== 'title');
          const normalized = expected.every(item =>
            nodes[item.index].getAttribute(item.paint) === item.color
            && Math.abs(Number(nodes[item.index].getAttribute(item.channel)) - item.alpha) < 1e-9);
          const genericOpacityUnchanged = nodes.length === originals.length && nodes.every((node, index) =>
            node.getAttribute('opacity') === originals[index].getAttribute('opacity'));
          const accessibility = svg.getAttribute('aria-label') === 'source-only sentinel'
            && svg.querySelector('title').textContent === 'source-only sentinel';
          const desc = window.document.createElementNS('http://www.w3.org/2000/svg', 'desc');
          desc.textContent = 'source-only sentinel';
          svg.appendChild(desc);
          // Checked native SVG may be attached, but its presentation attributes must
          // still win over live CSS when the protected raster path sanitizes again.
          for (const node of nodes) {
            node.style.fill = 'rgb(255, 0, 255)';
            node.style.stroke = 'rgb(255, 0, 255)';
          }
          vxcore.inplacePreviewContainer.appendChild(svg);
          try {
            const previewer = new GraphPreviewer(vxcore, vxcore.inplacePreviewContainer);
            const result = await previewer.processSvgAsPng(svg,
              {id:'protected-raster', graph, scale:1.5, pixelRatio:2}, true);
            return {result, normalized, genericOpacityUnchanged, accessibility,
              fill:expected.filter(item => item.paint === 'fill').length,
              stroke:expected.filter(item => item.paint === 'stroke').length,
              stops:expected.filter(item => item.paint === 'stop-color').length,
              sourceFree:!result.data.includes('source-only sentinel')
                && !svg.querySelector('title, desc') && !svg.hasAttribute('aria-label'),
              viewBox:svg.getAttribute('viewBox') === document.documentElement.getAttribute('viewBox')};
          } finally {
            svg.remove();
          }
        })())JS")
                              .arg(serialized, QString::number(opacity),
                                   graph ? QStringLiteral("true") : QStringLiteral("false")));
        QVERIFY2(!raster.contains(QStringLiteral("error")),
                 qPrintable(raster.value(QStringLiteral("error")).toString()));
        for (const auto &check :
             {QStringLiteral("normalized"), QStringLiteral("genericOpacityUnchanged"),
              QStringLiteral("accessibility"), QStringLiteral("sourceFree"),
              QStringLiteral("viewBox")}) {
          QVERIFY2(raster.value(check).toBool(), qPrintable(check));
        }
        QVERIFY(raster.value(QStringLiteral("stroke")).toInt() > 0);
        QVERIFY(
            raster.value(formula == 0 ? QStringLiteral("fill") : QStringLiteral("stops")).toInt() >
            0);
        const auto payload = raster.value(QStringLiteral("result")).toObject();
        QCOMPARE(payload.value(QStringLiteral("format")).toString(), QStringLiteral("svg"));
        QVERIFY(!payload.value(QStringLiteral("base64")).toBool());
        QVERIFY(!payload.value(QStringLiteral("needScale")).toBool());
        QCOMPARE(payload.value(QStringLiteral("logicalWidth")).toInt(),
                 qRound(native.m_sizePt.width() * 18 / 12 * 1.5));
        QCOMPARE(payload.value(QStringLiteral("logicalHeight")).toInt(),
                 qRound(native.m_sizePt.height() * 18 / 12 * 1.5));
        QByteArray png, mime;
        QVERIFY(ImageUtils::protectedImageData(
            payload.value(QStringLiteral("data")).toString().toUtf8(), png, mime));
        QCOMPARE(mime, QByteArrayLiteral("image/png"));
        const auto image = QImage::fromData(png, "PNG");
        QCOMPARE(image.size(),
                 QSize(static_cast<int>(std::ceil(native.m_sizePt.width() * 18 / 12 * 3)),
                       static_cast<int>(std::ceil(native.m_sizePt.height() * 18 / 12 * 3))));
        if (formula == 0)
          QVERIFY(transparentBorder(image));
        const auto color = formula == 0 ? QColor(31, 83, 157, qRound(173 * opacity))
                                        : QColor(42, 150, 60, qRound(192 * opacity));
        QVERIFY(rgbaPixels(image, color) > 20);
        if (formula != 0) {
          QVERIFY(std::abs(image.pixelColor(image.width() / 2, image.height() / 2).alpha() -
                           qRound(128 * opacity)) <= 2);
        }
      }
    }
  }
  QVERIFY(!nativeMathPending(runtime));
}

void TestMarkdownWebJobs::typstProtectedAndUnavailable() {
  for (const bool shared : {true, false}) {
    Runtime runtime(shared, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("mathjax"), true);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    QVERIFY(runtime.renderDocument(QStringLiteral("Protected $\\frac{a^2}{\\sqrt{b}}$ text"))
                .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QVERIFY(runtime.adapter->isDocumentReady());
    const auto dom = evaluateObject(runtime.page, QStringLiteral(R"JS((() => {
      const root = vxcore.contentContainer;
      const math = root.querySelector('.katex');
      return {renderer:vxcore.getWorker('math').renderer, formulas:root.querySelectorAll('.katex').length,
        typst:root.querySelectorAll('svg.typst-math').length, errors:root.querySelectorAll('.katex-error').length,
        width:math && math.getBoundingClientRect().width,
        source:root.querySelector('annotation[encoding="application/x-tex"]')?.textContent,
        workers:vxcore.numOfOngoingWorkers, mathjax:!!window.MathJax};
    })())JS"));
    QCOMPARE(dom.value(QStringLiteral("renderer")).toString(), QStringLiteral("katex"));
    QCOMPARE(dom.value(QStringLiteral("formulas")).toInt(), 1);
    QCOMPARE(dom.value(QStringLiteral("typst")).toInt(-1), 0);
    QCOMPARE(dom.value(QStringLiteral("errors")).toInt(-1), 0);
    QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
    QCOMPARE(dom.value(QStringLiteral("source")).toString(),
             QStringLiteral("\\frac{a^2}{\\sqrt{b}}"));
    QVERIFY(dom.value(QStringLiteral("width")).toDouble() > 10);
    QVERIFY(!dom.value(QStringLiteral("mathjax")).toBool());
    QCOMPARE(native.count(), 0);
    // Exercise the protected slot through the real WebChannel, not just a C++ call.
    evaluate(runtime.page, QStringLiteral("vxMarkdownAdapter.renderTypstMath('protected-web', "
                                          "'preview:forbidden', 'sqrt(x)', false, '#1F539DAD')"));
    QTRY_COMPARE_WITH_TIMEOUT(native.count(), 1, 10000);
    QCOMPARE(native.first().at(0).toString(), QStringLiteral("protected-web"));
    const auto rejected = native.first().at(1).toJsonObject();
    QVERIFY(!rejected.value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(rejected.value(QStringLiteral("svg")).toString().isEmpty());
    const auto blocked =
        submit(runtime.session, QStringLiteral("markdown.previewMath"),
               scopedPreview(QStringLiteral("$sqrt(x)$"), QStringLiteral("protected")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(blocked->finished, 30000);
    QCOMPARE(blocked->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(image(blocked->result.value).size(), QSize(560, 32));
    QVERIFY(inkPixels(image(blocked->result.value), QColor(31, 83, 157)) > 30);
    auto fenceArgs =
        scopedPreview(QStringLiteral("$$sqrt(x)$$"), QStringLiteral("protected-fence"));
    fenceArgs.insert(QStringLiteral("language"), QStringLiteral("mathjax"));
    const auto blockedFence =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"), fenceArgs, &owner);
    QTRY_VERIFY_WITH_TIMEOUT(blockedFence->finished, 30000);
    QCOMPARE(blockedFence->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(image(blockedFence->result.value).size(), QSize(560, 32));
    QCOMPARE(native.count(), 1); // Only the explicit denied RPC; no preview compiler was reached.
    QCOMPARE(rendered.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  }

  for (const auto &[protectedView, shared] : {std::pair{true, true}, std::pair{true, false},
                                              std::pair{false, true}, std::pair{false, false}}) {
    // This synthetic profile grants no protected-note lease. Typst is selected,
    // but a bare protected adapter must fail locally just like an absent runtime.
    Runtime runtime(shared, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("typst"), protectedView, protectedView);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    QVERIFY(runtime.renderDocument(QStringLiteral("Unavailable $sqrt(x)$ and $frac($ end"))
                .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QVERIFY(runtime.adapter->isDocumentReady());
    const auto dom = typstDom(runtime.page);
    QCOMPARE(evaluate(runtime.page, QStringLiteral("vxcore.getWorker('math').renderer")).toString(),
             QStringLiteral("typst"));
    QCOMPARE(native.count(), 2);
    QCOMPARE(dom.value(QStringLiteral("svgs")).toArray().size(), 0);
    QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
    QVERIFY(!dom.value(QStringLiteral("otherEngines")).toBool());
    QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("$sqrt(x)$")));
    QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("$frac($")));
    const auto failures = dom.value(QStringLiteral("failures")).toArray();
    QCOMPARE(failures.size(), 2);
    for (const auto &failure : failures) {
      QVERIFY(!failure.toObject().value(QStringLiteral("title")).toString().isEmpty());
    }
    const auto failed =
        submit(runtime.session, QStringLiteral("markdown.previewMath"),
               scopedPreview(QStringLiteral("$sqrt(x)$"), QStringLiteral("unavailable")), &owner);
    auto fenceArgs =
        scopedPreview(QStringLiteral("$$sqrt(x)$$"), QStringLiteral("unavailable-fence"));
    fenceArgs.insert(QStringLiteral("language"), QStringLiteral("mathjax"));
    const auto failedFence =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"), fenceArgs, &owner);
    const auto survivor =
        submit(runtime.session, QStringLiteral("markdown.htmlToMarkdown"),
               source(QStringLiteral("<p><strong>Still available</strong></p>")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(failed->finished && failedFence->finished && survivor->finished,
                             30000);
    QCOMPARE(failed->result.status, WebViewJobResult::Status::Error);
    QVERIFY(!failed->result.error.isEmpty());
    QCOMPARE(failedFence->result.status, WebViewJobResult::Status::Error);
    QVERIFY(!failedFence->result.error.isEmpty());
    QCOMPARE(native.count(), 4);
    QVERIFY(!nativeMathPending(runtime));
    for (const auto &reply : native) {
      const auto result = reply.at(1).toJsonObject();
      QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
      QVERIFY(result.value(QStringLiteral("svg")).toString().isEmpty());
    }
    if (protectedView) {
      for (const auto &language : {QStringLiteral("puml"), QStringLiteral("plantuml")}) {
        const auto blocked = submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                                    preview(QStringLiteral("Alice -> Bob"), language), &owner);
        QTRY_VERIFY_WITH_TIMEOUT(blocked->finished, 30000);
        QCOMPARE(blocked->result.status, WebViewJobResult::Status::Success);
        QCOMPARE(image(blocked->result.value).size(), QSize(560, 32));
      }
    }
    QCOMPARE(survivor->result.status, WebViewJobResult::Status::Success);
    QCOMPARE(survivor->result.value.toString(), QStringLiteral("**Still available**"));
    QVERIFY(!typstDom(runtime.page).value(QStringLiteral("otherEngines")).toBool());
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
    QVERIFY(runtime.renderDocument(QStringLiteral("No math after an unavailable renderer"))
                .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  }
}

void TestMarkdownWebJobs::typstBlocksReaderAndPreviews() {
  const QColor foreground(31, 83, 157);
  for (const bool shared : {true, false}) {
    for (const bool mathJax : {false, true}) {
      Runtime runtime(
          shared, foreground, QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
          mathJax ? QUrl::fromLocalFile(QStringLiteral(VNOTE_TEST_MATHJAX_SCRIPT)).toString()
                  : QString(),
          mathJax ? QStringLiteral("mathjax") : QString());
      QVERIFY(runtime.typstRuntime->isAvailable());
      QObject owner;
      QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
      QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
      const auto document = runtime.renderDocument(
          QStringLiteral("# Blocks\n\nBefore\n\n") + typstBlockFence(typstBlockMarkup()) +
          QLatin1Char('\n') + typstBlockFence(typstBlockPages()) + QLatin1Char('\n') +
          typstBlockFence(QStringLiteral("   ")) + QLatin1Char('\n') +
          typstBlockFence(QStringLiteral("#let broken = (")) +
          QStringLiteral("\nAfter\n\n```text\n$literal$ and #markup\n```\n"));
      QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
      QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
      QVERIFY(runtime.adapter->isDocumentReady());
      QCOMPARE(native.count(), 4);
      const auto dom = typstDom(runtime.page, true);
      const auto svgs = dom.value(QStringLiteral("svgs")).toArray();
      QCOMPARE(svgs.size(), 2);
      QVERIFY(dom.value(QStringLiteral("valid")).toBool());
      QVERIFY(dom.value(QStringLiteral("localReferences")).toInt() > 0);
      QVERIFY(!dom.value(QStringLiteral("unsafe")).toBool());
      QVERIFY(!dom.value(QStringLiteral("otherEngines")).toBool());
      QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
      QCOMPARE(
          evaluate(runtime.page, QStringLiteral("vxcore.getWorker('math').renderer")).toString(),
          mathJax ? QStringLiteral("mathjax") : QStringLiteral("katex"));
      QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("Before")));
      QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("After")));
      QVERIFY(dom.value(QStringLiteral("code"))
                  .toArray()
                  .contains(QStringLiteral("$literal$ and #markup\n")));
      const auto failures = dom.value(QStringLiteral("failures")).toArray();
      QCOMPARE(failures.size(), 2);
      QVERIFY(
          failures.first().toObject().value(QStringLiteral("text")).toString().trimmed().isEmpty());
      QCOMPARE(failures.last().toObject().value(QStringLiteral("text")).toString(),
               QStringLiteral("#let broken = (\n"));
      for (const auto &failure : failures)
        QVERIFY(!failure.toObject().value(QStringLiteral("title")).toString().isEmpty());

      const auto html =
          QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("html"), document.html}})
                                .toJson(QJsonDocument::Compact));
      const auto mappings = evaluateObject(runtime.page, QStringLiteral(R"JS((() => {
        const doc = new DOMParser().parseFromString(%1.html, 'text/html');
        return {nodes:Array.from(doc.querySelectorAll('code.lang-typst'), code => {
          const pre = code.parentElement;
          return {line:pre.getAttribute('data-source-line') || code.getAttribute('data-source-line'),
            end:pre.getAttribute('data-source-line-end') || code.getAttribute('data-source-line-end')};
        })};
      })())JS")
                                                             .arg(html))
                                .value(QStringLiteral("nodes"))
                                .toArray();
      QCOMPARE(mappings.size(), 4);
      QJsonArray originals;
      for (int i = 0; i < svgs.size(); ++i) {
        const auto svg = svgs.at(i).toObject();
        const auto reply = native.at(i).at(1).toJsonObject();
        QVERIFY2(reply.value(QStringLiteral("error")).toString().isEmpty(),
                 qPrintable(reply.value(QStringLiteral("error")).toString()));
        QCOMPARE(svg.value(QStringLiteral("source")).toString(), QStringLiteral("Typst"));
        QCOMPARE(svg.value(QStringLiteral("title")).toString(), QStringLiteral("Typst"));
        QCOMPARE(svg.value(QStringLiteral("pages")).toInt(), i + 1);
        QCOMPARE(reply.value(QStringLiteral("pageCount")).toInt(), i + 1);
        QVERIFY(svg.value(QStringLiteral("sourceClass")).toBool());
        QVERIFY(!svg.value(QStringLiteral("sourceLine")).toString().isEmpty());
        QCOMPARE(svg.value(QStringLiteral("sourceLine")),
                 mappings.at(i).toObject().value(QStringLiteral("line")));
        QCOMPARE(svg.value(QStringLiteral("sourceLineEnd")),
                 mappings.at(i).toObject().value(QStringLiteral("end")));
        QCOMPARE(reply.value(QStringLiteral("baselinePt")).toDouble(), 0.0);
        for (const auto &dimension : {QStringLiteral("width"), QStringLiteral("height")}) {
          const double expected =
              reply.value(dimension + QStringLiteral("Pt")).toDouble() * 18 / 12;
          QVERIFY(std::abs(svg.value(dimension).toDouble() - expected) < 0.05);
        }
        originals.append(
            QJsonObject{{QStringLiteral("svg"), reply.value(QStringLiteral("svg"))},
                        {QStringLiteral("width"), svg.value(QStringLiteral("width"))},
                        {QStringLiteral("height"), svg.value(QStringLiteral("height"))}});
      }
      QCOMPARE(svgs.at(1).toObject().value(QStringLiteral("viewWidth")).toDouble(), 120.0);
      QCOMPARE(svgs.at(1).toObject().value(QStringLiteral("viewHeight")).toDouble(), 168.0);
      QCOMPARE(evaluate(runtime.page,
                        QStringLiteral("vxcore.contentContainer.querySelectorAll('svg a').length"))
                   .toInt(),
               0);
      const auto live = rasterSvgs(runtime.page, svgs);
      const auto reference = rasterSvgs(runtime.page, originals);
      QVERIFY(!live.contains(QStringLiteral("error")));
      QVERIFY(!reference.contains(QStringLiteral("error")));
      QCOMPARE(live.value(QStringLiteral("images")).toArray().size(), 2);
      QCOMPARE(reference.value(QStringLiteral("images")).toArray().size(), 2);
      for (int i = 0; i < 2; ++i) {
        const auto raster = dataImage(live.value(QStringLiteral("images")).toArray().at(i));
        QVERIFY(inkPixels(raster, foreground) > 30);
        QCOMPARE(raster, dataImage(reference.value(QStringLiteral("images")).toArray().at(i)));
      }
      const auto pages = dataImage(live.value(QStringLiteral("images")).toArray().last());
      QVERIFY(inkPixels(pages.copy(0, 0, pages.width(), pages.height() / 2), foreground) > 100);
      QVERIFY(inkPixels(pages.copy(0, pages.height() / 2, pages.width(), pages.height() / 2),
                        QColor(161, 47, 89)) > 100);

      const auto repeatedScript = awaitObject(runtime.page, QStringLiteral(R"JS((async () => {
        const worker = vxcore.getWorker('typst');
        const script = document.createElement('script');
        script.src = document.querySelector('script[src$="/typst.js"]').src;
        try {
          await new Promise((resolve, reject) => {
            script.onload = resolve; script.onerror = reject; document.head.appendChild(script);
          });
          return {same:worker === vxcore.getWorker('typst'), workers:vxcore.numOfOngoingWorkers};
        } finally {script.remove();}
      })())JS"));
      QVERIFY2(!repeatedScript.contains(QStringLiteral("error")),
               qPrintable(repeatedScript.value(QStringLiteral("error")).toString()));
      QVERIFY(repeatedScript.value(QStringLiteral("same")).toBool());
      QCOMPARE(repeatedScript.value(QStringLiteral("workers")).toInt(-1), 0);

      const auto normal =
          submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                 typstBlockPreview(typstBlockMarkup(), QStringLiteral("block-normal")), &owner);
      QTRY_VERIFY_WITH_TIMEOUT(normal->finished, 30000);
      QCOMPARE(normal->result.status, WebViewJobResult::Status::Success);
      const auto baseline = image(normal->result.value);
      QVERIFY(inkPixels(baseline, foreground) > 30);
      const auto logical = normal->result.value.toObject();
      for (const auto &pair : {QPair<qreal, qreal>(1, 2), {2, 1}, {1.5, 1.25}, {1, 1}}) {
        auto args = typstBlockPreview(typstBlockMarkup(), QStringLiteral("block-narrow"));
        args.insert(QStringLiteral("zoom"), pair.first);
        args.insert(QStringLiteral("devicePixelRatio"), pair.second);
        args.insert(QStringLiteral("width"), 96);
        const auto scaled =
            submit(runtime.session, QStringLiteral("markdown.previewGraph"), args, &owner);
        QTRY_VERIFY_WITH_TIMEOUT(scaled->finished, 30000);
        QCOMPARE(scaled->result.status, WebViewJobResult::Status::Success);
        const auto data = scaled->result.value.toObject();
        const auto raster = image(scaled->result.value);
        QCOMPARE(data.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
        QVERIFY(!data.value(QStringLiteral("needScale")).toBool());
        QVERIFY(inkPixels(raster, foreground) > 30);
        for (const auto &dimension :
             {QStringLiteral("logicalWidth"), QStringLiteral("logicalHeight")})
          QVERIFY(std::abs(data.value(dimension).toInt() -
                           qRound(logical.value(dimension).toInt() * pair.first)) <= 1);
        QVERIFY(std::abs(raster.width() - baseline.width() * pair.first * pair.second) <= 3);
        QVERIFY(std::abs(raster.height() - baseline.height() * pair.first * pair.second) <= 3);
        if (pair.first == 1 && pair.second == 1)
          QCOMPARE(raster, baseline);
      }
      if (runtime.reader)
        runtime.reader->hide();
      const auto hidden =
          submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                 typstBlockPreview(typstBlockMarkup(), QStringLiteral("block-hidden")), &owner);
      QTRY_VERIFY_WITH_TIMEOUT(hidden->finished, 30000);
      QCOMPARE(hidden->result.status, WebViewJobResult::Status::Success);
      QCOMPARE(image(hidden->result.value), baseline);
      const auto paged =
          submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                 typstBlockPreview(typstBlockPages(), QStringLiteral("block-pages")), &owner);
      QTRY_VERIFY_WITH_TIMEOUT(paged->finished, 30000);
      QCOMPARE(paged->result.status, WebViewJobResult::Status::Success);
      QVERIFY(inkPixels(image(paged->result.value), foreground) > 100);
      QVERIFY(inkPixels(image(paged->result.value), QColor(161, 47, 89)) > 100);
      for (const auto &invalid : {QStringLiteral("   "), QStringLiteral("#let broken = ("),
                                  QStringLiteral("#read(\"secret.txt\")")}) {
        const auto failed =
            submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                   typstBlockPreview(invalid, QStringLiteral("block-recovery")), &owner);
        const auto recovered =
            submit(runtime.session, QStringLiteral("markdown.previewGraph"),
                   typstBlockPreview(typstBlockMarkup(), QStringLiteral("block-recovery")), &owner);
        QTRY_VERIFY_WITH_TIMEOUT(failed->finished && recovered->finished, 30000);
        QCOMPARE(failed->result.status, WebViewJobResult::Status::Error);
        QVERIFY(!failed->result.error.isEmpty());
        QCOMPARE(recovered->result.status, WebViewJobResult::Status::Success);
        QCOMPARE(image(recovered->result.value), baseline);
      }
      QCOMPARE(rendered.count(), 1);
      QCOMPARE(typstDom(runtime.page, true).value(QStringLiteral("svgs")).toArray().size(), 2);
      QVERIFY(!typstDom(runtime.page, true).value(QStringLiteral("otherEngines")).toBool());
      QCOMPARE(
          evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
              .toInt(),
          0);
      QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
    }
  }

  // No engine fallback or hung pass when the runtime is unavailable.
  for (const bool shared : {true, false}) {
    Runtime runtime(shared, foreground, QUrl::fromLocalFile(webDir() + QLatin1Char('/')), QString(),
                    QStringLiteral("katex"), false, false);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QVERIFY(runtime.renderDocument(typstBlockFence(typstBlockMarkup())).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    const auto dom = typstDom(runtime.page, true);
    QCOMPARE(dom.value(QStringLiteral("svgs")).toArray().size(), 0);
    QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
    QVERIFY(!dom.value(QStringLiteral("otherEngines")).toBool());
    const auto failures = dom.value(QStringLiteral("failures")).toArray();
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.first().toObject().value(QStringLiteral("text")).toString(),
             typstBlockMarkup() + QLatin1Char('\n'));
    QVERIFY(!failures.first().toObject().value(QStringLiteral("title")).toString().isEmpty());
    const auto failed =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockMarkup(), QStringLiteral("unavailable")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(failed->finished, 30000);
    QCOMPARE(failed->result.status, WebViewJobResult::Status::Error);
    QVERIFY(!failed->result.error.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  }
}

void TestMarkdownWebJobs::typstBlocksCancellationAndIsolation() {
  for (const auto &preference : {QStringLiteral("katex"), QStringLiteral("mathjax")}) {
    Runtime runtime(true, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), preference);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    QVERIFY(runtime.renderDocument(QStringLiteral("Ready")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    MathRuntimeGate gate(*runtime.typstRuntime);
    QVERIFY(gate.entered());
    // Both operation kinds share ownership. The release provider must reach Typst
    // even when the selected math engine is KaTeX or MathJax, without initializing it.
    evaluate(runtime.page, QStringLiteral(R"JS(
      window.blockScopeResults = [];
      const worker = vxcore.getWorker('typst');
      const record = name => svg => blockScopeResults.push({name, id:svg.id, error:false});
      const failed = name => error => blockScopeResults.push({name, error:true});
      worker.requestMarkup('preview:retire', '*Retire*', vxcore.contentContainer)
        .then(record('markup'), failed('markup'));
      worker.requestMath('preview:retire', 'sqrt(x)', false, vxcore.contentContainer)
        .then(record('math'), failed('math'));
      worker.requestMarkup('preview:survivor', '*Survivor*', vxcore.contentContainer)
        .then(record('survivor'), failed('survivor'));
    )JS"));
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QCOMPARE(native.count(), 0);
    const auto release = submit(runtime.session, QStringLiteral("markdown.releaseScope"),
                                {{QStringLiteral("scope"), QStringLiteral("retire")}}, &owner);
    QTRY_VERIFY_WITH_TIMEOUT(release->finished, 10000);
    QCOMPARE(release->result.status, WebViewJobResult::Status::Success);
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("blockScopeResults.length")).toInt(), 2, 10000);
    QVERIFY(
        evaluate(runtime.page, QStringLiteral("blockScopeResults.every(result => result.error)"))
            .toBool());
    QTRY_COMPARE_WITH_TIMEOUT(native.count(), 2, 10000);
    gate.release();
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("blockScopeResults.length")).toInt(), 3, 30000);
    QVERIFY(evaluate(runtime.page,
                     QStringLiteral(
                         "blockScopeResults[2].name === 'survivor' && !blockScopeResults[2].error"))
                .toBool());
    QCOMPARE(native.count(), 3);
    QSet<QString> ids;
    for (const auto &reply : native)
      ids.insert(reply.at(0).toString());
    QCOMPARE(ids.size(), 3);
    const auto reused =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockMarkup(), QStringLiteral("retire")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(reused->finished, 30000);
    QCOMPARE(reused->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(reused->result.value)) > 30);
    QVERIFY(!typstDom(runtime.page, true).value(QStringLiteral("otherEngines")).toBool());
    QCOMPARE(rendered.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);

    QObject retiring, siblingOwner;
    MathRuntimeGate cancellationGate(*runtime.typstRuntime);
    QVERIFY(cancellationGate.entered());
    const auto cancelled =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockMarkup(), QStringLiteral("cancelled")), &retiring);
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    const auto sibling =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockPages(), QStringLiteral("sibling")), &siblingOwner);
    runtime.session->cancelOwner(&retiring);
    QTRY_VERIFY_WITH_TIMEOUT(cancelled->finished, 10000);
    QCOMPARE(cancelled->result.status, WebViewJobResult::Status::Cancelled);
    evaluate(runtime.page,
             QStringLiteral("vxcore.getWorker('typst').cancelScope('preview:cancelled')"));
    QTRY_COMPARE_WITH_TIMEOUT(native.count(), 5, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QVERIFY(!sibling->finished);
    cancellationGate.release();
    QTRY_VERIFY_WITH_TIMEOUT(sibling->finished, 30000);
    QCOMPARE(sibling->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(sibling->result.value), QColor(31, 83, 157)) > 100);
    QVERIFY(inkPixels(image(sibling->result.value), QColor(161, 47, 89)) > 100);
    QCOMPARE(cancelled->result.status, WebViewJobResult::Status::Cancelled);
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  }

  {
    Runtime runtime(false);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QVERIFY(runtime.renderDocument(QStringLiteral("Ready")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    rendered.clear();
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    MathRuntimeGate gate(*runtime.typstRuntime);
    QVERIFY(gate.entered());
    QVERIFY(runtime.renderDocument(QStringLiteral("Old\n\n") + typstBlockFence(typstBlockMarkup()))
                .error.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    const auto sibling =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockMarkup(), QStringLiteral("sibling")), &owner);
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        1, 10000);
    QVERIFY(
        runtime.renderDocument(QStringLiteral("Current\n\n") + typstBlockFence(typstBlockPages()))
            .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("vxcore.documentGeneration")).toString(),
        runtime.adapter->documentGeneration(), 10000);
    QCOMPARE(rendered.count(), 0);
    gate.release();
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(sibling->finished, 30000);
    QCOMPARE(sibling->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(sibling->result.value)) > 30);
    const auto dom = typstDom(runtime.page, true);
    const auto svgs = dom.value(QStringLiteral("svgs")).toArray();
    QCOMPARE(svgs.size(), 1);
    QCOMPARE(svgs.first().toObject().value(QStringLiteral("pages")).toInt(), 2);
    QVERIFY(dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("Current")));
    QVERIFY(!dom.value(QStringLiteral("text")).toString().contains(QStringLiteral("Old")));
    QCOMPARE(dom.value(QStringLiteral("workers")).toInt(-1), 0);
    QCOMPARE(native.count(), 3);
    int successes = 0;
    for (const auto &reply : native)
      successes += reply.at(1).toJsonObject().value(QStringLiteral("error")).toString().isEmpty();
    QCOMPARE(successes, 2);
    const auto raster = rasterSvgs(runtime.page, svgs);
    QCOMPARE(raster.value(QStringLiteral("images")).toArray().size(), 1);
    const auto pages = dataImage(raster.value(QStringLiteral("images")).toArray().first());
    QVERIFY(inkPixels(pages, QColor(31, 83, 157)) > 100);
    QVERIFY(inkPixels(pages, QColor(161, 47, 89)) > 100);
    QVERIFY(runtime.renderDocument(QString()).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 2, 10000);
    QVERIFY(runtime
                .renderDocument(typstBlockFence(typstBlockMarkup()) + QLatin1Char('\n') +
                                typstBlockFence(typstBlockMarkup()))
                .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 3, 30000);
    const auto repeated = typstDom(runtime.page, true);
    QCOMPARE(repeated.value(QStringLiteral("svgs")).toArray().size(), 2);
    QVERIFY(repeated.value(QStringLiteral("valid")).toBool());
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  }

  for (const bool protect : {false, true}) {
    Runtime runtime(false);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QVERIFY(runtime.renderDocument(QStringLiteral("Ready")).error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    MathRuntimeGate gate(*runtime.typstRuntime);
    QVERIFY(gate.entered());
    const auto pending =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockMarkup(), QStringLiteral("pending")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(nativeMathPending(runtime), 10000);
    QVERIFY(
        runtime.renderDocument(QStringLiteral("Pending\n\n") + typstBlockFence(typstBlockPages()))
            .error.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(runtime.page,
                 QStringLiteral("vxcore.ongoingWorkers.has('typst') && "
                                "vxcore.contentContainer.textContent.includes('Pending')"))
            .toBool(),
        10000);
    if (protect)
      runtime.adapter->setProtectedView(true);
    else
      runtime.adapter->reset();
    QTRY_VERIFY_WITH_TIMEOUT(pending->finished, 10000);
    QCOMPARE(pending->result.status, WebViewJobResult::Status::Error);
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluate(runtime.page, QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0, 10000);
    QCOMPARE(typstDom(runtime.page, true).value(QStringLiteral("svgs")).toArray().size(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
    gate.release();
    if (protect)
      runtime.adapter->setProtectedView(false);
    const auto recovery =
        submit(runtime.session, QStringLiteral("markdown.previewGraph"),
               typstBlockPreview(typstBlockMarkup(), QStringLiteral("pending")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(recovery->finished, 30000);
    QCOMPARE(recovery->result.status, WebViewJobResult::Status::Success);
    QVERIFY(inkPixels(image(recovery->result.value)) > 30);
    QCOMPARE(pending->result.status, WebViewJobResult::Status::Error);
    QCOMPARE(typstDom(runtime.page, true).value(QStringLiteral("svgs")).toArray().size(), 0);
    QCOMPARE(
        evaluate(runtime.page, QStringLiteral("vxcore.inplacePreviewContainer.childElementCount"))
            .toInt(),
        0);
  }
}

void TestMarkdownWebJobs::typstBlocksExportAndCrossCopy() {
  const QColor foreground(31, 83, 157);
  for (const bool mathRaster : {false, true}) {
    Runtime runtime(false, foreground, QUrl::fromLocalFile(webDir() + QLatin1Char('/')), QString(),
                    QStringLiteral("typst"));
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstReady);
    QSignalSpy saved(runtime.adapter, &MarkdownViewerAdapter::contentReady);
    QSignalSpy copied(runtime.adapter, &MarkdownViewerAdapter::crossCopyReady);
    QSignalSpy exported(runtime.adapter, &MarkdownViewerAdapter::pdfRenderReady);
    QVERIFY(runtime
                .renderDocument(QStringLiteral("Before $sqrt(x)$\n\n") +
                                typstBlockFence(typstBlockMarkup()) + QLatin1Char('\n') +
                                typstBlockFence(typstBlockPages()) + QLatin1Char('\n') +
                                typstBlockFence(QStringLiteral("#let broken = (")) +
                                QStringLiteral("\nAfter\n"))
                .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QCOMPARE(native.count(), 4);
    auto live = typstDom(runtime.page).value(QStringLiteral("svgs")).toArray();
    QCOMPARE(live.size(), 1);
    const auto blocks = typstDom(runtime.page, true).value(QStringLiteral("svgs")).toArray();
    QCOMPARE(blocks.size(), 2);
    for (const auto &block : blocks)
      live.append(block);
    QJsonObject originalById;
    for (const auto &reply : native) {
      const auto result = reply.at(1).toJsonObject();
      if (result.value(QStringLiteral("error")).toString().isEmpty())
        originalById.insert(reply.at(0).toString(), result.value(QStringLiteral("svg")));
    }
    QCOMPARE(originalById.size(), 3);
    const auto inspectHtml = [&](const QString &p_html) {
      const auto html =
          QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("html"), p_html}})
                                .toJson(QJsonDocument::Compact));
      return evaluateObject(runtime.page, QStringLiteral(R"JS((() => {
        const doc = new DOMParser().parseFromString(%1.html, 'text/html');
        return {svgs:Array.from(doc.querySelectorAll('svg.typst-math, svg.typst-block'), svg => ({
            id:svg.id, svg:new XMLSerializer().serializeToString(svg)})),
          math:doc.querySelectorAll('svg.typst-math').length,
          blocks:doc.querySelectorAll('svg.typst-block').length,
          blockImages:doc.querySelectorAll('img[data-typst-block-png]').length,
          mathImages:doc.querySelectorAll('img[data-math-png]').length,
          mappedBlocks:doc.querySelectorAll('section.typst-block-container.source-line[data-source-line]').length,
          images:Array.from(doc.querySelectorAll('img'), img => ({src:img.src,
            width:parseFloat(img.style.width), height:parseFloat(img.style.height)})),
          unsafe:!!doc.querySelector('svg image, svg script, svg foreignObject, svg style, svg a'),
          external:Array.from(doc.querySelectorAll('svg *')).some(node => Array.from(node.attributes).some(attr =>
            attr.localName === 'href' && !attr.value.startsWith('#'))),
          failed:!!doc.querySelector('code.lang-typst[title]') &&
            doc.querySelector('code.lang-typst[title]').textContent === '#let broken = (\n'};
      })())JS")
                                              .arg(html));
    };
    runtime.adapter->saveContent();
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
    const auto html = saved.first().at(2).toString();
    const auto ordinary = inspectHtml(html);
    QCOMPARE(ordinary.value(QStringLiteral("math")).toInt(), 1);
    QCOMPARE(ordinary.value(QStringLiteral("blocks")).toInt(), 2);
    QCOMPARE(ordinary.value(QStringLiteral("images")).toArray().size(), 0);
    QCOMPARE(ordinary.value(QStringLiteral("mappedBlocks")).toInt(), 2);
    QVERIFY(ordinary.value(QStringLiteral("failed")).toBool());
    QVERIFY(!ordinary.value(QStringLiteral("unsafe")).toBool());
    QVERIFY(!ordinary.value(QStringLiteral("external")).toBool());
    QJsonArray standalone;
    const auto savedSvgs = ordinary.value(QStringLiteral("svgs")).toArray();
    QCOMPARE(savedSvgs.size(), live.size());
    for (int i = 0; i < live.size(); ++i) {
      auto svg = live.at(i).toObject();
      QCOMPARE(savedSvgs.at(i).toObject().value(QStringLiteral("id")),
               svg.value(QStringLiteral("id")));
      svg.insert(QStringLiteral("svg"), savedSvgs.at(i).toObject().value(QStringLiteral("svg")));
      standalone.append(svg);
    }
    const auto standaloneRaster = rasterSvgs(runtime.page, standalone);
    QVERIFY(!standaloneRaster.contains(QStringLiteral("error")));
    QCOMPARE(standaloneRaster.value(QStringLiteral("images")).toArray().size(), 3);
    for (const auto &png : standaloneRaster.value(QStringLiteral("images")).toArray())
      QVERIFY(inkPixels(dataImage(png), foreground) > 30);
    QVERIFY(inkPixels(dataImage(standaloneRaster.value(QStringLiteral("images")).toArray().last()),
                      QColor(161, 47, 89)) > 100);

    runtime.adapter->crossCopyRequested(301, 9, QStringLiteral("Evernote"),
                                        QUrl::fromLocalFile(webDir() + QLatin1Char('/')).toString(),
                                        html);
    QTRY_COMPARE_WITH_TIMEOUT(copied.count(), 1, 30000);
    QCOMPARE(copied.first().at(0).toULongLong(), quint64(301));
    QCOMPARE(copied.first().at(1).toULongLong(), quint64(9));
    const auto clipboard = inspectHtml(copied.first().at(2).toString());
    QCOMPARE(clipboard.value(QStringLiteral("svgs")).toArray().size(), 0);
    const auto clipboardImages = clipboard.value(QStringLiteral("images")).toArray();
    QCOMPARE(clipboardImages.size(), 3);
    QVERIFY(clipboard.value(QStringLiteral("failed")).toBool());
    QJsonArray references;
    for (int i = 0; i < clipboardImages.size(); ++i) {
      const auto item = clipboardImages.at(i).toObject();
      const auto png = dataImage(item.value(QStringLiteral("src")));
      const auto svg = live.at(i).toObject();
      QVERIFY(inkPixels(png, foreground) > 30);
      QVERIFY(std::abs(item.value(QStringLiteral("width")).toDouble() -
                       svg.value(QStringLiteral("width")).toDouble()) < 0.05);
      QVERIFY(std::abs(item.value(QStringLiteral("height")).toDouble() -
                       svg.value(QStringLiteral("height")).toDouble()) < 0.05);
      references.append(QJsonObject{
          {QStringLiteral("svg"), originalById.value(svg.value(QStringLiteral("id")).toString())},
          {QStringLiteral("width"), png.width() / 2.0},
          {QStringLiteral("height"), png.height() / 2.0}});
    }
    const auto reference = rasterSvgs(runtime.page, references);
    QVERIFY(!reference.contains(QStringLiteral("error")));
    QCOMPARE(reference.value(QStringLiteral("images")).toArray().size(), 3);
    for (int i = 0; i < 3; ++i)
      QVERIFY(sameMathInk(dataImage(clipboardImages.at(i).toObject().value(QStringLiteral("src"))),
                          dataImage(reference.value(QStringLiteral("images")).toArray().at(i))));
    QCOMPARE(typstDom(runtime.page, true).value(QStringLiteral("svgs")).toArray().size(), 2);

    // The production export hook settles through the native pdfRenderReady signal.
    // Diagram-only export must leave Typst math alone; math export includes blocks too.
    const auto options = mathRaster ? QStringLiteral("{rasterizeMath:true}")
                                    : QStringLiteral("{rasterizeDiagrams:true}");
    evaluate(runtime.page, QStringLiteral("vxcore.prepareForExport(%1)").arg(options));
    QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 30000);
    runtime.adapter->saveContent();
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, 10000);
    const auto converted = inspectHtml(saved.last().at(2).toString());
    QCOMPARE(converted.value(QStringLiteral("blocks")).toInt(), 0);
    QCOMPARE(converted.value(QStringLiteral("math")).toInt(), mathRaster ? 0 : 1);
    QCOMPARE(converted.value(QStringLiteral("blockImages")).toInt(), 2);
    QCOMPARE(converted.value(QStringLiteral("mathImages")).toInt(), mathRaster ? 1 : 0);
    QCOMPARE(converted.value(QStringLiteral("mappedBlocks")).toInt(), 2);
    QVERIFY(converted.value(QStringLiteral("failed")).toBool());
    const auto images = converted.value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), mathRaster ? 3 : 2);
    references = {};
    for (int i = 0; i < images.size(); ++i) {
      const auto item = images.at(i).toObject();
      const auto png = dataImage(item.value(QStringLiteral("src")));
      const auto svg = live.at(i + (mathRaster ? 0 : 1)).toObject();
      QVERIFY(inkPixels(png, foreground) > 30);
      QCOMPARE(item.value(QStringLiteral("width")).toInt(),
               static_cast<int>(std::ceil(svg.value(QStringLiteral("width")).toDouble())));
      QCOMPARE(item.value(QStringLiteral("height")).toInt(),
               static_cast<int>(std::ceil(svg.value(QStringLiteral("height")).toDouble())));
      references.append(QJsonObject{
          {QStringLiteral("svg"), originalById.value(svg.value(QStringLiteral("id")).toString())},
          {QStringLiteral("width"), png.width() / 2.0},
          {QStringLiteral("height"), png.height() / 2.0}});
    }
    const auto exportReference = rasterSvgs(runtime.page, references);
    QVERIFY(!exportReference.contains(QStringLiteral("error")));
    QCOMPARE(exportReference.value(QStringLiteral("images")).toArray().size(), images.size());
    for (int i = 0; i < images.size(); ++i)
      QCOMPARE(dataImage(images.at(i).toObject().value(QStringLiteral("src"))),
               dataImage(exportReference.value(QStringLiteral("images")).toArray().at(i)));
    QVERIFY(inkPixels(dataImage(images.last().toObject().value(QStringLiteral("src"))),
                      QColor(161, 47, 89)) > 100);
    evaluate(runtime.page, QStringLiteral("vxcore.prepareForExport(%1)").arg(options));
    QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 2, 10000);
    QCOMPARE(
        evaluate(
            runtime.page,
            QStringLiteral(
                "vxcore.contentContainer.querySelectorAll('img[data-typst-block-png]').length"))
            .toInt(),
        2);
    QCOMPARE(rendered.count(), 1);
  }
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
