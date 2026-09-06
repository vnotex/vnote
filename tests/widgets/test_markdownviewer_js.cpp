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
#include <QString>
#include <QtTest>

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

function Element(tagName) {
  this.tagName = tagName.toUpperCase();
  this.parentNode = null;
  this.childNodes = [];
  this.attributes = {};
  this.className = '';
  this.classList = new ClassList(this);
  this.hidden = false;
  this._text = '';
  this._listeners = {};
  this._rect = { y: 100, top: 100, bottom: 120 };
}
Object.defineProperty(Element.prototype, 'id', {
  get: function() { return this.attributes.id || ''; },
  set: function(value) { this.attributes.id = String(value); }
});
Object.defineProperty(Element.prototype, 'textContent', {
  get: function() {
    var text = this._text;
    for (var i = 0; i < this.childNodes.length; ++i) {
      text += this.childNodes[i].textContent;
    }
    return text;
  },
  set: function(value) {
    this._text = String(value);
    while (this.childNodes.length) {
      this.removeChild(this.childNodes[0]);
    }
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
Element.prototype.setAttribute = function(name, value) { this.attributes[name] = String(value); };
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
    setHeadings: function(headings) { this.headings = headings; },
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

window.__buttonFor = function(heading) { return heading.firstElementChild; };
window.__contentFor = function(heading) {
  return document.getElementById(window.__buttonFor(heading).getAttribute('aria-controls'));
};
)JS");
}

} // namespace

class TestMarkdownViewerJs : public QObject {
  Q_OBJECT

private slots:
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

private:
  // Evaluates the prelude plus the real markdownviewer.js.
  void setup(QJSEngine &p_engine, bool p_initializedAtChannel);
  void setupHeadingFolding(QJSEngine &p_engine);
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownViewerJs)
#include "test_markdownviewer_js.moc"
