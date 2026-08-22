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

private:
  // Evaluates the prelude plus the real markdownviewer.js.
  void setup(QJSEngine &p_engine, bool p_initializedAtChannel);
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownViewerJs)
#include "test_markdownviewer_js.moc"
