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
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QtTest>

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
  void testMathRenderer_initializationFanout();
  void testMathRenderer_failedInitializationReleasesPass();
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
  sectionNumberOptions: { enabled: false, pattern: '1.1.' },
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
function __setSectionOptions(enabled, pattern) {
  vxMarkdownAdapter.sectionNumberOptions = { enabled: enabled, pattern: pattern };
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
  fonts: { ready: Promise.resolve() },
  head: { appendChild: function() {} },
  createElement: element
};
window.getComputedStyle = function() { return { color: 'black', font: '16px serif' }; };
var container = {
  getElementsByClassName: function() { return [reading]; },
  appendChild: function(node) { node.parentNode = this; },
  removeChild: function(node) { node.parentNode = null; }
};
var Utils = {
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
  QCOMPARE(engine
               .evaluate(QStringLiteral(
                   "document.body.querySelectorAll('svg.vx-heading-fold-icon').length"))
               .toInt(),
           7);

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
  const auto row = [](const char *name, const char *json, const QStringList &expected,
                      int generated, bool numbered = true,
                      const QString &pattern = QStringLiteral("1.1.")) {
    QTest::newRow(name) << QJsonDocument::fromJson(json).array() << expected << generated
                        << numbered << pattern;
  };

  row("title-after-prose", R"JSON([[1,"Title"],[2,"A"],[3,"B"],[2,"C"]])JSON",
      {"Title", "1. A", "1.1. B", "2. C"}, 3);
  row("two-h1s", R"JSON([[1,"A"],[2,"B"],[1,"C"]])JSON", {"1. A", "1.1. B", "2. C"}, 3);
  row("sole-nonfirst-h1", R"JSON([[2,"A"],[1,"B"]])JSON", {"1.1. A", "2. B"}, 2);
  row("base-h3", R"JSON([[1,"Title"],[3,"A"],[3,"B"]])JSON", {"Title", "1. A", "2. B"}, 2);
  row("skipped-level", R"JSON([[1,"Title"],[2,"A"],[4,"B"],[2,"C"]])JSON",
      {"Title", "1. A", "1.1.1. B", "2. C"}, 3);
  row("no-headings", "[]", {}, 0, false);
  row("title-only", R"JSON([[1,"Title"]])JSON", {"Title"}, 0, false);
  row("no-suffix", R"JSON([[1,"Title"],[2,"A"],[3,"B"]])JSON", {"Title", "1 A", "1.1 B"}, 2, true,
      QStringLiteral("1.1"));
  row("parenthesis-suffix", R"JSON([[1,"Title"],[2,"A"],[3,"B"]])JSON", {"Title", "1) A", "1.1) B"},
      2, true, QStringLiteral("1.1)"));
  row("unknown-pattern", R"JSON([[2,"A"],[3,"B"]])JSON", {"1. A", "1.1. B"}, 2, true,
      QStringLiteral("unknown"));
  row("empty-pattern", R"JSON([[2,"A"],[3,"B"]])JSON", {"1. A", "1.1. B"}, 2, true, QString());

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
        << headings << names << 0 << true << QStringLiteral("1.1.");
  }
  row("five-match-sixth-mismatch",
      R"JSON([[1,"Title"],[2,"1. A"],[2,"2. B"],[2,"3. C"],[2,"4. D"],[2,"5. E"],[2,"F"]])JSON",
      {"Title", "1. A", "2. B", "3. C", "4. D", "5. E", "F"}, 0);
  row("fourth-mismatch-keeps-authored-prefixes",
      R"JSON([[1,"Title"],[2,"1. A"],[2,"2. B"],[2,"3. C"],[2,"D"],[2,"5. E"],[2,"6. F"]])JSON",
      {"Title", "1. 1. A", "2. 2. B", "3. 3. C", "4. D", "5. 5. E", "6. 6. F"}, 6);
  row("mixed-authored-styles",
      R"JSON([[2,"1 Intro"],[2,"1. Intro"],[2,"1) Intro"],[2,"1.2. Intro"],[2,"1.2) Intro"]])JSON",
      {"1 Intro", "1. Intro", "1) Intro", "1.2. Intro", "1.2) Intro"}, 0, true,
      QStringLiteral("1.1)"));
  row("numeric-only", R"JSON([[2,"0"],[2,"1."],[2,"1)"],[2,"1.2"],[2,"1.2)"]])JSON",
      {"0", "1.", "1)", "1.2", "1.2)"}, 0);
  row("leading-unicode-space", R"JSON([[2,"\u20030 Intro"],[3,"\t1.2 Detail"]])JSON",
      {QString(QChar(0x2003)) + QStringLiteral("0 Intro"), QStringLiteral("\t1.2 Detail")}, 0);
  row("nonleading-numeric-prefix", R"JSON([[2,"v1.2 Intro"]])JSON", {"1. v1.2 Intro"}, 1);
  row("missing-prefix-separator", R"JSON([[2,"1.2Intro"]])JSON", {"1. 1.2Intro"}, 1);
  row("empty-prefix-component", R"JSON([[2,"1.. Intro"]])JSON", {"1. 1.. Intro"}, 1);
  row("placeholders-before-title-and-base",
      R"JSON([[1,"[EMPTY]",true],[1,"Title"],[2,"[EMPTY]",true],[3,"A"]])JSON", {"Title", "1. A"},
      1);
  row("placeholders-do-not-break-authored-sample",
      R"JSON([[1,"Title"],[2,"1. A"],[3,"[EMPTY]",true],[4,"1.1.1. B"]])JSON",
      {"Title", "1. A", "1.1.1. B"}, 0);
  row("placeholders-do-not-advance-counters",
      R"JSON([[1,"Title"],[2,"A"],[3,"[EMPTY]",true],[4,"B"],[2,"C"]])JSON",
      {"Title", "1. A", "1.1.1. B", "2. C"}, 3);
  row("real-empty-name-participates",
      R"JSON([[1,"Title"],[2,"1. A"],[3,"[EMPTY]",true],[4,"[EMPTY]"]])JSON",
      {"Title", "1. 1. A", "1.1.1. [EMPTY]"}, 2);
  row("only-placeholders", R"JSON([[1,"[EMPTY]",true],[2,"[EMPTY]",true]])JSON", {}, 0, false);
  row("invalid-levels-are-not-headings",
      R"JSON([[0,"Invalid"],[1,"Title"],[-1,"Invalid"],[2,"A"]])JSON", {"Title", "1. A"}, 1);
}

void TestMarkdownViewerJs::testSectionNumber_policyParity() {
  QFETCH(QJsonArray, headings);
  QFETCH(QStringList, expected);
  QFETCH(int, generated);
  QFETCH(bool, hasSectionNumber);
  QFETCH(QString, pattern);

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
  const auto analysis = vnotex::SectionNumberUtils::analyze(cppHeadings);
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
  res = engine.evaluate(
      QStringLiteral("__sectionNumber.apply(__container, { enabled: true, pattern: __pattern });"));
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
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.' });
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
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.' });
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.' });
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  check(dottedNames, true, 4);

  res = engine.evaluate(
      QStringLiteral("__sectionNumber.apply(__container, { enabled: true, pattern: '1.1)' });"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  check({"Title", "1) Alpha Link Detail authored", "1.1) Nested", "2) Peer"}, true, 4);
  QVERIFY(engine
              .evaluate(QStringLiteral("originalChildren.every(function(node, index) { "
                                       "return heading.childNodes[index + 1] === node; })"))
              .toBool());
  res = engine.evaluate(QStringLiteral(R"JS(
var ownedSpans = __sectionHeadings.map(function(node) { return node.__vxSectionNumberSpan; })
                                .filter(function(node) { return !!node; });
__sectionNumber.apply(__container, { enabled: false, pattern: '1.1)' });
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
__sectionNumber.apply(__container, { enabled: true, pattern: '1.1.' });
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
__setSectionOptions(true, '1.1)');
__makeSectionFixture([[1, 'Title'], [2, 'Alpha'], [3, 'Detail'], [2, 'Beta']]);
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
    QStringList names;
  };
  const QVector<Transition> transitions{
      {true, QStringLiteral("1.1"), {"Title", "1 Alpha", "1.1 Detail", "2 Beta"}},
      {false, QStringLiteral("1.1"), {"Title", "Alpha", "Detail", "Beta"}},
      {true, QStringLiteral("1.1."), {"Title", "1. Alpha", "1.1. Detail", "2. Beta"}}};
  for (const auto &transition : transitions) {
    engine.globalObject().setProperty(QStringLiteral("nextEnabled"), transition.enabled);
    engine.globalObject().setProperty(QStringLiteral("nextPattern"), transition.pattern);
    res = engine.evaluate(
        QStringLiteral("__trace = []; __setSectionOptions(nextEnabled, nextPattern);"));
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
             transition.enabled ? 3 : 0);
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
  res = engine.evaluate(QStringLiteral("__trace = []; __setSectionOptions(false, '1.1.');"));
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
__setSectionOptions(true, '1.1.');
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
__sectionHeadings = [];
vxcore.setMarkdownText('empty replacement');
)JS"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QTRY_COMPARE(engine.evaluate(QStringLiteral("__finishSnapshots.length")).toInt(), 6);
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[5].names")).toVariant().toStringList(),
           QStringList());
  QCOMPARE(engine.evaluate(QStringLiteral("__finishSnapshots[5].hasSectionNumber")).toBool(),
           false);
  QCOMPARE(engine.evaluate(QStringLiteral("vxcore.numOfOngoingWorkers")).toInt(), 0);
  QCOMPARE(engine.evaluate(QStringLiteral("__optionHandlers.length")).toInt(), 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownViewerJs)
#include "test_markdownviewer_js.moc"
