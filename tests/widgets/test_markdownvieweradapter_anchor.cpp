// Tests for MarkdownViewerAdapter's heading-anchor request/response plumbing.
//
// This is the C++ bridge half of the edit-mode "Copy Link" heading action:
// fetchHeadingAnchor() sends a request to the web side and setHeadingAnchor()
// is the slot the web side calls back into. The anchor slug itself is computed
// in JavaScript and is covered by tests/web/test_markdownit_heading_anchor.js;
// the link composition is covered by test_markdown_heading_link.cpp.
//
// No web view is created here. The web side is simulated by listening for the
// headingAnchorRequested signal and invoking the setHeadingAnchor slot, which
// is exactly the contract markdownviewer.js implements.

#include <QtTest>

#include <QSignalSpy>
#include <QString>

#include <core/servicelocator.h>
#include <widgets/editors/markdownvieweradapter.h>

using namespace vnotex;

namespace tests {

class TestMarkdownViewerAdapterAnchor : public QObject {
  Q_OBJECT

private slots:
  void negativeLineNumberShortCircuits();
  void readyRequestCarriesTextAndLine();
  void responseDeliversFoundAnchor();
  void responseDeliversEmptyAnchorAsFound();
  void responseDeliversNotFound();
  void requestIsPendedUntilReady();
  void concurrentRequestsGetDistinctIds();

private:
  // Drives one full round trip and returns the resolved result.
  MarkdownViewerAdapter::HeadingAnchorResult roundTrip(MarkdownViewerAdapter &p_adapter,
                                                       const QString &p_text, int p_line,
                                                       bool p_found, const QString &p_anchor,
                                                       bool *p_invoked = nullptr);
};

MarkdownViewerAdapter::HeadingAnchorResult
TestMarkdownViewerAdapterAnchor::roundTrip(MarkdownViewerAdapter &p_adapter, const QString &p_text,
                                           int p_line, bool p_found, const QString &p_anchor,
                                           bool *p_invoked) {
  quint64 requestId = 0;
  bool gotRequest = false;
  connect(&p_adapter, &MarkdownViewerAdapter::headingAnchorRequested, this,
          [&requestId, &gotRequest](quint64 p_id, const QString &, int) {
            requestId = p_id;
            gotRequest = true;
          });

  MarkdownViewerAdapter::HeadingAnchorResult result;
  bool invoked = false;
  p_adapter.fetchHeadingAnchor(
      p_text, p_line, [&result, &invoked](const MarkdownViewerAdapter::HeadingAnchorResult &p_res) {
        result = p_res;
        invoked = true;
      });

  if (gotRequest) {
    // Simulate markdownviewer.js -> markdownviewercore.js -> setHeadingAnchor.
    p_adapter.setHeadingAnchor(requestId, p_found, p_anchor);
  }

  if (p_invoked) {
    *p_invoked = invoked;
  }
  return result;
}

void TestMarkdownViewerAdapterAnchor::negativeLineNumberShortCircuits() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  adapter.setReady(true);

  QSignalSpy spy(&adapter, &MarkdownViewerAdapter::headingAnchorRequested);

  bool invoked = false;
  MarkdownViewerAdapter::HeadingAnchorResult result;
  adapter.fetchHeadingAnchor(QStringLiteral("# First\n"), -1,
                             [&](const MarkdownViewerAdapter::HeadingAnchorResult &p_res) {
                               result = p_res;
                               invoked = true;
                             });

  // Resolved synchronously as not-found; the web side is never bothered.
  QVERIFY(invoked);
  QVERIFY(!result.m_found);
  QVERIFY(result.m_anchor.isEmpty());
  QCOMPARE(spy.count(), 0);
}

void TestMarkdownViewerAdapterAnchor::readyRequestCarriesTextAndLine() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  adapter.setReady(true);

  const QString text = QStringLiteral("# First\n\n## Second\n");
  QSignalSpy spy(&adapter, &MarkdownViewerAdapter::headingAnchorRequested);

  adapter.fetchHeadingAnchor(text, 2, [](const MarkdownViewerAdapter::HeadingAnchorResult &) {});

  QCOMPARE(spy.count(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(1).toString(), text);
  QCOMPARE(args.at(2).toInt(), 2);
}

void TestMarkdownViewerAdapterAnchor::responseDeliversFoundAnchor() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  adapter.setReady(true);

  bool invoked = false;
  const auto result = roundTrip(adapter, QStringLiteral("## Overview\n"), 0, true,
                                QStringLiteral("overview"), &invoked);

  QVERIFY(invoked);
  QVERIFY(result.m_found);
  QCOMPARE(result.m_anchor, QStringLiteral("overview"));
}

void TestMarkdownViewerAdapterAnchor::responseDeliversEmptyAnchorAsFound() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  adapter.setReady(true);

  // An empty ATX heading legitimately slugs to "", which is why the protocol
  // carries a separate found flag instead of using "" as a sentinel.
  bool invoked = false;
  const auto result = roundTrip(adapter, QStringLiteral("## \n"), 0, true, QString(), &invoked);

  QVERIFY(invoked);
  QVERIFY(result.m_found);
  QVERIFY(result.m_anchor.isEmpty());
}

void TestMarkdownViewerAdapterAnchor::responseDeliversNotFound() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  adapter.setReady(true);

  // What the web side reports for e.g. a heading-looking line inside a fence.
  bool invoked = false;
  const auto result =
      roundTrip(adapter, QStringLiteral("```\n## Overview\n```\n"), 1, false, QString(), &invoked);

  QVERIFY(invoked);
  QVERIFY(!result.m_found);
  QVERIFY(result.m_anchor.isEmpty());
}

void TestMarkdownViewerAdapterAnchor::requestIsPendedUntilReady() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  // Deliberately NOT ready: this is the state during early edit-mode startup.

  const QString text = QStringLiteral("## Overview\n");
  QSignalSpy spy(&adapter, &MarkdownViewerAdapter::headingAnchorRequested);

  bool invoked = false;
  MarkdownViewerAdapter::HeadingAnchorResult result;
  adapter.fetchHeadingAnchor(text, 0, [&](const MarkdownViewerAdapter::HeadingAnchorResult &p_res) {
    result = p_res;
    invoked = true;
  });

  // Queued, not sent, and the callback has not run.
  QCOMPARE(spy.count(), 0);
  QVERIFY(!invoked);

  quint64 requestId = 0;
  connect(&adapter, &MarkdownViewerAdapter::headingAnchorRequested, this,
          [&requestId](quint64 p_id, const QString &, int) { requestId = p_id; });

  adapter.setReady(true);

  // Flushed on ready with the original arguments intact.
  QCOMPARE(spy.count(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(1).toString(), text);
  QCOMPARE(args.at(2).toInt(), 0);

  adapter.setHeadingAnchor(requestId, true, QStringLiteral("overview"));
  QVERIFY(invoked);
  QCOMPARE(result.m_anchor, QStringLiteral("overview"));
}

void TestMarkdownViewerAdapterAnchor::concurrentRequestsGetDistinctIds() {
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  adapter.setReady(true);

  QVector<quint64> ids;
  connect(&adapter, &MarkdownViewerAdapter::headingAnchorRequested, this,
          [&ids](quint64 p_id, const QString &, int) { ids.append(p_id); });

  QString firstAnchor;
  QString secondAnchor;
  adapter.fetchHeadingAnchor(QStringLiteral("## A\n"), 0,
                             [&firstAnchor](const MarkdownViewerAdapter::HeadingAnchorResult &p_r) {
                               firstAnchor = p_r.m_anchor;
                             });
  adapter.fetchHeadingAnchor(
      QStringLiteral("## B\n"), 0,
      [&secondAnchor](const MarkdownViewerAdapter::HeadingAnchorResult &p_r) {
        secondAnchor = p_r.m_anchor;
      });

  QCOMPARE(ids.size(), 2);
  QVERIFY(ids[0] != ids[1]);

  // Answer out of order: each id must route to its own callback.
  adapter.setHeadingAnchor(ids[1], true, QStringLiteral("b"));
  adapter.setHeadingAnchor(ids[0], true, QStringLiteral("a"));

  QCOMPARE(firstAnchor, QStringLiteral("a"));
  QCOMPARE(secondAnchor, QStringLiteral("b"));
}

} // namespace tests

QTEST_MAIN(tests::TestMarkdownViewerAdapterAnchor)
#include "test_markdownvieweradapter_anchor.moc"
