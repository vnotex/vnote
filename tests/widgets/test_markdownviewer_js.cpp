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
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QtTest>

#include <cmark.h>
#include <cstdlib>
#include <memory>
#include <utility>

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
  setMarkdownText: function() {}, scrollToLine: function() {}, scrollToAnchor: function() {},
  previewGraph: function() {}, previewMath: function() {}, scroll: function() {},
  htmlToMarkdown: function() {}, highlightCodeBlock: function() {}, highlightMath: function() {},
  parseStyleSheet: function() {}, getHeadingAnchor: function() {}, crossCopy: function() {},
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
  textUpdated: makeSignal(), editLineNumberUpdated: makeSignal(),
  anchorScrollRequested: makeSignal(), graphPreviewRequested: makeSignal(),
  mathPreviewRequested: makeSignal(), scrollRequested: makeSignal(),
  htmlToMarkdownRequested: makeSignal(), highlightCodeBlockRequested: makeSignal(),
  highlightMathRequested: makeSignal(), parseStyleSheetRequested: makeSignal(),
  headingAnchorRequested: makeSignal(), crossCopyRequested: makeSignal(),
  findTextRequested: makeSignal(), contentRequested: makeSignal(),
  graphRenderDataReady: makeSignal(), taskListToggleRejected: makeSignal(),
  presentationModeRequested: makeSignal(), setPresentationState: function() {},
  toggleTaskListItem: function(line, checked) {
    window.__toggleCalls.push({ line: line, checked: checked });
  }
};

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
  void testPresentation_groupingAndRestoration();
  void testPresentation_pinnedHeaderScrolling();
  void testPresentation_cancellationRenderAndExport();
  void testNavigation_visibleRectangles_data();
  void testNavigation_visibleRectangles();
  void testNavigation_staleSnapshots();
  void testNavigation_protectedDestinations();
  void testMathHeadings_linkTargets();
  void testCmarkMathTableDom_data();
  void testCmarkMathTableDom();
  void testMathRenderer_initializationFanout();
  void testMathRenderer_failedInitializationReleasesPass();
  void testMathRenderer_ignoresUnrelatedFontReadiness();
  void testInstall_loadBeforeChannel();
  void testInstall_channelBeforeLoad();
  void testInstall_onlyOnce();
  void testClick_forwardsLineAndState();
  void testClick_noSourceLineReverts();
  void testReject_revertsCheckbox();
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
  // Evaluates the prelude plus the real markdownviewer.js.
  void setup(QJSEngine &p_engine, bool p_initializedAtChannel);
  void setupHeadingFolding(QJSEngine &p_engine);
  void setupSectionNumber(QJSEngine &p_engine);
  void setupMath(QJSEngine &p_engine);
  void setupPresentationPage(QWebEnginePage &p_page);
  void setPresentationActive(QWebEnginePage &p_page, bool p_active);
  void setupNavigationPage(QWebEnginePage &p_page, bool p_protected = false);
  void evaluateNavigation(QWebEnginePage &p_page, const QString &p_script, QJsonObject &p_result);
};

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
var __optionHandlers = [], __publications = [], __finishSnapshots = [], __trace = [];
window.vxMarkdownAdapter = {
  sectionNumberOptions: { enabled: false, pattern: '1.1.', detectHeading1ForSectionNumber: true },
  sectionNumberOptionsChanged: {
    connect: function(callback) { __optionHandlers.push(callback); },
    emit: function() { __optionHandlers.slice().forEach(function(callback) { callback(); }); }
  },
  setHeadings: function(headings, hasSectionNumber) {
    this.headings = headings;
    this.hasSectionNumber = hasSectionNumber;
    __publications.push({ names: headings.map(function(heading) { return heading.name; }),
                          hasSectionNumber: hasSectionNumber });
    __trace.push('headings');
  },
  setWorkFinished: function() {
    __finishSnapshots.push({
      names: (this.headings || []).map(function(heading) { return heading.name; }),
      hasSectionNumber: this.hasSectionNumber
    });
    __trace.push('finished');
  },
  setCurrentHeadingAnchor: function() {},
  setTopLineNumber: function() {}
};
function __setSectionOptions(enabled, pattern, detectHeading1ForSectionNumber) {
  vxMarkdownAdapter.sectionNumberOptions = { enabled: enabled, pattern: pattern,
                                            detectHeading1ForSectionNumber: detectHeading1ForSectionNumber };
  vxMarkdownAdapter.sectionNumberOptionsChanged.emit();
}
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

void TestMarkdownViewerJs::setupMath(QJSEngine &p_engine) {
  auto res = p_engine.evaluate(QStringLiteral(R"JS(
var window = this;
var console = { log: function(){}, warn: function(){}, error: function(){} };
var vxOptions = { mathRenderer: 'katex' };
var scripts = [], styles = [], results = [], passes = 0;
var reading = { textContent: '$a+b$' };
function element() {
  return { style: {}, querySelector: function() { return null; } };
}
var document = {
  currentScript: { src: 'qrc:/vnotex/data/extra/web/js/mathjax.js' },
  fonts: [],
  head: { appendChild: function() {} },
  createElement: element
};
window.getComputedStyle = function() { return { color: 'black', font: '16px serif' }; };
var container = {
  getBoundingClientRect: function() { return { width: 100, height: 20 }; },
  getElementsByClassName: function() { return [reading]; },
  appendChild: function(node) { node.parentNode = this; },
  removeChild: function(node) { node.parentNode = null; }
};
var Utils = {
  parentFolder: function(path) { return path.substring(0, path.lastIndexOf('/')); },
  loadScript: function(url, cb) { scripts.push(cb); },
  httpGet: function(url, type, cb) { styles.push(cb); }
};
var vxcore = {
  contentContainer: container,
  on: function() {},
  getWorker: function() {
    return { getCodeNodes: function() { return []; }, addLangsToSkipHighlight: function() {} };
  },
  registerWorker: function(worker) { window.worker = worker; worker.register(this); },
  finishWorker: function() { ++passes; }
};
function startRequests() {
  worker.render(container, 'tex-to-render');
  worker.renderText(container, '$x$', function(node) { results.push({ id: 1, node: node }); });
  worker.renderText(container, '$y$', function(node) { results.push({ id: 2, node: node }); });
}
function installLibrary() {
  window.katex = { render: function(tex, node) { node.rendered = tex; } };
}
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QString err;
  QString source;
  for (const auto &name : {QStringLiteral("vxworker.js"), QStringLiteral("mathjax.js")}) {
    source += readFile(webDir() + QStringLiteral("/js/") + name, &err) + QLatin1Char('\n');
    QVERIFY2(err.isEmpty(), qPrintable(err));
  }
  res = p_engine.evaluate(source, QStringLiteral("mathjax.js"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
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
  QString script = QStringLiteral("window.vxOptions = {htmlTagEnabled:true, protectedView:%1};\n")
                       .arg(p_protected ? QStringLiteral("true") : QStringLiteral("false"));
  for (const auto *name : {"eventemitter.js",
                           "vxcore.js",
                           "vxworker.js",
                           "utils.js",
                           "markdownviewercore.js",
                           "markdown-it/markdown-it.min.js",
                           "markdown-it/markdown-it-container.min.js",
                           "markdown-it/markdown-it-emoji.min.js",
                           "markdown-it/markdown-it-footnote.min.js",
                           "markdown-it/markdown-it-front-matter.js",
                           "markdown-it/markdown-it-imsize.min.js",
                           "markdown-it/markdown-it-sub.min.js",
                           "markdown-it/markdown-it-sup.min.js",
                           "markdown-it/markdown-it-task-lists.js",
                           "markdown-it/markdown-it-texmath.js",
                           "markdown-it/markdown-it-inject-linenumbers.js",
                           "markdown-it/markdownItAnchor.umd.js",
                           "markdown-it/markdownItTocDoneRight.umd.js",
                           "markdown-it/markdown-it-implicit-figure.js",
                           "markdown-it/markdown-it-mark.min.js",
                           "markdownit.js"}) {
    script += readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &error) +
              QLatin1Char('\n');
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }
  // Scripts are installed after load, so select the container without unrelated preview workers.
  script += QStringLiteral(R"JS(
window.vxcore.contentContainer = document.getElementById('vx-content');
window.vxcore.initialized = true;
window.__protectedLinks = [];
window.__anchors = [];
window.vxcore.nodeLineMapper = {scrollToAnchor: function(anchor) { __anchors.push(anchor); }};
window.vxMarkdownAdapter = {
  setWorkFinished: function() {},
  activateProtectedLink: function(href) { __protectedLinks.push(href); },
  protectedImageUrl: function(src, callback) { window.__finishImage = callback; }
};
return {ready: true};
)JS");
  QJsonObject result;
  evaluateNavigation(p_page, script, result);
  QVERIFY(result.value(QStringLiteral("ready")).toBool());
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
window.vxMarkdownAdapter.setSavedContent = function(head, styles, html, classes) {
  window.__savedPresentationContent = {styles, html, classes};
};
window.vxMarkdownAdapter.onPdfRenderReady = function() { window.__exportReady = true; };
window.vxI18n = {tr: function(key) { return key; }};
window.vxImageViewer = {isViewingImage: function() { return false; }, setupForAllImages: function() {}};
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

void TestMarkdownViewerJs::setPresentationActive(QWebEnginePage &p_page, bool p_active) {
  QJsonObject result;
  evaluateNavigation(p_page,
                     QStringLiteral("vxPresentation.setActive(%1); return {requested: true};")
                         .arg(p_active ? QStringLiteral("true") : QStringLiteral("false")),
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
vxcore.numOfOngoingWorkers = 1;
vxcore.emit('basicMarkdownRendered');
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
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.contentContainer.innerHTML = '<h2 id="only">Only</h2><p>Body</p>';
window.__originalHtml = vxcore.contentContainer.outerHTML;
vxPresentation.setActive(true);
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
vxPresentation.setActive(true);
vxPresentation.setActive(false);
vxPresentation.setActive(true);
return {requested: true};
)JS"),
                     result);
  setPresentationActive(page, true);
  setPresentationActive(page, false);

  evaluateNavigation(page, QStringLiteral(R"JS(
const bundledReveal = window.Reveal;
window.Reveal = undefined;
vxPresentation.setActive(true);
window.Reveal = bundledReveal;
const report = __presentationReports[__presentationReports.length - 1];
return {failedClosed: report.active === false && report.error.length > 0 &&
  vxPresentation.session === null && vxcore.contentContainer.outerHTML === __originalHtml};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("failedClosed")).toBool());

  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.numOfOngoingWorkers = 1;
vxPresentation.setActive(true);
const deferred = vxPresentation.isActive() === false;
vxcore.finishWorker('held');
return {deferred};
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("deferred")).toBool());
  setPresentationActive(page, true);
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.saveContent();
return {restored: vxcore.contentContainer.outerHTML === __originalHtml,
  html: __savedPresentationContent.html === __originalHtml,
  noPresentationStyles: !__savedPresentationContent.styles.includes('--r-main-font') &&
    __savedPresentationContent.styles.includes('vx-presentation') === false,
  noPresentationClasses: !__savedPresentationContent.classes.includes('vx-presentation'),
  stylesRestored: document.querySelectorAll('[data-vx-presentation-style]').length === 4,
  inactive: !vxPresentation.isActive()};
)JS"),
                     result);
  for (const auto *key : {"restored", "html", "noPresentationStyles", "noPresentationClasses",
                          "stylesRestored", "inactive"}) {
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
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.setMarkdownText('## Replacement');
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

void TestMarkdownViewerJs::testNavigation_staleSnapshots() {
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  setupNavigationPage(page);
  QJsonObject result;
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.setMarkdownText('[Original](sibling.md#first) [Second](#second)');
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
vxcore.setMarkdownText('[Fresh](replacement.md#target)');
snapshot = vxcore.getNavigationTargets();
document.querySelector('base').href = 'file:///another/current.md';
const baseChangedRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
document.querySelector('base').href = 'file:///navigation/current.md';
snapshot = vxcore.getNavigationTargets();
vxcore.setMarkdownText('[Newest](newest.md)');
const rerenderRejected = vxcore.resolveNavigationTarget(snapshot.snapshot, snapshot.targets[0].index) === null;
const fresh = vxcore.getNavigationTargets();
const freshDestination = vxcore.resolveNavigationTarget(fresh.snapshot, fresh.targets[0].index);
return { initial, changedHrefRejected, supersededRejected, actualScroll, scrolledRejected,
         replacementRejected, removedRejected, hiddenRejected, baseChangedRejected,
         rerenderRejected, freshDestination,
         invalidIndexRejected: vxcore.resolveNavigationTarget(fresh.snapshot, -1) === null
           && vxcore.resolveNavigationTarget(fresh.snapshot, 0.5) === null
           && vxcore.resolveNavigationTarget(fresh.snapshot, fresh.targets.length) === null };
)JS"),
                     result);
  QCOMPARE(
      result.value(QStringLiteral("initial")).toObject().value(QStringLiteral("url")).toString(),
      QStringLiteral("file:///navigation/sibling.md#first"));
  QCOMPARE(result.value(QStringLiteral("actualScroll")).toDouble(), 10.0);
  for (const auto *key : {"changedHrefRejected", "supersededRejected", "scrolledRejected",
                          "replacementRejected", "removedRejected", "hiddenRejected",
                          "baseChangedRejected", "rerenderRejected", "invalidIndexRejected"}) {
    QVERIFY2(result.value(QLatin1String(key)).toBool(), key);
  }
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
  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.setMarkdownText(`[Fragment](#target%20name) [Relative](sibling.md#target)
[HTTP](http://example.com/path) [HTTPS](https://example.com/path)
<a href="javascript:alert(1)">Javascript</a>
<a href="data:text/html,blocked">Data</a>
<a href="file:///private/note.md">File</a>
<a href="//example.com/path">Network path</a>
<a href="/absolute.md">Absolute</a>
<a href="mailto:user@example.com">Mail</a>`);
return {started: true};
)JS"),
                     result);
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

  evaluateNavigation(page, QStringLiteral(R"JS(
vxcore.setMarkdownText('[New document](new.md)');
return {started: true};
)JS"),
                     result);
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

  evaluateNavigation(page, QStringLiteral(R"JS(
// Hold the actual protected renderer's image transport, then replace text while it is busy.
vxcore.setMarkdownText('[Pending](pending.md) ![Held](data:image/png;base64,AAAA)');
const rendering = vxcore.getNavigationTargets();
const oldRejected = vxcore.resolveNavigationTarget(__beforePendingSnapshot.snapshot,
  __beforePendingSnapshot.targets[0].index) === null;
vxcore.setMarkdownText('[Replacement](replacement.md)');
const pending = vxcore.getNavigationTargets();
window.__pendingSnapshot = pending;
const result = {oldRejected, renderingTargets: rendering.targets, pendingTargets: pending.targets,
                transportHeld: typeof __finishImage === 'function'};
__finishImage('');
return result;
)JS"),
                     result);
  QVERIFY(result.value(QStringLiteral("oldRejected")).toBool());
  QVERIFY(result.value(QStringLiteral("transportHeld")).toBool());
  QCOMPARE(result.value(QStringLiteral("renderingTargets")).toArray(), QJsonArray());
  QCOMPARE(result.value(QStringLiteral("pendingTargets")).toArray(), QJsonArray());
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

void TestMarkdownViewerJs::testMathHeadings_linkTargets() {
  QString err;
  QString source = QStringLiteral(R"JS(
(function() {
try {
window.vxOptions = {};
var worker;
window.vxcore = { registerWorker: function(value) { worker = value; } };
)JS");
  for (const auto *name :
       {"vxworker.js", "utils.js", "markdown-it/markdown-it.min.js",
        "markdown-it/markdown-it-container.min.js", "markdown-it/markdown-it-emoji.min.js",
        "markdown-it/markdown-it-footnote.min.js", "markdown-it/markdown-it-front-matter.js",
        "markdown-it/markdown-it-imsize.min.js", "markdown-it/markdown-it-sub.min.js",
        "markdown-it/markdown-it-sup.min.js", "markdown-it/markdown-it-task-lists.js",
        "markdown-it/markdown-it-texmath.js", "markdown-it/markdown-it-inject-linenumbers.js",
        "markdown-it/markdownItAnchor.umd.js", "markdown-it/markdownItTocDoneRight.umd.js",
        "markdown-it/markdown-it-implicit-figure.js", "markdown-it/markdown-it-mark.min.js",
        "markdownit.js"}) {
    source +=
        readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &err) + QLatin1Char('\n');
    QVERIFY2(err.isEmpty(), qPrintable(err));
  }

  source += QStringLiteral(R"JS(
var text = ['[toc]', '', '```cpp', 'struct CacheEntry {};', '```', '',
            '## 1. $a*b=c$', '', '## 2. $a*b=c$', '', '## $a*b=c$', '', '## Sum $$a+b$$', '',
            '## Plain `code` and **bold** [link][ref]', '', '[ref]: https://example.com'].join('\n');
document.body.innerHTML = worker.mdit.render(text);
var copiedAnchors = [6, 8, 10, 12, 14].map(function(line) {
  var result = worker.getHeadingAnchor(text, line);
  if (!result.found) { throw new Error('heading not found at line ' + line); }
  return result.anchor;
});
function targets(selector) {
  return Array.from(document.querySelectorAll(selector), function(link) {
    return link.getAttribute('href').substring(1);
  });
}
return JSON.stringify({
  renderedIds: Array.from(document.querySelectorAll('h2'), function(node) { return node.id; }),
  tocTargets: targets('nav a'),
  permalinkTargets: targets('.vx-header-anchor'),
  copiedAnchors: copiedAnchors,
  inlineMath: document.querySelector('h2 eq').textContent,
  displayMath: document.querySelector('h2 eqn').textContent
});
} catch (error) { return JSON.stringify({ error: String(error) + '\n' + error.stack }); }
})()
)JS");
  // Use the viewer's JavaScript engine: QJSEngine does not implement the Unicode
  // property escapes used by generateHeaderId. No renderer or network is needed.
  QVariant result;
  bool finished = false;
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.setHtml(QStringLiteral("<!doctype html><html><body></body></html>"));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());
  page.runJavaScript(source, [&result, &finished](const QVariant &p_result) {
    result = p_result;
    finished = true;
  });
  QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
  const auto document = QJsonDocument::fromJson(result.toString().toUtf8());
  QVERIFY2(document.isObject(), qPrintable(result.toString()));
  const auto values = document.object();
  QVERIFY2(!values.contains(QStringLiteral("error")),
           qPrintable(values.value(QStringLiteral("error")).toString()));
  const auto expected =
      QJsonArray::fromStringList({"abc", "abc-1", "abc-2", "sum-ab", "plain-code-and-bold-link"});
  for (const auto *name : {"renderedIds", "tocTargets", "permalinkTargets", "copiedAnchors"}) {
    QCOMPARE(values.value(QLatin1String(name)).toArray(), expected);
  }
  QCOMPARE(values.value(QStringLiteral("inlineMath")).toString(), QStringLiteral("$a*b=c$"));
  QCOMPARE(values.value(QStringLiteral("displayMath")).toString(), QStringLiteral("$$a+b$$"));
}

void TestMarkdownViewerJs::testCmarkMathTableDom_data() {
  QTest::addColumn<bool>("sanitize");
  QTest::newRow("sanitize-off") << false;
  QTest::newRow("sanitize-on") << true;
}

void TestMarkdownViewerJs::testCmarkMathTableDom() {
  QFETCH(bool, sanitize);

  struct Formula {
    QByteArray markdown;
    int options;
  };
  const Formula formulas[] = {
      {"$p_1$", CMARK_OPT_DEFAULT},
      {"$a\\$b$", CMARK_OPT_DEFAULT},
      {"$\\text{</eq><img>}$", CMARK_OPT_DEFAULT},
      {"$$\np_1\n$$", CMARK_OPT_DEFAULT},
      {"$$\np_1\n$$", CMARK_OPT_SOURCEPOS},
  };
  QStringList fragments;
  for (const auto &formula : formulas) {
    const std::unique_ptr<char, decltype(&std::free)> html(
        cmark_markdown_to_html(formula.markdown.constData(),
                               static_cast<size_t>(formula.markdown.size()), formula.options),
        &std::free);
    QVERIFY2(html != nullptr, "cmark_markdown_to_html failed");
    fragments.append(QString::fromUtf8(html.get()));
  }
  const auto tableHtml =
      QStringLiteral("<table><tbody><tr><td colspan=\"2\"><!--vte-md:$p_1$-->%1</td></tr>"
                     "<tr><td>%2</td><td>%3</td></tr>"
                     "<tr><td>%4</td><td>%5</td></tr>"
                     "<tr><td>$literal$</td><td><code>$code$</code></td></tr></tbody></table>")
          .arg(fragments[0], fragments[1], fragments[2], fragments[3], fragments[4]);
  const QJsonObject fixture{{QStringLiteral("sanitize"), sanitize},
                            {QStringLiteral("html"), tableHtml}};
  QString err;
  QString source =
      QStringLiteral(R"JS(
(function() {
try {
var fixture = %1;
window.vxOptions = {
  htmlTagEnabled: true, protectFromXss: fixture.sanitize, protectedView: false
};
var worker;
window.vxcore = { registerWorker: function(value) { worker = value; } };
)JS")
          .arg(QString::fromUtf8(QJsonDocument(fixture).toJson(QJsonDocument::Compact)));
  for (const auto *name :
       {"vxworker.js", "utils.js", "markdown-it/markdown-it.min.js",
        "markdown-it/markdown-it-container.min.js", "markdown-it/markdown-it-emoji.min.js",
        "markdown-it/markdown-it-footnote.min.js", "markdown-it/markdown-it-front-matter.js",
        "markdown-it/markdown-it-imsize.min.js", "markdown-it/markdown-it-sub.min.js",
        "markdown-it/markdown-it-sup.min.js", "markdown-it/markdown-it-task-lists.js",
        "markdown-it/markdown-it-texmath.js", "markdown-it/markdown-it-inject-linenumbers.js",
        "markdown-it/markdownItAnchor.umd.js", "markdown-it/markdownItTocDoneRight.umd.js",
        "markdown-it/markdown-it-implicit-figure.js", "markdown-it/markdown-it-mark.min.js",
        "markdown-it/xss.min.js", "markdown-it/markdown-it-xss.js"}) {
    source +=
        readFile(webDir() + QStringLiteral("/js/") + QLatin1String(name), &err) + QLatin1Char('\n');
    QVERIFY2(err.isEmpty(), qPrintable(err));
  }
  // The real XSS scripts are already loaded; bypass only their asynchronous transport.
  source += QStringLiteral("\nUtils.loadScripts = function(urls, callback) { callback(); };\n");
  source += readFile(webDir() + QStringLiteral("/js/markdownit.js"), &err) + QLatin1Char('\n');
  QVERIFY2(err.isEmpty(), qPrintable(err));
  source += QStringLiteral(R"JS(
var input = document.createElement('template');
input.innerHTML = fixture.html;
if (fixture.sanitize) {
  input.content.querySelector('eq.tex-to-render').setAttribute('onclick', 'void 0');
}
document.body.innerHTML = worker.mdit.render(input.innerHTML);
var table = document.querySelector('table');
var merged = table.rows[0].cells[0];
var mergedMath = merged.querySelector('eq.tex-to-render');
var escapedDollarMath = table.rows[1].cells[0].querySelector('eq.tex-to-render');
var tagMath = table.rows[1].cells[1].querySelector('eq.tex-to-render');
var blockMath = table.rows[2].cells[0].querySelector('section > eqn.tex-to-render');
var sourceposMath = table.rows[2].cells[1].querySelector('section > eqn.tex-to-render');
window.__cmarkMathTableResult = JSON.stringify({
  tableCount: document.querySelectorAll('table').length,
  rowCellCounts: Array.from(table.rows, function(row) { return row.cells.length; }),
  colSpan: merged.colSpan,
  mergedText: mergedMath.textContent,
  mergedClass: mergedMath.getAttribute('class'),
  mergedOnclick: mergedMath.hasAttribute('onclick'),
  escapedDollarText: escapedDollarMath.textContent,
  tagText: tagMath.textContent,
  tagNodeTypes: Array.from(tagMath.childNodes, function(node) { return node.nodeType; }),
  imageCount: document.querySelectorAll('img').length,
  inlineCount: document.querySelectorAll('eq').length,
  displayCount: document.querySelectorAll('eqn').length,
  markedMathCount: document.querySelectorAll('.tex-to-render').length,
  blockText: blockMath.textContent,
  sourceposText: sourceposMath.textContent,
  sourcepos: sourceposMath.getAttribute('data-sourcepos'),
  literalHtml: table.rows[3].cells[0].innerHTML,
  codeHtml: table.rows[3].cells[1].innerHTML
});
} catch (error) {
  window.__cmarkMathTableResult = JSON.stringify({ error: String(error) + '\n' + error.stack });
}
})();
)JS");

  // A real script element supplies document.currentScript during worker construction.
  const QJsonObject scriptData{{QStringLiteral("source"), source}};
  const auto execute =
      QStringLiteral(R"JS(
(function() {
var script = document.createElement('script');
script.textContent = %1.source;
document.head.appendChild(script);
script.remove();
return window.__cmarkMathTableResult;
})()
)JS")
          .arg(QString::fromUtf8(QJsonDocument(scriptData).toJson(QJsonDocument::Compact)));
  QVariant result;
  bool finished = false;
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.setHtml(QStringLiteral("<!doctype html><html><body></body></html>"));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());
  page.runJavaScript(execute, [&result, &finished](const QVariant &p_result) {
    result = p_result;
    finished = true;
  });
  QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
  const auto document = QJsonDocument::fromJson(result.toString().toUtf8());
  QVERIFY2(document.isObject(), qPrintable(result.toString()));
  const auto values = document.object();
  QVERIFY2(!values.contains(QStringLiteral("error")),
           qPrintable(values.value(QStringLiteral("error")).toString()));
  QCOMPARE(values.value(QStringLiteral("tableCount")).toInt(), 1);
  QCOMPARE(values.value(QStringLiteral("rowCellCounts")).toArray(), QJsonArray({1, 2, 2, 2}));
  QCOMPARE(values.value(QStringLiteral("colSpan")).toInt(), 2);
  QCOMPARE(values.value(QStringLiteral("mergedText")).toString(), QStringLiteral("$p_1$"));
  QCOMPARE(values.value(QStringLiteral("mergedClass")).toString(), QStringLiteral("tex-to-render"));
  QCOMPARE(values.value(QStringLiteral("escapedDollarText")).toString(), QStringLiteral("$a\\$b$"));
  QCOMPARE(values.value(QStringLiteral("tagText")).toString(),
           QStringLiteral("$\\text{</eq><img>}$"));
  QCOMPARE(values.value(QStringLiteral("tagNodeTypes")).toArray(), QJsonArray({3}));
  QCOMPARE(values.value(QStringLiteral("imageCount")).toInt(), 0);
  QCOMPARE(values.value(QStringLiteral("inlineCount")).toInt(), 3);
  QCOMPARE(values.value(QStringLiteral("displayCount")).toInt(), 2);
  QCOMPARE(values.value(QStringLiteral("markedMathCount")).toInt(), 5);
  QCOMPARE(values.value(QStringLiteral("blockText")).toString(), QStringLiteral("$$p_1\n$$"));
  QCOMPARE(values.value(QStringLiteral("sourceposText")).toString(), QStringLiteral("$$p_1\n$$"));
  QCOMPARE(values.value(QStringLiteral("sourcepos")).toString(), QStringLiteral("1:1-3:2"));
  QCOMPARE(values.value(QStringLiteral("literalHtml")).toString(), QStringLiteral("$literal$"));
  QCOMPARE(values.value(QStringLiteral("codeHtml")).toString(),
           QStringLiteral("<code>$code$</code>"));
  if (sanitize) {
    QVERIFY(!values.value(QStringLiteral("mergedOnclick")).toBool());
  }
}

void TestMarkdownViewerJs::testMathRenderer_initializationFanout() {
  QJSEngine engine;
  setupMath(engine);
  auto res = engine.evaluate(QStringLiteral("startRequests();"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("scripts.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("styles.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("passes + results.length")).toInt(), 0);

  engine.evaluate(QStringLiteral("installLibrary(); scripts[0]();"));
  QTest::qWait(1);
  QCOMPARE(engine.evaluate(QStringLiteral("passes + results.length")).toInt(), 0);
  engine.evaluate(QStringLiteral("styles[0]('.katex { display: inline; }');"));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("results.length")).toInt(), 2);
  QTRY_COMPARE(engine.evaluate(QStringLiteral("passes")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("reading.rendered")).toString(), QStringLiteral("a+b"));
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "results.map(function(r) { return r.id + ':' + r.node.rendered; }).join(',')"))
               .toString(),
           QStringLiteral("1:x,2:y"));
  QVERIFY(engine.evaluate(QStringLiteral("results[0].node !== results[1].node")).toBool());
}

void TestMarkdownViewerJs::testMathRenderer_failedInitializationReleasesPass() {
  for (const bool scriptFails : {true, false}) {
    QJSEngine engine;
    setupMath(engine);
    engine.evaluate(QStringLiteral("startRequests();"));
    QTRY_COMPARE(engine.evaluate(QStringLiteral("scripts.length")).toInt(), 1);
    if (scriptFails) {
      engine.evaluate(QStringLiteral("scripts[0](); styles[0]('.katex {}');"));
    } else {
      engine.evaluate(QStringLiteral("installLibrary(); scripts[0](); styles[0](null);"));
    }
    QTRY_COMPARE(engine.evaluate(QStringLiteral("results.length")).toInt(), 2);
    QTRY_COMPARE(engine.evaluate(QStringLiteral("passes")).toInt(), 1);
    QVERIFY(
        engine.evaluate(QStringLiteral("results.every(function(r) { return r.node === null; })"))
            .toBool());
    QCOMPARE(engine.evaluate(QStringLiteral("reading.textContent")).toString(),
             QStringLiteral("$a+b$"));

    engine.evaluate(QStringLiteral("worker.render(container, 'tex-to-render');"));
    QTRY_COMPARE(engine.evaluate(QStringLiteral("passes")).toInt(), 2);
    QCOMPARE(engine.evaluate(QStringLiteral("results.length")).toInt(), 2);
    QCOMPARE(engine.evaluate(QStringLiteral("scripts.length")).toInt(), 1);
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

  auto res = engine.evaluate(QStringLiteral("window.__adapter.taskListToggleRejected.emit(3)"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  QCOMPARE(engine.evaluate(QStringLiteral("window.__reverted.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("window.__reverted[0]")).toString(),
           QStringLiteral("[data-source-line=\"3\"] input.task-list-item-checkbox"));
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
  QJSEngine engine;
  setupSectionNumber(engine);
  auto res = engine.evaluate(QStringLiteral(R"JS(
// The retained property changes before the worker can subscribe on its first render.
__setSectionOptions(true, '1.1)', true);
__makeSectionFixture([[1, 'Title'], [2, 'Alpha'], [3, 'Detail'], [2, 'Beta']]);
__sectionHeadings[1].textContent = '';
var inlineLink = add(__sectionHeadings[1], 'a', '', 'Alpha');
inlineLink.setAttribute('href', '#heading-2');
vxcore.nodeLineMapper = new __NodeLineMapper(vxcore, __container);
vxcore.setHeadingFoldingEnabled(true);
// Supply rendered child nodes at the renderer boundary; dispatch, worker accounting,
// folding, heading extraction and the outgoing adapter bridge remain production code.
vxcore.on('markdownTextUpdated', function() { vxcore.setBasicMarkdownRendered(); });
vxcore.on('basicMarkdownRendered', function() { __trace.push('basic-listeners-done'); });
__trace = [];
vxcore.setMarkdownText('first fixture');
__trace.push('render-return');
var synchronousTrace = __trace.join('|');
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine.evaluate(QStringLiteral("synchronousTrace")).toString(),
           QStringLiteral("headings|basic-listeners-done|render-return"));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 1);
  QCOMPARE(engine.evaluate(QStringLiteral("__trace.join('|')")).toString(),
           QStringLiteral("headings|basic-listeners-done|render-return|finished"));
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[0].names")).toVariant().toStringList(),
           (QStringList{"Title", "1) Alpha", "1.1) Detail", "2) Beta"}));
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[0].hasSectionNumber")).toBool(), true);
  QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);
  QCOMPARE(engine.evaluate(QStringLiteral("__optionHandlers.length")).toInt(), 1);
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "vxMarkdownAdapter.headings.map(function(heading) { return heading.anchor; })"))
               .toVariant()
               .toStringList(),
           (QStringList{"heading-0", "heading-1", "heading-2", "heading-3"}));

  res = engine.evaluate(QStringLiteral(R"JS(
__buttonFor(__sectionHeadings[1]).click();
var foldedBeforeNavigation = __contentFor(__sectionHeadings[1]).hidden;
vxcore.scrollToAnchor('heading-2');
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QVERIFY(engine.evaluate(QStringLiteral("foldedBeforeNavigation")).toBool());
  QVERIFY(!engine.evaluate(QStringLiteral("__contentFor(__sectionHeadings[1]).hidden")).toBool());
  QVERIFY(engine.evaluate(QStringLiteral("__sectionHeadings[2]._scrolledIntoView")).toBool());

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
    engine.globalObject().setProperty(QStringLiteral("nextEnabled"), transition.enabled);
    engine.globalObject().setProperty(QStringLiteral("nextPattern"), transition.pattern);
    engine.globalObject().setProperty(QStringLiteral("nextDetectHeading1"),
                                      transition.detectHeading1);
    res = engine.evaluate(QStringLiteral(
        "__trace = []; __setSectionOptions(nextEnabled, nextPattern, nextDetectHeading1);"));
    QVERIFY2(!res.isError(), qPrintable(res.toString()));
    QCOMPARE(engine.evaluate(QStringLiteral("__sectionNames()")).toVariant().toStringList(),
             transition.names);
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "vxMarkdownAdapter.headings.map(function(heading) { return heading.name; })"))
                 .toVariant()
                 .toStringList(),
             transition.names);
    QCOMPARE(engine.evaluate(QStringLiteral("vxMarkdownAdapter.hasSectionNumber")).toBool(),
             transition.enabled);
    QCOMPARE(engine.evaluate(QStringLiteral("__trace.join('|')")).toString(),
             QStringLiteral("headings"));
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "__container.querySelectorAll('button.vx-heading-fold-toggle').length"))
                 .toInt(),
             4);
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "__container.querySelectorAll('span.vx-section-number').length"))
                 .toInt(),
             transition.enabled ? (transition.detectHeading1 ? 3 : 4) : 0);
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "__sectionHeadings.map(function(heading) { return heading.id; })"))
                 .toVariant()
                 .toStringList(),
             (QStringList{"heading-0", "heading-1", "heading-2", "heading-3"}));
    QVERIFY(
        engine.evaluate(QStringLiteral("inlineLink.parentNode === __sectionHeadings[1]")).toBool());
    QCOMPARE(engine.evaluate(QStringLiteral("inlineLink.getAttribute('href')")).toString(),
             QStringLiteral("#heading-2"));
    QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 1);
    QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);
  }

  const QStringList dottedNames{"Title", "1. Alpha", "1.1. Detail", "2. Beta"};
  for (const bool foldingEnabled : {false, true}) {
    engine.globalObject().setProperty(QStringLiteral("nextFolding"), foldingEnabled);
    res = engine.evaluate(QStringLiteral("vxcore.setHeadingFoldingEnabled(nextFolding);"));
    QVERIFY2(!res.isError(), qPrintable(res.toString()));
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "vxMarkdownAdapter.headings.map(function(heading) { return heading.name; })"))
                 .toVariant()
                 .toStringList(),
             dottedNames);
    QCOMPARE(engine.evaluate(QStringLiteral("vxMarkdownAdapter.hasSectionNumber")).toBool(), true);
    QCOMPARE(engine
                 .evaluate(QStringLiteral(
                     "__container.querySelectorAll('button.vx-heading-fold-toggle').length"))
                 .toInt(),
             foldingEnabled ? 4 : 0);
  }

  for (int round = 2; round <= 3; ++round) {
    res = engine.evaluate(QStringLiteral(R"JS(
__trace = [];
vxcore.setMarkdownText('same rendered fixture');
__trace.push('render-return');
synchronousTrace = __trace.join('|');
)JS"));
    QVERIFY2(!res.isError(), qPrintable(res.toString()));
    QCOMPARE(engine.evaluate(QStringLiteral("synchronousTrace")).toString(),
             QStringLiteral("headings|basic-listeners-done|render-return"));
    QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), round);
    QCOMPARE(engine.evaluate(QStringLiteral("__trace.join('|')")).toString(),
             QStringLiteral("headings|basic-listeners-done|render-return|finished"));
    QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[%1].names").arg(round - 1))
                 .toVariant()
                 .toStringList(),
             dottedNames);
    QCOMPARE(
        engine.evaluate(QStringLiteral("__finishSnapshots[%1].hasSectionNumber").arg(round - 1))
            .toBool(),
        true);
    QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);
    QCOMPARE(engine.evaluate(QStringLiteral("__optionHandlers.length")).toInt(), 1);
  }

  // A later option update must still publish exactly once and never finish a render unit.
  res = engine.evaluate(QStringLiteral("__trace = []; __setSectionOptions(false, '1.1.', true);"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(engine.evaluate(QStringLiteral("__trace.join('|')")).toString(),
           QStringLiteral("headings"));
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "vxMarkdownAdapter.headings.map(function(heading) { return heading.name; })"))
               .toVariant()
               .toStringList(),
           (QStringList{"Title", "Alpha", "Detail", "Beta"}));
  QCOMPARE(engine.evaluate(QStringLiteral("vxMarkdownAdapter.hasSectionNumber")).toBool(), false);
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 3);

  res = engine.evaluate(QStringLiteral("vxcore.setMarkdownText('disabled existing fixture');"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 4);
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[3].names")).toVariant().toStringList(),
           (QStringList{"Title", "Alpha", "Detail", "Beta"}));
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[3].hasSectionNumber")).toBool(),
           false);
  QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);

  res = engine.evaluate(QStringLiteral(R"JS(
__container.textContent = '';
__sectionHeadings = [add(__container, 'h1', 'new-title', 'Replacement title')];
__setSectionOptions(true, '1.1.', true);
vxcore.setMarkdownText('title-only replacement');
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 5);
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[4].names")).toVariant().toStringList(),
           (QStringList{"Replacement title"}));
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[4].hasSectionNumber")).toBool(),
           false);
  QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);

  res = engine.evaluate(QStringLiteral(R"JS(
__container.textContent = '';
__sectionHeadings = [add(__container, 'h1', 'numbered-title', 'Replacement title')];
__setSectionOptions(true, '1.1.', false);
vxcore.setMarkdownText('numbered title-only replacement');
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 6);
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[5].names")).toVariant().toStringList(),
           (QStringList{"1. Replacement title"}));
  QVERIFY(engine.evaluate(QStringLiteral("__finishSnapshots[5].hasSectionNumber")).toBool());
  QCOMPARE(
      engine
          .evaluate(QStringLiteral("__container.querySelectorAll('span.vx-section-number').length"))
          .toInt(),
      1);
  QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);

  res = engine.evaluate(QStringLiteral(R"JS(
__container.textContent = '';
__sectionHeadings = [];
vxcore.setMarkdownText('empty replacement');
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 7);
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[6].names")).toVariant().toStringList(),
           QStringList());
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[6].hasSectionNumber")).toBool(),
           false);
  QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);
  QCOMPARE(engine.evaluate(QStringLiteral("__optionHandlers.length")).toInt(), 1);
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
