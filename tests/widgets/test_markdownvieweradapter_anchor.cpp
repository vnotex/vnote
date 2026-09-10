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

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QPixmap>
#include <QSignalSpy>
#include <QString>
#include <QTextBlock>
#include <QTextDocument>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownhighlighter.h>
#include <vtextedit/previewdata.h>
#include <vtextedit/previewmgr.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/vmarkdowneditor.h>

#include <core/servicelocator.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/previewhelper.h>

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
  void previewResultsKeepHighResolutionAtLogicalSize();
  void mathPreviewZoomWhileRasterPending_data();
  void mathPreviewZoomWhileRasterPending();
  void numberingStatusTracksDocumentReplacement();

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

void TestMarkdownViewerAdapterAnchor::previewResultsKeepHighResolutionAtLogicalSize() {
  QVector<QString> payloads;
  for (const auto &size : {QSize(400, 120), QSize(800, 240)}) {
    QPixmap image(size);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    payloads.append(QString::fromLatin1(bytes.toBase64()));
  }

  for (auto source : {vte::PreviewData::Source::CodeBlock, vte::PreviewData::Source::MathBlock}) {
    ServiceLocator services;
    MarkdownViewerAdapter adapter(services);
    auto textConfig = QSharedPointer<vte::TextEditorConfig>::create();
    textConfig->m_scaleFactor = 1.25;
    auto config = QSharedPointer<vte::MarkdownEditorConfig>::create(textConfig);
    vte::VMarkdownEditor editor(config, QSharedPointer<vte::TextEditorParameters>::create());
    editor.resize(900, 600);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    editor.setText(QStringLiteral("Before.\nPreview source.\nFollowing."));
    auto highlighter = editor.getHighlighter();
    QSignalSpy highlighted(highlighter, &vte::MarkdownHighlighter::highlightCompleted);
    highlighter->updateHighlight();
    QTRY_VERIFY_WITH_TIMEOUT(highlighted.count() > 0, 60000);
    QTest::qWait(50);
    QCoreApplication::processEvents();
    editor.getPreviewMgr()->setPreviewEnabled(source, true);

    const auto block = editor.document()->findBlockByNumber(1);
    auto blockRect = [&]() {
      return editor.document()->documentLayout()->blockBoundingRect(block);
    };
    auto followingY = [&]() {
      return editor.document()->documentLayout()->blockBoundingRect(block.next()).y();
    };
    const auto unpreviewedY = followingY();
    quint64 id = 101;
    quint64 timeStamp = 7001;
    int completions = 0;
    QSize receivedLogicalSize;
    auto publish = [&](const MarkdownViewerAdapter::PreviewData &p_data,
                       vte::PreviewData::Source p_source) {
      ++completions;
      QCOMPARE(p_source, source);
      QCOMPARE(p_data.m_id, id);
      QCOMPARE(p_data.m_timeStamp, timeStamp);
      QVERIFY(!p_data.m_needScale);
      QPixmap image;
      QVERIFY(image.loadFromData(p_data.m_data, qPrintable(p_data.m_format)));
      receivedLogicalSize = p_data.m_logicalSize;
      auto item = QSharedPointer<vte::PreviewItem>::create();
      item->m_blockNumber = block.blockNumber();
      item->m_blockPos = block.position();
      item->m_startPos = block.position();
      item->m_endPos = block.position() + block.length() - 1;
      item->m_isBlockwise = true;
      item->m_name = QStringLiteral("adapter_preview_%1").arg(p_data.m_id);
      item->m_image = image;
      item->m_logicalSize = p_data.m_logicalSize;
      if (p_source == vte::PreviewData::Source::CodeBlock) {
        editor.getPreviewMgr()->updateCodeBlocks({item});
      } else {
        editor.getPreviewMgr()->updateMathBlocks({item});
      }
    };
    connect(&adapter, &MarkdownViewerAdapter::graphPreviewDataReady, &editor,
            [&](const MarkdownViewerAdapter::PreviewData &p_data) {
              publish(p_data, vte::PreviewData::Source::CodeBlock);
            });
    connect(&adapter, &MarkdownViewerAdapter::mathPreviewDataReady, &editor,
            [&](const MarkdownViewerAdapter::PreviewData &p_data) {
              publish(p_data, vte::PreviewData::Source::MathBlock);
            });
    const auto slot = source == vte::PreviewData::Source::CodeBlock ? "setGraphPreviewData"
                                                                    : "setMathPreviewData";
    auto invoke = [&](const QString &p_payload, int p_width, int p_height) {
      ++id;
      ++timeStamp;
      const bool invoked = QMetaObject::invokeMethod(
          &adapter, slot, Qt::DirectConnection, Q_ARG(quint64, id), Q_ARG(quint64, timeStamp),
          Q_ARG(QString, QStringLiteral("png")), Q_ARG(QString, p_payload), Q_ARG(bool, true),
          Q_ARG(bool, false), Q_ARG(int, p_width), Q_ARG(int, p_height));
      QCoreApplication::processEvents();
      return invoked;
    };

    QVERIFY(invoke(payloads[0], 0, 0));
    QCOMPARE(completions, 1);
    const auto legacyRect = blockRect();
    const auto legacyY = followingY();
    QVERIFY(legacyY > unpreviewedY);

    QVERIFY(invoke(payloads[1], 320, 96));
    QCOMPARE(completions, 2);
    QCOMPARE(blockRect(), legacyRect);
    QCOMPARE(followingY(), legacyY);
    const auto blockPreview = vte::BlockPreviewData::get(block);
    QVERIFY(blockPreview);
    const auto previews = blockPreview->getPreviewData();
    QCOMPARE(previews.size(), 1);
    const auto retainedImage =
        editor.findImageFromDocumentResourceMgr(previews.first()->getImageData()->m_imageName);
    QVERIFY(retainedImage);
    QCOMPARE(retainedImage->size(), QSize(800, 240));

    // Establish the high-resolution bitmap's intrinsic geometry independently
    // before checking that a partially specified size takes the same path.
    QVERIFY(invoke(payloads[1], 0, 0));
    QCOMPARE(completions, 3);
    const auto intrinsicRect = blockRect();
    const auto intrinsicY = followingY();
    QVERIFY(intrinsicY > legacyY);

    QVERIFY(invoke(payloads[1], 320, 0));
    QCOMPARE(completions, 4);
    QCOMPARE(receivedLogicalSize, QSize());
    QCOMPARE(blockRect(), intrinsicRect);
    QCOMPARE(followingY(), intrinsicY);
  }
}

void TestMarkdownViewerAdapterAnchor::mathPreviewZoomWhileRasterPending_data() {
  QTest::addColumn<bool>("blockwise");
  QTest::newRow("inline-math") << false;
  QTest::newRow("display-math") << true;
}

void TestMarkdownViewerAdapterAnchor::mathPreviewZoomWhileRasterPending() {
  QFETCH(bool, blockwise);
  ServiceLocator services;
  MarkdownViewerAdapter adapter(services);
  auto textConfig = QSharedPointer<vte::TextEditorConfig>::create();
  textConfig->m_scaleFactor = 1.25;
  auto config = QSharedPointer<vte::MarkdownEditorConfig>::create(textConfig);
  vte::VMarkdownEditor editor(config, QSharedPointer<vte::TextEditorParameters>::create());
  const int baseFontSize = editor.baseEditorFontPointSize();
  // Integral geometry at every reachable point-size ratio, including odd base fonts.
  const QSize baseSize(baseFontSize * 8, baseFontSize * 2);
  PreviewHelper helper(&editor);
  connect(editor.getHighlighter(), &vte::MarkdownHighlighter::mathBlocksUpdated, &helper,
          &PreviewHelper::mathBlocksUpdated);
  connect(&adapter, &MarkdownViewerAdapter::mathPreviewDataReady, &helper,
          &PreviewHelper::handleMathPreviewData);
  connect(&helper, &PreviewHelper::inplacePreviewMathBlockUpdated, editor.getPreviewMgr(),
          &vte::PreviewMgr::updateMathBlocks);
  connect(&helper, &PreviewHelper::potentialObsoletePreviewBlocksUpdated, editor.getPreviewMgr(),
          &vte::PreviewMgr::checkBlocksForObsoletePreview);
  QSignalSpy requests(&helper, &PreviewHelper::mathPreviewRequested);
  QVector<vte::md::MathBlock> mathBlocks;
  connect(editor.getHighlighter(), &vte::MarkdownHighlighter::mathBlocksUpdated, &editor,
          [&](const QVector<vte::md::MathBlock> &p_blocks) { mathBlocks = p_blocks; });
  editor.resize(1200, 700);
  editor.show();
  QVERIFY(QTest::qWaitForWindowExposed(&editor));
  const QString expression = QStringLiteral("x^2 + y^2 + z^2 = a^2 + b^2 + c^2");
  editor.setText(QStringLiteral("Before\n\n%1\n\nFollowing")
                     .arg(blockwise ? QStringLiteral("$$\n%1\n$$").arg(expression)
                                    : QStringLiteral("$%1$").arg(expression)));
  QTRY_COMPARE_WITH_TIMEOUT(requests.count(), 1, 60000);
  QCOMPARE(mathBlocks.size(), 1);
  const auto block = editor.document()->findBlockByNumber(mathBlocks.first().m_blockNumber);
  auto previewImage = [&]() -> const vte::PreviewImageData * {
    const auto data = vte::BlockPreviewData::get(block);
    if (data) {
      for (const auto preview : data->getPreviewData()) {
        if (preview->source() == vte::PreviewData::Source::MathBlock) {
          return preview->getImageData();
        }
      }
    }
    return nullptr;
  };
  auto previewSize = [&]() {
    const auto image = previewImage();
    return image ? image->m_imageSize : QSize();
  };
  auto rasterSize = [&]() {
    const auto data = previewImage();
    const auto image = data ? editor.findImageFromDocumentResourceMgr(data->m_imageName) : nullptr;
    return image ? image->size() : QSize();
  };
  auto blockHeight = [&]() {
    return editor.document()->documentLayout()->blockBoundingRect(block).height();
  };
  auto deliver = [&](const QList<QVariant> &p_request) {
    const qreal zoom = p_request[3].toReal();
    const QSize logicalSize(qRound(baseSize.width() * zoom), qRound(baseSize.height() * zoom));
    QPixmap raster(logicalSize * (textConfig->m_scaleFactor * 2));
    raster.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !raster.save(&buffer, "PNG")) {
      return false;
    }
    adapter.setMathPreviewData(p_request[0].toULongLong(), p_request[1].toULongLong(),
                               QStringLiteral("png"), QString::fromLatin1(bytes.toBase64()), true,
                               false, logicalSize.width(), logicalSize.height());
    return true;
  };
  QVERIFY(deliver(requests.last()));
  QTRY_COMPARE(previewSize(), baseSize);
  const auto baseRasterSize = baseSize * (textConfig->m_scaleFactor * 2);
  QCOMPARE(rasterSize(), baseRasterSize);

  // Zoom the existing preview before the request debounce or a new raster can
  // complete. This is the interval hidden by arithmetic/adapter-only tests.
  editor.zoom(baseFontSize);
  const auto heightBeforePreviewZoom = blockHeight();
  helper.editorZoomChanged();
  QCOMPARE(previewSize(), baseSize * 2);
  QCOMPARE(blockHeight(), heightBeforePreviewZoom + baseSize.height());
  QCOMPARE(rasterSize(), baseRasterSize);
  // A reset before the debounce fires must not be mistaken for "no change"
  // merely because the last raster request was also at 1x.
  editor.zoom(0);
  helper.editorZoomChanged();
  QCOMPARE(previewSize(), baseSize);
  editor.zoom(baseFontSize);
  helper.editorZoomChanged();
  QCOMPARE(previewSize(), baseSize * 2);
  QTRY_COMPARE(requests.count(), 2);
  const auto obsoleteRequest = requests.last();
  QCOMPARE(obsoleteRequest[3].toReal(), 2.0);
  QCOMPARE(previewSize(), baseSize * 2);

  // Another zoom while that raster is still in flight must use the original
  // logical geometry, and its late response must not undo the new zoom.
  editor.zoom(-baseFontSize / 2);
  helper.editorZoomChanged();
  const qreal smallZoom = helper.editorZoomFactor();
  const QSize smallSize(qRound(baseSize.width() * smallZoom),
                        qRound(baseSize.height() * smallZoom));
  QCOMPARE(previewSize(), smallSize);
  QVERIFY(deliver(obsoleteRequest));
  QCOMPARE(previewSize(), smallSize);
  QCOMPARE(rasterSize(), baseRasterSize);
  QTRY_COMPARE(requests.count(), 3);
  QCOMPARE(previewSize(), smallSize);
  QVERIFY(deliver(requests.last()));
  QTRY_COMPARE(rasterSize(), smallSize * (textConfig->m_scaleFactor * 2));
  QCOMPARE(previewSize(), smallSize);

  // Reset immediately, including after replacing the high-density raster.
  editor.zoom(0);
  helper.editorZoomChanged();
  QCOMPARE(previewSize(), baseSize);
  QTRY_COMPARE(requests.count(), 4);
  QVERIFY(deliver(requests.last()));
  QTRY_COMPARE(rasterSize(), baseRasterSize);
  QCOMPARE(previewSize(), baseSize);
}

void TestMarkdownViewerAdapterAnchor::numberingStatusTracksDocumentReplacement() {
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setSectionNumberOptions(true, QStringLiteral("1.1)"));
  const QJsonArray headings{QJsonObject{{"name", "Title"}, {"level", 1}, {"anchor", "title"}},
                            QJsonObject{{"name", "1) Detail"}, {"level", 3}, {"anchor", "detail"}}};
  adapter.setHeadings(headings, true);
  QVERIFY(adapter.getHeadingsHaveSectionNumber());
  QVERIFY(adapter.getHeadings()[1].m_isPlaceholder);
  QSignalSpy scrollSpy(&adapter, &MarkdownViewerAdapter::anchorScrollRequested);
  adapter.scrollToHeading(2);
  QCOMPARE(scrollSpy.count(), 1);
  QCOMPARE(scrollSpy.first().first().toString(), QStringLiteral("detail"));
  adapter.setHeadings(headings, false);
  QVERIFY(!adapter.getHeadingsHaveSectionNumber());
  adapter.setHeadings(headings, true);
  adapter.setHeadings(QJsonArray(), true);
  QVERIFY(!adapter.getHeadingsHaveSectionNumber());
  adapter.setHeadings(headings, true);
  adapter.reset();
  QVERIFY(!adapter.getHeadingsHaveSectionNumber());
  QVERIFY(adapter.getHeadings().isEmpty());
  QVERIFY(adapter.property("sectionNumberOptions").toJsonObject() ==
          (QJsonObject{{"enabled", true}, {"pattern", "1.1)"}}));
}

} // namespace tests

QTEST_MAIN(tests::TestMarkdownViewerAdapterAnchor)
#include "test_markdownvieweradapter_anchor.moc"
