#include <QDir>
#include <QFile>
#include <QRegularExpression>
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
  void previewHelperKeepsIdContracts();

private:
  static void populateDocument(QTextDocument &p_document, int p_blockCount);
  static QVector<int> ids(const QVector<PreviewDispatchRequest> &p_requests);
  static QString stripComments(const QString &p_source);
  static QString functionBody(const QString &p_source, const QString &p_signature);
  static QString normalized(QString p_source);
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

QString TestPreviewDispatchPlanner::stripComments(const QString &p_source) {
  QString result;
  bool lineComment = false;
  bool blockComment = false;
  QChar quote;
  bool escaped = false;
  for (int i = 0; i < p_source.size(); ++i) {
    const QChar ch = p_source[i];
    const QChar next = i + 1 < p_source.size() ? p_source[i + 1] : QChar();
    if (lineComment) {
      if (ch == QLatin1Char('\n')) {
        lineComment = false;
        result.append(ch);
      }
    } else if (blockComment) {
      if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
        blockComment = false;
        ++i;
      } else if (ch == QLatin1Char('\n')) {
        result.append(ch);
      }
    } else if (!quote.isNull()) {
      result.append(ch);
      if (escaped) {
        escaped = false;
      } else if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == quote) {
        quote = QChar();
      }
    } else if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
      lineComment = true;
      ++i;
    } else if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
      blockComment = true;
      ++i;
    } else {
      result.append(ch);
      if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
        quote = ch;
      }
    }
  }
  return result;
}

QString TestPreviewDispatchPlanner::functionBody(const QString &p_source,
                                                 const QString &p_signature) {
  const int signatureStart = p_source.indexOf(p_signature);
  if (signatureStart < 0) {
    return QString();
  }
  const int bodyStart = p_source.indexOf(QLatin1Char('{'), signatureStart + p_signature.size());
  if (bodyStart < 0) {
    return QString();
  }

  int depth = 0;
  QChar quote;
  bool escaped = false;
  for (int i = bodyStart; i < p_source.size(); ++i) {
    const QChar ch = p_source[i];
    if (!quote.isNull()) {
      if (escaped) {
        escaped = false;
      } else if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == quote) {
        quote = QChar();
      }
      continue;
    }
    if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
      quote = ch;
    } else if (ch == QLatin1Char('{')) {
      ++depth;
    } else if (ch == QLatin1Char('}') && --depth == 0) {
      return p_source.mid(bodyStart, i - bodyStart + 1);
    }
  }
  return QString();
}

QString TestPreviewDispatchPlanner::normalized(QString p_source) {
  p_source.remove(QRegularExpression(QStringLiteral("\\s+")));
  return p_source;
}

void TestPreviewDispatchPlanner::previewHelperKeepsIdContracts() {
  QFile file(QDir(QStringLiteral(VNOTE_SRC_DIR))
                 .filePath(QStringLiteral("widgets/editors/previewhelper.cpp")));
  QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
  const QString source = stripComments(QString::fromUtf8(file.readAll()));

  const QString dispatch = normalized(
      functionBody(source, QStringLiteral("void PreviewHelper::handleCodeBlocksUpdate()")));
  QVERIFY(!dispatch.isEmpty());
  QVERIFY(dispatch.contains(
      QStringLiteral("needPreviewBlocks.append({blockPreviewIdx,cb.m_startBlock,cb.m_endBlock})")));
  QVERIFY(dispatch.contains(QStringLiteral("inplacePreviewCodeBlock(request.m_previewId)")));

  const QString web = normalized(
      functionBody(source, QStringLiteral("void PreviewHelper::handleGraphPreviewData(")));
  const QString local =
      normalized(functionBody(source, QStringLiteral("void PreviewHelper::handleLocalData(")));
  QVERIFY(web.contains(QStringLiteral("m_codeBlocksData[p_data.m_id]")));
  QVERIFY(local.contains(QStringLiteral("m_codeBlocksData[p_id]")));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPreviewDispatchPlanner)
#include "test_previewdispatchplanner.moc"
