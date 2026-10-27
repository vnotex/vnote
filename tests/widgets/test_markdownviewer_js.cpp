// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_markdownviewer_js.cpp
//
// JS half of the clickable task-list-checkbox contract.
//
// The defect this gates is an install-ordering race: window.vxcore.contentContainer
// is only set by MarkdownViewerCore::initOnLoad(), driven by the window 'load'
// handler, while the QWebChannel callback may run before OR after that. Only one
// of the two can be the installer, and neither may install twice (a double
// listener would toggle the source line twice per click).
//
// Follows the QJSEngine seam of test_pdfviewercore_js.cpp: the REAL
// src/data/extra/web/js/markdownviewer.js is read from disk and evaluated
// unmodified; only its collaborators are stubbed.

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QtTest>

#include <memory>
#include <utility>

#include <utils/markdownhtmlrenderer.h>
#include <utils/sectionnumberutils.h>

namespace tests {

namespace {

QString webDir() {
#ifdef VNOTE_SRC_DIR
  return QStringLiteral(VNOTE_SRC_DIR) + QStringLiteral("/data/extra/web");
#else
  return QDir::currentPath() + QStringLiteral("/../../../src/data/extra/web");
#endif
}

QString readFile(const QString &p_path, QString *p_error) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *p_error = QStringLiteral("cannot open %1").arg(p_path);
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

// Host environment. QJSEngine gives no DOM and no QWebChannel.
// @p_initializedAtChannel: whether window.vxcore.initialized is already true
// when the QWebChannel callback fires (i.e. 'load' won the race).
QString prelude(bool p_initializedAtChannel) {
  return QStringLiteral(R"JS(
var window = this;
var console = { log: function(){}, warn: function(){}, error: function(){} };

window.__listeners = [];
window.__container = {
  addEventListener: function(type, cb) { window.__listeners.push({ type: type, cb: cb }); }
};

window.__reverted = [];
var document = {
  querySelector: function(sel) {
    return { set checked(v) { window.__reverted.push(sel); }, get checked() { return true; } };
  }
};

window.__readyCallbacks = [];
window.vxcore = {
  initialized: %1,
  contentContainer: %1 ? window.__container : null,
  on: function(evt, cb) { if (evt === 'ready') { window.__readyCallbacks.push(cb); } },
  kickOffMarkdown: function() {},
  documentGeneration: 'document-1',
  setRenderedHtml: function() {}, scrollToLine: function() {}, scrollToAnchor: function() {},
  scroll: function() {}, crossCopy: function() {},
  findText: function() {}, saveContent: function() {}, graphRenderDataReady: function() {}
};

// Fire the window 'load' equivalent: initOnLoad() sets contentContainer, then
// every 'ready' callback runs.
window.__fireReady = function() {
  window.vxcore.initialized = true;
  window.vxcore.contentContainer = window.__container;
  for (var i = 0; i < window.__readyCallbacks.length; ++i) {
    window.__readyCallbacks[i]();
  }
};

window.__toggleCalls = [];
function makeSignal() {
  var handlers = [];
  return {
    connect: function(h) { handlers.push(h); },
    emit: function() {
      var args = arguments;
      for (var i = 0; i < handlers.length; ++i) { handlers[i].apply(null, args); }
    }
  };
}

window.__adapter = {
  htmlUpdated: makeSignal(), editLineNumberUpdated: makeSignal(),
  anchorScrollRequested: makeSignal(), scrollRequested: makeSignal(),
  crossCopyRequested: makeSignal(),
  findTextRequested: makeSignal(), contentRequested: makeSignal(),
  graphRenderDataReady: makeSignal(), taskListToggleRejected: makeSignal(),
  presentationModeRequested: makeSignal(), setPresentationState: function() {},
  toggleTaskListItem: function(generation, line, checked) {
    window.__toggleCalls.push({ generation: generation, line: line, checked: checked });
  }
};

window.vxWebJobs = { bind: function() {}, setReady: function() {} };
var qt = { webChannelTransport: {} };
function QWebChannel(transport, cb) { cb({ objects: { vxAdapter: window.__adapter } }); }

// A checkbox click target. @p_line is the data-source-line of the enclosing li,
// or null to model a checkbox with no source mapping.
window.__makeEvent = function(line, checked) {
  var el = {
    tagName: 'INPUT',
    checked: checked,
    classList: { contains: function(c) { return c === 'task-list-item-checkbox'; } },
    closest: function(sel) {
      if (line === null) { return null; }
      return { getAttribute: function() { return String(line); } };
    }
  };
  return { target: el };
};

window.__click = function(event) {
  for (var i = 0; i < window.__listeners.length; ++i) {
    if (window.__listeners[i].type === 'click') { window.__listeners[i].cb(event); }
  }
};
)JS")
      .arg(p_initializedAtChannel ? QStringLiteral("true") : QStringLiteral("false"));
}

QString foldingPrelude() {
  return QStringLiteral(R"JS(
var window = this;
var console = { log: function(){}, info: function(){}, warn: function(){}, error: function(){} };

function ClassList(node) { this.node = node; }
ClassList.prototype.contains = function(name) {
  return this.node.className.split(/\s+/).indexOf(name) >= 0;
};
ClassList.prototype.add = function(name) {
  if (!this.contains(name)) {
    this.node.className = (this.node.className + ' ' + name).trim();
  }
};
ClassList.prototype.remove = function(name) {
  this.node.className = this.node.className.split(/\s+/).filter(function(item) {
    return item && item !== name;
  }).join(' ');
};

function TextNode(text) {
  this.parentNode = null;
  this.childNodes = [];
  this.textContent = String(text);
}

function Element(tagName) {
  this.tagName = tagName.toUpperCase();
  this.parentNode = null;
  this.childNodes = [];
  this.attributes = {};
  this.className = '';
  this.classList = new ClassList(this);
  this.hidden = false;
  this._listeners = {};
  this._rect = { y: 100, top: 100, bottom: 120 };
}
Object.defineProperty(Element.prototype, 'id', {
  get: function() { return this.attributes.id || ''; },
  set: function(value) { this.attributes.id = String(value); }
});
Object.defineProperty(Element.prototype, 'textContent', {
  get: function() {
    var text = '';
    for (var i = 0; i < this.childNodes.length; ++i) {
      text += this.childNodes[i].textContent;
    }
    return text;
  },
  set: function(value) {
    while (this.childNodes.length) {
      this.removeChild(this.childNodes[0]);
    }
    if (String(value)) { this.appendChild(new TextNode(value)); }
  }
});
Object.defineProperty(Element.prototype, 'firstChild', {
  get: function() { return this.childNodes.length ? this.childNodes[0] : null; }
});
Object.defineProperty(Element.prototype, 'children', {
  get: function() { return this.childNodes.filter(function(node) { return !!node.tagName; }); }
});
Object.defineProperty(Element.prototype, 'firstElementChild', {
  get: function() { return this.children.length ? this.children[0] : null; }
});
Object.defineProperty(Element.prototype, 'nextElementSibling', {
  get: function() {
    if (!this.parentNode) { return null; }
    var siblings = this.parentNode.children;
    var idx = siblings.indexOf(this);
    return idx >= 0 && idx + 1 < siblings.length ? siblings[idx + 1] : null;
  }
});
Element.prototype.appendChild = function(child) {
  if (child.parentNode) { child.parentNode.removeChild(child); }
  this.childNodes.push(child);
  child.parentNode = this;
  return child;
};
Element.prototype.insertBefore = function(child, reference) {
  if (child.parentNode) { child.parentNode.removeChild(child); }
  var idx = reference ? this.childNodes.indexOf(reference) : -1;
  if (idx < 0) { this.childNodes.push(child); }
  else { this.childNodes.splice(idx, 0, child); }
  child.parentNode = this;
  return child;
};
Element.prototype.removeChild = function(child) {
  var idx = this.childNodes.indexOf(child);
  if (idx >= 0) { this.childNodes.splice(idx, 1); child.parentNode = null; }
  return child;
};
Element.prototype.setAttribute = function(name, value) {
  this.attributes[name] = String(value);
  if (name === 'class') { this.className = String(value); }
};
Element.prototype.getAttribute = function(name) {
  return Object.prototype.hasOwnProperty.call(this.attributes, name) ? this.attributes[name] : null;
};
Element.prototype.addEventListener = function(type, callback) {
  if (!this._listeners[type]) { this._listeners[type] = []; }
  this._listeners[type].push(callback);
};
Element.prototype.click = function() {
  var event = { target: this, defaultPrevented: false,
                preventDefault: function() { this.defaultPrevented = true; } };
  var node = this;
  while (node) {
    var listeners = node._listeners && node._listeners.click ? node._listeners.click.slice() : [];
    for (var i = 0; i < listeners.length; ++i) { listeners[i](event); }
    node = node.parentNode;
  }
};
Element.prototype.contains = function(node) {
  while (node) {
    if (node === this) { return true; }
    node = node.parentNode;
  }
  return false;
};
Element.prototype.getBoundingClientRect = function() { return this._rect; };
Element.prototype.scrollIntoView = function() { this._scrolledIntoView = true; };

function matchesSelector(node, selector) {
  if (!node.tagName) { return false; }
  if (selector.charAt(0) === '#') { return node.id === selector.substr(1); }
  var parts = selector.split('.');
  var tag = parts[0];
  var cls = parts.length > 1 ? parts[1] : '';
  return (!tag || node.tagName === tag.toUpperCase()) && (!cls || node.classList.contains(cls));
}
Element.prototype.querySelectorAll = function(selector) {
  var selectors = selector.split(',').map(function(item) { return item.trim(); });
  var found = [];
  function visit(parent) {
    for (var i = 0; i < parent.childNodes.length; ++i) {
      var child = parent.childNodes[i];
      for (var j = 0; j < selectors.length; ++j) {
        if (matchesSelector(child, selectors[j])) { found.push(child); break; }
      }
      visit(child);
    }
  }
  visit(this);
  return found;
};
Element.prototype.querySelector = function(selector) {
  var found = this.querySelectorAll(selector);
  return found.length ? found[0] : null;
};
Element.prototype.getElementsByClassName = function(name) {
  return this.querySelectorAll('.' + name);
};

var document = {
  body: new Element('body'),
  documentElement: { scrollHeight: 1000, clientHeight: 500, scrollTop: 0 },
  readyState: 'complete',
  createElement: function(tag) { return new Element(tag); },
  createTextNode: function(text) { return new TextNode(text); },
  createElementNS: function(namespaceUri, tag) { return new Element(tag); },
  getElementById: function(id) { return this.body.querySelector('#' + id); },
  querySelector: function(selector) {
    var parts = selector.trim().split(/\s+/);
    var node = this.body.querySelector(parts[0]);
    for (var i = 1; node && i < parts.length; ++i) { node = node.querySelector(parts[i]); }
    return node;
  },
  addEventListener: function() {}
};
window.addEventListener = function() {};
window.setTimeout = function(callback) { callback(); };
window.scrollTo = function() {};

function add(parent, tag, id, text, className) {
  var node = document.createElement(tag);
  if (id) { node.id = id; }
  if (text) { node.textContent = text; }
  if (className) { node.className = className; }
  parent.appendChild(node);
  return node;
}

window.__makeHeadingFixture = function() {
  document.body = new Element('body');
  var container = add(document.body, 'div', 'vx-content');
  var alpha = add(container, 'h1', 'alpha', 'Alpha');
  var anchor = add(alpha, 'a', '', '', 'vx-header-anchor');
  add(container, 'p', 'alpha-body', 'A');
  var child = add(container, 'h2', 'child', 'Child');
  add(container, 'p', '', 'C');
  var grandchild = add(container, 'h3', 'grandchild', 'Grandchild');
  add(container, 'p', '', 'G');
  var peer = add(container, 'h2', 'peer', 'Peer');
  var empty = add(container, 'h2', 'empty', 'Empty');
  var beta = add(container, 'h1', 'beta', 'Beta');
  var tail = add(container, 'p', 'tail', 'Tail');
  var quote = add(container, 'blockquote', 'quote');
  var quoted = add(quote, 'h2', 'quoted', 'Quoted');
  add(quote, 'p', '', 'Quote body');
  var afterQuote = add(container, 'p', 'after-quote', 'After quote');

  var handlers = {};
  var adapter = {
    on: function(name, callback) { handlers[name] = callback; },
    isScrollMuted: function() { return false; },
    muteScroll: function() {}, unmuteScroll: function() {},
    setHeadings: function(headings, hasSectionNumber) {
      this.headings = headings; this.hasSectionNumber = hasSectionNumber;
    },
    setCurrentHeadingAnchor: function(index, anchorId) {
      this.currentHeadingIndex = index; this.currentHeadingAnchor = anchorId;
    },
    setTopLineNumber: function() {}
  };
  window.__nodes = { container: container, alpha: alpha, anchor: anchor, child: child,
                     grandchild: grandchild, peer: peer, empty: empty, beta: beta,
                     tail: tail, quote: quote, quoted: quoted, afterQuote: afterQuote };
  window.__adapter = adapter;
  window.__mapper = new window.__NodeLineMapper(adapter, container);
};

window.__buttonFor = function(heading) {
  return heading.querySelector('button.vx-heading-fold-toggle');
};
window.__contentFor = function(heading) {
  return document.getElementById(window.__buttonFor(heading).getAttribute('aria-controls'));
};
)JS");
}

} // namespace

class TestMarkdownViewerJs : public QObject {
  Q_OBJECT

private slots:
  void testCrossCopy_localImages_data();
  void testCrossCopy_localImages();
  void testCrossCopy_svgImages_data();
  void testCrossCopy_svgImages();
  void testCrossCopy_katexImages_data();
  void testCrossCopy_katexImages();
  void testPresentation_followsThemeAndRestoresReader();
  void testPresentation_groupingAndRestoration();
  void testPresentation_pinnedHeaderScrolling();
  void testPresentation_cancellationRenderAndExport();
  void testNavigation_visibleRectangles_data();
  void testNavigation_visibleRectangles();
  void testNavigation_staleSnapshots();
  void testNavigation_protectedDestinations();
  void testContextLink_hitTesting_data();
  void testContextLink_hitTesting();
  void testContextLink_protectedDestinations();
  void testContextLink_renderReadiness();
  void testCustomAnchors_data();
  void testCustomAnchors();
  void testSanitizedUrls_data();
  void testSanitizedUrls();
  void testSanitizedUrls_activation();
  void testMathHeadings_linkTargets();
  void testCmarkMathTableDom_data();
  void testCmarkMathTableDom();
  void testMathRenderer_initializationFanout();
  void testMathRenderer_failedInitializationReleasesPass();
  void testMathRenderer_ignoresUnrelatedFontReadiness();
  void testMathRenderer_exportImages_data();
  void testMathRenderer_exportImages();
  void testInstall_loadBeforeChannel();
  void testInstall_channelBeforeLoad();
  void testInstall_onlyOnce();
  void testClick_forwardsLineAndState();
  void testClick_noSourceLineReverts();
  void testReject_revertsCheckbox();
  void testTaskList_nativeSourceMapping();
  void testDocument_latestPendingAndCompletion();
  void testHeadingFolding_enabledAndDisabledDecoration();
  void testHeadingFolding_hierarchyBoundaries();
  void testHeadingFolding_buttonOnlyAndNestedState();
  void testHeadingFolding_refreshAndStaticBootstrapAreIdempotent();
  void testHeadingFolding_navigationExpandsAncestors();
  void testSectionNumber_policyParity_data();
  void testSectionNumber_policyParity();
  void testSectionNumber_reapplicationPreservesDom();
  void testSectionNumber_renderLifecycleAndFolding();

private:
  void runCrossCopyScript(const QString &p_script, const QUrl &p_baseUrl, QJsonObject &p_result);
  // Evaluates the prelude plus the real markdownviewer.js.
  void setup(QJSEngine &p_engine, bool p_initializedAtChannel);
  void setupHeadingFolding(QJSEngine &p_engine);
  void setupSectionNumber(QJSEngine &p_engine);
  void setupMath(QWebEnginePage &p_page);
  void setupPresentationPage(QWebEnginePage &p_page);
  void setPresentationActive(QWebEnginePage &p_page, bool p_active, bool p_darkTheme = true);
  void setupNavigationPage(QWebEnginePage &p_page, bool p_protected = false);
  void renderNavigationDocument(QWebEnginePage &p_page, const QString &p_markdown,
                                const QString &p_generation, bool p_wait = true);
  void waitForDocument(QWebEnginePage &p_page, const QString &p_generation);
  void evaluateNavigation(QWebEnginePage &p_page, const QString &p_script, QJsonObject &p_result);
};

void TestMarkdownViewerJs::runCrossCopyScript(const QString &p_script, const QUrl &p_baseUrl,
                                              QJsonObject &p_result) {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.setHtml(QStringLiteral("<!doctype html><html><body></body></html>"), p_baseUrl);
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());
  QString source;
  QString error;
  for (const auto *name : {"utils.js", "svg-to-image.js", "crosscopy.js"}) {
    source += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  source += QStringLiteral(R"JS(
(async function() {
%1
})().then(function(result) {
  window.__crossCopyResult = JSON.stringify(result);
  document.title = 'cross-copy-finished';
}, function(error) {
  window.__crossCopyResult = JSON.stringify({error: String(error) + '\n' + error.stack});
  document.title = 'cross-copy-finished';
});
)JS")
                .arg(p_script);
  page.runJavaScript(source);
  QTRY_COMPARE_WITH_TIMEOUT(page.title(), QStringLiteral("cross-copy-finished"), 60000);
  const auto state = std::make_shared<std::pair<bool, QVariant>>(false, QVariant());
  page.runJavaScript(QStringLiteral("window.__crossCopyResult"), [state](const QVariant &p_value) {
    state->second = p_value;
    state->first = true;
  });
  QTRY_VERIFY_WITH_TIMEOUT(state->first, 10000);
  const auto result = QJsonDocument::fromJson(state->second.toString().toUtf8());
  QVERIFY2(result.isObject(), qPrintable(state->second.toString()));
  p_result = result.object();
  QVERIFY2(!p_result.contains(QStringLiteral("error")),
           qPrintable(p_result.value(QStringLiteral("error")).toString()));
}

void TestMarkdownViewerJs::testCrossCopy_localImages_data() {
  QTest::addColumn<QString>("target");
  QTest::newRow("wechat") << QStringLiteral("WeChat Public Account Editor");
  QTest::newRow("evernote") << QStringLiteral("Evernote");
}

void TestMarkdownViewerJs::testCrossCopy_localImages() {
  QFETCH(QString, target);
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto imagePath = dir.filePath(QString::fromUtf8("local 图片.PNG"));
  QImage image(3, 2, QImage::Format_ARGB32);
  image.fill(QColor(17, 85, 153));
  QVERIFY(image.save(imagePath));
  QFile pngFile(imagePath);
  QVERIFY(pngFile.open(QIODevice::ReadOnly));
  const auto png = pngFile.readAll();
  const auto gif =
      QByteArray::fromBase64("R0lGODlhAQABAIAAAP///wAAACH5BAEAAAAALAAAAAABAAEAAAICRAEAOw==");
  QFile gifFile(dir.filePath(QStringLiteral("animation.gif")));
  QVERIFY(gifFile.open(QIODevice::WriteOnly));
  QCOMPARE(gifFile.write(gif), gif.size());
  gifFile.close();
  const auto dataUrl =
      QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
  const QJsonObject fixtures{
      {QStringLiteral("target"), target},
      {QStringLiteral("absolute"), QUrl::fromLocalFile(imagePath).toString()},
      {QStringLiteral("data"), dataUrl}};
  QJsonObject result;
  runCrossCopyScript(
      QStringLiteral("const fixtures = %1;\n")
              .arg(QString::fromUtf8(QJsonDocument(fixtures).toJson(QJsonDocument::Compact))) +
          QStringLiteral(R"JS(
let result;
const copy = new CrossCopy({setCrossCopyTargets() {}, setCrossCopyResult(id, stamp, html) {result = html;}});
const html = '<img id="relative" src="local%20图片.PNG" srcset="unused.png 2x" alt="caption" '
    + 'title="title" width="30" height="20" style="border:1px solid;vertical-align:middle">'
    + '<img id="absolute" src="' + fixtures.absolute + '">'
    + '<img id="gif" src="animation.gif"><img id="missing" src="missing.png">'
    + '<img id="https" src="https://example.invalid/image.png">'
    + '<img id="http" src="http://example.invalid/image.png">'
    + '<img id="protocol" src="//example.invalid/image.png">'
    + '<img id="empty" src=" "><img id="absent">'
    + '<img id="data" src="' + fixtures.data + '">';
await copy.crossCopy(1, 2, fixtures.target, document.baseURI, html);
const doc = new DOMParser().parseFromString(result, 'text/html');
const sources = {};
doc.querySelectorAll('img').forEach(img => { sources[img.id] = img.getAttribute('src'); });
const img = doc.getElementById('relative');
return {sources, count: doc.querySelectorAll('img').length, alt: img.alt, title: img.title,
    width: img.width, height: img.height, border: img.style.borderTopWidth,
    align: img.style.verticalAlign, srcset: img.hasAttribute('srcset'),
    fragments: result.includes('<!--StartFragment-->') && result.includes('<!--EndFragment-->')};
)JS"),
      QUrl::fromLocalFile(dir.path() + QLatin1Char('/')), result);
  const auto sources = result.value(QStringLiteral("sources")).toObject();
  QCOMPARE(sources.value(QStringLiteral("relative")).toString(), dataUrl);
  QCOMPARE(sources.value(QStringLiteral("absolute")).toString(), dataUrl);
  QCOMPARE(sources.value(QStringLiteral("gif")).toString(),
           QStringLiteral("data:image/gif;base64,") + QString::fromLatin1(gif.toBase64()));
  QCOMPARE(sources.value(QStringLiteral("data")).toString(), dataUrl);
  QCOMPARE(sources.value(QStringLiteral("missing")).toString(), QStringLiteral("missing.png"));
  QCOMPARE(sources.value(QStringLiteral("https")).toString(),
           QStringLiteral("https://example.invalid/image.png"));
  QCOMPARE(sources.value(QStringLiteral("http")).toString(),
           QStringLiteral("http://example.invalid/image.png"));
  QCOMPARE(sources.value(QStringLiteral("protocol")).toString(),
           QStringLiteral("//example.invalid/image.png"));
  QCOMPARE(sources.value(QStringLiteral("empty")).toString(), QStringLiteral(" "));
  QVERIFY(sources.value(QStringLiteral("absent")).isNull());
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 10);
  QCOMPARE(result.value(QStringLiteral("alt")).toString(), QStringLiteral("caption"));
  QCOMPARE(result.value(QStringLiteral("title")).toString(), QStringLiteral("title"));
  QCOMPARE(result.value(QStringLiteral("width")).toInt(), 30);
  QCOMPARE(result.value(QStringLiteral("height")).toInt(), 20);
  QCOMPARE(result.value(QStringLiteral("border")).toString(), QStringLiteral("1px"));
  QCOMPARE(result.value(QStringLiteral("align")).toString(), QStringLiteral("middle"));
  QVERIFY(!result.value(QStringLiteral("srcset")).toBool());
  QCOMPARE(result.value(QStringLiteral("fragments")).toBool(),
           target == QStringLiteral("Evernote"));
}

void TestMarkdownViewerJs::testCrossCopy_svgImages_data() {
  testCrossCopy_localImages_data();
  QTest::newRow("generic-rule") << QStringLiteral("SVG only");
}

void TestMarkdownViewerJs::testCrossCopy_svgImages() {
  QFETCH(QString, target);
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray svg =
      "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20' "
      "id='graph-svg' viewBox='0 0 40 20'><rect width='40' height='20' fill='#115599'/></svg>";
  QFile svgFile(dir.filePath(QStringLiteral("local.svg")));
  QVERIFY(svgFile.open(QIODevice::WriteOnly));
  QCOMPARE(svgFile.write(svg), svg.size());
  svgFile.close();
  const QJsonObject fixtures{{QStringLiteral("target"), target},
                             {QStringLiteral("data"), QStringLiteral("data:image/svg+xml;base64,") +
                                                          QString::fromLatin1(svg.toBase64())}};
  QJsonObject result;
  runCrossCopyScript(
      QStringLiteral("const fixtures = %1;\n")
              .arg(QString::fromUtf8(QJsonDocument(fixtures).toJson(QJsonDocument::Compact))) +
          QStringLiteral(R"JS(
document.body.innerHTML = '<div id="content" style="color:rgb(17,85,153)">'
  + '<div id="graph"><svg id="graph-svg" width="120" height="60" viewBox="0 0 120 60">'
  + '<title>Graph title</title><rect width="120" height="60" fill="currentColor"/>'
  + '<foreignObject x="10" y="10" width="90" height="35">'
  + '<div xmlns="http://www.w3.org/1999/xhtml" style="background:rgb(230,40,30);width:80px;height:25px">'
  + 'Graph<br>label</div></foreignObject></svg></div>'
  + '<mjx-container id="math"><svg width="40" height="20" viewBox="0 0 40 20" style="vertical-align:-2px">'
  + '<defs><path id="math-glyph" d="M0 0h40v20H0z"/></defs>'
  + '<svg><use href="#math-glyph" fill="currentColor"/></svg></svg>'
  + '<mjx-assistive-mml><math>equation</math></mjx-assistive-mml></mjx-container></div>';
const container = document.getElementById('content');
const original = container.outerHTML;
let result;
const copy = new CrossCopy({contentContainer:container, setCrossCopyTargets() {},
  setCrossCopyResult(id, stamp, html) {result = html;}});
copy.addTarget('SVG only', {rules:[CrossCopy.rule_svgToImage]});
const html = original + '<img id="local" src="local.svg" width="40" height="20">'
  + '<img id="data" src="' + fixtures.data + '">'
  + '<div id="detached" style="color:rgb(17,85,153)"><svg width="40" height="20" viewBox="0 0 40 20">'
  + '<rect width="40" height="20" fill="currentColor"/></svg></div>';
await copy.crossCopy(1, 2, fixtures.target, document.baseURI, html);
const doc = new DOMParser().parseFromString(result, 'text/html');
const images = {};
for (const id of ['graph','math','local','data','detached']) {
  const root = doc.getElementById(id);
  const img = root.tagName === 'IMG' ? root : root.querySelector('img');
  if (!img) { throw new Error('SVG not converted: ' + id); }
  images[id] = {src: img.src, width: img.style.width, height: img.style.height,
                align: img.style.verticalAlign, alt:img.alt};
  // A rich-text destination may discard CSS but must retain the logical image size.
  img.removeAttribute('style');
  images[id].unstyledWidth = img.width;
  images[id].unstyledHeight = img.height;
}
return {images, svgs:doc.querySelectorAll('svg').length,
        mathImages:doc.getElementById('math').querySelectorAll('img').length,
        assistive:doc.querySelectorAll('mjx-assistive-mml').length,
        unchanged:original === container.outerHTML};
)JS"),
      QUrl::fromLocalFile(dir.path() + QLatin1Char('/')), result);
  QCOMPARE(result.value(QStringLiteral("svgs")).toInt(), 0);
  QCOMPARE(result.value(QStringLiteral("mathImages")).toInt(), 1);
  QCOMPARE(result.value(QStringLiteral("assistive")).toInt(), 0);
  QVERIFY(result.value(QStringLiteral("unchanged")).toBool());
  const auto images = result.value(QStringLiteral("images")).toObject();
  for (const auto *id : {"graph", "math", "local", "data", "detached"}) {
    const auto entry = images.value(QLatin1String(id)).toObject();
    const auto data = entry.value(QStringLiteral("src")).toString();
    QVERIFY2(data.startsWith(QStringLiteral("data:image/png;base64,")), qPrintable(data));
    const auto png =
        QByteArray::fromBase64(data.mid(data.indexOf(QLatin1Char(',')) + 1).toLatin1());
    const auto raster = QImage::fromData(png, "PNG");
    const bool graph = QLatin1String(id) == QLatin1String("graph");
    QCOMPARE(raster.size(), graph ? QSize(240, 120) : QSize(80, 40));
    QCOMPARE(entry.value(QStringLiteral("unstyledWidth")).toInt(), graph ? 120 : 40);
    QCOMPARE(entry.value(QStringLiteral("unstyledHeight")).toInt(), graph ? 60 : 20);
    QCOMPARE(raster.pixelColor(2, 2), QColor(17, 85, 153));
    if (graph) {
      // The foreignObject label must be painted, not merely the graph outline.
      QCOMPARE(raster.pixelColor(30, 60), QColor(230, 40, 30));
      QCOMPARE(entry.value(QStringLiteral("alt")).toString(), QStringLiteral("Graph title"));
      QCOMPARE(entry.value(QStringLiteral("width")).toString(), QStringLiteral("120px"));
    }
  }
  QCOMPARE(
      images.value(QStringLiteral("math")).toObject().value(QStringLiteral("align")).toString(),
      QStringLiteral("-2px"));
}

void TestMarkdownViewerJs::testCrossCopy_katexImages_data() {
  QTest::addColumn<QString>("target");
  QTest::addColumn<qreal>("pixelRatio");
  QTest::newRow("wechat-1x") << QStringLiteral("WeChat Public Account Editor") << qreal(1);
  QTest::newRow("wechat-2x") << QStringLiteral("WeChat Public Account Editor") << qreal(2);
  QTest::newRow("evernote-1x") << QStringLiteral("Evernote") << qreal(1);
  QTest::newRow("evernote-2x") << QStringLiteral("Evernote") << qreal(2);
}

void TestMarkdownViewerJs::testCrossCopy_katexImages() {
  QFETCH(QString, target);
  QFETCH(qreal, pixelRatio);
  const QJsonObject fixtures{
      {QStringLiteral("target"), target},
      {QStringLiteral("pixelRatio"), pixelRatio},
      {QStringLiteral("js"), QUrl::fromLocalFile(webDir() + QStringLiteral("/js")).toString()}};
  QJsonObject result;
  runCrossCopyScript(
      QStringLiteral("const fixtures = %1;\n")
              .arg(QString::fromUtf8(QJsonDocument(fixtures).toJson(QJsonDocument::Compact))) +
          QStringLiteral(R"JS(
window.vxOptions = {mathRenderer:'katex'};
Object.defineProperty(window, 'devicePixelRatio', {value:fixtures.pixelRatio});
document.body.innerHTML = '<div id="content" style="width:640px;font:18px Arial;color:rgb(17,85,153)">'
  + '<p id="first">Before <eqn style="color:rgb(180,40,60)"></eqn> After</p>'
  + '<p id="second">Before <eqn style="color:rgb(40,80,180)"></eqn> After</p>'
  + '<section id="display"><eqn></eqn></section></div>';
let html;
window.vxcore = {
  contentContainer:document.getElementById('content'),
  registerWorker(worker) { window.mathWorker = worker; worker.vxcore = this; },
  getWorker(name) { return name === 'math' ? mathWorker : null; },
  setCrossCopyTargets() {}, setCrossCopyResult(id, stamp, result) {html = result;}
};
for (const file of ['vxworker.js', 'mathjax.js']) {
  await new Promise(resolve => Utils.loadScript(fixtures.js + '/' + file, resolve));
}
await mathWorker.initialize();
const formula = String.raw`\frac{\velocity^2+1}{\sqrt{y}}`;
const macros = {'\\velocity':'V'};
mathWorker.renderKatex(document.querySelector('#first eqn'), {text:formula, display:false}, macros);
mathWorker.renderKatex(document.querySelector('#second eqn'), {text:formula, display:false}, macros);
mathWorker.renderKatex(document.querySelector('#display eqn'), {
  text:String.raw`\begin{aligned}\cot\Theta_{max}&=\frac{\sqrt{V^2-v_0^2}}{v_0}\\\Theta_{max}&=\arcsin\sqrt{1-\frac{v_0^2}{V^2}}\end{aligned}`,
  display:true
}, macros);
await mathWorker.waitForMathFonts(vxcore.contentContainer);
const original = vxcore.contentContainer.outerHTML;
const copied = document.createElement('div');
// Reversing identical formulas must still select each formula's own live color/layout.
for (const id of ['second','first','display']) {
  copied.appendChild(document.getElementById(id).cloneNode(true));
}
// Native Chromium copy omits the annotation; it cannot be used to locate or re-render math.
copied.querySelectorAll('annotation').forEach(node => node.remove());
const copy = new CrossCopy(vxcore);
await copy.crossCopy(1, 2, fixtures.target, document.baseURI, copied.innerHTML);
const doc = new DOMParser().parseFromString(html, 'text/html');
const images = [];
for (const id of ['second','first','display']) {
  const root = doc.getElementById(id);
  const img = root.querySelector('img');
  if (!img) {throw new Error('Missing KaTeX image: ' + id);}
  const align = img.style.verticalAlign;
  const tex = id === 'display'
    ? document.querySelector('#display annotation').textContent
    : formula.replace('\\velocity', 'V');
  const delimiter = id === 'display' ? '$$' : '$';
  const preview = await new Promise(resolve =>
    mathWorker.renderText(document.body, delimiter + tex + delimiter, resolve));
  await mathWorker.waitForMathFonts(preview);
  const rect = preview.getBoundingClientRect();
  const previewWidth = Math.ceil(Math.max(rect.width, preview.scrollWidth));
  const previewHeight = Math.ceil(Math.max(rect.height, preview.scrollHeight));
  preview.remove();
  img.removeAttribute('style');
  images.push({src:img.src, width:img.width, height:img.height, align, previewWidth, previewHeight,
    text:root.textContent, parentTag:img.parentElement.tagName,
    count:root.querySelectorAll('img').length});
}
const successUnchanged = original === vxcore.contentContainer.outerHTML;
// Changing the reader width must not shrink the formula by baking page margins into its PNG.
vxcore.contentContainer.style.width = '1280px';
const wideOriginal = vxcore.contentContainer.outerHTML;
await copy.crossCopy(2, 3, fixtures.target, document.baseURI, copied.innerHTML);
const wide = new DOMParser().parseFromString(html, 'text/html');
const widthIndependent = Array.from(wide.querySelectorAll('img')).every((img, index) =>
  img.width === images[index].width && img.height === images[index].height);
const wideUnchanged = wideOriginal === vxcore.contentContainer.outerHTML;
const rasterize = mathWorker.rasterizeHtml;
try {
  mathWorker.rasterizeHtml = () => Promise.reject(new Error('Injected raster failure'));
  await copy.crossCopy(3, 4, fixtures.target, document.baseURI, copied.innerHTML);
} finally {
  mathWorker.rasterizeHtml = rasterize;
}
const rejected = new DOMParser().parseFromString(html, 'text/html');
const rejectedIntact = rejected.querySelectorAll('.katex').length === 3
  && rejected.querySelectorAll('img').length === 0
  && rejected.querySelectorAll('.katex svg').length === copied.querySelectorAll('.katex svg').length;
const unmatched = document.querySelector('#first .katex').cloneNode(true);
// Rerendering invalidates the old clipboard identity, even when another formula looks identical.
mathWorker.renderKatex(document.querySelector('#first eqn'), {text:formula, display:false}, macros);
const beforeFailure = vxcore.contentContainer.outerHTML;
const unmatchedText = unmatched.textContent;
await copy.crossCopy(3, 4, fixtures.target, document.baseURI, unmatched.outerHTML);
const failed = new DOMParser().parseFromString(html, 'text/html');
return {images, fragments:doc.querySelectorAll('.katex, .katex-display, math, svg').length,
  unchanged:successUnchanged && wideUnchanged && beforeFailure === vxcore.contentContainer.outerHTML,
  widthIndependent, rejectedIntact, temporaryWrappers:document.querySelectorAll('.vx-math-raster').length,
  unmatchedText:failed.querySelector('.katex').textContent === unmatchedText,
  unmatchedSvg:failed.querySelectorAll('.katex svg').length === unmatched.querySelectorAll('svg').length,
  unmatchedImages:failed.querySelectorAll('img').length};
)JS"),
      QUrl::fromLocalFile(webDir() + QLatin1Char('/')), result);
  QCOMPARE(result.value(QStringLiteral("fragments")).toInt(), 0);
  QVERIFY(result.value(QStringLiteral("unchanged")).toBool());
  QVERIFY(result.value(QStringLiteral("unmatchedText")).toBool());
  QVERIFY(result.value(QStringLiteral("unmatchedSvg")).toBool());
  QCOMPARE(result.value(QStringLiteral("unmatchedImages")).toInt(), 0);
  QVERIFY(result.value(QStringLiteral("widthIndependent")).toBool());
  QVERIFY(result.value(QStringLiteral("rejectedIntact")).toBool());
  QCOMPARE(result.value(QStringLiteral("temporaryWrappers")).toInt(), 0);
  const auto images = result.value(QStringLiteral("images")).toArray();
  QCOMPARE(images.size(), 3);
  const QColor colors[] = {QColor(40, 80, 180), QColor(180, 40, 60), QColor(17, 85, 153)};
  for (int i = 0; i < images.size(); ++i) {
    const auto entry = images.at(i).toObject();
    QCOMPARE(entry.value(QStringLiteral("count")).toInt(), 1);
    const int width = entry.value(QStringLiteral("width")).toInt();
    const int height = entry.value(QStringLiteral("height")).toInt();
    QCOMPARE(width, entry.value(QStringLiteral("previewWidth")).toInt());
    QCOMPARE(height, entry.value(QStringLiteral("previewHeight")).toInt());
    const auto data = entry.value(QStringLiteral("src")).toString();
    QVERIFY(data.startsWith(QStringLiteral("data:image/png;base64,")));
    const auto png =
        QByteArray::fromBase64(data.mid(data.indexOf(QLatin1Char(',')) + 1).toLatin1());
    const auto raster = QImage::fromData(png, "PNG");
    QCOMPARE(raster.size(), QSize(qRound(width * 2 * pixelRatio), qRound(height * 2 * pixelRatio)));
    QColor ink;
    QRect inkBounds;
    for (int y = 0; y < raster.height(); ++y) {
      for (int x = 0; x < raster.width(); ++x) {
        const auto color = raster.pixelColor(x, y);
        if (color.alpha() > 32) {
          inkBounds |= QRect(x, y, 1, 1);
        }
        if (!ink.isValid() && color.alpha() == 255) {
          ink = color;
        }
      }
    }
    // Preview padding leaves glyphs intact instead of cropping fractions and radicals.
    QVERIFY(inkBounds.left() > 0 && inkBounds.top() > 0);
    QVERIFY(inkBounds.right() < raster.width() - 1 && inkBounds.bottom() < raster.height() - 1);
    // Font antialiasing/unpremultiplication can round channels by a few levels.
    QVERIFY(ink.isValid());
    QVERIFY(qAbs(ink.red() - colors[i].red()) <= 3);
    QVERIFY(qAbs(ink.green() - colors[i].green()) <= 3);
    QVERIFY(qAbs(ink.blue() - colors[i].blue()) <= 3);
    if (i < 2) {
      QCOMPARE(entry.value(QStringLiteral("text")).toString(), QStringLiteral("Before  After"));
      QVERIFY(entry.value(QStringLiteral("align")).toString().endsWith(QStringLiteral("px")));
    } else {
      QCOMPARE(entry.value(QStringLiteral("parentTag")).toString(), QStringLiteral("DIV"));
    }
  }
}

void TestMarkdownViewerJs::setup(QJSEngine &p_engine, bool p_initializedAtChannel) {
  QString err;
  const QString src = readFile(webDir() + QStringLiteral("/js/markdownviewer.js"), &err);
  QVERIFY2(err.isEmpty(), qPrintable(err));

  auto res = p_engine.evaluate(prelude(p_initializedAtChannel));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  res = p_engine.evaluate(src, QStringLiteral("markdownviewer.js"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
}

void TestMarkdownViewerJs::setupHeadingFolding(QJSEngine &p_engine) {
  QString err;
  const QString src = readFile(webDir() + QStringLiteral("/js/nodelinemapper.js"), &err);
  QVERIFY2(err.isEmpty(), qPrintable(err));

  auto res = p_engine.evaluate(foldingPrelude());
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  res = p_engine.evaluate(src + QStringLiteral("\nwindow.__NodeLineMapper = NodeLineMapper;"
                                               "\nwindow.__HeadingFolding = HeadingFolding;"),
                          QStringLiteral("nodelinemapper.js"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
}

void TestMarkdownViewerJs::setupSectionNumber(QJSEngine &p_engine) {
  setupHeadingFolding(p_engine);
  auto res = p_engine.evaluate(
      QStringLiteral("var navigator = { appVersion: 'Win' }; var vxOptions = {};"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QString err;
  QString source;
  for (const auto &name : {QStringLiteral("eventemitter.js"), QStringLiteral("vxcore.js"),
                           QStringLiteral("markdownviewercore.js"), QStringLiteral("vxworker.js"),
                           QStringLiteral("sectionnumber.js")}) {
    source += readFile(webDir() + QStringLiteral("/js/") + name, &err) + QLatin1Char('\n');
    QVERIFY2(err.isEmpty(), qPrintable(err));
  }
  res = p_engine.evaluate(source, QStringLiteral("sectionnumber.js"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  res = p_engine.evaluate(QStringLiteral(R"JS(
var __sectionNumber = vxcore.getWorker('sectionnumber');
var __container, __sectionHeadings;
function __makeSectionFixture(rows) {
  document.body = new Element('body');
  __container = add(document.body, 'div', 'vx-content');
  add(__container, 'p', '', 'Prose before the first heading');
  __sectionHeadings = [];
  rows.forEach(function(row) {
    // Gap-fillers and invalid levels are C++ records, not DOM headings.
    if (row[2] || row[0] <= 0) { return; }
    __sectionHeadings.push(add(__container, 'h' + row[0],
                              'heading-' + __sectionHeadings.length, row[1]));
  });
  vxcore.contentContainer = __container;
}
function __sectionNames() {
  return __sectionHeadings.map(function(heading) { return heading.textContent; });
}
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
}

void TestMarkdownViewerJs::setupMath(QWebEnginePage &p_page) {
  QSignalSpy loaded(&p_page, &QWebEnginePage::loadFinished);
  p_page.setHtml(QStringLiteral("<!doctype html><html><body><iframe width='640' height='480' "
                                "srcdoc=\"<!doctype html><html><body><div id='content'>"
                                "<eq class='tex-to-render'>$a+b$</eq></div><div id='preview'></div>"
                                "</body></html>\"></iframe></body></html>"),
                 QUrl::fromLocalFile(webDir() + QLatin1Char('/')));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.first().first().toBool());
  QString source = QStringLiteral(R"JS(
window.vxOptions = {mathRenderer: 'katex'};
window.mathInit = {passes: 0, results: [], scriptReady: false, styleReady: false};
window.vxcore = {
  contentContainer: document.getElementById('content'),
  getWorker() { return {getCodeNodes() { return []; }}; },
  registerWorker(worker) { window.mathWorker = worker; worker.vxcore = this; },
  finishWorker() { ++mathInit.passes; }
};
)JS");
  QString error;
  for (const auto *name : {"utils.js", "vxworker.js", "mathjax.js"}) {
    source += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  const QJsonObject assets{
      {QStringLiteral("path"), QUrl::fromLocalFile(webDir() + QStringLiteral("/js")).toString()}};
  source += QStringLiteral("mathWorker.scriptFolderPath = %1.path;\n")
                .arg(QString::fromUtf8(QJsonDocument(assets).toJson(QJsonDocument::Compact)));
  source += QStringLiteral(R"JS(
const load = Utils.loadScript;
const get = Utils.httpGet;
Utils.loadScript = (url, callback) => load(url, () => {
  window.releaseMathScript = fail => { if (fail) window.katex = null; callback(); };
  mathInit.scriptReady = true;
});
Utils.httpGet = (url, type, callback) => get(url, type, css => {
  window.releaseMathStyle = fail => callback(fail ? null : css);
  mathInit.styleReady = true;
});
window.startMathRequests = () => {
  vxcore.documentGeneration = 'math-1';
  mathWorker.beginWork(vxcore.documentGeneration);
  mathWorker.render(vxcore.contentContainer, 'tex-to-render');
  for (const text of ['$x$', '$y$']) {
    mathWorker.renderText(document.getElementById('preview'), text, node => mathInit.results.push(node));
  }
};
window.mathInitializationState = () => ({
  passes: mathInit.passes, previews: mathInit.results.length,
  scriptReady: mathInit.scriptReady, styleReady: mathInit.styleReady,
  reading: vxcore.contentContainer.querySelector('annotation')?.textContent || '',
  source: vxcore.contentContainer.textContent,
  formulas: mathInit.results.map(node => node?.querySelector('annotation')?.textContent || ''),
  failed: mathInit.results.every(node => node === null),
  distinct: mathInit.results.length === 2 && mathInit.results[0] !== mathInit.results[1]
});
)JS");
  const QJsonObject script{{QStringLiteral("source"), source}};
  QJsonObject result;
  evaluateNavigation(
      p_page,
      QStringLiteral(
          "const script = document.createElement('script'); script.textContent = %1.source; "
          "document.head.appendChild(script); script.remove(); startMathRequests(); return {};")
          .arg(QString::fromUtf8(QJsonDocument(script).toJson(QJsonDocument::Compact))),
      result);
}

void TestMarkdownViewerJs::evaluateNavigation(QWebEnginePage &p_page, const QString &p_script,
                                              QJsonObject &p_result) {
  const auto script = QStringLiteral("(function() { try {\n%1\n"
                                     "} catch (error) { return {error: String(error) + '\\n' + "
                                     "error.stack}; } })()")
                          .arg(p_script);
  const QJsonObject source{
      {QStringLiteral("source"), QStringLiteral("JSON.stringify(%1)").arg(script)}};
  const auto state = std::make_shared<std::pair<bool, QVariant>>(false, QVariant());
  p_page.runJavaScript(
      QStringLiteral("document.querySelector('iframe').contentWindow.eval(%1.source)")
          .arg(QString::fromUtf8(QJsonDocument(source).toJson(QJsonDocument::Compact))),
      [state](const QVariant &p_value) {
        state->second = p_value;
        state->first = true;
      });
  QTRY_VERIFY_WITH_TIMEOUT(state->first, 10000);
  const auto result = QJsonDocument::fromJson(state->second.toString().toUtf8());
  QVERIFY2(result.isObject(), qPrintable(state->second.toString()));
  p_result = result.object();
  QVERIFY2(!p_result.contains(QStringLiteral("error")),
           qPrintable(p_result.value(QStringLiteral("error")).toString()));
}

void TestMarkdownViewerJs::setupNavigationPage(QWebEnginePage &p_page, bool p_protected) {
  // QWebEnginePage has no QWidget viewport. A same-origin fixed-size frame supplies a
  // real, deterministic CSS viewport without replacing DOM layout or Utils.viewPortRect().
  QSignalSpy loaded(&p_page, &QWebEnginePage::loadFinished);
  p_page.setHtml(QStringLiteral(
      "<!doctype html><html><body><iframe width='640' height='480' style='border:0' "
      "srcdoc=\"<!doctype html><html><head><base href='file:///navigation/current.md'>"
      "<style>html,body{margin:0}#vx-content{position:relative;min-height:1600px}</style>"
      "</head><body><div id='vx-content'></div></body></html>\"></iframe></body></html>"));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());

  QString error;
  QString script = QStringLiteral("window.vxOptions = {protectedView:%1};\n")
                       .arg(p_protected ? QStringLiteral("true") : QStringLiteral("false"));
  for (const auto *name :
       {"eventemitter.js", "vxcore.js", "vxworker.js", "utils.js", "markdownviewercore.js",
        "imageviewer.js", "xss.min.js", "documentrenderer.js"}) {
    script += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  // Scripts are installed after load, so select the container without unrelated preview workers.
  script += QStringLiteral(R"JS(
window.Utils = Utils;
window.vxcore.contentContainer = document.getElementById('vx-content');
window.vxcore.initialized = true;
window.__completedGenerations = [];
window.__protectedLinks = [];
window.__anchors = [];
window.vxcore.nodeLineMapper = {scrollToAnchor: function(anchor) { __anchors.push(anchor); }};
window.vxMarkdownAdapter = {
  setWorkFinished: function(generation) { __completedGenerations.push(generation); },
  activateLink: function(generation, href) { __protectedLinks.push(href); },
  protectedImageUrl: function(src, callback) { window.__finishImage = callback; }
};
return {ready: true};
)JS");
  QJsonObject result;
  evaluateNavigation(p_page, script, result);
  QVERIFY(result.value(QStringLiteral("ready")).toBool());
}

void TestMarkdownViewerJs::renderNavigationDocument(QWebEnginePage &p_page,
                                                    const QString &p_markdown,
                                                    const QString &p_generation, bool p_wait) {
  const auto rendered = vnotex::MarkdownHtmlRenderer::render(p_markdown.toUtf8(), {});
  QVERIFY2(rendered.error.isEmpty(), qPrintable(rendered.error));
  const QJsonObject payload{{QStringLiteral("html"), rendered.html},
                            {QStringLiteral("generation"), p_generation}};
  QJsonObject result;
  evaluateNavigation(
      p_page,
      QStringLiteral("const payload = %1; "
                     "vxcore.setRenderedHtml(payload.html, payload.generation); "
                     "return {submitted: true};")
          .arg(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))),
      result);
  if (p_wait) {
    waitForDocument(p_page, p_generation);
  }
}

void TestMarkdownViewerJs::waitForDocument(QWebEnginePage &p_page, const QString &p_generation) {
  QJsonObject result;
  const auto finished = [&]() {
    evaluateNavigation(p_page, QStringLiteral(R"JS(
return {generation: vxcore.documentGeneration, finished: vxcore.numOfOngoingWorkers === 0 &&
  vxcore.pendingData.document === null, completions: __completedGenerations};
)JS"),
                       result);
    return result.value(QStringLiteral("generation")).toString() == p_generation &&
           result.value(QStringLiteral("finished")).toBool();
  };
  QTRY_VERIFY_WITH_TIMEOUT(finished(), 10000);
  int completions = 0;
  for (const auto &value : result.value(QStringLiteral("completions")).toArray()) {
    completions += value.toString() == p_generation;
  }
  QCOMPARE(completions, 1);
}

void TestMarkdownViewerJs::setupPresentationPage(QWebEnginePage &p_page) {
  setupNavigationPage(p_page, true);
  QString error;
  QString source = QStringLiteral(R"JS(
window.__presentationReports = [];
window.__nativeKeys = [];
window.vxMarkdownAdapter.setPresentationState = function(active, error) {
  __presentationReports.push({active, error});
};
window.vxMarkdownAdapter.setKeyPress = function(key) { __nativeKeys.push(key); };
window.vxMarkdownAdapter.setSavedContent = function(generation, head, styles, html, classes) {
  window.__savedPresentationContent = {generation, styles, html, classes};
};
window.vxMarkdownAdapter.onPdfRenderReady = function() { window.__exportReady = true; };
window.vxI18n = {tr: function(key) { return key; }};
)JS");
  for (const auto *name : {"nodelinemapper.js", "easyaccess.js", "vxworker.js",
                           "codeblockactions.js", "reveal/reveal.js", "presentation.js"}) {
    source += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  source += QStringLiteral("\nwindow.vxEasyAccess.setupViNavigation();\n");
  QJsonArray styles;
  for (const auto *name : {"js/reveal/reset.css", "js/reveal/reveal.css", "js/reveal/black.css",
                           "css/presentation.css"}) {
    styles.append(readFile(webDir() + QLatin1Char('/') + QLatin1String(name), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  const QJsonObject assets{{QStringLiteral("source"), source}, {QStringLiteral("styles"), styles}};
  QJsonObject result;
  evaluateNavigation(
      p_page,
      QStringLiteral(R"JS(
const assets = %1;
for (const text of assets.styles) {
  const style = document.createElement('style');
  style.setAttribute('data-vx-presentation-style', '');
  style.media = 'not all';
  style.textContent = text;
  document.head.appendChild(style);
}
const script = document.createElement('script');
script.textContent = assets.source;
document.head.appendChild(script);
script.remove();
return {ready: !!window.vxPresentation};
)JS")
          .arg(QString::fromUtf8(QJsonDocument(assets).toJson(QJsonDocument::Compact))),
      result);
  QVERIFY(result.value(QStringLiteral("ready")).toBool());
}

void TestMarkdownViewerJs::setPresentationActive(QWebEnginePage &p_page, bool p_active,
                                                 bool p_darkTheme) {
  QJsonObject result;
  evaluateNavigation(p_page,
                     QStringLiteral("vxPresentation.setActive(%1, %2); return {requested: true};")
                         .arg(p_active ? QStringLiteral("true") : QStringLiteral("false"),
                              p_darkTheme ? QStringLiteral("true") : QStringLiteral("false")),
                     result);
  const auto update = [&]() {
    evaluateNavigation(p_page, QStringLiteral(R"JS(
const reports = window.__presentationReports;
const last = reports.length ? reports[reports.length - 1] : {};
return {active: vxPresentation.isActive(), ready: !!document.querySelector('#vx-presentation.ready'),
        settled: vxPresentation.session === null, failure: last.error || ''};
)JS"),
                       result);
    return result.value(QStringLiteral("failure")).toString().isEmpty() &&
           (p_active ? result.value(QStringLiteral("ready")).toBool()
                     : result.value(QStringLiteral("settled")).toBool());
  };
  QTRY_VERIFY_WITH_TIMEOUT(update(), 10000);
  QCOMPARE(result.value(QStringLiteral("active")).toBool(), p_active);
}

void TestMarkdownViewerJs::testPresentation_followsThemeAndRestoresReader() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupPresentationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
// Presentation must restore the reader even when its colors differ from the deck.
vxcore.contentContainer.innerHTML = '<h2>Theme</h2><p>Text <code>inline</code></p>' +
  '<pre><code>const answer = 42;</code></pre><a href="https://example.com">Link</a>';
document.body.style.backgroundColor = 'rgb(240, 240, 240)';
document.body.style.color = 'rgb(30, 30, 30)';
window.__readerHtmlClass = document.documentElement.getAttribute('class');
window.__readerBodyClass = document.body.getAttribute('class');
window.__readerColorScheme = getComputedStyle(document.documentElement).colorScheme;
return {prepared: true};
)JS"),
                     result);

  for (bool dark : {true, false, true}) {
    setPresentationActive(page, true, dark);
    evaluateNavigation(page, QStringLiteral(R"JS(
const root = document.getElementById('vx-presentation');
return {
  background: getComputedStyle(root).backgroundColor,
  foreground: getComputedStyle(root.querySelector('h2')).color,
  link: getComputedStyle(root.querySelector('a')).color,
  code: getComputedStyle(root.querySelector('pre')).backgroundColor,
  inlineCode: getComputedStyle(root.querySelector('p code')).backgroundColor,
  colorScheme: getComputedStyle(document.documentElement).colorScheme
};
)JS"),
                       result);
    QCOMPARE(result.value(QStringLiteral("background")).toString(),
             dark ? QStringLiteral("rgb(25, 25, 25)") : QStringLiteral("rgb(255, 255, 255)"));
    QCOMPARE(result.value(QStringLiteral("foreground")).toString(),
             dark ? QStringLiteral("rgb(255, 255, 255)") : QStringLiteral("rgb(34, 34, 34)"));
    QCOMPARE(result.value(QStringLiteral("link")).toString(),
             dark ? QStringLiteral("rgb(66, 175, 250)") : QStringLiteral("rgb(42, 118, 221)"));
    const auto codeBackground =
        dark ? QStringLiteral("rgba(255, 255, 255, 0.08)") : QStringLiteral("rgba(0, 0, 0, 0.06)");
    QCOMPARE(result.value(QStringLiteral("code")).toString(), codeBackground);
    QCOMPARE(result.value(QStringLiteral("inlineCode")).toString(), codeBackground);
    QCOMPARE(result.value(QStringLiteral("colorScheme")).toString(),
             dark ? QStringLiteral("dark") : QStringLiteral("light"));
    setPresentationActive(page, false);
    evaluateNavigation(page, QStringLiteral(R"JS(
return {
  restored: document.documentElement.getAttribute('class') === __readerHtmlClass &&
    document.body.getAttribute('class') === __readerBodyClass &&
    getComputedStyle(document.documentElement).colorScheme === __readerColorScheme &&
    getComputedStyle(document.body).backgroundColor === 'rgb(240, 240, 240)' &&
    getComputedStyle(document.body).color === 'rgb(30, 30, 30)'
};
)JS"),
                       result);
    QVERIFY(result.value(QStringLiteral("restored")).toBool());
  }
}

void TestMarkdownViewerJs::testPresentation_groupingAndRestoration() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupPresentationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
const content = vxcore.contentContainer;
content.innerHTML = `
<!-- Leading whitespace and comments are not a preamble slide. -->
<h1 id="title">Title</h1><p id="intro">Introduction</p>
<h2 id="parent">Parent <em id="emphasis">heading</em></h2><p>Parent body</p>
<h3 id="child">Child</h3>
<section id="math" class="eqno"><eqn><svg id="equation" viewBox="0 0 20 20">
  <defs><path id="glyph" d="M0 0L10 10"/></defs><use href="#glyph"/></svg></eqn></section>
<div id="diagram" class="vx-mermaid-graph"><svg viewBox="0 0 120 40">
  <text x="4" y="20" fill="#222">Rendered diagram</text></svg></div>
<section id="semantic" data-state="unwanted"><p>User section</p>
  <section id="nested">Nested section</section></section>
<blockquote><h3 id="quote">Quoted heading</h3><p id="quote-body">Quoted body</p></blockquote>
<ul><li><h2 id="list-heading">List heading</h2><p>List body</p></li></ul>
<div class="code-toolbar"><pre><code id="code">one\ntwo\nthree\nfour\nfive</code></pre>
  <div class="toolbar"></div></div>
<p><a id="protected" href="#">Protected notebook link</a></p>
<p><span id="fragment" class="fragment" data-fragment-index="1">Always visible</span></p>
<button id="note-fullscreen" class="enter-fullscreen">Note control</button>
<h4 id="minor">Minor heading</h4><p>Same child slide</p>
<h2 id="second">Second</h2><h3 id="last">Last child</h3>`;
document.body.style.backgroundColor = 'rgb(250, 250, 250)';
document.body.style.padding = '13px';
window.__originalBodyStyle = document.body.getAttribute('style');
window.__folding = new HeadingFolding(content);
__folding.setEnabled(true);
__folding.refresh();
for (const button of content.querySelectorAll('.vx-heading-fold-toggle')) {
  __folding.setExpanded(button, document.getElementById(button.getAttribute('aria-controls')), false);
}
// This fixture already contains enhanced math/diagram DOM. Admit only its
// remaining code-actions pass, with the same token and debt as the live core.
const generation = 'presentation-grouping';
const codeActions = vxcore.getWorker('codeblockactions');
vxcore.documentGeneration = generation;
vxcore.ongoingWorkers = new Set([codeActions.name]);
vxcore.numOfOngoingWorkers = vxcore.ongoingWorkers.size;
codeActions.beginWork(generation);
vxcore.emit('basicMarkdownRendered', generation);
window.__protected = document.getElementById('protected');
vxcore.navigationLinkDestinations.set(__protected, 'private.md#destination');
window.__originalNodes = Array.from(content.querySelectorAll('*'));
window.__originalHtml = content.outerHTML;
window.__originalText = content.textContent;
window.__originalInlineStyles = __originalNodes.map(node => node.style.cssText);
window.__originalBody = document.body.getAttribute('class');
window.__originalRoot = document.documentElement.getAttribute('class');
window.__equation = document.getElementById('equation');
window.__code = document.getElementById('code');
window.__fullscreenRequests = 0;
Element.prototype.requestFullscreen = function() { ++__fullscreenRequests; return Promise.resolve(); };
window.__readerY = 120;
window.scrollTo(0, __readerY);
return {prepared: true};
)JS"),
                     result);
  waitForDocument(page, QStringLiteral("presentation-grouping"));

  // Repetition catches destruction leaks and stale placeholder/section restoration state.
  for (int pass = 0; pass < 2; ++pass) {
    setPresentationActive(page, true);
    evaluateNavigation(page, QStringLiteral(R"JS(
const root = document.getElementById('vx-presentation');
const slides = Array.from(root.querySelector('.slides').children);
const deck = vxPresentation.session.deck;
const child = document.getElementById('child').closest('.vx-presentation-slide');
const header = child.querySelector('.vx-presentation-parent');
vxcore.scrollToAnchor('child');
const collapse = child.querySelector('.vx-collapse-btn');
collapse.click();
const collapsed = child.querySelector('.code-toolbar').classList.contains('vx-collapsed');
collapse.click();
document.getElementById('note-fullscreen').click();
return {
  count: deck.getSlides().length,
  boundaries: slides.map(slide => Array.from(slide.querySelector('.vx-presentation-body').children)
    .find(node => /^H[1-3]$/.test(node.tagName)).id),
  allHorizontal: slides.length === root.querySelectorAll('.slides section').length &&
    root.querySelector('.stack') === null,
  titleIntro: slides[0].classList.contains('vx-presentation-title') && slides[0].contains(document.getElementById('intro')),
  parentText: header.textContent.trim(), parentIds: header.querySelectorAll('[id]').length,
  nestedStay: ['math', 'semantic', 'nested', 'quote', 'list-heading', 'minor']
    .every(id => child.contains(document.getElementById(id))),
  retagged: ['math', 'semantic', 'nested'].every(id => document.getElementById(id).tagName === 'DIV'),
  diagramSurface: getComputedStyle(document.getElementById('diagram')).backgroundColor === 'rgb(250, 250, 250)',
  expanded: !root.querySelector('.vx-heading-fold-content[hidden]'),
  controlsHidden: getComputedStyle(document.getElementById('child').querySelector('button')).display === 'none',
  originalObjects: __equation === document.getElementById('equation') && __code === document.getElementById('code'),
  protectedDestination: vxcore.navigationLinkDestinations.get(document.getElementById('protected')),
  codeAction: collapsed && !child.querySelector('.code-toolbar').classList.contains('vx-collapsed'),
  fragmentVisible: getComputedStyle(document.getElementById('fragment')).visibility === 'visible',
  anchorSlide: deck.getCurrentSlide() === child,
  nativeFullscreenOnly: __fullscreenRequests === 0
};
)JS"),
                       result);
    QCOMPARE(result.value(QStringLiteral("count")).toInt(), 5);
    QCOMPARE(result.value(QStringLiteral("boundaries")).toArray(),
             QJsonArray({QStringLiteral("title"), QStringLiteral("parent"), QStringLiteral("child"),
                         QStringLiteral("second"), QStringLiteral("last")}));
    QCOMPARE(result.value(QStringLiteral("parentText")).toString(),
             QStringLiteral("Parent heading"));
    QCOMPARE(result.value(QStringLiteral("parentIds")).toInt(), 0);
    QCOMPARE(result.value(QStringLiteral("protectedDestination")).toString(),
             QStringLiteral("private.md#destination"));
    for (const auto *key : {"allHorizontal", "titleIntro", "nestedStay", "retagged", "expanded",
                            "controlsHidden", "originalObjects", "codeAction", "fragmentVisible",
                            "diagramSurface", "anchorSlide", "nativeFullscreenOnly"}) {
      QVERIFY2(result.value(QLatin1String(key)).toBool(), key);
    }
    setPresentationActive(page, false);
    evaluateNavigation(page, QStringLiteral(R"JS(
const nodes = Array.from(vxcore.contentContainer.querySelectorAll('*'));
return {
  readerContent: vxcore.contentContainer.textContent === __originalText,
  readerInlineStyles: nodes.every((node, i) => node.style.cssText === __originalInlineStyles[i]),
  readerFolds: Array.from(vxcore.contentContainer.querySelectorAll('.vx-heading-fold-content')).every(node => node.hidden),
  sameNodes: nodes.length === __originalNodes.length && nodes.every((node, i) => node === __originalNodes[i]),
  readerClasses: document.body.getAttribute('class') === __originalBody &&
    document.documentElement.getAttribute('class') === __originalRoot,
  readerStyles: document.body.getAttribute('style') === __originalBodyStyle,
  scroll: window.scrollY === __readerY,
  stylesDisabled: Array.from(document.querySelectorAll('[data-vx-presentation-style]'))
    .every(style => style.media === 'not all'),
  noDeck: !document.getElementById('vx-presentation')
};
)JS"),
                       result);
    for (const auto *key :
         {"readerContent", "readerInlineStyles", "readerFolds", "sameNodes", "readerClasses",
          "readerStyles", "scroll", "stylesDisabled", "noDeck"}) {
      QVERIFY2(result.value(QLatin1String(key)).toBool(), key);
    }
  }

  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.contentContainer.innerHTML = '<p id="preamble">Preamble</p><h1 id="first">In flow</h1>' +
  '<h3 id="orphan">Orphan</h3><h2 id="parent-two">Parent</h2><h3 id="under-parent">Child</h3>' +
  '<h1 id="reset">Reset ancestry without splitting</h1><h3 id="after-reset">Orphan again</h3>';
return {prepared: true};
)JS"),
                     result);
  setPresentationActive(page, true);
  evaluateNavigation(page, QStringLiteral(R"JS(
const slides = vxPresentation.session.deck.getSlides();
return {count: slides.length,
  preamble: slides[0].contains(document.getElementById('preamble')) && slides[0].contains(document.getElementById('first')),
  noTitle: !document.querySelector('.vx-presentation-title'),
  parents: slides.map(slide => !!slide.querySelector('.vx-presentation-parent')),
  noH1Split: document.getElementById('reset').closest('.vx-presentation-slide') === slides[3]};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 5);
  QCOMPARE(result.value(QStringLiteral("parents")).toArray(),
           QJsonArray({false, false, false, true, false}));
  QVERIFY(result.value(QStringLiteral("preamble")).toBool());
  QVERIFY(result.value(QStringLiteral("noTitle")).toBool());
  QVERIFY(result.value(QStringLiteral("noH1Split")).toBool());
  setPresentationActive(page, false);
}

void TestMarkdownViewerJs::testPresentation_pinnedHeaderScrolling() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupPresentationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
const content = vxcore.contentContainer;
content.innerHTML = '<h2 id="parent">Pinned parent</h2><h3 id="child">Long child</h3>' +
  Array.from({length: 80}, (_, i) => '<p>Paragraph ' + i + '</p>').join('') + '<h3 id="next">Next child</h3>';
return {prepared: true};
)JS"),
                     result);
  setPresentationActive(page, true);
  evaluateNavigation(page, QStringLiteral(R"JS(
vxPresentation.scrollToAnchor('child');
const deck = vxPresentation.session.deck;
const slide = deck.getCurrentSlide();
const body = slide.querySelector('.vx-presentation-body');
const header = slide.querySelector('.vx-presentation-parent');
const key = value => document.dispatchEvent(new KeyboardEvent('keydown', {key:value, bubbles:true, cancelable:true}));
const before = header.getBoundingClientRect().top;
key('PageDown');
const pageDown = body.scrollTop > 0 && deck.getCurrentSlide() === slide;
key('End');
const atEnd = Math.abs(body.scrollTop + body.clientHeight - body.scrollHeight) <= 1;
const pinned = Math.abs(header.getBoundingClientRect().top - before) < 0.5;
key('ArrowRight');
const next = deck.getCurrentSlide().contains(document.getElementById('next'));
key('ArrowLeft');
const previous = deck.getCurrentSlide() === slide;
key('Home');
key('ArrowDown');
const down = body.scrollTop > 0 && body.scrollTop < body.scrollHeight - body.clientHeight;
key('Escape');
key('f');
return {pageDown, atEnd, pinned, next, previous, down,
  overflow: body.scrollHeight > body.clientHeight && getComputedStyle(body).overflowY === 'auto',
  usableHeight: body.clientHeight > 300 && header.getBoundingClientRect().bottom <= body.getBoundingClientRect().top + 1,
  blackTheme: getComputedStyle(header.querySelector('h2')).color === 'rgb(255, 255, 255)',
  nativeEscapeOnly: __nativeKeys.length === 1 && __nativeKeys[0] === 27,
  noWebFullscreen: !document.fullscreenElement,
  hashUnchanged: !location.hash};
)JS"),
                     result);
  for (const auto *key :
       {"pageDown", "atEnd", "pinned", "next", "previous", "down", "overflow", "usableHeight",
        "blackTheme", "nativeEscapeOnly", "noWebFullscreen", "hashUnchanged"}) {
    QVERIFY2(result.value(QLatin1String(key)).toBool(), key);
  }
  setPresentationActive(page, false);
}

void TestMarkdownViewerJs::testPresentation_cancellationRenderAndExport() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupPresentationPage(page);
  renderNavigationDocument(page, QStringLiteral("## Only\n\nBody"),
                           QStringLiteral("presentation-1"));
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__originalHtml = vxcore.contentContainer.outerHTML;
vxPresentation.setActive(true, true);
vxPresentation.setActive(false);
return {restoredImmediately: vxcore.contentContainer.outerHTML === __originalHtml && !vxPresentation.isActive()};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("restoredImmediately")).toBool());
  const auto settled = [&]() {
    evaluateNavigation(
        page,
        QStringLiteral("return {settled: vxPresentation.session === null, "
                       "noActiveReport: !__presentationReports.some(report => report.active)};"),
        result);
    return result.value(QStringLiteral("settled")).toBool();
  };
  QTRY_VERIFY_WITH_TIMEOUT(settled(), 10000);
  QVERIFY(result.value(QStringLiteral("noActiveReport")).toBool());

  // A new request must survive the old, canceled initialize() completing later.
  evaluateNavigation(page, QStringLiteral(R"JS(
vxPresentation.setActive(true, true);
vxPresentation.setActive(false);
vxPresentation.setActive(true, true);
return {requested: true};
)JS"),
                     result);
  setPresentationActive(page, true);
  setPresentationActive(page, false);

  evaluateNavigation(page, QStringLiteral(R"JS(
const bundledReveal = window.Reveal;
window.Reveal = undefined;
vxPresentation.setActive(true, true);
window.Reveal = bundledReveal;
const report = __presentationReports[__presentationReports.length - 1];
return {failedClosed: report.active === false && report.error.length > 0 &&
  vxPresentation.session === null && vxcore.contentContainer.outerHTML === __originalHtml};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("failedClosed")).toBool());

  evaluateNavigation(page, QStringLiteral(R"JS(
// Reinstall the native-produced fragment with a new producer token; the real
// protected document installation must finish before presentation can begin.
vxcore.setRenderedHtml(vxcore.contentContainer.innerHTML, 'presentation-deferred');
vxPresentation.setActive(true, true);
const deferred = vxPresentation.isActive() === false;
return {deferred};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("deferred")).toBool());
  setPresentationActive(page, true);
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.saveContent(vxcore.documentGeneration);
return {restored: vxcore.contentContainer.outerHTML === __originalHtml,
  html: __savedPresentationContent.html === __originalHtml,
  generation: __savedPresentationContent.generation === vxcore.documentGeneration,
  noPresentationStyles: !__savedPresentationContent.styles.includes('--r-main-font') &&
    __savedPresentationContent.styles.includes('vx-presentation') === false,
  noPresentationClasses: !__savedPresentationContent.classes.includes('vx-presentation'),
  stylesRestored: document.querySelectorAll('[data-vx-presentation-style]').length === 4,
  inactive: !vxPresentation.isActive()};
)JS"),
                     result);
  for (const auto *key : {"restored", "html", "generation", "noPresentationStyles",
                          "noPresentationClasses", "stylesRestored", "inactive"}) {
    QVERIFY2(result.value(QLatin1String(key)).toBool(), key);
  }
  setPresentationActive(page, true);
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.prepareForExport({});
return {restoredBeforeExport: !vxPresentation.isActive() && vxcore.contentContainer.outerHTML === __originalHtml};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("restoredBeforeExport")).toBool());
  setPresentationActive(page, true);
  renderNavigationDocument(page, QStringLiteral("## Replacement"), QStringLiteral("presentation-2"),
                           false);
  evaluateNavigation(page, QStringLiteral(R"JS(
return {restoredBeforeRender: !vxPresentation.isActive() && !document.getElementById('vx-presentation'),
  noPlaceholders: !vxcore.contentContainer.innerHTML.includes('vx-presentation')};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("restoredBeforeRender")).toBool());
  QVERIFY(result.value(QStringLiteral("noPlaceholders")).toBool());
  const auto rendered = [&]() {
    evaluateNavigation(page, QStringLiteral(R"JS(
return {finished: vxcore.numOfOngoingWorkers === 0,
  replacement: vxcore.contentContainer.textContent.trim() === 'Replacement',
  noOldContent: document.getElementById('only') === null,
  inactive: vxPresentation.isActive() === false};
)JS"),
                       result);
    return result.value(QStringLiteral("finished")).toBool();
  };
  QTRY_VERIFY_WITH_TIMEOUT(rendered(), 10000);
  QVERIFY(result.value(QStringLiteral("replacement")).toBool());
  QVERIFY(result.value(QStringLiteral("noOldContent")).toBool());
  QVERIFY(result.value(QStringLiteral("inactive")).toBool());
}

void TestMarkdownViewerJs::testNavigation_visibleRectangles_data() {
  QTest::addColumn<qreal>("zoom");
  QTest::newRow("normal") << qreal(1);
  QTest::newRow("125-percent") << qreal(1.25);
  QTest::newRow("150-percent") << qreal(1.5);
}

void TestMarkdownViewerJs::testNavigation_visibleRectangles() {
  QFETCH(qreal, zoom);
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  page.setZoomFactor(zoom);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
const content = vxcore.contentContainer;
content.innerHTML = `
<a href="duplicate.md" style="position:absolute;left:20px;top:60px;width:100px;height:20px">Later</a>
<a href="duplicate.md" style="position:absolute;left:20px;top:20px;width:100px;height:20px">Earlier</a>
<a href="#destination" style="position:absolute;left:150px;top:20px;width:100px;height:20px">Anchor</a>
<a href="partial.md" style="position:absolute;left:-10px;top:130px;width:50px;height:20px">Partial</a>
<div style="position:absolute;left:40px;top:170px;width:80px;height:20px;overflow:hidden">
  <a href="clipped.md" style="position:absolute;left:-10px;top:10px;width:120px;height:30px">Clipped</a>
  <a href="fully-clipped.md" style="position:absolute;left:0;top:40px">Clipped out</a>
</div>
<div id="wrap-box" style="position:absolute;left:20px;top:230px;width:80px;height:28px;overflow:hidden">
  <div style="position:relative;top:-20px;font:14px/20px monospace">
    <a id="wrapped" href="wrapped.md">one two three four five six seven eight nine ten</a>
  </div>
</div>
<a href="display-hidden.md" style="display:none">Hidden</a>
<div style="visibility:hidden"><a href="visibility-hidden.md">Hidden</a></div>
<div style="opacity:0"><a href="transparent.md">Transparent</a></div>
<details><summary>Folded</summary><a href="folded.md">Folded content</a></details>
<a href="zero.md" style="position:absolute;width:0;height:0">Zero area</a>
<a href="offscreen.md" style="position:absolute;top:1200px">Offscreen</a>
<a href="">Empty</a><a href="#">Empty fragment</a><a href="http://[invalid">Invalid</a>`;
const snapshot = vxcore.getNavigationTargets();
const wrapped = document.getElementById('wrapped');
const clip = document.getElementById('wrap-box').getBoundingClientRect();
const fragments = Array.from(wrapped.getClientRects());
const firstVisible = fragments.find(rect => rect.bottom > clip.top && rect.top < clip.bottom);
return {
  targets: snapshot.targets,
  destinations: snapshot.targets.map(target => vxcore.resolveNavigationTarget(snapshot.snapshot, target.index).href),
  duplicateUrls: [snapshot.targets[0], snapshot.targets[2]].map(target =>
    vxcore.resolveNavigationTarget(snapshot.snapshot, target.index).url),
  firstWrappedFragmentHidden: fragments[0].bottom <= clip.top,
  wrappedRect: {x: Math.max(clip.left, firstVisible.left), y: Math.max(clip.top, firstVisible.top),
                width: Math.min(clip.right, firstVisible.right) - Math.max(clip.left, firstVisible.left),
                height: Math.min(clip.bottom, firstVisible.bottom) - Math.max(clip.top, firstVisible.top)}
};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("destinations")).toArray(),
           QJsonArray::fromStringList({"duplicate.md", "#destination", "duplicate.md", "partial.md",
                                       "clipped.md", "wrapped.md"}));
  QCOMPARE(result.value(QStringLiteral("duplicateUrls")).toArray(),
           QJsonArray::fromStringList(
               {"file:///navigation/duplicate.md", "file:///navigation/duplicate.md"}));
  const auto targets = result.value(QStringLiteral("targets")).toArray();
  QCOMPARE(targets.size(), 6);
  QVERIFY(targets[0].toObject().value(QStringLiteral("index")) !=
          targets[2].toObject().value(QStringLiteral("index")));
  const auto checkRect = [&targets](int p_index, qreal p_x, qreal p_y, qreal p_width,
                                    qreal p_height) {
    const auto target = targets[p_index].toObject();
    QCOMPARE(target.value(QStringLiteral("x")).toDouble(), p_x);
    QCOMPARE(target.value(QStringLiteral("y")).toDouble(), p_y);
    QCOMPARE(target.value(QStringLiteral("width")).toDouble(), p_width);
    QCOMPARE(target.value(QStringLiteral("height")).toDouble(), p_height);
  };
  // CSS coordinates remain unscaled at all web zooms; C++ applies the zoom once.
  checkRect(0, 20, 20, 100, 20);
  checkRect(1, 150, 20, 100, 20);
  checkRect(2, 20, 60, 100, 20);
  checkRect(3, 0, 130, 40, 20);
  checkRect(4, 40, 180, 80, 10);
  QVERIFY(result.value(QStringLiteral("firstWrappedFragmentHidden")).toBool());
  const auto wrapped = result.value(QStringLiteral("wrappedRect")).toObject();
  checkRect(5, wrapped.value(QStringLiteral("x")).toDouble(),
            wrapped.value(QStringLiteral("y")).toDouble(),
            wrapped.value(QStringLiteral("width")).toDouble(),
            wrapped.value(QStringLiteral("height")).toDouble());
}

void TestMarkdownViewerJs::testContextLink_hitTesting_data() {
  QTest::addColumn<qreal>("zoom");
  QTest::newRow("normal") << qreal(1);
  QTest::newRow("125-percent") << qreal(1.25);
  QTest::newRow("150-percent") << qreal(1.5);
}

void TestMarkdownViewerJs::testContextLink_hitTesting() {
  QFETCH(qreal, zoom);
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  page.setZoomFactor(zoom);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
const content = vxcore.contentContainer;
content.innerHTML = `
<a id="nested" href="sibling%20note.md#beta%20heading" style="position:absolute;left:20px;top:20px;width:140px;height:24px"><span>Nested <em>label</em></span></a>
<a href="#beta%20heading" style="position:absolute;left:190px;top:20px;width:100px;height:24px">Fragment</a>
<a href="https://example.com/note#heading" style="position:absolute;left:20px;top:60px;width:140px;height:24px">External</a>
<a href="http://[invalid" style="position:absolute;left:20px;top:100px;width:140px;height:24px">Invalid</a>
<a href="" style="position:absolute;left:20px;top:140px;width:140px;height:24px">Empty</a>
<a href="#" style="position:absolute;left:20px;top:180px;width:140px;height:24px">Empty fragment</a>
<a href="hidden.md" style="position:absolute;left:20px;top:220px;width:140px;height:24px;visibility:hidden">Hidden</a>
<div style="position:absolute;left:20px;top:260px;opacity:0"><a href="transparent.md">Transparent</a></div>
<div style="position:absolute;left:20px;top:300px;width:60px;height:20px;overflow:hidden">
  <a href="clipped.md" style="position:absolute;left:50px;top:0;width:80px;height:20px">Clipped</a>
</div>
<a href="covered.md" style="position:absolute;left:200px;top:100px;width:100px;height:24px">Covered</a>
<div style="position:absolute;left:200px;top:100px;width:100px;height:24px;background:white">Cover</div>
<div style="position:absolute;left:200px;top:160px;width:80px;font:14px/20px monospace">
  <a id="wrapped-context" href="wrapped.md">one two three four five six</a>
</div>
<a href="  " style="position:absolute;left:20px;top:340px;width:140px;height:24px">Whitespace</a>
<div style="position:absolute;left:20px;top:380px;display:none"><a href="display-hidden.md">Hidden</a></div>`;
document.body.insertAdjacentHTML('beforeend', '<a href="outside.md" style="position:fixed;left:350px;top:20px;width:120px;height:24px">Outside content</a>');
const nested = document.querySelector('#nested em').getBoundingClientRect();
const wrapped = Array.from(document.getElementById('wrapped-context').getClientRects());
const last = wrapped[wrapped.length - 1];
return {
  nested: vxcore.resolveContextLink(nested.left + nested.width / 2, nested.top + nested.height / 2),
  fragment: vxcore.resolveContextLink(200, 30),
  external: vxcore.resolveContextLink(30, 70),
  clipped: vxcore.resolveContextLink(75, 310),
  wrapped: vxcore.resolveContextLink(last.left + last.width / 2, last.top + last.height / 2),
  wrappedLines: wrapped.length,
  rejected: [[30, 110], [30, 150], [30, 190], [30, 230], [30, 270], [100, 310],
    [210, 110], [360, 30], [30, 350], [30, 390], [500, 400], [-1, -1],
    [window.innerWidth + 1, 30], [30, window.innerHeight + 1], [NaN, 30], [30, Infinity],
    ['30', 30]].every(point => vxcore.resolveContextLink(point[0], point[1]) === null)
};
)JS"),
                     result);
  const auto nested = result.value(QStringLiteral("nested")).toObject();
  QCOMPARE(nested.value(QStringLiteral("href")).toString(),
           QStringLiteral("sibling%20note.md#beta%20heading"));
  QCOMPARE(nested.value(QStringLiteral("url")).toString(),
           QStringLiteral("file:///navigation/sibling%20note.md#beta%20heading"));
  const auto fragment = result.value(QStringLiteral("fragment")).toObject();
  QCOMPARE(fragment.value(QStringLiteral("href")).toString(), QStringLiteral("#beta%20heading"));
  QCOMPARE(fragment.value(QStringLiteral("url")).toString(),
           QStringLiteral("file:///navigation/current.md#beta%20heading"));
  QCOMPARE(
      result.value(QStringLiteral("external")).toObject().value(QStringLiteral("url")).toString(),
      QStringLiteral("https://example.com/note#heading"));
  QCOMPARE(
      result.value(QStringLiteral("clipped")).toObject().value(QStringLiteral("href")).toString(),
      QStringLiteral("clipped.md"));
  QVERIFY(result.value(QStringLiteral("wrappedLines")).toInt() > 1);
  QCOMPARE(
      result.value(QStringLiteral("wrapped")).toObject().value(QStringLiteral("href")).toString(),
      QStringLiteral("wrapped.md"));
  QVERIFY(result.value(QStringLiteral("rejected")).toBool());

  evaluateNavigation(page, QStringLiteral(R"JS(
const link = document.getElementById('nested');
link.setAttribute('href', 'changed.md#new');
const changed = vxcore.resolveContextLink(30, 30);
document.querySelector('base').href = 'file:///another/current.md';
const rebased = vxcore.resolveContextLink(30, 30);
link.remove();
return {changed, rebased, removed: vxcore.resolveContextLink(30, 30) === null};
)JS"),
                     result);
  QCOMPARE(
      result.value(QStringLiteral("changed")).toObject().value(QStringLiteral("url")).toString(),
      QStringLiteral("file:///navigation/changed.md#new"));
  QCOMPARE(
      result.value(QStringLiteral("rebased")).toObject().value(QStringLiteral("url")).toString(),
      QStringLiteral("file:///another/changed.md#new"));
  QVERIFY(result.value(QStringLiteral("removed")).toBool());
}

void TestMarkdownViewerJs::testContextLink_protectedDestinations() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page, true);
  renderNavigationDocument(
      page, QStringLiteral(R"MD([Relative](private.md#target) [Fragment](#target%20name)
[External](https://example.com/path)
<a href="file:///private/note.md">File</a>
<a href="javascript:alert(1)">Javascript</a>
<a href="//example.com/path">Network</a>)MD"),
      QStringLiteral("context-protected-1"));
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__contextAt = function(element) {
  const rect = element.getBoundingClientRect();
  return vxcore.resolveContextLink(rect.left + rect.width / 2, rect.top + rect.height / 2);
};
const links = Array.from(vxcore.contentContainer.querySelectorAll('a'));
const relative = links.find(link => link.textContent === 'Relative');
window.__oldContextLink = relative;
const fragment = links.find(link => link.textContent === 'Fragment');
const external = links.find(link => link.textContent === 'External');
const blocked = links.filter(link => ['File', 'Javascript', 'Network'].includes(link.textContent));
const injected = document.createElement('a');
injected.href = 'vxprotected-resource://capability/target';
injected.textContent = 'Unregistered capability';
vxcore.contentContainer.appendChild(injected);
return {relative: __contextAt(relative), fragment: __contextAt(fragment), external: __contextAt(external),
  relativeDomHref: relative.getAttribute('href'), blockedCount: blocked.length,
  blocked: blocked.every(link => __contextAt(link) === null),
  unregistered: __contextAt(injected) === null};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("relativeDomHref")).toString(), QStringLiteral("#"));
  QCOMPARE(
      result.value(QStringLiteral("relative")).toObject().value(QStringLiteral("href")).toString(),
      QStringLiteral("private.md#target"));
  QCOMPARE(
      result.value(QStringLiteral("relative")).toObject().value(QStringLiteral("url")).toString(),
      QStringLiteral("file:///navigation/private.md#target"));
  QCOMPARE(
      result.value(QStringLiteral("fragment")).toObject().value(QStringLiteral("href")).toString(),
      QStringLiteral("#target%20name"));
  QCOMPARE(
      result.value(QStringLiteral("external")).toObject().value(QStringLiteral("href")).toString(),
      QStringLiteral("https://example.com/path"));
  QCOMPARE(result.value(QStringLiteral("blockedCount")).toInt(), 3);
  QVERIFY(result.value(QStringLiteral("blocked")).toBool());
  QVERIFY(result.value(QStringLiteral("unregistered")).toBool());

  renderNavigationDocument(page, QStringLiteral("[New](new.md)"),
                           QStringLiteral("context-protected-2"));
  evaluateNavigation(page, QStringLiteral(R"JS(
const fresh = vxcore.contentContainer.querySelector('a');
const destination = __contextAt(fresh);
vxcore.contentContainer.appendChild(__oldContextLink);
return {destination, stale: __contextAt(__oldContextLink) === null};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("destination"))
               .toObject()
               .value(QStringLiteral("href"))
               .toString(),
           QStringLiteral("new.md"));
  QVERIFY(result.value(QStringLiteral("stale")).toBool());
}

void TestMarkdownViewerJs::testContextLink_renderReadiness() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page, true);
  renderNavigationDocument(page, QStringLiteral("[Initial](initial.md)"),
                           QStringLiteral("context-ready-1"));
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__resolveFirstContextLink = function() {
  const rect = document.querySelector('#vx-content a').getBoundingClientRect();
  return vxcore.resolveContextLink(rect.left + rect.width / 2, rect.top + rect.height / 2);
};
const initial = __resolveFirstContextLink();
vxcore.initialized = false;
const notReady = __resolveFirstContextLink() === null;
vxcore.initialized = true;
const container = vxcore.contentContainer;
vxcore.contentContainer = null;
const noContainer = __resolveFirstContextLink() === null;
vxcore.contentContainer = container;
return {initial, notReady, noContainer};
)JS"),
                     result);
  QCOMPARE(
      result.value(QStringLiteral("initial")).toObject().value(QStringLiteral("href")).toString(),
      QStringLiteral("initial.md"));
  QVERIFY(result.value(QStringLiteral("notReady")).toBool());
  QVERIFY(result.value(QStringLiteral("noContainer")).toBool());

  // Hold real protected rendering, then queue a newer document while its transport is pending.
  renderNavigationDocument(
      page, QStringLiteral("[Held](held.md) ![Held image](data:image/png;base64,AAAA)"),
      QStringLiteral("context-ready-2"), false);
  evaluateNavigation(page, QStringLiteral(R"JS(
return {held: typeof __finishImage === 'function', updating: vxcore.numOfOngoingWorkers > 0,
  rejected: __resolveFirstContextLink() === null};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("held")).toBool());
  QVERIFY(result.value(QStringLiteral("updating")).toBool());
  QVERIFY(result.value(QStringLiteral("rejected")).toBool());
  renderNavigationDocument(page, QStringLiteral("[Replacement](replacement.md#done)"),
                           QStringLiteral("context-ready-3"), false);
  evaluateNavigation(page, QStringLiteral(R"JS(
const result = {pending: vxcore.pendingData.document !== null,
  rejected: __resolveFirstContextLink() === null};
__finishImage('');
return result;
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("pending")).toBool());
  QVERIFY(result.value(QStringLiteral("rejected")).toBool());
  waitForDocument(page, QStringLiteral("context-ready-3"));
  evaluateNavigation(page, QStringLiteral("return {destination: __resolveFirstContextLink()};"),
                     result);
  QCOMPARE(result.value(QStringLiteral("destination"))
               .toObject()
               .value(QStringLiteral("href"))
               .toString(),
           QStringLiteral("replacement.md#done"));
}

void TestMarkdownViewerJs::testNavigation_staleSnapshots() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  renderNavigationDocument(page, QStringLiteral("[Original](sibling.md#first) [Second](#second)"),
                           QStringLiteral("navigation-1"));
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
let snapshot = vxcore.getNavigationTargets();
const initial = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index);
const first = document.querySelector('#vx-content a');
first.setAttribute('href', 'changed.md');
const changedHrefRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
first.setAttribute('href', 'sibling.md#first');
const old = snapshot;
snapshot = vxcore.getNavigationTargets();
const supersededRejected = vxcore.resolveNavigationTarget(old.snapshot, old.targets[0].index) === null;
window.scrollTo(0, 10);
const actualScroll = window.scrollY;
const scrolledRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
window.scrollTo(0, 0);
snapshot = vxcore.getNavigationTargets();
first.replaceWith(first.cloneNode(true));
const replacementRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
snapshot = vxcore.getNavigationTargets();
document.querySelector('#vx-content a').remove();
const removedRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
snapshot = vxcore.getNavigationTargets();
document.querySelector('#vx-content a').style.visibility = 'hidden';
const hiddenRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
return {initial, changedHrefRejected, supersededRejected, actualScroll, scrolledRejected,
        replacementRejected, removedRejected, hiddenRejected};
)JS"),
                     result);
  QCOMPARE(
      result.value(QStringLiteral("initial")).toObject().value(QStringLiteral("url")).toString(),
      QStringLiteral("file:///navigation/sibling.md#first"));
  QCOMPARE(result.value(QStringLiteral("actualScroll")).toDouble(), 10.0);
  for (const auto *key : {"changedHrefRejected", "supersededRejected", "scrolledRejected",
                          "replacementRejected", "removedRejected", "hiddenRejected"}) {
    QVERIFY2(result.value(QLatin1String(key)).toBool(), key);
  }
  renderNavigationDocument(page, QStringLiteral("[Fresh](replacement.md#target)"),
                           QStringLiteral("navigation-2"));
  evaluateNavigation(page, QStringLiteral(R"JS(
let snapshot = vxcore.getNavigationTargets();
document.querySelector('base').href = 'file:///another/current.md';
const baseChangedRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
document.querySelector('base').href = 'file:///navigation/current.md';
window.__oldNavigationSnapshot = vxcore.getNavigationTargets();
return {baseChangedRejected};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("baseChangedRejected")).toBool());
  renderNavigationDocument(page, QStringLiteral("[Newest](newest.md)"),
                           QStringLiteral("navigation-3"));
  evaluateNavigation(page, QStringLiteral(R"JS(
const old = __oldNavigationSnapshot;
const fresh = vxcore.getNavigationTargets();
return {rerenderRejected: vxcore.resolveNavigationTarget(old.snapshot, old.targets[0].index) === null,
  freshDestination: vxcore.resolveNavigationTarget(fresh.snapshot, fresh.targets[0].index),
  invalidIndexRejected: vxcore.resolveNavigationTarget(fresh.snapshot, -1) === null
    && vxcore.resolveNavigationTarget(fresh.snapshot, 0.5) === null
    && vxcore.resolveNavigationTarget(fresh.snapshot, fresh.targets.length) === null};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("rerenderRejected")).toBool());
  QVERIFY(result.value(QStringLiteral("invalidIndexRejected")).toBool());
  QCOMPARE(result.value(QStringLiteral("freshDestination"))
               .toObject()
               .value(QStringLiteral("href"))
               .toString(),
           QStringLiteral("newest.md"));
}

void TestMarkdownViewerJs::testNavigation_protectedDestinations() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page, true);
  QJsonObject result;
  renderNavigationDocument(
      page, QStringLiteral(R"MD([Fragment](#target%20name) [Relative](sibling.md#target)
[HTTP](http://example.com/path) [HTTPS](https://example.com/path)
<a href="javascript:alert(1)">Javascript</a>
<a href="data:text/html,blocked">Data</a>
<a href="file:///private/note.md">File</a>
<a href="//example.com/path">Network path</a>
<a href="/absolute.md">Absolute</a>
<a href="mailto:user@example.com">Mail</a>)MD"),
      QStringLiteral("protected-1"));
  evaluateNavigation(page, QStringLiteral(R"JS(
const snapshot = vxcore.getNavigationTargets();
window.__protectedSnapshot = snapshot;
const destinations = snapshot.targets.map(target => vxcore.resolveNavigationTarget(snapshot.snapshot, target.index));
const links = Array.from(vxcore.contentContainer.querySelectorAll('a'));
const fragment = links.find(link => link.textContent === 'Fragment');
const relative = links.find(link => link.textContent === 'Relative');
const relativeDomHref = relative.getAttribute('href');
window.__savedProtectedLink = relative;
const syntheticTrust = [];
relative.addEventListener('click', event => syntheticTrust.push(event.isTrusted), true);
fragment.click();
const syntheticCancelled = !relative.dispatchEvent(new MouseEvent('click', {bubbles:true, cancelable:true}));
const relativeTarget = snapshot.targets.find(target =>
  vxcore.resolveNavigationTarget(snapshot.snapshot, target.index).href === 'sibling.md#target');
relative.setAttribute('href', '#changed');
const tamperedRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, relativeTarget.index) === null;
relative.setAttribute('href', '#');
return {destinations, relativeDomHref, syntheticCancelled, syntheticTrust,
        protectedActivations: __protectedLinks, anchorActivations: __anchors, tamperedRejected,
        forbidden: links.filter(link => !link.hasAttribute('href')).map(link => link.textContent)};
)JS"),
                     result);
  const auto destinations = result.value(QStringLiteral("destinations")).toArray();
  QCOMPARE(destinations.size(), 4);
  QStringList hrefs;
  for (const auto &destination : destinations) {
    hrefs.append(destination.toObject().value(QStringLiteral("href")).toString());
  }
  QCOMPARE(hrefs, (QStringList{"#target%20name", "sibling.md#target", "http://example.com/path",
                               "https://example.com/path"}));
  QCOMPARE(destinations[1].toObject().value(QStringLiteral("url")).toString(),
           QStringLiteral("file:///navigation/sibling.md#target"));
  QCOMPARE(result.value(QStringLiteral("relativeDomHref")).toString(), QStringLiteral("#"));
  QCOMPARE(result.value(QStringLiteral("forbidden")).toArray(),
           QJsonArray::fromStringList(
               {"Javascript", "Data", "File", "Network path", "Absolute", "Mail"}));
  QVERIFY(result.value(QStringLiteral("syntheticCancelled")).toBool());
  QCOMPARE(result.value(QStringLiteral("syntheticTrust")).toArray(), QJsonArray({false}));
  QCOMPARE(result.value(QStringLiteral("protectedActivations")).toArray(), QJsonArray());
  QCOMPARE(result.value(QStringLiteral("anchorActivations")).toArray(), QJsonArray());
  QVERIFY(result.value(QStringLiteral("tamperedRejected")).toBool());

  renderNavigationDocument(page, QStringLiteral("[New document](new.md)"),
                           QStringLiteral("protected-2"));
  evaluateNavigation(page, QStringLiteral(R"JS(
// Even if an old sanitized element is reattached, its original href belongs to the old document.
vxcore.contentContainer.appendChild(__savedProtectedLink);
const snapshot = vxcore.getNavigationTargets();
window.__beforePendingSnapshot = snapshot;
return {
  oldRejected: vxcore.resolveNavigationTarget(__protectedSnapshot.snapshot,
    __protectedSnapshot.targets[0].index) === null,
  hrefs: snapshot.targets.map(target => vxcore.resolveNavigationTarget(snapshot.snapshot, target.index).href)
};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("oldRejected")).toBool());
  QCOMPARE(result.value(QStringLiteral("hrefs")).toArray(), QJsonArray::fromStringList({"new.md"}));

  // Hold the actual protected renderer's image transport while a newer document arrives.
  renderNavigationDocument(
      page, QStringLiteral("[Pending](pending.md) ![Held](data:image/png;base64,AAAA)"),
      QStringLiteral("protected-3"), false);
  evaluateNavigation(page, QStringLiteral(R"JS(
const rendering = vxcore.getNavigationTargets();
const oldRejected = vxcore.resolveNavigationTarget(__beforePendingSnapshot.snapshot,
  __beforePendingSnapshot.targets[0].index) === null;
window.__renderingSnapshot = {oldRejected, targets: rendering.targets};
return {held: typeof __finishImage === 'function'};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("held")).toBool());
  renderNavigationDocument(page, QStringLiteral("[Replacement](replacement.md)"),
                           QStringLiteral("protected-4"), false);
  evaluateNavigation(page, QStringLiteral(R"JS(
const pending = vxcore.getNavigationTargets();
window.__pendingSnapshot = pending;
const result = {oldRejected: __renderingSnapshot.oldRejected,
                renderingTargets: __renderingSnapshot.targets, pendingTargets: pending.targets,
                transportHeld: typeof __finishImage === 'function'};
__finishImage('');
return result;
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("oldRejected")).toBool());
  QVERIFY(result.value(QStringLiteral("transportHeld")).toBool());
  QCOMPARE(result.value(QStringLiteral("renderingTargets")).toArray(), QJsonArray());
  QCOMPARE(result.value(QStringLiteral("pendingTargets")).toArray(), QJsonArray());
  waitForDocument(page, QStringLiteral("protected-4"));
  evaluateNavigation(page, QStringLiteral(R"JS(
const snapshot = vxcore.getNavigationTargets();
return {
  pendingRejected: vxcore.resolveNavigationTarget(__pendingSnapshot.snapshot, 0) === null,
  hrefs: snapshot.targets.map(target => vxcore.resolveNavigationTarget(snapshot.snapshot, target.index).href)
};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("pendingRejected")).toBool());
  QCOMPARE(result.value(QStringLiteral("hrefs")).toArray(),
           QJsonArray::fromStringList({"replacement.md"}));
}

void TestMarkdownViewerJs::testSanitizedUrls_data() {
  QTest::addColumn<QString>("tag");
  QTest::addColumn<QString>("attribute");
  QTest::addColumn<QString>("url");
  QTest::addColumn<QString>("expected");
  const auto blocked = [](const char *name, const char *tag, const char *attribute,
                          const char *url) {
    QTest::newRow(name) << QString::fromLatin1(tag) << QString::fromLatin1(attribute)
                        << QString::fromUtf8(url) << QString();
  };
  const auto allowed = [](const char *name, const char *url, const char *expected) {
    QTest::newRow(name) << QStringLiteral("a") << QStringLiteral("href") << QString::fromUtf8(url)
                        << QString::fromUtf8(expected);
  };
  blocked("plain-script", "a", "href", "javascript:void(window.__urlProbe=42)");
  blocked("named-tab", "a", "href", "java&Tab;script:void(window.__urlProbe=42)");
  blocked("named-newline", "area", "href", "java&NewLine;script:void(0)");
  blocked("named-colon", "audio", "src", "javascript&colon;void(0)");
  blocked("numeric-tab", "img", "src", "java&#9;script:void(0)");
  blocked("hex-newline", "video", "src", "java&#x0a;script:void(0)");
  blocked("encoded-cr", "video", "poster", "java&#13;script:void(0)");
  blocked("leading-controls", "blockquote", "cite", "&#1;&#32;JaVaScRiPt:void(0)");
  blocked("vbscript", "a", "href", "vb&Tab;script:MsgBox(1)");
  blocked("data-link", "a", "href", "d&Tab;ata:text/html,probe");
  blocked("data-html-image", "img", "src", "data:text/html,probe");
  blocked("data-not-image", "img", "src", "data:image/png-not-a-mime,probe");
  blocked("data-image-link", "a", "href", "data:image/png;base64,AA==");
  blocked("data-poster", "video", "poster", "data:image/png;base64,AA==");
  blocked("invalid-url", "a", "href", "http://[invalid");
  allowed("relative-query", "sibling.md?a=1&amp;b=2#part", "sibling.md?a=1&b=2#part");
  allowed("heading", "#part", "#part");
  allowed("file", "file:///navigation/sibling.md", "file:///navigation/sibling.md");
  allowed("https", "https://example.invalid/a?x=1&amp;y=2", "https://example.invalid/a?x=1&y=2");
  allowed("http", "http://example.invalid/", "http://example.invalid/");
  allowed("mail", "mailto:user@example.invalid", "mailto:user@example.invalid");
  allowed("custom", "vnote-test:note/123", "vnote-test:note/123");
  allowed("double-encoded", "java&amp;Tab;script:literal", "java&Tab;script:literal");
  allowed("quote", "notes/a&quot;b.md", "notes/a\"b.md");
  allowed("empty", "", "");
  const auto gif = QStringLiteral("data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAE"
                                  "AAAAALAAAAAABAAEAAAIBRAA7");
  QTest::newRow("embedded-raster") << QStringLiteral("img") << QStringLiteral("src") << gif << gif;
  const auto svg = QStringLiteral("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg'"
                                  " width='1' height='1'%3E%3C/svg%3E");
  QTest::newRow("embedded-svg") << QStringLiteral("img") << QStringLiteral("src") << svg << svg;
}

void TestMarkdownViewerJs::testSanitizedUrls() {
  QFETCH(QString, tag);
  QFETCH(QString, attribute);
  QFETCH(QString, url);
  QFETCH(QString, expected);
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral("vxOptions.protectFromXss = true; return {};"), result);
  const auto html =
      QStringLiteral("<%1 id=\"url-probe\" %2=\"%3\"></%1>\n").arg(tag, attribute, url);
  renderNavigationDocument(page, html, QStringLiteral("url-1"));
  const auto check = QStringLiteral(R"JS(
const node = vxcore.contentContainer.querySelector('#url-probe');
const attribute = '%1';
const value = node.getAttribute(attribute);
return {present: node.hasAttribute(attribute), value,
  protocol: value === null ? null : new URL(value, document.baseURI).protocol};
)JS")
                         .arg(attribute);
  evaluateNavigation(page, check, result);
  QVERIFY2(result.value(QStringLiteral("present")).toBool() == !expected.isNull(),
           QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
  if (!expected.isNull()) {
    QCOMPARE(result.value(QStringLiteral("value")).toString(), expected);
  } else {
    QVERIFY(result.value(QStringLiteral("protocol")).isNull());
  }
}

void TestMarkdownViewerJs::testSanitizedUrls_activation() {
  // A positive control proves this real engine executes the same authored URL when
  // protection is off. void keeps successful script execution from replacing the page.
  const auto source = QStringLiteral(
      "<a id=\"url-probe\" href=\"java&Tab;script:void(window.__urlProbe=42)\">Probe</a>");
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QJsonObject result;
  renderNavigationDocument(page, source, QStringLiteral("url-control"));
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__urlProbe = 0;
vxcore.contentContainer.querySelector('#url-probe').click();
return {};
)JS"),
                     result);
  const auto executed = [&]() {
    evaluateNavigation(page, QStringLiteral("return {marker: window.__urlProbe};"), result);
    return result.value(QStringLiteral("marker")).toInt() == 42;
  };
  QTRY_VERIFY_WITH_TIMEOUT(executed(), 10000);
  evaluateNavigation(page, QStringLiteral("vxOptions.protectFromXss = true; return {};"), result);
  renderNavigationDocument(page, source, QStringLiteral("url-protected"));
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__urlProbe = 0;
window.__urlClickSettled = false;
const node = vxcore.contentContainer.querySelector('#url-probe');
node.click();
setTimeout(() => { window.__urlClickSettled = true; }, 100);
return {hasHref: node.hasAttribute('href')};
)JS"),
                     result);
  QVERIFY(!result.value(QStringLiteral("hasHref")).toBool());
  const auto settled = [&]() {
    evaluateNavigation(
        page,
        QStringLiteral("return {settled: window.__urlClickSettled, marker: window.__urlProbe};"),
        result);
    return result.value(QStringLiteral("settled")).toBool();
  };
  QTRY_VERIFY_WITH_TIMEOUT(settled(), 10000);
  QCOMPARE(result.value(QStringLiteral("marker")).toInt(), 0);

  QWebEnginePage protectedPage(&profile);
  setupNavigationPage(protectedPage, true);
  renderNavigationDocument(protectedPage, source, QStringLiteral("url-encrypted"));
  evaluateNavigation(
      protectedPage,
      QStringLiteral("return {hasHref: "
                     "vxcore.contentContainer.querySelector('#url-probe').hasAttribute('href')};"),
      result);
  QVERIFY(!result.value(QStringLiteral("hasHref")).toBool());
}

void TestMarkdownViewerJs::testCustomAnchors_data() {
  QTest::addColumn<QString>("html");
  QTest::addColumn<QString>("expectedAnchor");
  QTest::addColumn<bool>("block");
  struct Case {
    const char *name;
    const char *html;
    const char *anchor;
  };
  const Case cases[] = {
      {"exported-id", "<a id=\"m3-node-726f6f74\">target</a>", "m3-node-726f6f74"},
      {"allowed-alphabet", "<a id=\"Az_09-x\">target</a>", "Az_09-x"},
      {"link-attributes",
       "<a id=\"m3-node-x\" href=\"#m3-node-y\" title=\"Jump\" target=\"_self\" "
       "onclick=\"alert(1)\">target</a>",
       "m3-node-x"},
      {"former-prefix-only", "<a id=\"m3-node-\">target</a>", "m3-node-"},
      {"empty-id", "<a id=\"\">target</a>", ""},
      {"application-name", "<a id=\"vx-content\">target</a>", ""},
      {"single-digit", "<a id=\"9\">target</a>", "9"},
      {"double-quote", "<a id='anchor\" onclick=\"alert(1)'>target</a>", ""},
      {"single-quote", "<a id=\"anchor'y\">target</a>", ""},
      {"named-entity", "<a id='anchor&quot; onclick=&quot;alert(1)'>target</a>", ""},
      {"numeric-entity", "<a id='anchor&#34; onclick=&#34;alert(1)'>target</a>", ""},
      {"space", "<a id=\"anchor y\">target</a>", ""},
      {"embedded-newline", "<a id=\"anchor\ny\">target</a>", ""},
      {"tab", "<a id=\"anchor\ty\">target</a>", ""},
      {"html-delimiter", "<a id=\"anchor<y\">target</a>", ""},
      {"non-ascii", "<a id=\"中文\">target</a>", "中文"},
      {"other-tag", "<span id=\"anchor\">target</span>", ""},
      {"legacy-name", "<a name=\"anchor\">target</a>", ""},
      {"javascript-link", "<a href=\"javascript:alert(1)\" onclick=\"alert(1)\">target</a>", ""},
      {"data-link", "<a href=\"data:text/html,injected\">target</a>", ""}};
  for (const auto &entry : cases) {
    for (bool block : {false, true}) {
      const QByteArray row = QByteArray(entry.name) + (block ? "-block" : "-inline");
      QTest::newRow(row.constData())
          << QString::fromUtf8(entry.html) << QString::fromUtf8(entry.anchor) << block;
    }
  }
}

void TestMarkdownViewerJs::testCustomAnchors() {
  QFETCH(QString, html);
  QFETCH(QString, expectedAnchor);
  QFETCH(bool, block);
  const bool linkAttributes = html.contains(QStringLiteral("#m3-node-y"));
  if (block) {
    html = QStringLiteral("<div>\n") + html + QStringLiteral("\n</div>\n");
  }
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral("vxOptions.protectFromXss = true; return {};"), result);
  renderNavigationDocument(page, html, QStringLiteral("anchors-1"));
  evaluateNavigation(page, QStringLiteral(R"JS(
const target = vxcore.contentContainer.querySelector('a, span');
return {text: target.textContent, id: target.id, href: target.getAttribute('href'),
  title: target.title, target: target.getAttribute('target'),
  elements: vxcore.contentContainer.querySelectorAll('a, span').length,
  injectedHandler: !!vxcore.contentContainer.querySelector('[onclick], script, img'),
  activeHref: Array.from(vxcore.contentContainer.querySelectorAll('[href]')).some(node =>
    ['javascript:', 'vbscript:', 'data:'].includes(
      new URL(node.getAttribute('href'), document.baseURI).protocol))};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("target"));
  QCOMPARE(result.value(QStringLiteral("elements")).toInt(), 1);
  QVERIFY(!result.value(QStringLiteral("injectedHandler")).toBool());
  QVERIFY(!result.value(QStringLiteral("activeHref")).toBool());
  if (!expectedAnchor.isEmpty()) {
    QCOMPARE(result.value(QStringLiteral("id")).toString(), expectedAnchor);
  }
  if (linkAttributes) {
    QCOMPARE(result.value(QStringLiteral("href")).toString(), QStringLiteral("#m3-node-y"));
    QCOMPARE(result.value(QStringLiteral("title")).toString(), QStringLiteral("Jump"));
    QCOMPARE(result.value(QStringLiteral("target")).toString(), QStringLiteral("_self"));
  }
}

void TestMarkdownViewerJs::testMathHeadings_linkTargets() {
  const auto markdown =
      QStringLiteral("[toc]\n\n```cpp\nstruct CacheEntry {};\n```\n\n"
                     "## 1. $a*b=c$\n\n## 2. $a*b=c$\n\n## $a*b=c$\n\n## Sum $$a+b$$\n\n"
                     "## Plain `code` and **bold** [link][ref]\n\n[ref]: https://example.com");
  const auto headings = vnotex::MarkdownHtmlRenderer::render(markdown.toUtf8(), {}, {}, false);
  QVERIFY2(headings.error.isEmpty(), qPrintable(headings.error));
  const auto expected =
      QJsonArray::fromStringList({"abc", "abc-1", "abc-2", "sum-ab", "plain-code-and-bold-link"});
  QJsonArray copiedAnchors;
  const QList<int> lines{6, 8, 10, 12, 14};
  QCOMPARE(headings.headings.size(), lines.size());
  for (int index = 0; index < lines.size(); ++index) {
    QCOMPARE(headings.headings[index].line, lines[index]);
    copiedAnchors.append(headings.headings[index].anchor);
  }
  QCOMPARE(copiedAnchors, expected);
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral("vxOptions.protectFromXss = true; return {};"), result);
  renderNavigationDocument(page, markdown, QStringLiteral("math-headings"));
  evaluateNavigation(page, QStringLiteral(R"JS(
function targets(selector) {
  return Array.from(vxcore.contentContainer.querySelectorAll(selector), link => link.getAttribute('href').substring(1));
}
return {
  renderedIds: Array.from(vxcore.contentContainer.querySelectorAll('h2'), node => node.id),
  tocTargets: targets('nav a'), permalinkTargets: targets('.vx-header-anchor'),
  inlineMath: vxcore.contentContainer.querySelector('h2 eq').textContent,
  displayMath: vxcore.contentContainer.querySelector('h2 eqn').textContent,
  sourceLines: Array.from(vxcore.contentContainer.querySelectorAll('h2'), node => Number(node.dataset.sourceLine))
};
)JS"),
                     result);
  for (const auto *name : {"renderedIds", "tocTargets", "permalinkTargets"}) {
    QCOMPARE(result.value(QLatin1String(name)).toArray(), expected);
  }
  QCOMPARE(result.value(QStringLiteral("sourceLines")).toArray(), QJsonArray({6, 8, 10, 12, 14}));
  QCOMPARE(result.value(QStringLiteral("inlineMath")).toString(), QStringLiteral("$a*b=c$"));
  QCOMPARE(result.value(QStringLiteral("displayMath")).toString(), QStringLiteral("$$a+b$$"));
}

void TestMarkdownViewerJs::testCmarkMathTableDom_data() {
  QTest::addColumn<bool>("sanitize");
  QTest::newRow("sanitize-off") << false;
  QTest::newRow("sanitize-on") << true;
}

void TestMarkdownViewerJs::testCmarkMathTableDom() {
  QFETCH(bool, sanitize);
  const QByteArray formulas[] = {"$p_1$", "$a\\$b$", "$\\text{</eq><img>}$", "$$\np_1\n$$",
                                 "$$\np_2\n$$"};
  QStringList fragments;
  for (const auto &formula : formulas) {
    const auto rendered = vnotex::MarkdownHtmlRenderer::render(formula, {});
    QVERIFY2(rendered.error.isEmpty(), qPrintable(rendered.error));
    fragments.append(rendered.html);
  }
  const auto tableHtml =
      QStringLiteral("<table><tbody><tr><td colspan=\"2\"><!--vte-md:$p_1$-->%1</td></tr>"
                     "<tr><td>%2</td><td>%3</td></tr>"
                     "<tr><td>%4</td><td>%5</td></tr>"
                     "<tr><td>$literal$</td><td><code>$code$</code></td></tr></tbody></table>")
          .arg(fragments[0], fragments[1], fragments[2], fragments[3], fragments[4]);
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page,
                     QStringLiteral("vxOptions.protectFromXss = %1; return {};")
                         .arg(sanitize ? QStringLiteral("true") : QStringLiteral("false")),
                     result);
  // The editor's table HTML passes through the same native producer and DOM installer as a note.
  renderNavigationDocument(page, tableHtml, QStringLiteral("math-table"));
  evaluateNavigation(page, QStringLiteral(R"JS(
const table = vxcore.contentContainer.querySelector('table');
const merged = table.rows[0].cells[0];
const mergedMath = merged.querySelector('eq.tex-to-render');
const escapedDollarMath = table.rows[1].cells[0].querySelector('eq.tex-to-render');
const tagMath = table.rows[1].cells[1].querySelector('eq.tex-to-render');
return {
  tableCount: vxcore.contentContainer.querySelectorAll('table').length,
  rowCellCounts: Array.from(table.rows, row => row.cells.length), colSpan: merged.colSpan,
  mergedText: mergedMath.textContent, escapedDollarText: escapedDollarMath.textContent,
  tagText: tagMath.textContent, tagNodeTypes: Array.from(tagMath.childNodes, node => node.nodeType),
  imageCount: table.querySelectorAll('img').length, inlineCount: table.querySelectorAll('eq').length,
  displayCount: table.querySelectorAll('eqn').length, markedMathCount: table.querySelectorAll('.tex-to-render').length,
  blockText: table.rows[2].cells[0].querySelector('section > eqn.tex-to-render').textContent,
  secondBlockText: table.rows[2].cells[1].querySelector('section > eqn.tex-to-render').textContent,
  literalText: table.rows[3].cells[0].textContent, codeText: table.rows[3].cells[1].querySelector('code').textContent
};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("tableCount")).toInt(), 1);
  QCOMPARE(result.value(QStringLiteral("rowCellCounts")).toArray(), QJsonArray({1, 2, 2, 2}));
  QCOMPARE(result.value(QStringLiteral("colSpan")).toInt(), 2);
  QCOMPARE(result.value(QStringLiteral("mergedText")).toString(), QStringLiteral("$p_1$"));
  QCOMPARE(result.value(QStringLiteral("escapedDollarText")).toString(), QStringLiteral("$a\\$b$"));
  QCOMPARE(result.value(QStringLiteral("tagText")).toString(),
           QStringLiteral("$\\text{</eq><img>}$"));
  QCOMPARE(result.value(QStringLiteral("tagNodeTypes")).toArray(), QJsonArray({3}));
  QCOMPARE(result.value(QStringLiteral("imageCount")).toInt(), 0);
  QCOMPARE(result.value(QStringLiteral("inlineCount")).toInt(), 3);
  QCOMPARE(result.value(QStringLiteral("displayCount")).toInt(), 2);
  QCOMPARE(result.value(QStringLiteral("markedMathCount")).toInt(), 5);
  const QStringList blockTexts{result.value(QStringLiteral("blockText")).toString(),
                               result.value(QStringLiteral("secondBlockText")).toString()};
  for (int index = 0; index < blockTexts.size(); ++index) {
    const auto &text = blockTexts[index];
    // Display guards and TeX content are consumed by the math renderer; whitespace
    // immediately inside the guards is not part of the formula's visible meaning.
    QVERIFY(text.startsWith(QStringLiteral("$$")) && text.endsWith(QStringLiteral("$$")));
    QCOMPARE(text.mid(2, text.size() - 4).trimmed(), QStringLiteral("p_%1").arg(index + 1));
  }
  QCOMPARE(result.value(QStringLiteral("literalText")).toString(), QStringLiteral("$literal$"));
  QCOMPARE(result.value(QStringLiteral("codeText")).toString(), QStringLiteral("$code$"));
}

void TestMarkdownViewerJs::testMathRenderer_exportImages_data() {
  QTest::addColumn<qreal>("pixelRatio");
  QTest::newRow("1x") << qreal(1);
  QTest::newRow("3x") << qreal(3);
}

void TestMarkdownViewerJs::testMathRenderer_exportImages() {
  QFETCH(qreal, pixelRatio);
  const QJsonObject fixtures{
      {QStringLiteral("pixelRatio"), pixelRatio},
      {QStringLiteral("js"), QUrl::fromLocalFile(webDir() + QStringLiteral("/js")).toString()}};
  QJsonObject result;
  runCrossCopyScript(
      QStringLiteral("const fixtures = %1;\n")
              .arg(QString::fromUtf8(QJsonDocument(fixtures).toJson(QJsonDocument::Compact))) +
          QStringLiteral(R"JS(
window.vxOptions = {mathRenderer:'katex'};
Object.defineProperty(window, 'devicePixelRatio', {value:fixtures.pixelRatio});
document.body.innerHTML = '<div id="content" style="width:640px;font:16px/1.5 Arial;color:black">'
  + '<p id="fraction">Before <eqn class="tex-to-render"></eqn> After</p>'
  + '<section id="display"><eqn class="tex-to-render"></eqn></section>'
  + '<p id="slash">Before <eqn class="tex-to-render"></eqn> After</p>'
  + '<p id="nested">Before <eqn class="tex-to-render"></eqn> After</p></div>';
window.vxcore = {
  contentContainer:document.getElementById('content'),
  registerWorker(worker) { window.mathWorker = worker; worker.vxcore = this; }
};
for (const file of ['vxworker.js', 'mathjax.js']) {
  await new Promise(resolve => Utils.loadScript(fixtures.js + '/' + file, resolve));
}
await mathWorker.initialize();
const formulas = [
  {id:'fraction', text:String.raw`\frac{1}{2}`, display:false},
  {id:'display', text:String.raw`\frac{5}{5}`, display:true},
  {id:'slash', text:String.raw`1/2`, display:false},
  {id:'nested', text:String.raw`\frac{1}{\frac{2}{3}}`, display:false}
];
for (const formula of formulas) {
  mathWorker.renderKatex(document.querySelector('#' + formula.id + ' eqn'), formula, {});
}
await mathWorker.waitForMathFonts(vxcore.contentContainer);
// A painted bar and an independent text baseline detect incorrect image offsets.
const marker = document.createElement('span');
marker.style.cssText = 'display:inline-block;width:0;height:0;padding:0;margin:0;vertical-align:baseline';
document.querySelector('#fraction eqn').after(marker);
const bar = document.querySelector('#fraction .frac-line');
bar.style.borderColor = 'rgb(200,40,60)';
const barRect = bar.getBoundingClientRect();
const originalBarOffset = (barRect.top + barRect.bottom) / 2 - marker.getBoundingClientRect().bottom;
await mathWorker.prepareForExport({rasterizeMath:false});
const liveMath = formulas.map(({id}) => {
  const host = document.getElementById(id);
  return {katex:host.querySelectorAll('.katex').length,
    mathml:host.querySelectorAll('.katex-mathml math').length,
    images:host.querySelectorAll('img[data-math-png]').length};
});
await mathWorker.prepareForExport({rasterizeMath:true});
const images = [];
for (const {id} of formulas) {
  const host = document.getElementById(id);
  const image = host.querySelector('img[data-math-png]');
  if (!image) { throw new Error('Missing exported KaTeX image: ' + id); }
  // decode() can wait for a paint frame forever on a hidden WebEngine page.
  await new Promise((resolve, reject) => {
    SvgToImage.loadImage(image.src, {}, error => error ? reject(error) : resolve());
  });
}
// Measure only after every replacement has decoded and the final layout has settled.
for (const {id} of formulas) {
  const host = document.getElementById(id);
  const image = host.querySelector('img[data-math-png]');
  const rect = image.getBoundingClientRect();
  const hostRect = host.getBoundingClientRect();
  images.push({id, src:image.src, count:host.querySelectorAll('img[data-math-png]').length,
    fragments:host.querySelectorAll('.katex, .katex-mathml, math').length,
    top:rect.top, height:rect.height, naturalHeight:image.naturalHeight,
    centerOffset:(rect.left + rect.right - hostRect.left - hostRect.right) / 2});
}
return {liveMath, images, originalBarOffset, baseline:marker.getBoundingClientRect().bottom};
)JS"),
      QUrl::fromLocalFile(webDir() + QLatin1Char('/')), result);
  const auto liveMath = result.value(QStringLiteral("liveMath")).toArray();
  QCOMPARE(liveMath.size(), 4);
  for (const auto &value : liveMath) {
    const auto entry = value.toObject();
    QCOMPARE(entry.value(QStringLiteral("katex")).toInt(), 1);
    QCOMPARE(entry.value(QStringLiteral("mathml")).toInt(), 1);
    QCOMPARE(entry.value(QStringLiteral("images")).toInt(), 0);
  }
  const auto images = result.value(QStringLiteral("images")).toArray();
  QCOMPARE(images.size(), 4);
  for (const auto &value : images) {
    const auto entry = value.toObject();
    const auto id = entry.value(QStringLiteral("id")).toString();
    QCOMPARE(entry.value(QStringLiteral("count")).toInt(), 1);
    QCOMPARE(entry.value(QStringLiteral("fragments")).toInt(), 0);
    const auto data = entry.value(QStringLiteral("src")).toString();
    QVERIFY(data.startsWith(QStringLiteral("data:image/png;base64,")));
    const auto png =
        QByteArray::fromBase64(data.mid(data.indexOf(QLatin1Char(',')) + 1).toLatin1());
    const auto raster = QImage::fromData(png, "PNG");
    QVERIFY2(!raster.isNull(), qPrintable(id));
    QRect inkBounds;
    QRect redBounds;
    for (int y = 0; y < raster.height(); ++y) {
      for (int x = 0; x < raster.width(); ++x) {
        const auto color = raster.pixelColor(x, y);
        if (color.alpha() > 32) {
          inkBounds |= QRect(x, y, 1, 1);
          if (color.red() > 150 && color.green() < 100 && color.blue() < 100) {
            redBounds |= QRect(x, y, 1, 1);
          }
        }
      }
    }
    QVERIFY2(inkBounds.isValid(), qPrintable(id));
    QVERIFY2(inkBounds.left() > 0 && inkBounds.top() > 0 &&
                 inkBounds.right() < raster.width() - 1 && inkBounds.bottom() < raster.height() - 1,
             qPrintable(QStringLiteral("%1: ink [%2,%3..%4,%5] touches raster %6x%7")
                            .arg(id)
                            .arg(inkBounds.left())
                            .arg(inkBounds.top())
                            .arg(inkBounds.right())
                            .arg(inkBounds.bottom())
                            .arg(raster.width())
                            .arg(raster.height())));
    if (id == QStringLiteral("fraction")) {
      QVERIFY(redBounds.isValid());
      QCOMPARE(entry.value(QStringLiteral("naturalHeight")).toInt(), raster.height());
      const auto barOffset = entry.value(QStringLiteral("top")).toDouble() +
                             (redBounds.top() + redBounds.bottom() + 1) / 2.0 *
                                 entry.value(QStringLiteral("height")).toDouble() /
                                 raster.height() -
                             result.value(QStringLiteral("baseline")).toDouble();
      QVERIFY(qAbs(barOffset - result.value(QStringLiteral("originalBarOffset")).toDouble()) <=
              1.5);
    } else if (id == QStringLiteral("display")) {
      QVERIFY(qAbs(entry.value(QStringLiteral("centerOffset")).toDouble()) <= 1.5);
    }
  }
}

void TestMarkdownViewerJs::testMathRenderer_initializationFanout() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupMath(page);
  QJsonObject state;
  const auto update = [&] {
    evaluateNavigation(page, QStringLiteral("return mathInitializationState();"), state);
    return state.value(QStringLiteral("scriptReady")).toBool() &&
           state.value(QStringLiteral("styleReady")).toBool();
  };
  QTRY_VERIFY_WITH_TIMEOUT(update(), 10000);
  QCOMPARE(state.value(QStringLiteral("passes")).toInt(), 0);
  QCOMPARE(state.value(QStringLiteral("previews")).toInt(), 0);
  evaluateNavigation(page, QStringLiteral("releaseMathScript(false); return {};"), state);
  QVERIFY(update());
  QCOMPARE(state.value(QStringLiteral("passes")).toInt(), 0);
  QCOMPARE(state.value(QStringLiteral("previews")).toInt(), 0);
  evaluateNavigation(page, QStringLiteral("releaseMathStyle(false); return {};"), state);
  QTRY_VERIFY_WITH_TIMEOUT((update(), state.value(QStringLiteral("previews")).toInt() == 2 &&
                                          state.value(QStringLiteral("passes")).toInt() == 1),
                           30000);
  QCOMPARE(state.value(QStringLiteral("reading")).toString(), QStringLiteral("a+b"));
  QCOMPARE(state.value(QStringLiteral("formulas")).toArray(), QJsonArray({"x", "y"}));
  QVERIFY(state.value(QStringLiteral("distinct")).toBool());
}

void TestMarkdownViewerJs::testMathRenderer_failedInitializationReleasesPass() {
  for (const bool scriptFails : {true, false}) {
    QWebEngineProfile profile;
    QWebEnginePage page(&profile);
    setupMath(page);
    QJsonObject state;
    const auto update = [&] {
      evaluateNavigation(page, QStringLiteral("return mathInitializationState();"), state);
      return state.value(QStringLiteral("scriptReady")).toBool() &&
             state.value(QStringLiteral("styleReady")).toBool();
    };
    QTRY_VERIFY_WITH_TIMEOUT(update(), 10000);
    evaluateNavigation(page,
                       QStringLiteral("releaseMathScript(%1); releaseMathStyle(%2); return {};")
                           .arg(scriptFails ? QStringLiteral("true") : QStringLiteral("false"),
                                scriptFails ? QStringLiteral("false") : QStringLiteral("true")),
                       state);
    QTRY_VERIFY_WITH_TIMEOUT((update(), state.value(QStringLiteral("previews")).toInt() == 2 &&
                                            state.value(QStringLiteral("passes")).toInt() == 1),
                             10000);
    QVERIFY(state.value(QStringLiteral("failed")).toBool());
    QCOMPARE(state.value(QStringLiteral("source")).toString(), QStringLiteral("$a+b$"));
    evaluateNavigation(
        page,
        QStringLiteral("vxcore.documentGeneration = 'math-2'; "
                       "mathWorker.beginWork(vxcore.documentGeneration); "
                       "mathWorker.render(vxcore.contentContainer, 'tex-to-render'); return {};"),
        state);
    QTRY_VERIFY_WITH_TIMEOUT((update(), state.value(QStringLiteral("passes")).toInt() == 2), 10000);
    QCOMPARE(state.value(QStringLiteral("previews")).toInt(), 2);
  }
}

void TestMarkdownViewerJs::testMathRenderer_ignoresUnrelatedFontReadiness() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  page.setVisible(false);
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.setHtml(QStringLiteral("<!doctype html><html><body><iframe width='640' height='480' "
                              "srcdoc=\"<!doctype html><html><body><div id='content'></div>"
                              "<div id='preview'></div></body></html>\"></iframe></body></html>"),
               QUrl::fromLocalFile(webDir() + QLatin1Char('/')));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());

  QString source = QStringLiteral(R"JS(
window.vxOptions = {mathRenderer: 'katex'};
window.__mathFontsTest = {started: false, readDone: 0, previewDone: false, exportDone: false};
window.vxcore = {
  contentContainer: document.getElementById('content'),
  registerWorker(worker) { window.mathWorker = worker; worker.vxcore = this; },
  getWorker() { return {getCodeNodes() { return []; }}; },
  finishWorker() { ++__mathFontsTest.readDone; }
};
)JS");
  QString error;
  for (const auto *file : {"utils.js", "vxworker.js", "svg-to-image.js", "mathjax.js"}) {
    source += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(file), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  const QJsonObject assets{
      {QStringLiteral("path"), QUrl::fromLocalFile(webDir() + QStringLiteral("/js")).toString()}};
  source += QStringLiteral("mathWorker.scriptFolderPath = %1.path;\n")
                .arg(QString::fromUtf8(QJsonDocument(assets).toJson(QJsonDocument::Compact)));
  source += QStringLiteral(R"JS(
(async function() {
  try {
    const forever = new Promise(() => {});
    let release;
    const ownFont = {family: '"KaTeX_Main"', status: 'loading',
                     loaded: new Promise(resolve => { release = resolve; })};
    const unrelatedFont = {family: 'OtherDiagram', status: 'loading', loaded: forever};
    const fonts = document.fonts;
    // Keep native font loading intact; expose controlled waits only at the renderer's API boundary.
    Object.defineProperty(document, 'fonts', {value: {
      ready: forever,
      [Symbol.iterator]() { return [ownFont, unrelatedFont, ...fonts][Symbol.iterator](); }
    }});
    window.releaseMathFont = () => { ownFont.status = 'loaded'; release(); };
    await Promise.all([mathWorker.initialize(), mathWorker.initializeRasterizer()]);
    const equation = document.createElement('eq');
    equation.className = 'tex-to-render'; equation.textContent = '$x^2$';
    vxcore.contentContainer.appendChild(equation);
    vxcore.documentGeneration = 'math-fonts';
    mathWorker.beginWork(vxcore.documentGeneration);
    mathWorker.render(vxcore.contentContainer, 'tex-to-render');
    new Promise(resolve => mathWorker.renderText(document.getElementById('preview'), '$x^2$', resolve))
      .then(node => mathWorker.rasterizeHtml(node, 1))
      .then(raster => new Promise((resolve, reject) => {
        const image = new Image();
        image.onload = () => {
          const canvas = document.createElement('canvas');
          canvas.width = image.naturalWidth; canvas.height = image.naturalHeight;
          const context = canvas.getContext('2d'); context.drawImage(image, 0, 0);
          const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
          __mathFontsTest.previewHasInk = pixels.some((value, index) => index % 4 === 3 && value > 0);
          __mathFontsTest.previewDone = true;
          resolve();
        };
        image.onerror = () => reject(new Error('Math preview is not a decodable image'));
        image.src = raster.dataUrl;
      })).catch(error => { __mathFontsTest.error = String(error); });
    __mathFontsTest.started = true;
  } catch (error) { __mathFontsTest.error = String(error); }
})();
)JS");
  const QJsonObject scriptData{{QStringLiteral("source"), source}};
  QJsonObject result;
  evaluateNavigation(
      page,
      QStringLiteral("const script = document.createElement('script');"
                     "script.textContent = %1.source; document.head.appendChild(script);"
                     "script.remove(); return {installed: true};")
          .arg(QString::fromUtf8(QJsonDocument(scriptData).toJson(QJsonDocument::Compact))),
      result);
  auto update = [&]() {
    evaluateNavigation(page, QStringLiteral("return __mathFontsTest;"), result);
    return !result.contains(QStringLiteral("error"));
  };
  QTRY_VERIFY_WITH_TIMEOUT(update() && result.value(QStringLiteral("started")).toBool(), 10000);
  QCOMPARE(result.value(QStringLiteral("readDone")).toInt(), 0);
  QVERIFY(!result.value(QStringLiteral("previewDone")).toBool());
  evaluateNavigation(page, QStringLiteral("releaseMathFont(); return {released: true};"), result);
  QTRY_VERIFY_WITH_TIMEOUT(update() && result.value(QStringLiteral("readDone")).toInt() == 1 &&
                               result.value(QStringLiteral("previewDone")).toBool(),
                           10000);
  QVERIFY(result.value(QStringLiteral("previewHasInk")).toBool());
  evaluateNavigation(page, QStringLiteral(R"JS(
mathWorker.prepareForExport({rasterizeMath: true}).then(() => {
  __mathFontsTest.exportDone = true;
  __mathFontsTest.exported = Boolean(document.querySelector('.tex-to-render img[data-math-png]'));
});
return {exportStarted: true};
)JS"),
                     result);
  QTRY_VERIFY_WITH_TIMEOUT(update() && result.value(QStringLiteral("exportDone")).toBool(), 10000);
  QVERIFY(result.value(QStringLiteral("exported")).toBool());
}

void TestMarkdownViewerJs::testInstall_loadBeforeChannel() {
  // 'load' already ran: the QWebChannel callback must be the installer.
  QJSEngine engine;
  setup(engine, true);

  QCOMPARE(engine.evaluate(QStringLiteral("window.__listeners.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__listeners[0].type")).toString(),
           QStringLiteral("click"));
}

void TestMarkdownViewerJs::testInstall_channelBeforeLoad() {
  // The QWebChannel callback ran first; there was no container yet, so the
  // 'ready' handler must install.
  QJSEngine engine;
  setup(engine, false);

  QCOMPARE(engine.evaluate(QStringLiteral("window.__listeners.length")).toInt(), 0);

  auto res = engine.evaluate(QStringLiteral("window.__fireReady()"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine.evaluate(QStringLiteral("window.__listeners.length")).toInt(), 1);
}

void TestMarkdownViewerJs::testInstall_onlyOnce() {
  // Both orders happening (channel first, then 'load') must still install once.
  QJSEngine engine;
  setup(engine, true);

  auto res = engine.evaluate(QStringLiteral("window.__fireReady()"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine.evaluate(QStringLiteral("window.__listeners.length")).toInt(), 1);
}

void TestMarkdownViewerJs::testClick_forwardsLineAndState() {
  QJSEngine engine;
  setup(engine, true);

  auto res = engine.evaluate(QStringLiteral("window.__click(window.__makeEvent(7, true))"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine.evaluate(QStringLiteral("window.__toggleCalls.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__toggleCalls[0].generation")).toString(),
           QStringLiteral("document-1"));
  QCOMPARE(engine.evaluate(QStringLiteral("window.__toggleCalls[0].line")).toInt(), 7);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__toggleCalls[0].checked")).toBool(), true);
}

void TestMarkdownViewerJs::testClick_noSourceLineReverts() {
  QJSEngine engine;
  setup(engine, true);

  auto res = engine.evaluate(
      QStringLiteral("window.__ev = window.__makeEvent(null, true); window.__click(window.__ev)"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine.evaluate(QStringLiteral("window.__toggleCalls.length")).toInt(), 0);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__ev.target.checked")).toBool(), false);
}

void TestMarkdownViewerJs::testReject_revertsCheckbox() {
  QJSEngine engine;
  setup(engine, true);

  auto res = engine.evaluate(
      QStringLiteral("window.__adapter.taskListToggleRejected.emit('document-1', 3)"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine.evaluate(QStringLiteral("window.__reverted.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__reverted[0]")).toString(),
           QStringLiteral("[data-source-line=\"3\"] input.task-list-item-checkbox"));
  res = engine.evaluate(
      QStringLiteral("window.vxcore.documentGeneration = 'document-2'; "
                     "window.__adapter.taskListToggleRejected.emit('document-1', 3);"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine.evaluate(QStringLiteral("window.__reverted.length")).toInt(), 1);
}

void TestMarkdownViewerJs::testTaskList_nativeSourceMapping() {
  for (bool protectedView : {false, true}) {
    QWebEngineProfile profile;
    QWebEnginePage page(&profile);
    setupNavigationPage(page, protectedView);
    QString error;
    QString script = QStringLiteral(R"JS(
function signal() {
  const callbacks = [];
  return {connect(callback) { callbacks.push(callback); }, emit(...args) { callbacks.forEach(callback => callback(...args)); }};
}
for (const name of ['htmlUpdated', 'editLineNumberUpdated', 'anchorScrollRequested',
  'scrollRequested', 'crossCopyRequested', 'findTextRequested', 'contentRequested',
  'graphRenderDataReady', 'taskListToggleRejected', 'presentationModeRequested']) {
  vxMarkdownAdapter[name] = signal();
}
window.__taskToggles = [];
vxMarkdownAdapter.toggleTaskListItem = (generation, line, checked) => __taskToggles.push({generation, line, checked});
vxMarkdownAdapter.setReady = function() {};
window.vxWebJobs = {bind() {}, setReady() {}};
var qt = {webChannelTransport: {}};
function QWebChannel(transport, callback) { callback({objects: {vxAdapter: vxMarkdownAdapter}}); }
vxOptions.protectFromXss = true;
vxcore.getWorker('document').addLangsToSkipHighlight(['mermaid']);
)JS");
    script += readFile(webDir() + QStringLiteral("/js/markdownviewer.js"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QJsonObject result;
    evaluateNavigation(page, script + QStringLiteral("\nreturn {};"), result);
    renderNavigationDocument(
        page,
        QStringLiteral("- [ ] Parent\n  - [X] Nested\n- [x] Done\n\n```mermaid\nflowchart "
                       "LR\n```\n\n```cpp\nint x;\n```"),
        QStringLiteral("tasks-1"));
    evaluateNavigation(page, QStringLiteral(R"JS(
const boxes = Array.from(vxcore.contentContainer.querySelectorAll('input.task-list-item-checkbox'));
const initial = boxes.map(box => box.checked);
const lines = boxes.map(box => Number(box.closest('[data-source-line]').dataset.sourceLine));
boxes.forEach(box => box.click());
const documentWorker = vxcore.getWorker('document');
return {initial, lines, toggles: __taskToggles, enabled: boxes.every(box => !box.disabled),
  diagrams: documentWorker.getCodeNodes(['mermaid']).map(node => node.textContent.trim()),
  ordinaryCode: documentWorker.getCodeNodes().map(node => node.textContent.trim()),
  codeLines: Array.from(vxcore.contentContainer.querySelectorAll('pre > code'), node =>
    Number(node.closest('[data-source-line]').dataset.sourceLine))};
)JS"),
                       result);
    QCOMPARE(result.value(QStringLiteral("initial")).toArray(), QJsonArray({false, true, true}));
    QCOMPARE(result.value(QStringLiteral("lines")).toArray(), QJsonArray({0, 1, 2}));
    QVERIFY(result.value(QStringLiteral("enabled")).toBool());
    const auto toggles = result.value(QStringLiteral("toggles")).toArray();
    QCOMPARE(toggles.size(), 3);
    for (int index = 0; index < toggles.size(); ++index) {
      const auto toggle = toggles[index].toObject();
      QCOMPARE(toggle.value(QStringLiteral("generation")).toString(), QStringLiteral("tasks-1"));
      QCOMPARE(toggle.value(QStringLiteral("line")).toInt(), index);
      QCOMPARE(toggle.value(QStringLiteral("checked")).toBool(), index == 0);
    }
    QCOMPARE(result.value(QStringLiteral("diagrams")).toArray(),
             QJsonArray::fromStringList({"flowchart LR"}));
    QCOMPARE(result.value(QStringLiteral("ordinaryCode")).toArray(),
             QJsonArray::fromStringList({"int x;"}));
    QCOMPARE(result.value(QStringLiteral("codeLines")).toArray(), QJsonArray({4, 8}));
    renderNavigationDocument(page, QStringLiteral("- [ ] Replacement"), QStringLiteral("tasks-2"));
    evaluateNavigation(page, QStringLiteral(R"JS(
const box = vxcore.contentContainer.querySelector('input');
box.click();
vxMarkdownAdapter.taskListToggleRejected.emit('tasks-1', 0);
const staleUnchanged = box.checked;
vxMarkdownAdapter.taskListToggleRejected.emit('tasks-2', 0);
return {staleUnchanged, currentReverted: !box.checked, toggle: __taskToggles[3],
  diagrams: vxcore.getWorker('document').getCodeNodes(['mermaid']).length};
)JS"),
                       result);
    QVERIFY(result.value(QStringLiteral("staleUnchanged")).toBool());
    QVERIFY(result.value(QStringLiteral("currentReverted")).toBool());
    QCOMPARE(result.value(QStringLiteral("toggle"))
                 .toObject()
                 .value(QStringLiteral("generation"))
                 .toString(),
             QStringLiteral("tasks-2"));
    QCOMPARE(result.value(QStringLiteral("diagrams")).toInt(), 0);
  }
}

void TestMarkdownViewerJs::testDocument_latestPendingAndCompletion() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page, true);
  renderNavigationDocument(page,
                           QStringLiteral("[Held](held.md) ![Image](data:image/png;base64,AAAA)"),
                           QStringLiteral("held"), false);
  renderNavigationDocument(page, QStringLiteral("[Discarded](discarded.md)"),
                           QStringLiteral("discarded"), false);
  renderNavigationDocument(page, QString(), QStringLiteral("empty"), false);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__oldImageCallback = __finishImage;
const result = {completed: __completedGenerations.slice(), targets: vxcore.getNavigationTargets().targets};
__oldImageCallback('');
return result;
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("completed")).toArray(), QJsonArray());
  QCOMPARE(result.value(QStringLiteral("targets")).toArray(), QJsonArray());
  waitForDocument(page, QStringLiteral("empty"));
  evaluateNavigation(page, QStringLiteral(R"JS(
return {empty: vxcore.contentContainer.childNodes.length === 0, completed: __completedGenerations};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("empty")).toBool());
  QCOMPARE(result.value(QStringLiteral("completed")).toArray(),
           QJsonArray::fromStringList({"held", "empty"}));
  renderNavigationDocument(page, QStringLiteral("[Current](current.md)"),
                           QStringLiteral("current"));
  evaluateNavigation(page, QStringLiteral(R"JS(
__oldImageCallback('data:image/png;base64,AAAA');
vxcore.finishWorker('document', 'held');
vxcore.finishWorker('document', 'current');
const snapshot = vxcore.getNavigationTargets();
return {completed: __completedGenerations, images: vxcore.contentContainer.querySelectorAll('img').length,
  destinations: snapshot.targets.map(target => vxcore.resolveNavigationTarget(snapshot.snapshot, target.index).href)};
)JS"),
                     result);
  QCOMPARE(result.value(QStringLiteral("completed")).toArray(),
           QJsonArray::fromStringList({"held", "empty", "current"}));
  QCOMPARE(result.value(QStringLiteral("images")).toInt(), 0);
  QCOMPARE(result.value(QStringLiteral("destinations")).toArray(),
           QJsonArray::fromStringList({"current.md"}));
}

void TestMarkdownViewerJs::testHeadingFolding_enabledAndDisabledDecoration() {
  QJSEngine engine;
  setupHeadingFolding(engine);

  auto res = engine.evaluate(QStringLiteral(
      "window.__makeHeadingFixture(); window.__mapper.setHeadingFoldingEnabled(true);"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "document.body.querySelectorAll('button.vx-heading-fold-toggle').length"))
               .toInt(),
           7);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__adapter.headings[0].name")).toString(),
           QStringLiteral("Alpha"));
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__buttonFor(window.__nodes.alpha).getAttribute('aria-expanded')"))
               .toString(),
           QStringLiteral("true"));
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__buttonFor(window.__nodes.alpha).getAttribute('aria-label')"))
               .toString(),
           QStringLiteral("Collapse section"));

  res = engine.evaluate(QStringLiteral("window.__mapper.setHeadingFoldingEnabled(false);"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "document.body.querySelectorAll('section.vx-heading-fold').length"))
               .toInt(),
           0);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "document.body.querySelectorAll('button.vx-heading-fold-toggle').length"))
               .toInt(),
           0);
}

void TestMarkdownViewerJs::testHeadingFolding_hierarchyBoundaries() {
  QJSEngine engine;
  setupHeadingFolding(engine);
  auto res = engine.evaluate(QStringLiteral(
      "window.__makeHeadingFixture(); window.__mapper.setHeadingFoldingEnabled(true);"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine
               .evaluate(QStringLiteral("window.__nodes.child.parentNode.parentNode === "
                                        "window.__contentFor(window.__nodes.alpha)"))
               .toBool(),
           true);
  QCOMPARE(engine
               .evaluate(QStringLiteral("window.__nodes.grandchild.parentNode.parentNode === "
                                        "window.__contentFor(window.__nodes.child)"))
               .toBool(),
           true);
  QCOMPARE(engine
               .evaluate(QStringLiteral("window.__nodes.peer.parentNode.parentNode === "
                                        "window.__contentFor(window.__nodes.alpha)"))
               .toBool(),
           true);
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__contentFor(window.__nodes.peer).childNodes.length"))
          .toInt(),
      0);
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__contentFor(window.__nodes.empty).childNodes.length"))
          .toInt(),
      0);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__nodes.beta.parentNode.parentNode === window.__nodes.container"))
               .toBool(),
           true);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__contentFor(window.__nodes.beta).contains(window.__nodes.afterQuote)"))
               .toBool(),
           true);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__nodes.quoted.parentNode.parentNode === window.__nodes.quote"))
               .toBool(),
           true);
}

void TestMarkdownViewerJs::testHeadingFolding_buttonOnlyAndNestedState() {
  QJSEngine engine;
  setupHeadingFolding(engine);
  auto res = engine.evaluate(QStringLiteral(R"JS(
window.__makeHeadingFixture();
window.__mapper.setHeadingFoldingEnabled(true);
window.__buttonFor(window.__nodes.child).click();
window.__buttonFor(window.__nodes.alpha).click();
window.__buttonFor(window.__nodes.alpha).click();
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__contentFor(window.__nodes.child).hidden")).toBool(),
      true);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__buttonFor(window.__nodes.child).getAttribute('aria-label')"))
               .toString(),
           QStringLiteral("Expand section"));

  res = engine.evaluate(QStringLiteral(R"JS(
var before = window.__contentFor(window.__nodes.alpha).hidden;
window.__nodes.alpha.click();
window.__nodes.anchor.click();
window.__headingTextClicksIgnored =
    before === window.__contentFor(window.__nodes.alpha).hidden;
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine.evaluate(QStringLiteral("window.__headingTextClicksIgnored")).toBool(), true);
}

void TestMarkdownViewerJs::testHeadingFolding_refreshAndStaticBootstrapAreIdempotent() {
  QJSEngine engine;
  setupHeadingFolding(engine);
  auto res = engine.evaluate(QStringLiteral(R"JS(
window.__makeHeadingFixture();
window.__mapper.setHeadingFoldingEnabled(true);
window.__mapper.updateHeadingNodes();
window.__mapper.updateHeadingNodes();
var post = document.createElement('div');
post.id = 'post-content';
document.body.appendChild(post);
post.appendChild(window.__nodes.container);
window.__HeadingFolding.bootstrapStaticPage();
window.__HeadingFolding.bootstrapStaticPage();
window.__buttonFor(window.__nodes.alpha).click();
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__nodes.container._listeners.click.length")).toInt(),
      1);
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__contentFor(window.__nodes.alpha).hidden")).toBool(),
      true);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "window.__buttonFor(window.__nodes.alpha).getAttribute('aria-expanded')"))
               .toString(),
           QStringLiteral("false"));
}

void TestMarkdownViewerJs::testHeadingFolding_navigationExpandsAncestors() {
  QJSEngine engine;
  setupHeadingFolding(engine);
  auto res = engine.evaluate(QStringLiteral(R"JS(
window.__makeHeadingFixture();
window.__mapper.setHeadingFoldingEnabled(true);
window.__buttonFor(window.__nodes.child).click();
window.__buttonFor(window.__nodes.alpha).click();
window.__nodes.alpha._rect = { y: -100, top: -100, bottom: -50 };
window.__nodes.beta._rect = { y: 10, top: 10, bottom: 30 };
window.__nodes.quoted._rect = { y: 50, top: 50, bottom: 70 };
window.__currentVisibleHeading = window.__mapper.currentHeadingIndex();
window.__mapper.scrollToNode(window.__nodes.grandchild, false, false);
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine.evaluate(QStringLiteral("window.__currentVisibleHeading")).toInt(), 5);
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__contentFor(window.__nodes.alpha).hidden")).toBool(),
      false);
  QCOMPARE(
      engine.evaluate(QStringLiteral("window.__contentFor(window.__nodes.child).hidden")).toBool(),
      false);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__nodes.grandchild._scrolledIntoView")).toBool(),
           true);
}

void TestMarkdownViewerJs::testSectionNumber_policyParity_data() {
  QTest::addColumn<QJsonArray>("headings");
  QTest::addColumn<QStringList>("expected");
  QTest::addColumn<int>("generated");
  QTest::addColumn<bool>("hasSectionNumber");
  QTest::addColumn<QString>("pattern");
  QTest::addColumn<bool>("detectHeading1ForSectionNumber");
  const auto row = [](const char *name, bool detectHeading1ForSectionNumber, const char *json,
                      const QStringList &expected, int generated, bool numbered = true,
                      const QString &pattern = QStringLiteral("1.1.")) {
    QTest::newRow(name) << QJsonDocument::fromJson(json).array() << expected << generated
                        << numbered << pattern << detectHeading1ForSectionNumber;
  };

  row("title-after-prose", true, R"JSON([[1,"Title"],[2,"A"],[3,"B"],[2,"C"]])JSON",
      {"Title", "1. A", "1.1. B", "2. C"}, 3);
  row("two-h1s", true, R"JSON([[1,"A"],[2,"B"],[1,"C"]])JSON", {"1. A", "1.1. B", "2. C"}, 3);
  row("sole-nonfirst-h1", true, R"JSON([[2,"A"],[1,"B"]])JSON", {"1.1. A", "2. B"}, 2);
  row("base-h3", true, R"JSON([[1,"Title"],[3,"A"],[3,"B"]])JSON", {"Title", "1. A", "2. B"}, 2);
  row("skipped-level", true, R"JSON([[1,"Title"],[2,"A"],[4,"B"],[2,"C"]])JSON",
      {"Title", "1. A", "1.1.1. B", "2. C"}, 3);
  row("no-headings", true, "[]", {}, 0, false);
  row("title-only", true, R"JSON([[1,"Title"]])JSON", {"Title"}, 0, false);
  row("no-suffix", true, R"JSON([[1,"Title"],[2,"A"],[3,"B"]])JSON", {"Title", "1 A", "1.1 B"}, 2,
      true, QStringLiteral("1.1"));
  row("parenthesis-suffix", true, R"JSON([[1,"Title"],[2,"A"],[3,"B"]])JSON",
      {"Title", "1) A", "1.1) B"}, 2, true, QStringLiteral("1.1)"));
  row("unknown-pattern", true, R"JSON([[2,"A"],[3,"B"]])JSON", {"1. A", "1.1. B"}, 2, true,
      QStringLiteral("unknown"));
  row("empty-pattern", true, R"JSON([[2,"A"],[3,"B"]])JSON", {"1. A", "1.1. B"}, 2, true,
      QString());

  for (int count = 1; count <= 4; ++count) {
    QJsonArray headings;
    headings.append(QJsonArray{1, QStringLiteral("Title")});
    QStringList names{QStringLiteral("Title")};
    for (int i = 1; i <= count; ++i) {
      const QString name = QStringLiteral("%1. Authored").arg(i);
      headings.append(QJsonArray{2, name});
      names.append(name);
    }
    QTest::newRow(qPrintable(QStringLiteral("short-authored-%1").arg(count)))
        << headings << names << 0 << true << QStringLiteral("1.1.") << true;
  }
  row("five-match-sixth-mismatch", true,
      R"JSON([[1,"Title"],[2,"1. A"],[2,"2. B"],[2,"3. C"],[2,"4. D"],[2,"5. E"],[2,"F"]])JSON",
      {"Title", "1. A", "2. B", "3. C", "4. D", "5. E", "F"}, 0);
  row("fourth-mismatch-keeps-authored-prefixes", true,
      R"JSON([[1,"Title"],[2,"1. A"],[2,"2. B"],[2,"3. C"],[2,"D"],[2,"5. E"],[2,"6. F"]])JSON",
      {"Title", "1. 1. A", "2. 2. B", "3. 3. C", "4. D", "5. 5. E", "6. 6. F"}, 6);
  row("mixed-authored-styles", true,
      R"JSON([[2,"1 Intro"],[2,"1. Intro"],[2,"1) Intro"],[2,"1.2. Intro"],[2,"1.2) Intro"]])JSON",
      {"1 Intro", "1. Intro", "1) Intro", "1.2. Intro", "1.2) Intro"}, 0, true,
      QStringLiteral("1.1)"));
  row("numeric-only", true, R"JSON([[2,"0"],[2,"1."],[2,"1)"],[2,"1.2"],[2,"1.2)"]])JSON",
      {"0", "1.", "1)", "1.2", "1.2)"}, 0);
  row("leading-unicode-space", true, R"JSON([[2,"\u20030 Intro"],[3,"\t1.2 Detail"]])JSON",
      {QString(QChar(0x2003)) + QStringLiteral("0 Intro"), QStringLiteral("\t1.2 Detail")}, 0);
  row("nonleading-numeric-prefix", true, R"JSON([[2,"v1.2 Intro"]])JSON", {"1. v1.2 Intro"}, 1);
  row("missing-prefix-separator", true, R"JSON([[2,"1.2Intro"]])JSON", {"1. 1.2Intro"}, 1);
  row("empty-prefix-component", true, R"JSON([[2,"1.. Intro"]])JSON", {"1. 1.. Intro"}, 1);
  row("placeholders-before-title-and-base", true,
      R"JSON([[1,"[EMPTY]",true],[1,"Title"],[2,"[EMPTY]",true],[3,"A"]])JSON", {"Title", "1. A"},
      1);
  row("placeholders-do-not-break-authored-sample", true,
      R"JSON([[1,"Title"],[2,"1. A"],[3,"[EMPTY]",true],[4,"1.1.1. B"]])JSON",
      {"Title", "1. A", "1.1.1. B"}, 0);
  row("placeholders-do-not-advance-counters", true,
      R"JSON([[1,"Title"],[2,"A"],[3,"[EMPTY]",true],[4,"B"],[2,"C"]])JSON",
      {"Title", "1. A", "1.1.1. B", "2. C"}, 3);
  row("real-empty-name-participates", true,
      R"JSON([[1,"Title"],[2,"1. A"],[3,"[EMPTY]",true],[4,"[EMPTY]"]])JSON",
      {"Title", "1. 1. A", "1.1.1. [EMPTY]"}, 2);
  row("only-placeholders", true, R"JSON([[1,"[EMPTY]",true],[2,"[EMPTY]",true]])JSON", {}, 0,
      false);
  row("invalid-levels-are-not-headings", true,
      R"JSON([[0,"Invalid"],[1,"Title"],[-1,"Invalid"],[2,"A"]])JSON", {"Title", "1. A"}, 1);

  row("detection-off-title", false, R"JSON([[1,"Title"],[2,"A"],[3,"B"],[2,"C"]])JSON",
      {"1. Title", "1.1. A", "1.1.1. B", "1.2. C"}, 4);
  row("detection-off-title-only", false, R"JSON([[1,"Title"]])JSON", {"1. Title"}, 1);
  row("detection-off-placeholders", false,
      R"JSON([[1,"[EMPTY]",true],[1,"Title"],[2,"[EMPTY]",true],[3,"A"]])JSON",
      {"1. Title", "1.1.1. A"}, 2);
  row("detection-off-authored", false, R"JSON([[1,"1. Title"],[2,"1.1. A"]])JSON",
      {"1. Title", "1.1. A"}, 0);
  row("detection-off-no-h1", false, R"JSON([[2,"A"],[3,"B"],[2,"C"]])JSON",
      {"1. A", "1.1. B", "2. C"}, 3);
  row("detection-on-no-h1", true, R"JSON([[2,"A"],[3,"B"],[2,"C"]])JSON",
      {"1. A", "1.1. B", "2. C"}, 3);
  row("detection-off-two-h1s", false, R"JSON([[1,"A"],[2,"B"],[1,"C"]])JSON",
      {"1. A", "1.1. B", "2. C"}, 3);
}

void TestMarkdownViewerJs::testSectionNumber_policyParity() {
  QFETCH(QJsonArray, headings);
  QFETCH(QStringList, expected);
  QFETCH(int, generated);
  QFETCH(bool, hasSectionNumber);
  QFETCH(QString, pattern);
  QFETCH(bool, detectHeading1ForSectionNumber);

  struct Heading {
    int m_level;
    QString m_name;
    bool m_isPlaceholder;
  };
  QVector<Heading> cppHeadings;
  int maximumLevel = 0;
  for (const auto &value : headings) {
    const auto heading = value.toArray();
    cppHeadings.append({heading[0].toInt(), heading[1].toString(), heading[2].toBool()});
    if (!cppHeadings.last().m_isPlaceholder) {
      maximumLevel = qMax(maximumLevel, cppHeadings.last().m_level);
    }
  }
  const auto analysis =
      vnotex::SectionNumberUtils::analyze(cppHeadings, detectHeading1ForSectionNumber);
  QVector<int> numbers(maximumLevel + 1, 0);
  QStringList cppNames;
  int cppGenerated = 0;
  for (int i = 0; i < cppHeadings.size(); ++i) {
    const auto &heading = cppHeadings[i];
    if (heading.m_isPlaceholder || heading.m_level <= 0) {
      continue;
    }
    QString name = heading.m_name;
    if (!analysis.m_skip && i >= analysis.m_firstNumberedHeading) {
      vnotex::SectionNumberUtils::increaseSectionNumber(numbers, heading.m_level,
                                                        analysis.m_baseLevel);
      name.prepend(vnotex::SectionNumberUtils::joinSectionNumber(numbers, pattern) +
                   QLatin1Char(' '));
      ++cppGenerated;
    }
    cppNames.append(name);
  }
  QCOMPARE(cppNames, expected);
  QCOMPARE(cppGenerated, generated);

  QJSEngine engine;
  setupSectionNumber(engine);
  auto res = engine.evaluate(
      QStringLiteral("__makeSectionFixture(%1);")
          .arg(QString::fromUtf8(QJsonDocument(headings).toJson(QJsonDocument::Compact))));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  engine.globalObject().setProperty(QStringLiteral("__pattern"), pattern);
  engine.globalObject().setProperty(QStringLiteral("__detectHeading1"),
                                    detectHeading1ForSectionNumber);
  res = engine.evaluate(
      QStringLiteral("__sectionNumber.apply(__container, { enabled: true, pattern: __pattern, "
                     "detectHeading1ForSectionNumber: __detectHeading1 });"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.toBool(), hasSectionNumber);
  QCOMPARE(engine.evaluate(QStringLiteral("__container.__vxHasSectionNumber")).toBool(),
           hasSectionNumber);
  const auto jsNames =
      engine.evaluate(QStringLiteral("__sectionNames()")).toVariant().toStringList();
  QCOMPARE(jsNames, expected);
  QCOMPARE(jsNames, cppNames);
  QCOMPARE(
      engine
          .evaluate(QStringLiteral("__container.querySelectorAll('span.vx-section-number').length"))
          .toInt(),
      generated);
}

void TestMarkdownViewerJs::testSectionNumber_reapplicationPreservesDom() {
  QJSEngine engine;
  setupSectionNumber(engine);
  auto res = engine.evaluate(QStringLiteral(R"JS(
__makeSectionFixture([[1, 'Title'], [2, 'Alpha']]);
var heading = __sectionHeadings[1];
heading.setAttribute('data-source-line', '9');
heading.textContent = '';
var text = document.createTextNode('Alpha ');
heading.appendChild(text);
var link = add(heading, 'a', '', 'Link');
link.setAttribute('href', '#heading-0');
heading.appendChild(document.createTextNode(' '));
var emphasis = add(heading, 'em', '', 'Detail');
var authored = add(heading, 'span', '', ' authored', 'vx-section-number');
var originalChildren = heading.childNodes.slice();
var linkClicks = 0;
link.addEventListener('click', function() { ++linkClicks; });
__sectionHeadings.push(add(add(__container, 'blockquote'), 'h3', 'nested', 'Nested'));
__sectionHeadings.push(add(add(__container, 'div'), 'h2', 'peer', 'Peer'));
var outside = add(document.body, 'h1', 'outside', 'Outside');
var code = add(add(__container, 'pre'), 'code', '', '# Not a heading');
var toc = add(add(__container, 'nav'), 'a', '', 'Title');
toc.setAttribute('href', '#heading-0');
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.', detectHeading1ForSectionNumber: true });
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  const QStringList originalNames{"Title", "Alpha Link Detail authored", "Nested", "Peer"};
  const QStringList dottedNames{"Title", "1. Alpha Link Detail authored", "1.1. Nested", "2. Peer"};
  const auto check = [&](const QStringList &names, bool numbered, int spanCount) {
    QCOMPARE(engine.evaluate(QStringLiteral("__sectionNames()")).toVariant().toStringList(), names);
    QCOMPARE(engine.evaluate(QStringLiteral("__container.__vxHasSectionNumber")).toBool(),
             numbered);
    // Count authored and generated spans together: class collisions must survive restoration.
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "__container.querySelectorAll('span.vx-section-number').length"))
                 .toInt(),
             spanCount);
  };
  check(dottedNames, true, 4);
  res = engine.evaluate(QStringLiteral(R"JS(
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.', detectHeading1ForSectionNumber: true });
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.', detectHeading1ForSectionNumber: true });
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  check(dottedNames, true, 4);

  res =
      engine.evaluate(QStringLiteral("__sectionNumber.apply(__container, { enabled: true, pattern: "
                                     "'1.1)', detectHeading1ForSectionNumber: true });"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  check({"Title", "1) Alpha Link Detail authored", "1.1) Nested", "2) Peer"}, true, 4);
  QVERIFY(engine
              .evaluate(QStringLiteral("originalChildren.every(function(node, index) { "
                                       "return heading.childNodes[index + 1] === node; })"))
              .toBool());
  res = engine.evaluate(QStringLiteral(R"JS(
var ownedSpans = __sectionHeadings.map(function(node) { return node.__vxSectionNumberSpan; })
                                .filter(function(node) { return !!node; });
__sectionNumber.apply(__container, { enabled: false, pattern: '1.1)', detectHeading1ForSectionNumber: true });
link.click();
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  check(originalNames, false, 1);
  QVERIFY(engine
              .evaluate(QStringLiteral(
                  "ownedSpans.every(function(node) { return node.parentNode === null; })"))
              .toBool());
  QVERIFY(engine
              .evaluate(QStringLiteral("heading.childNodes.length === originalChildren.length && "
                                       "originalChildren.every(function(node, index) { return "
                                       "heading.childNodes[index] === node; })"))
              .toBool());

  res = engine.evaluate(QStringLiteral(R"JS(
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.', detectHeading1ForSectionNumber: true });
link.click();
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  check(dottedNames, true, 4);
  QCOMPARE(engine.evaluate(QStringLiteral("linkClicks")).toInt(), 2);
  QCOMPARE(engine.evaluate(QStringLiteral("heading.id")).toString(), QStringLiteral("heading-1"));
  QCOMPARE(engine.evaluate(QStringLiteral("heading.getAttribute('data-source-line')")).toString(),
           QStringLiteral("9"));
  QCOMPARE(engine.evaluate(QStringLiteral("link.getAttribute('href')")).toString(),
           QStringLiteral("#heading-0"));
  QVERIFY(engine
              .evaluate(QStringLiteral(
                  "text.parentNode === heading && link.parentNode === heading && "
                  "emphasis.parentNode === heading && authored.parentNode === heading"))
              .toBool());
  QCOMPARE(engine.evaluate(QStringLiteral("outside.textContent")).toString(),
           QStringLiteral("Outside"));
  QCOMPARE(engine.evaluate(QStringLiteral("code.textContent")).toString(),
           QStringLiteral("# Not a heading"));
  QCOMPARE(engine.evaluate(QStringLiteral("toc.textContent")).toString(), QStringLiteral("Title"));

  res = engine.evaluate(QStringLiteral("__sectionNumber.apply(__container);"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.toBool(), false);
  check(originalNames, false, 1);
  res = engine.evaluate(QStringLiteral("__sectionNumber.apply(null, { enabled: true });"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.toBool(), false);
}

void TestMarkdownViewerJs::testSectionNumber_renderLifecycleAndFolding() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QString error;
  QString script;
  for (const auto *name : {"vxworker.js", "sectionnumber.js", "nodelinemapper.js"}) {
    script += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  script += QStringLiteral(R"JS(
window.__sectionPublications = [];
window.__sectionFinishes = [];
window.__optionCallbacks = [];
vxMarkdownAdapter.sectionNumberOptions = {enabled: true, pattern: '1.1)', detectHeading1ForSectionNumber: true};
vxMarkdownAdapter.sectionNumberOptionsChanged = {connect(callback) { __optionCallbacks.push(callback); }};
vxMarkdownAdapter.setHeadings = function(headings, hasSectionNumber) {
  this.headings = headings; this.hasSectionNumber = hasSectionNumber;
  __sectionPublications.push(headings.map(heading => heading.name));
};
vxMarkdownAdapter.setCurrentHeadingAnchor = function() {};
vxMarkdownAdapter.setTopLineNumber = function() {};
const completed = vxMarkdownAdapter.setWorkFinished;
vxMarkdownAdapter.setWorkFinished = function(generation) {
  completed(generation);
  __sectionFinishes.push({generation, names: this.headings.map(heading => heading.name),
    numbered: this.hasSectionNumber});
};
window.__setSectionOptions = function(enabled, pattern, detectHeading1ForSectionNumber) {
  vxMarkdownAdapter.sectionNumberOptions = {enabled, pattern, detectHeading1ForSectionNumber};
  __optionCallbacks.forEach(callback => callback());
};
vxcore.nodeLineMapper = new NodeLineMapper(vxcore, vxcore.contentContainer);
vxcore.setHeadingFoldingEnabled(true);
return {ready: true};
)JS");
  QJsonObject result;
  evaluateNavigation(page, script, result);
  const auto markdown =
      QStringLiteral("# Title\n\n## [Alpha](#detail)\n\n### Detail\n\nBody\n\n## Beta");
  renderNavigationDocument(page, markdown, QStringLiteral("sections-1"));
  evaluateNavigation(page, QStringLiteral(R"JS(
window.__inlineLink = document.querySelector('#alpha a:not(.vx-header-anchor)');
const button = document.querySelector('#alpha .vx-heading-fold-toggle');
button.click();
const folded = document.getElementById(button.getAttribute('aria-controls')).hidden;
vxcore.scrollToAnchor('detail');
return {finished: __sectionFinishes[0], anchors: vxMarkdownAdapter.headings.map(heading => heading.anchor),
  folded, expanded: !document.getElementById(button.getAttribute('aria-controls')).hidden,
  sourceLines: Array.from(vxcore.contentContainer.querySelectorAll('h1,h2,h3'), node => Number(node.dataset.sourceLine))};
)JS"),
                     result);
  QCOMPARE(
      result.value(QStringLiteral("finished")).toObject().value(QStringLiteral("names")).toArray(),
      QJsonArray::fromStringList({"Title", "1) Alpha", "1.1) Detail", "2) Beta"}));
  QVERIFY(result.value(QStringLiteral("finished"))
              .toObject()
              .value(QStringLiteral("numbered"))
              .toBool());
  QCOMPARE(result.value(QStringLiteral("anchors")).toArray(),
           QJsonArray::fromStringList({"title", "alpha", "detail", "beta"}));
  QCOMPARE(result.value(QStringLiteral("sourceLines")).toArray(), QJsonArray({0, 2, 4, 8}));
  QVERIFY(result.value(QStringLiteral("folded")).toBool());
  QVERIFY(result.value(QStringLiteral("expanded")).toBool());
  struct Transition {
    bool enabled;
    QString pattern;
    bool detectHeading1;
    QStringList names;
  };
  const QVector<Transition> transitions{
      {true, QStringLiteral("1.1"), true, {"Title", "1 Alpha", "1.1 Detail", "2 Beta"}},
      {false, QStringLiteral("1.1"), true, {"Title", "Alpha", "Detail", "Beta"}},
      {true, QStringLiteral("1.1."), true, {"Title", "1. Alpha", "1.1. Detail", "2. Beta"}},
      {true,
       QStringLiteral("1.1."),
       false,
       {"1. Title", "1.1. Alpha", "1.1.1. Detail", "1.2. Beta"}},
      {true, QStringLiteral("1.1."), true, {"Title", "1. Alpha", "1.1. Detail", "2. Beta"}}};
  for (const auto &transition : transitions) {
    const QJsonObject options{{QStringLiteral("enabled"), transition.enabled},
                              {QStringLiteral("pattern"), transition.pattern},
                              {QStringLiteral("detectHeading1"), transition.detectHeading1}};
    evaluateNavigation(
        page,
        QStringLiteral(R"JS(
const options = %1;
const publications = __sectionPublications.length;
__setSectionOptions(options.enabled, options.pattern, options.detectHeading1);
return {names: vxMarkdownAdapter.headings.map(heading => heading.name), numbered: vxMarkdownAdapter.hasSectionNumber,
  publications: __sectionPublications.length - publications, finishes: __sectionFinishes.length,
  buttons: vxcore.contentContainer.querySelectorAll('.vx-heading-fold-toggle').length,
  numbers: vxcore.contentContainer.querySelectorAll('.vx-section-number').length,
  anchors: vxMarkdownAdapter.headings.map(heading => heading.anchor),
  linkPreserved: __inlineLink === document.querySelector('#alpha a:not(.vx-header-anchor)') && __inlineLink.getAttribute('href') === '#detail'};
)JS")
            .arg(QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact))),
        result);
    QCOMPARE(result.value(QStringLiteral("names")).toArray(),
             QJsonArray::fromStringList(transition.names));
    QCOMPARE(result.value(QStringLiteral("numbered")).toBool(), transition.enabled);
    QCOMPARE(result.value(QStringLiteral("publications")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("finishes")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("buttons")).toInt(), 4);
    QCOMPARE(result.value(QStringLiteral("numbers")).toInt(),
             transition.enabled ? (transition.detectHeading1 ? 3 : 4) : 0);
    QCOMPARE(result.value(QStringLiteral("anchors")).toArray(),
             QJsonArray::fromStringList({"title", "alpha", "detail", "beta"}));
    QVERIFY(result.value(QStringLiteral("linkPreserved")).toBool());
  }
  for (bool enabled : {false, true}) {
    evaluateNavigation(page,
                       QStringLiteral(R"JS(
vxcore.setHeadingFoldingEnabled(%1);
return {names: vxMarkdownAdapter.headings.map(heading => heading.name),
  buttons: vxcore.contentContainer.querySelectorAll('.vx-heading-fold-toggle').length};
)JS")
                           .arg(enabled ? QStringLiteral("true") : QStringLiteral("false")),
                       result);
    QCOMPARE(result.value(QStringLiteral("names")).toArray(),
             QJsonArray::fromStringList({"Title", "1. Alpha", "1.1. Detail", "2. Beta"}));
    QCOMPARE(result.value(QStringLiteral("buttons")).toInt(), enabled ? 4 : 0);
  }
  for (int round = 2; round <= 3; ++round) {
    renderNavigationDocument(page, markdown, QStringLiteral("sections-%1").arg(round));
    evaluateNavigation(page, QStringLiteral(R"JS(
return {finished: __sectionFinishes[__sectionFinishes.length - 1],
  buttons: vxcore.contentContainer.querySelectorAll('.vx-heading-fold-toggle').length};
)JS"),
                       result);
    QCOMPARE(result.value(QStringLiteral("finished"))
                 .toObject()
                 .value(QStringLiteral("names"))
                 .toArray(),
             QJsonArray::fromStringList({"Title", "1. Alpha", "1.1. Detail", "2. Beta"}));
    QCOMPARE(result.value(QStringLiteral("buttons")).toInt(), 4);
  }
  evaluateNavigation(page, QStringLiteral("__setSectionOptions(false, '1.1.', true); return {};"),
                     result);
  renderNavigationDocument(page, markdown, QStringLiteral("sections-4"));
  evaluateNavigation(page, QStringLiteral("return __sectionFinishes[3];"), result);
  QCOMPARE(result.value(QStringLiteral("names")).toArray(),
           QJsonArray::fromStringList({"Title", "Alpha", "Detail", "Beta"}));
  QVERIFY(!result.value(QStringLiteral("numbered")).toBool());
  for (bool detectTitle : {true, false}) {
    evaluateNavigation(page,
                       QStringLiteral("__setSectionOptions(true, '1.1.', %1); return {};")
                           .arg(detectTitle ? QStringLiteral("true") : QStringLiteral("false")),
                       result);
    renderNavigationDocument(page, QStringLiteral("# Replacement title"),
                             detectTitle ? QStringLiteral("sections-5")
                                         : QStringLiteral("sections-6"));
    evaluateNavigation(
        page, QStringLiteral("return __sectionFinishes[__sectionFinishes.length - 1];"), result);
    QCOMPARE(result.value(QStringLiteral("names")).toArray(),
             QJsonArray::fromStringList({detectTitle ? QStringLiteral("Replacement title")
                                                     : QStringLiteral("1. Replacement title")}));
    QCOMPARE(result.value(QStringLiteral("numbered")).toBool(), !detectTitle);
  }
  renderNavigationDocument(page, QString(), QStringLiteral("sections-7"));
  evaluateNavigation(page, QStringLiteral(R"JS(
return {finished: __sectionFinishes[6], completions: __sectionFinishes.length,
  empty: vxcore.contentContainer.childNodes.length === 0};
)JS"),
                     result);
  QCOMPARE(
      result.value(QStringLiteral("finished")).toObject().value(QStringLiteral("names")).toArray(),
      QJsonArray());
  QVERIFY(!result.value(QStringLiteral("finished"))
               .toObject()
               .value(QStringLiteral("numbered"))
               .toBool());
  QCOMPARE(result.value(QStringLiteral("completions")).toInt(), 7);
  QVERIFY(result.value(QStringLiteral("empty")).toBool());
}

} // namespace tests

int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
  QGuiApplication app(argc, argv);
  tests::TestMarkdownViewerJs test;
  return QTest::qExec(&test, argc, argv);
}
#include "test_markdownviewer_js.moc"
