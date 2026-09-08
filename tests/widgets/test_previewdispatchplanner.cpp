#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>
#include <QtTest>

#include "../../src/widgets/editors/previewdispatchplanner.h"

namespace tests {

using vnotex::PreviewDispatchPlanner;
using vnotex::PreviewDispatchRequest;

class TestPreviewDispatchPlanner : public QObject {
  Q_OBJECT

private slots:
  void inclusiveBoundariesAreVisibleFirst();
  void spanningAndFoldedBlocksRespectBlockVisibility();
  void orderingIsStableAndIdsAreRetained();
  void fallbackAndInvalidInputsPreserveRequests();

private:
  static void populateDocument(QTextDocument &p_document, int p_blockCount);
  static QVector<int> ids(const QVector<PreviewDispatchRequest> &p_requests);
};

void TestPreviewDispatchPlanner::populateDocument(QTextDocument &p_document, int p_blockCount) {
  QStringList lines;
  for (int i = 0; i < p_blockCount; ++i) {
    lines.append(QString::number(i));
  }
  p_document.setPlainText(lines.join(QLatin1Char('\n')));
}

QVector<int> TestPreviewDispatchPlanner::ids(const QVector<PreviewDispatchRequest> &p_requests) {
  QVector<int> result;
  for (const auto &request : p_requests) {
    result.append(request.m_previewId);
  }
  return result;
}

void TestPreviewDispatchPlanner::inclusiveBoundariesAreVisibleFirst() {
  QTextDocument document;
  populateDocument(document, 12);
  const QVector<PreviewDispatchRequest> requests = {
      {10, 0, 2}, {20, 3, 3}, {30, 7, 7}, {40, 8, 10}};

  const auto ordered = PreviewDispatchPlanner::visibleFirst(requests, &document, {3, 7});
  QCOMPARE(ids(ordered), QVector<int>({20, 30, 10, 40}));
}

void TestPreviewDispatchPlanner::spanningAndFoldedBlocksRespectBlockVisibility() {
  QTextDocument document;
  populateDocument(document, 14);
  document.findBlockByNumber(1).setVisible(false);
  document.findBlockByNumber(10).setVisible(false);

  const QVector<PreviewDispatchRequest> spanning = {{1, 1, 10}, {2, 11, 12}};
  QCOMPARE(ids(PreviewDispatchPlanner::visibleFirst(spanning, &document, {5, 6})),
           QVector<int>({1, 2}));

  document.findBlockByNumber(4).setVisible(true);
  document.findBlockByNumber(5).setVisible(false);
  document.findBlockByNumber(6).setVisible(false);
  document.findBlockByNumber(7).setVisible(true);
  const QVector<PreviewDispatchRequest> ownFold = {{3, 4, 7}, {4, 0, 1}};
  QCOMPARE(ids(PreviewDispatchPlanner::visibleFirst(ownFold, &document, {4, 7})),
           QVector<int>({3, 4}));

  document.findBlockByNumber(4).setVisible(false);
  document.findBlockByNumber(7).setVisible(false);
  const QVector<PreviewDispatchRequest> enclosingFold = {{5, 4, 7}, {6, 8, 8}};
  QCOMPARE(ids(PreviewDispatchPlanner::visibleFirst(enclosingFold, &document, {4, 8})),
           QVector<int>({6, 5}));
}

void TestPreviewDispatchPlanner::orderingIsStableAndIdsAreRetained() {
  QTextDocument document;
  populateDocument(document, 10);
  const QVector<PreviewDispatchRequest> requests = {
      {42, 0, 0}, {7, 3, 3}, {99, 1, 1}, {3, 5, 5}, {81, 8, 8}};

  const auto ordered = PreviewDispatchPlanner::visibleFirst(requests, &document, {2, 6});
  QCOMPARE(ids(ordered), QVector<int>({7, 3, 42, 99, 81}));
  QCOMPARE(ordered[0].m_startBlock, 3);
  QCOMPARE(ordered[1].m_startBlock, 5);

  QCOMPARE(PreviewDispatchPlanner::visibleFirst(requests, &document, {0, 9}), requests);
  QCOMPARE(PreviewDispatchPlanner::visibleFirst(requests, &document, {6, 7}), requests);
}

void TestPreviewDispatchPlanner::fallbackAndInvalidInputsPreserveRequests() {
  QTextDocument document;
  populateDocument(document, 10);
  const QVector<PreviewDispatchRequest> requests = {{8, 3, 3}, {2, -1, 4}, {6, 5, 5}};

  QCOMPARE(PreviewDispatchPlanner::visibleFirst(requests, nullptr, {3, 5}), requests);
  QCOMPARE(PreviewDispatchPlanner::visibleFirst(requests, &document, {-1, 5}), requests);
  QCOMPARE(PreviewDispatchPlanner::visibleFirst(requests, &document, {5, 4}), requests);
  QCOMPARE(PreviewDispatchPlanner::visibleFirst(requests, &document, {3, 10}), requests);
  QCOMPARE(ids(PreviewDispatchPlanner::visibleFirst(requests, &document, {3, 5})),
           QVector<int>({8, 6, 2}));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPreviewDispatchPlanner)
#include "test_previewdispatchplanner.moc"
