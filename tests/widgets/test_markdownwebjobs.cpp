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

#include <vtextedit/typstmathrenderer.h>
#include <vtextedit/typstruntime.h>

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

QJsonObject typstDom(QWebEnginePage *p_page) {
  return evaluateObject(p_page, QStringLiteral(R"JS((() => {
    const root = vxcore.contentContainer;
    const roots = Array.from(root.querySelectorAll('svg.typst-math'));
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
        viewWidth:svg.viewBox.baseVal.width, viewHeight:svg.viewBox.baseVal.height};
    });
    return {svgs, valid:valid && roots.every(svg => !!svg.id), localReferences,
      workers:vxcore.numOfOngoingWorkers, text:root.textContent,
      rows:root.querySelectorAll('table tbody tr').length,
      cells:root.querySelectorAll('table td').length,
      tableMath:root.querySelectorAll('table svg.typst-math').length,
      code:Array.from(root.querySelectorAll('code'), node => node.textContent),
      failures:Array.from(root.querySelectorAll('.tex-to-render')).filter(node => !node.querySelector('svg'))
        .map(node => ({text:node.textContent, title:node.getAttribute('title')})),
      otherEngines:!!window.katex || !!window.MathJax,
      unsafe:!!root.querySelector('svg script, svg image, svg foreignObject, svg style')};
  })())JS"));
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
  const auto *renderer = p_runtime.adapter->findChild<vte::TypstMathRenderer *>();
  return renderer && renderer->isRendering();
}

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
  void typstReaderAndPreviews();
  void typstCancellationAndIsolation();
  void typstExportAndCrossCopy();
  void typstProtectedAndUnavailable();
  void typstAdapterBoundary();
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
    return p_result.size() == 5 && p_result.value(QStringLiteral("svg")).isString() &&
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
    return p_result.size() == 5 && p_result.value(QStringLiteral("error")).isString() &&
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
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstMathReady);
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
      QSignalSpy replies(adapter, &MarkdownViewerAdapter::typstMathReady);
      adapter->renderTypstMath(QStringLiteral("unavailable"), scope, formula, false, color);
      QVERIFY(QTest::qWaitFor([&] { return replies.count() == 1; }, 30000));
      QVERIFY(failed(replies.first().at(1).toJsonObject()));
    }
  }

  {
    MarkdownViewerAdapter adapter(services);
    adapter.setRenderedHtml(QStringLiteral("7"), QStringLiteral("<p>reader</p>"));
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstMathReady);
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
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstMathReady);
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
    adapter.cancelTypstMath(QStringLiteral("preview:not-owned"));
    adapter.cancelTypstMath(retire);
    adapter.cancelTypstMath(retire); // Idempotent, no second cancellations.
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
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstMathReady);
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
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstMathReady);
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
    QSignalSpy replies(&adapter, &MarkdownViewerAdapter::typstMathReady);
    bool replaced = false;
    connect(&adapter, &MarkdownViewerAdapter::typstMathReady, &adapter,
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
      adapter.cancelTypstMath(scope);
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
    connect(adapter.get(), &MarkdownViewerAdapter::typstMathReady, this,
            [&](const QString &, const QJsonObject &p_result) {
              ++completions;
              result = p_result;
              adapter.reset();
            });
    adapter->renderTypstMath(QStringLiteral("delete-on-result"), scope, formula, false, color);
    if (cancel)
      adapter->cancelTypstMath(scope);
    QVERIFY(QTest::qWaitFor([&] { return guarded.isNull(); }, 30000));
    QCOMPARE(completions, 1);
    QVERIFY(cancel ? failed(result) : succeeded(result));
    // A surviving adapter can finish after destruction, with suppressed callbacks harmless.
    MarkdownViewerAdapter survivor(services);
    QSignalSpy replies(&survivor, &MarkdownViewerAdapter::typstMathReady);
    survivor.renderTypstMath(QStringLiteral("survivor"), scope, formula, false, color);
    QVERIFY(QTest::qWaitFor([&] { return replies.count() == 1; }, 30000));
    QVERIFY(succeeded(replies.first().at(1).toJsonObject()));
    QCOMPARE(completions, 1);
  }

  {
    int completions = 0;
    auto adapter = std::make_unique<MarkdownViewerAdapter>(services);
    connect(adapter.get(), &MarkdownViewerAdapter::typstMathReady, this,
            [&](const QString &, const QJsonObject &) { ++completions; });
    adapter->renderTypstMath(QStringLiteral("destroy-active"), scope, formula, false, color);
    adapter->renderTypstMath(QStringLiteral("destroy-queued"), scope, formula, false, color);
    adapter.reset();
    QCOMPARE(completions, 0);
    MarkdownViewerAdapter survivor(services);
    QSignalSpy replies(&survivor, &MarkdownViewerAdapter::typstMathReady);
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

void TestMarkdownWebJobs::typstReaderAndPreviews() {
  const QColor foreground(31, 83, 157);
  for (const bool shared : {true, false}) {
    Runtime runtime(shared, foreground, QUrl::fromLocalFile(webDir() + QLatin1Char('/')), QString(),
                    QStringLiteral("typst"));
    QVERIFY(runtime.typstRuntime->isAvailable());
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstMathReady);
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
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstMathReady);
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
             QStringLiteral("vxcore.getWorker('math').releasePreviewScope('retire')"));
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
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstMathReady);
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
  QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstMathReady);
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

void TestMarkdownWebJobs::typstProtectedAndUnavailable() {
  for (const bool shared : {true, false}) {
    Runtime runtime(shared, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("typst"), true);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QSignalSpy native(runtime.adapter, &MarkdownViewerAdapter::typstMathReady);
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
    QCOMPARE(native.count(), 1); // Only the explicit denied RPC; no preview compiler was reached.
    QCOMPARE(rendered.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(runtime.session->isIdle(), 10000);
  }

  for (const bool shared : {true, false}) {
    Runtime runtime(shared, QColor(31, 83, 157), QUrl::fromLocalFile(webDir() + QLatin1Char('/')),
                    QString(), QStringLiteral("typst"), false, false);
    QObject owner;
    QSignalSpy rendered(runtime.adapter, &MarkdownViewerAdapter::workFinished);
    QVERIFY(runtime.renderDocument(QStringLiteral("Unavailable $sqrt(x)$ and $frac($ end"))
                .error.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 1, 30000);
    QVERIFY(runtime.adapter->isDocumentReady());
    const auto dom = typstDom(runtime.page);
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
    const auto survivor =
        submit(runtime.session, QStringLiteral("markdown.htmlToMarkdown"),
               source(QStringLiteral("<p><strong>Still available</strong></p>")), &owner);
    QTRY_VERIFY_WITH_TIMEOUT(failed->finished && survivor->finished, 30000);
    QCOMPARE(failed->result.status, WebViewJobResult::Status::Error);
    QVERIFY(!failed->result.error.isEmpty());
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

} // namespace tests

int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  tests::TestMarkdownWebJobs test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markdownwebjobs.moc"
