// Native preview geometry, zoom and canvas regressions, plus reader heading state.

#include <QtTest>

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTextBlock>
#include <QTextDocument>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownhighlighter.h>
#include <vtextedit/previewdata.h>
#include <vtextedit/previewmgr.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/typstfragmentrenderer.h>
#include <vtextedit/typstruntime.h>
#include <vtextedit/vmarkdowneditor.h>
#include <vtextedit/vtextedit.h>

#include <controllers/markdownwebjobcontroller.h>
#include <core/servicelocator.h>
#include <widgets/editors/graphvizhelper.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/previewhelper.h>

using namespace vnotex;

namespace tests {

class TestMarkdownViewerAdapterAnchor : public QObject {
  Q_OBJECT

private slots:
  void previewResultsKeepHighResolutionAtLogicalSize();
  void mathPreviewsFollowDocumentOrder();
  void mathPreviewZoomWhileRasterPending_data();
  void mathPreviewZoomWhileRasterPending();
  void graphPreviewCanvas_data();
  void graphPreviewCanvas();
  void localGraphPreviewCanvas();
  void typstBlockUsesNativePreview();
  void graphPreviewCanvasChangesWithTheme();
  void numberingStatusTracksDocumentReplacement();
  void htmlAfterRepeatedUnreadyReset_data();
  void htmlAfterRepeatedUnreadyReset();
  void emptyHtmlReplacesVisibleDocument();
  void reentrantHtmlPublication_data();
  void reentrantHtmlPublication();
  void pageResetDuringHtmlSuppressesOldScroll();
  void deletionDuringHtml_data();
  void deletionDuringHtml();
  void deletionDuringScroll();
  void deletionDuringContentCancellation_data();
  void deletionDuringContentCancellation();
  void deletionDuringImageCancellation_data();
  void deletionDuringImageCancellation();
  void resetCancellationKeepsIncomingHtml();
  void taskTogglesRequireCurrentGeneration();
  void graphCallbacksFollowDocumentLifetime_data();
  void graphCallbacksFollowDocumentLifetime();
  void contentExtractionRequiresCurrentFinishedGeneration();
};

void TestMarkdownViewerAdapterAnchor::htmlAfterRepeatedUnreadyReset_data() {
  QTest::addColumn<QString>("html");
  QTest::newRow("nonempty") << QStringLiteral("<p>Incoming</p>");
  QTest::newRow("empty") << QStringLiteral("");
}

void TestMarkdownViewerAdapterAnchor::htmlAfterRepeatedUnreadyReset() {
  QFETCH(QString, html);
  MarkdownViewerAdapter adapter;
  QSignalSpy published(&adapter, &MarkdownViewerAdapter::htmlUpdated);
  QSignalSpy scrolled(&adapter, &MarkdownViewerAdapter::editLineNumberUpdated);
  adapter.setRenderedHtml(QStringLiteral("retired-a"), QStringLiteral("<p>A</p>"), 1);
  adapter.reset();
  adapter.setRenderedHtml(QStringLiteral("retired-b"), QStringLiteral("<p>B</p>"), 2);
  adapter.reset();
  adapter.reset();
  adapter.setRenderedHtml(QStringLiteral("superseded"), QStringLiteral("<p>Superseded</p>"), 3);
  adapter.setRenderedHtml(QStringLiteral("incoming"), html, 7);
  QCOMPARE(published.size(), 0);
  adapter.setReady(true);
  QCOMPARE(published.size(), 1);
  QCOMPARE(published.first().at(0).toString(), html);
  QCOMPARE(published.first().at(1).toString(), QStringLiteral("incoming"));
  QCOMPARE(scrolled.size(), 1);
  QCOMPARE(scrolled.first().at(0).toInt(), 7);
  adapter.setWorkFinished(QStringLiteral("retired-b"));
  QVERIFY(!adapter.isDocumentReady());
  adapter.setWorkFinished(QStringLiteral("incoming"));
  QVERIFY(adapter.isDocumentReady());
  QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
  QCOMPARE(published.size(), 1);
}

void TestMarkdownViewerAdapterAnchor::emptyHtmlReplacesVisibleDocument() {
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy published(&adapter, &MarkdownViewerAdapter::htmlUpdated);
  QSignalSpy completed(&adapter, &MarkdownViewerAdapter::contentReady);
  adapter.setRenderedHtml(QStringLiteral("visible"), QStringLiteral("<p>Visible</p>"));
  adapter.setWorkFinished(QStringLiteral("visible"));
  QVERIFY(adapter.isDocumentReady());
  adapter.setRenderedHtml(QStringLiteral("empty"), QStringLiteral(""));
  QCOMPARE(published.size(), 2);
  QVERIFY(published.last().at(0).toString().isEmpty());
  QCOMPARE(published.last().at(1).toString(), QStringLiteral("empty"));
  QVERIFY(!adapter.isDocumentReady());
  adapter.setWorkFinished(QStringLiteral("empty"));
  QVERIFY(adapter.isDocumentReady());
  adapter.saveContent();
  adapter.setSavedContent(QStringLiteral("empty"), {}, {}, QStringLiteral(""), {});
  QCOMPARE(completed.size(), 1);
  QVERIFY(completed.first().at(2).toString().isEmpty());
  QVERIFY(!completed.first().at(2).toString().isNull());
}

void TestMarkdownViewerAdapterAnchor::reentrantHtmlPublication_data() {
  QTest::addColumn<bool>("resetPage");
  QTest::addColumn<bool>("readyDuringSignal");
  QTest::addColumn<QString>("html");
  QTest::newRow("replace-nonempty") << false << true << QStringLiteral("<p>Incoming</p>");
  QTest::newRow("replace-empty") << false << true << QStringLiteral("");
  QTest::newRow("reset-ready-nonempty") << true << true << QStringLiteral("<p>Incoming</p>");
  QTest::newRow("reset-ready-empty") << true << true << QStringLiteral("");
  QTest::newRow("reset-unready-nonempty") << true << false << QStringLiteral("<p>Incoming</p>");
  QTest::newRow("reset-unready-empty") << true << false << QStringLiteral("");
}

void TestMarkdownViewerAdapterAnchor::reentrantHtmlPublication() {
  QFETCH(bool, resetPage);
  QFETCH(bool, readyDuringSignal);
  QFETCH(QString, html);
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  bool inOutgoingReceiver = false;
  bool nestedPublication = false;
  connect(&adapter, &MarkdownViewerAdapter::htmlUpdated, this,
          [&](const QString &, const QString &p_generation) {
            if (p_generation != QStringLiteral("outgoing")) {
              nestedPublication = inOutgoingReceiver;
              return;
            }
            inOutgoingReceiver = true;
            if (resetPage) {
              adapter.reset();
            }
            adapter.setRenderedHtml(QStringLiteral("intermediate"), QStringLiteral("<p>Old</p>"),
                                    13);
            adapter.setRenderedHtml(QStringLiteral("incoming"), html, 29);
            if (resetPage && readyDuringSignal) {
              adapter.setReady(true);
            }
            QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
            inOutgoingReceiver = false;
          });
  // A later receiver must see the old fragment before the new one, never vice versa.
  QSignalSpy published(&adapter, &MarkdownViewerAdapter::htmlUpdated);
  QSignalSpy scrolled(&adapter, &MarkdownViewerAdapter::editLineNumberUpdated);
  adapter.setRenderedHtml(QStringLiteral("outgoing"), QStringLiteral("<p>Outgoing</p>"), 5);
  QVERIFY(!nestedPublication);
  QCOMPARE(published.size(), 1);
  QCOMPARE(published.first().at(1).toString(), QStringLiteral("outgoing"));
  QCOMPARE(scrolled.size(), 0);
  if (resetPage && !readyDuringSignal) {
    adapter.setReady(true);
  }
  QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
  QVERIFY(!nestedPublication);
  QCOMPARE(published.size(), 2);
  QCOMPARE(published.last().at(0).toString(), html);
  QCOMPARE(published.last().at(1).toString(), QStringLiteral("incoming"));
  QCOMPARE(scrolled.size(), 1);
  QCOMPARE(scrolled.first().at(0).toInt(), 29);
  QCOMPARE(adapter.getTopLineNumber(), 29);
  adapter.setWorkFinished(QStringLiteral("outgoing"));
  QVERIFY(!adapter.isDocumentReady());
  adapter.setWorkFinished(QStringLiteral("incoming"));
  QVERIFY(adapter.isDocumentReady());
  QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
  QCOMPARE(published.size(), 2);
}

void TestMarkdownViewerAdapterAnchor::pageResetDuringHtmlSuppressesOldScroll() {
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy scrolled(&adapter, &MarkdownViewerAdapter::editLineNumberUpdated);
  connect(&adapter, &MarkdownViewerAdapter::htmlUpdated, this, [&]() {
    // Retire the page epoch without changing the native token.
    adapter.setReady(false);
    adapter.setReady(true);
  });
  adapter.setRenderedHtml(QStringLiteral("native"), QStringLiteral("<p>Native</p>"), 11);
  QCOMPARE(scrolled.size(), 0);
  QCOMPARE(adapter.getTopLineNumber(), -1);
}

void TestMarkdownViewerAdapterAnchor::deletionDuringHtml_data() {
  QTest::addColumn<bool>("initiallyReady");
  QTest::newRow("immediate") << true;
  QTest::newRow("readiness-action") << false;
}

void TestMarkdownViewerAdapterAnchor::deletionDuringHtml() {
  QFETCH(bool, initiallyReady);
  QObject owner;
  QPointer<MarkdownViewerAdapter> adapter = new MarkdownViewerAdapter(&owner);
  adapter->setReady(initiallyReady);
  QSignalSpy published(adapter, &MarkdownViewerAdapter::htmlUpdated);
  connect(adapter, &MarkdownViewerAdapter::htmlUpdated, this, [&]() {
    adapter->setRenderedHtml(QStringLiteral("queued"), QStringLiteral("<p>Queued</p>"));
    delete adapter.data();
  });
  adapter->setRenderedHtml(QStringLiteral("outgoing"), QStringLiteral("<p>Outgoing</p>"), 9);
  if (!initiallyReady) {
    adapter->setReady(true);
  }
  QVERIFY(adapter.isNull());
  QCOMPARE(published.size(), 1);
}

void TestMarkdownViewerAdapterAnchor::deletionDuringScroll() {
  QObject owner;
  QPointer<MarkdownViewerAdapter> adapter = new MarkdownViewerAdapter(&owner);
  adapter->setReady(true);
  connect(adapter, &MarkdownViewerAdapter::editLineNumberUpdated, this,
          [&]() { delete adapter.data(); });
  adapter->setRenderedHtml(QStringLiteral("outgoing"), QStringLiteral("<p>Outgoing</p>"), 9);
  QVERIFY(adapter.isNull());
}

void TestMarkdownViewerAdapterAnchor::deletionDuringContentCancellation_data() {
  QTest::addColumn<bool>("resetPage");
  QTest::newRow("replacement") << false;
  QTest::newRow("reset") << true;
}

void TestMarkdownViewerAdapterAnchor::deletionDuringContentCancellation() {
  QFETCH(bool, resetPage);
  QObject owner;
  QPointer<MarkdownViewerAdapter> adapter = new MarkdownViewerAdapter(&owner);
  adapter->setReady(true);
  adapter->setRenderedHtml(QStringLiteral("outgoing"), QStringLiteral("<p>Outgoing</p>"));
  adapter->setWorkFinished(QStringLiteral("outgoing"));
  adapter->saveContent();
  connect(adapter, &MarkdownViewerAdapter::contentReady, this, [&]() { delete adapter.data(); });
  if (resetPage) {
    adapter->reset();
  } else {
    adapter->setRenderedHtml(QStringLiteral("incoming"), QStringLiteral("<p>Incoming</p>"));
  }
  QVERIFY(adapter.isNull());
}

void TestMarkdownViewerAdapterAnchor::deletionDuringImageCancellation_data() {
  QTest::addColumn<int>("operation");
  QTest::newRow("cancel") << 0;
  QTest::newRow("reset") << 1;
  QTest::newRow("replace-image") << 2;
}

void TestMarkdownViewerAdapterAnchor::deletionDuringImageCancellation() {
  QFETCH(int, operation);
  QObject owner;
  QPointer<MarkdownViewerAdapter> adapter = new MarkdownViewerAdapter(&owner);
  adapter->setReady(true);
  QVERIFY(adapter->requestImageView(QStringLiteral("old.png")) != 0);
  connect(adapter, &MarkdownViewerAdapter::imageViewCancelRequested, this,
          [&]() { delete adapter.data(); });
  if (operation == 0) {
    adapter->cancelImageView();
  } else if (operation == 1) {
    adapter->reset();
  } else {
    QCOMPARE(adapter->requestImageView(QStringLiteral("new.png")), quint64(0));
  }
  QVERIFY(adapter.isNull());
}

void TestMarkdownViewerAdapterAnchor::resetCancellationKeepsIncomingHtml() {
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setRenderedHtml(QStringLiteral("outgoing"), QStringLiteral("<p>Outgoing</p>"));
  adapter.setWorkFinished(QStringLiteral("outgoing"));
  adapter.saveContent();
  QSignalSpy published(&adapter, &MarkdownViewerAdapter::htmlUpdated);
  connect(&adapter, &MarkdownViewerAdapter::contentReady, this, [&]() {
    adapter.setRenderedHtml(QStringLiteral("incoming"), QStringLiteral("<p>Incoming</p>"), 19);
    adapter.setReady(true);
  });
  adapter.reset();
  QCOMPARE(published.size(), 1);
  QCOMPARE(published.first().at(0).toString(), QStringLiteral("<p>Incoming</p>"));
  QCOMPARE(published.first().at(1).toString(), QStringLiteral("incoming"));
  QCOMPARE(adapter.getTopLineNumber(), 19);
  adapter.setWorkFinished(QStringLiteral("incoming"));
  QVERIFY(adapter.isDocumentReady());
}

void TestMarkdownViewerAdapterAnchor::taskTogglesRequireCurrentGeneration() {
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  QSignalSpy requested(&adapter, &MarkdownViewerAdapter::taskListItemToggleRequested);
  QSignalSpy rejected(&adapter, &MarkdownViewerAdapter::taskListToggleRejected);
  adapter.setRenderedHtml(QStringLiteral("retired"), QStringLiteral("<p>Retired</p>"));
  adapter.setRenderedHtml(QStringLiteral("current"), QStringLiteral("<p>Current</p>"));
  adapter.toggleTaskListItem(QStringLiteral("retired"), 4, true);
  adapter.toggleTaskListItem(QString(), 4, true);
  adapter.toggleTaskListItem(QStringLiteral("current"), -1, true);
  QCOMPARE(requested.size(), 0);
  QCOMPARE(rejected.size(), 3);
  QCOMPARE(rejected.first().at(0).toString(), QStringLiteral("retired"));
  QCOMPARE(rejected.first().at(1).toInt(), 4);
  adapter.toggleTaskListItem(QStringLiteral("current"), 4, true);
  QCOMPARE(requested.size(), 1);
  QCOMPARE(requested.first().at(0).toString(), QStringLiteral("current"));
  QCOMPARE(requested.first().at(1).toInt(), 4);
  QCOMPARE(requested.first().at(2).toBool(), true);
  adapter.reset();
  adapter.toggleTaskListItem(QStringLiteral("current"), 4, false);
  QCOMPARE(requested.size(), 1);
  QCOMPARE(rejected.size(), 4);
  adapter.setRenderedHtml(QStringLiteral("incoming"), QStringLiteral("<p>Incoming</p>"));
  adapter.setReady(true);
  adapter.toggleTaskListItem(QStringLiteral("incoming"), 7, false);
  QCOMPARE(requested.size(), 2);
  QCOMPARE(requested.last().at(0).toString(), QStringLiteral("incoming"));
  QCOMPARE(requested.last().at(1).toInt(), 7);
  QCOMPARE(requested.last().at(2).toBool(), false);
}

void TestMarkdownViewerAdapterAnchor::graphCallbacksFollowDocumentLifetime_data() {
  QTest::addColumn<QString>("retirement");
  QTest::newRow("native-token") << QStringLiteral("token");
  QTest::newRow("reset-epoch") << QStringLiteral("reset");
  QTest::newRow("deleted-adapter") << QStringLiteral("delete");
}

void TestMarkdownViewerAdapterAnchor::graphCallbacksFollowDocumentLifetime() {
  QFETCH(QString, retirement);
  const auto dot = QStandardPaths::findExecutable(QStringLiteral("dot"));
  if (dot.isEmpty()) {
    QSKIP("Local Graphviz requires dot on PATH");
  }
  auto &helper = GraphvizHelper::getInst();
  const auto restoreHelper = qScopeGuard([&]() { helper.update(QString()); });
  helper.update(dot);
  QObject owner;
  QPointer<MarkdownViewerAdapter> adapter = new MarkdownViewerAdapter(&owner);
  adapter->setReady(true);
  adapter->setRenderedHtml(QStringLiteral("outgoing"), QStringLiteral("<p>Outgoing</p>"));
  QSignalSpy outgoing(adapter, &MarkdownViewerAdapter::graphRenderDataReady);
  adapter->renderGraph(1, 3, QStringLiteral("svg"), QStringLiteral("graphviz"),
                       QStringLiteral("digraph Retired { A -> B; }"), 0);
  QCOMPARE(outgoing.size(), 0);
  if (retirement == QStringLiteral("delete")) {
    delete adapter.data();
    adapter = new MarkdownViewerAdapter(&owner);
  } else if (retirement == QStringLiteral("reset")) {
    adapter->reset();
  }
  // Keep the token equal for the reset row so only the page epoch can retire it.
  adapter->setRenderedHtml(retirement == QStringLiteral("reset") ? QStringLiteral("outgoing")
                                                                 : QStringLiteral("incoming"),
                           QStringLiteral("<p>Incoming</p>"));
  adapter->setReady(true);
  QSignalSpy incoming(adapter, &MarkdownViewerAdapter::graphRenderDataReady);
  adapter->renderGraph(2, 5, QStringLiteral("svg"), QStringLiteral("graphviz"),
                       QStringLiteral("digraph Current { B -> C; }"), 0);
  // The helper is FIFO: this real success proves the retired callback has drained.
  QTRY_COMPARE_WITH_TIMEOUT(incoming.size(), 1, 10000);
  QCOMPARE(incoming.first().at(0).toULongLong(), quint64(2));
  QCOMPARE(incoming.first().at(1).toULongLong(), quint64(5));
  QCOMPARE(incoming.first().at(2).toString(), QStringLiteral("svg"));
  QVERIFY(incoming.first().at(3).toString().contains(QStringLiteral("<svg")));
  QVERIFY(incoming.first().at(4).toBool());
  QCOMPARE(outgoing.size(), retirement == QStringLiteral("delete") ? 0 : 1);
}

void TestMarkdownViewerAdapterAnchor::contentExtractionRequiresCurrentFinishedGeneration() {
  MarkdownViewerAdapter adapter;
  QSignalSpy requested(&adapter, &MarkdownViewerAdapter::contentRequested);
  QSignalSpy completed(&adapter, &MarkdownViewerAdapter::contentReady);
  adapter.setRenderedHtml(QStringLiteral("retired"), QStringLiteral("<p>Retired</p>"));
  adapter.reset();
  adapter.setRenderedHtml(QStringLiteral("empty"), QStringLiteral(""));
  adapter.setReady(true);
  QVERIFY(!adapter.isDocumentReady());
  adapter.setWorkFinished(QStringLiteral("retired"));
  QVERIFY(!adapter.isDocumentReady());
  adapter.saveContent();
  QCOMPARE(requested.size(), 0);
  QCOMPARE(completed.size(), 1);
  QVERIFY(completed.first().at(2).toString().isNull());

  adapter.setWorkFinished(QStringLiteral("empty"));
  QVERIFY(adapter.isDocumentReady());
  adapter.saveContent();
  QCOMPARE(requested.size(), 1);
  adapter.setRenderedHtml(QStringLiteral("current"), QStringLiteral("<p>Current</p>"));
  QCOMPARE(completed.size(), 2);
  QVERIFY(completed.last().at(2).toString().isNull());
  adapter.setSavedContent(QStringLiteral("empty"), {}, {}, QStringLiteral("retired result"), {});
  QCOMPARE(completed.size(), 2);
  adapter.setWorkFinished(QStringLiteral("empty"));
  QVERIFY(!adapter.isDocumentReady());

  adapter.setWorkFinished(QStringLiteral("current"));
  QVERIFY(adapter.isDocumentReady());
  adapter.saveContent();
  QCOMPARE(requested.size(), 2);
  adapter.setSavedContent(QStringLiteral("current"), {}, {}, QStringLiteral("<p>Current</p>"), {});
  QCOMPARE(completed.size(), 3);
  QVERIFY(!completed.last().at(2).toString().isNull());
}

void TestMarkdownViewerAdapterAnchor::previewResultsKeepHighResolutionAtLogicalSize() {
  for (auto source : {vte::PreviewData::Source::CodeBlock, vte::PreviewData::Source::MathBlock}) {
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
    editor.getPreviewMgr()->setPreviewEnabled(source, true);

    const auto block = editor.document()->findBlockByNumber(1);
    auto blockRect = [&]() {
      return editor.document()->documentLayout()->blockBoundingRect(block);
    };
    auto followingY = [&]() {
      return editor.document()->documentLayout()->blockBoundingRect(block.next()).y();
    };
    const auto unpreviewedY = followingY();
    int sequence = 0;
    auto publish = [&](const QSize &p_rasterSize, const QSize &p_logicalSize) {
      QPixmap image(p_rasterSize);
      image.fill(Qt::red);
      auto item = QSharedPointer<vte::PreviewItem>::create();
      item->m_blockNumber = block.blockNumber();
      item->m_blockPos = block.position();
      item->m_startPos = block.position();
      item->m_endPos = block.position() + block.length() - 1;
      item->m_isBlockwise = true;
      item->m_name = QStringLiteral("geometry_preview_%1").arg(++sequence);
      item->m_image = image;
      item->m_logicalSize = p_logicalSize;
      if (source == vte::PreviewData::Source::CodeBlock) {
        editor.getPreviewMgr()->updateCodeBlocks({item});
      } else {
        editor.getPreviewMgr()->updateMathBlocks({item});
      }
      QCoreApplication::processEvents();
    };

    publish(QSize(400, 120), QSize());
    const auto intrinsicRect = blockRect();
    const auto intrinsicY = followingY();
    QVERIFY(intrinsicY > unpreviewedY);

    publish(QSize(800, 240), QSize(320, 96));
    QCOMPARE(blockRect(), intrinsicRect);
    QCOMPARE(followingY(), intrinsicY);
    const auto blockPreview = vte::BlockPreviewData::get(block);
    QVERIFY(blockPreview);
    const auto previews = blockPreview->getPreviewData();
    QCOMPARE(previews.size(), 1);
    const auto retainedImage =
        editor.findImageFromDocumentResourceMgr(previews.first()->getImageData()->m_imageName);
    QVERIFY(retainedImage);
    QCOMPARE(retainedImage->size(), QSize(800, 240));

    publish(QSize(800, 240), QSize());
    QVERIFY(followingY() > intrinsicY);
  }
}

void TestMarkdownViewerAdapterAnchor::mathPreviewsFollowDocumentOrder() {
  auto textConfig = QSharedPointer<vte::TextEditorConfig>::create();
  auto config = QSharedPointer<vte::MarkdownEditorConfig>::create(textConfig);
  vte::VMarkdownEditor editor(config, QSharedPointer<vte::TextEditorParameters>::create());
  PreviewHelper helper(&editor);
  connect(editor.getHighlighter(), &vte::MarkdownHighlighter::mathBlocksUpdated, &helper,
          &PreviewHelper::mathBlocksUpdated);
  QSignalSpy requests(&helper, &PreviewHelper::mathPreviewRequested);
  auto requestedTexts = [&]() {
    QStringList texts;
    for (const auto &request : requests) {
      texts.append(request[2].toString());
    }
    return texts;
  };

  editor.setText(QStringLiteral(R"MD($$
\newcommand{\BigO}[1]{\mathcal{O}(#1)}
$$

Inline: $\BigO{n^2}$, then $a^2$ and $$b^2$$.

$$
c^2
$$

Finally $d^2$.
)MD"));
  const QStringList firstOrder{QStringLiteral("$$\n\\newcommand{\\BigO}[1]{\\mathcal{O}(#1)}\n$$"),
                               QStringLiteral("$\\BigO{n^2}$"),
                               QStringLiteral("$a^2$"),
                               QStringLiteral("$$b^2$$"),
                               QStringLiteral("$$\nc^2\n$$"),
                               QStringLiteral("$d^2$")};
  QTRY_COMPARE_WITH_TIMEOUT(requests.count(), firstOrder.size(), 60000);
  QCOMPARE(requestedTexts(), firstOrder);

  // Fresh sources keep this generation observable even if the first one was cached.
  requests.clear();
  editor.setText(QStringLiteral(R"MD(First $d^3$.

$$
c^3
$$

Then $a^3$ and $$b^3$$.

$$
\newcommand{\BigTheta}[1]{\Theta(#1)}
$$

Finally $\BigTheta{n^3}$.
)MD"));
  const QStringList secondOrder{QStringLiteral("$d^3$"),
                                QStringLiteral("$$\nc^3\n$$"),
                                QStringLiteral("$a^3$"),
                                QStringLiteral("$$b^3$$"),
                                QStringLiteral("$$\n\\newcommand{\\BigTheta}[1]{\\Theta(#1)}\n$$"),
                                QStringLiteral("$\\BigTheta{n^3}$")};
  QTRY_COMPARE_WITH_TIMEOUT(requests.count(), secondOrder.size(), 60000);
  QCOMPARE(requestedTexts(), secondOrder);
}

void TestMarkdownViewerAdapterAnchor::mathPreviewZoomWhileRasterPending_data() {
  QTest::addColumn<bool>("blockwise");
  QTest::newRow("inline-math") << false;
  QTest::newRow("display-math") << true;
}

void TestMarkdownViewerAdapterAnchor::mathPreviewZoomWhileRasterPending() {
  QFETCH(bool, blockwise);
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
    helper.handleMathPreviewData(MarkdownWebJobController::PreviewData(
        p_request[0].toULongLong(), p_request[1].toULongLong(), QStringLiteral("png"), bytes, false,
        logicalSize));
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

namespace {

const QColor c_editorBackground(QStringLiteral("#202830"));
const QColor c_graphCanvas(QStringLiteral("#b0bec5"));

QSharedPointer<vte::MarkdownEditorConfig> graphCanvasConfig(const QColor &p_canvas,
                                                            qreal p_scaleFactor = 1) {
  const QJsonObject json{
      {"metadata", QJsonObject{{"type", "vtextedit"}}},
      {"editor-styles",
       QJsonObject{{"Text", QJsonObject{{"font-family", "Arial"},
                                        {"font-size", 12},
                                        {"text-color", "#eeeeee"},
                                        {"background-color", c_editorBackground.name()}}}}},
      {"markdown-editor-styles",
       p_canvas.isValid()
           ? QJsonObject{{"Preview", QJsonObject{{"background-color", p_canvas.name()}}}}
           : QJsonObject()}};
  auto textConfig = QSharedPointer<vte::TextEditorConfig>::create();
  textConfig->m_theme = vte::Theme::createThemeFromContent(
      QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
  textConfig->m_scaleFactor = p_scaleFactor;
  auto config = QSharedPointer<vte::MarkdownEditorConfig>::create(textConfig);
  config->m_inplacePreviewSources = vte::MarkdownEditorConfig::CodeBlock;
  config->m_autoFoldPreviewedBlocksEnabled = false;
  return config;
}

struct GraphCanvasFixture {
  explicit GraphCanvasFixture(qreal p_scaleFactor = 1, bool p_protected = false,
                              bool p_webGraphviz = true, const QColor &p_canvas = c_graphCanvas)
      : m_editor(graphCanvasConfig(p_canvas, p_scaleFactor),
                 QSharedPointer<vte::TextEditorParameters>::create()),
        m_helper(&m_editor), m_requests(&m_helper, &PreviewHelper::graphPreviewRequested) {
    m_helper.setProtectedView(p_protected);
    m_helper.setWebGraphvizEnabled(p_webGraphviz);
    m_helper.setInplacePreviewCodeBlocksEnabled(true);
    QObject::connect(m_editor.getHighlighter(), &vte::MarkdownHighlighter::codeBlocksUpdated,
                     &m_helper, &PreviewHelper::codeBlocksUpdated);
    QObject::connect(&m_helper, &PreviewHelper::inplacePreviewCodeBlockUpdated,
                     m_editor.getPreviewMgr(), &vte::PreviewMgr::updateCodeBlocks);
    QObject::connect(&m_helper, &PreviewHelper::potentialObsoletePreviewBlocksUpdated,
                     m_editor.getPreviewMgr(), &vte::PreviewMgr::checkBlocksForObsoletePreview);
    QObject::connect(&m_helper, &PreviewHelper::inplacePreviewCodeBlockUpdated, &m_editor,
                     [this](const QVector<QSharedPointer<vte::PreviewItem>> &p_items) {
                       if (!p_items.isEmpty()) {
                         ++m_publications;
                       }
                     });
    m_editor.resize(640, 480);
    m_editor.show();
  }

  void setSource(const QString &p_language, const QString &p_source) {
    m_editor.setText(
        QStringLiteral("Before\n\n```%1\n%2\n```\n\nFollowing\n").arg(p_language, p_source));
    auto cursor = m_editor.getTextEdit()->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_editor.getTextEdit()->setTextCursor(cursor);
  }

  bool deliver(const QList<QVariant> &p_request, bool p_svg = false,
               const QColor &p_ink = Qt::red) {
    QString payload;
    if (p_svg) {
      payload = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='80' height='48' "
                               "viewBox='0 0 80 48'><rect x='32' y='16' width='16' height='16' "
                               "fill='%1'/></svg>")
                    .arg(p_ink.name());
    } else {
      QImage image(80, 48, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      QPainter painter(&image);
      painter.fillRect(QRect(32, 16, 16, 16), p_ink);
      painter.end();
      QByteArray bytes;
      QBuffer buffer(&bytes);
      if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return false;
      }
      payload = QString::fromLatin1(bytes.toBase64());
    }
    m_helper.handleGraphPreviewData(MarkdownWebJobController::PreviewData(
        p_request[0].toULongLong(), p_request[1].toULongLong(),
        p_svg ? QStringLiteral("svg") : QStringLiteral("png"),
        p_svg ? payload.toUtf8() : QByteArray::fromBase64(payload.toLatin1()), p_svg,
        p_svg ? QSize() : QSize(80, 48)));
    return true;
  }

  QImage render() {
    auto layout = m_editor.document()->documentLayout();
    const auto size = layout->documentSize();
    QImage image(qMax(640, qCeil(size.width())), qCeil(size.height()) + 16,
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(c_editorBackground);
    QPainter painter(&image);
    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = image.rect();
    context.palette = m_editor.getTextEdit()->palette();
    layout->draw(&painter, context);
    return image;
  }

  vte::VMarkdownEditor m_editor;
  PreviewHelper m_helper;
  QSignalSpy m_requests;
  int m_publications = 0;
};

QRect graphInkBounds(const QImage &p_image) {
  QRect bounds;
  for (int y = 0; y < p_image.height(); ++y) {
    for (int x = 0; x < p_image.width(); ++x) {
      if (p_image.pixelColor(x, y) == QColor(Qt::red)) {
        bounds |= QRect(x, y, 1, 1);
      }
    }
  }
  return bounds;
}

void verifyGraphCanvas(const QImage &p_image, const QColor &p_canvas) {
  const auto ink = graphInkBounds(p_image);
  QVERIFY2(!ink.isEmpty(), "The real document painter must draw the graph's red marker");
  QCOMPARE(p_image.pixelColor(ink.center()), QColor(Qt::red));
  // All fixtures have transparent padding around their opaque red marker.
  const QPoint interior(ink.left() - 6, ink.center().y());
  QVERIFY(p_image.rect().contains(interior));
  QCOMPARE(p_image.pixelColor(interior), p_canvas);
  // Same scanline, well beyond the preview: never a full-width editor band.
  QCOMPARE(p_image.pixelColor(p_image.width() - 8, ink.center().y()), c_editorBackground);
}

} // namespace

void TestMarkdownViewerAdapterAnchor::graphPreviewCanvas_data() {
  QTest::addColumn<QString>("language");
  QTest::addColumn<QString>("source");
  QTest::addColumn<bool>("protectedView");
  QTest::addColumn<bool>("svg");
  QTest::addColumn<bool>("canvas");
  QTest::addColumn<QColor>("previewColor");
  QTest::newRow("mermaid") << QStringLiteral("mermaid") << QStringLiteral("graph TD; A-->B")
                           << false << false << true << c_graphCanvas;
  QTest::newRow("flowchart") << QStringLiteral("flowchart") << QStringLiteral("st=>start: Start")
                             << false << false << true << c_graphCanvas;
  QTest::newRow("wavedrom") << QStringLiteral("wavedrom")
                            << QStringLiteral("{\"signal\":[{\"name\":\"clk\",\"wave\":\"p...\"}]}")
                            << false << false << true << c_graphCanvas;
  QTest::newRow("plantuml") << QStringLiteral("plantuml")
                            << QStringLiteral("@startuml\nAlice -> Bob\n@enduml") << false << false
                            << true << c_graphCanvas;
  QTest::newRow("graphviz") << QStringLiteral("graphviz") << QStringLiteral("digraph G { A -> B; }")
                            << false << false << true << c_graphCanvas;
  QTest::newRow("mathjax") << QStringLiteral("mathjax") << QStringLiteral("x^2") << false << false
                           << false << c_graphCanvas;
  QTest::newRow("protected-plantuml")
      << QStringLiteral("plantuml") << QStringLiteral("@startuml\nAlice -> Bob\n@enduml") << true
      << false << false << c_graphCanvas;
  QTest::newRow("graphviz-svg-scaled")
      << QStringLiteral("graphviz") << QStringLiteral("digraph G { A -> B; }") << false << true
      << true << c_graphCanvas;
  QTest::newRow("mermaid-no-canvas-style")
      << QStringLiteral("mermaid") << QStringLiteral("graph TD; A-->B") << false << false << false
      << QColor();
  QTest::newRow("graphviz-svg-no-canvas-style")
      << QStringLiteral("graphviz") << QStringLiteral("digraph G { A -> B; }") << false << true
      << false << QColor();
}

void TestMarkdownViewerAdapterAnchor::graphPreviewCanvas() {
  QFETCH(QString, language);
  QFETCH(QString, source);
  QFETCH(bool, protectedView);
  QFETCH(bool, svg);
  QFETCH(bool, canvas);
  QFETCH(QColor, previewColor);
  GraphCanvasFixture fixture(svg ? 1.25 : 1, protectedView, true, previewColor);
  QVERIFY(QTest::qWaitForWindowExposed(&fixture.m_editor));
  fixture.setSource(language, source);
  QTRY_COMPARE_WITH_TIMEOUT(fixture.m_requests.count(), 1, 60000);
  QVERIFY(fixture.deliver(fixture.m_requests.last(), svg));
  QTRY_COMPARE(fixture.m_publications, 1);
  const auto image = fixture.render();
  const auto artifactDir = qEnvironmentVariable("VNOTE_TEST_ARTIFACT_DIR");
  if (!artifactDir.isEmpty()) {
    QVERIFY(image.save(QDir(artifactDir)
                           .filePath(QStringLiteral("graph-canvas-%1.png")
                                         .arg(QString::fromLatin1(QTest::currentDataTag())))));
  }
  verifyGraphCanvas(image, canvas ? c_graphCanvas : c_editorBackground);
}

void TestMarkdownViewerAdapterAnchor::typstBlockUsesNativePreview() {
  vte::TypstRuntime runtime;
  QVERIFY(runtime.isAvailable());
  GraphCanvasFixture fixture;
  fixture.m_helper.setInplacePreviewMathBlocksEnabled(false);
  QVERIFY(QTest::qWaitForWindowExposed(&fixture.m_editor));
  vte::TypstFragmentRenderer renderer(&runtime);
  quint64 requestId = 0;
  quint64 requestStamp = 0;
  connect(&fixture.m_helper, &PreviewHelper::graphPreviewRequested, &renderer,
          [&](quint64 p_id, TimeStamp p_stamp, const QString &p_language, const QString &p_source,
              qreal) {
            QCOMPARE(p_language, QStringLiteral("typst"));
            requestId = p_id;
            requestStamp = p_stamp;
            QVERIFY(renderer.renderMarkupToSvg(p_source, QColor(36, 122, 200)));
          });
  connect(&renderer, &vte::TypstFragmentRenderer::finished, &fixture.m_editor,
          [&](const vte::TypstFragmentRenderer::Result &p_result) {
            QVERIFY2(p_result.isSuccess(), qPrintable(p_result.m_error));
            fixture.m_helper.handleGraphPreviewData(MarkdownWebJobController::PreviewData(
                requestId, requestStamp, QStringLiteral("svg"), p_result.m_svg, true));
          });
  fixture.setSource(
      QStringLiteral("typst"),
      QStringLiteral("#rect(width: 80pt, height: 30pt, fill: rgb(\"#247ac8\"), stroke: none)"));
  const auto authored = fixture.m_editor.getText();
  QTRY_COMPARE_WITH_TIMEOUT(fixture.m_publications, 1, 60000);
  QCOMPARE(fixture.m_editor.getText(), authored);
  const auto image = fixture.render();
  int blue = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (qAbs(color.red() - 36) <= 3 && qAbs(color.green() - 122) <= 3 &&
          qAbs(color.blue() - 200) <= 3 && color.alpha() > 240)
        ++blue;
    }
  }
  QVERIFY(blue > 100);
}

void TestMarkdownViewerAdapterAnchor::localGraphPreviewCanvas() {
  if (QStandardPaths::findExecutable(QStringLiteral("dot")).isEmpty()) {
    QSKIP("Local Graphviz requires dot on PATH");
  }
  GraphvizHelper::getInst().update(QString());
  GraphCanvasFixture fixture(1, false, false);
  QVERIFY(QTest::qWaitForWindowExposed(&fixture.m_editor));
  fixture.setSource(
      QStringLiteral("graphviz"),
      QStringLiteral("digraph G { bgcolor=\"transparent\"; margin=0; pad=0.5; "
                     "a [shape=box, style=filled, fillcolor=red, color=red, label=\"\"]; }"));
  QTRY_COMPARE_WITH_TIMEOUT(fixture.m_publications, 1, 60000);
  QCOMPARE(fixture.m_requests.count(), 0);
  verifyGraphCanvas(fixture.render(), c_graphCanvas);
}

void TestMarkdownViewerAdapterAnchor::graphPreviewCanvasChangesWithTheme() {
  GraphCanvasFixture fixture;
  QVERIFY(QTest::qWaitForWindowExposed(&fixture.m_editor));
  fixture.setSource(QStringLiteral("mermaid"), QStringLiteral("graph TD; A-->B"));
  const auto source = fixture.m_editor.getText();
  QTRY_COMPARE_WITH_TIMEOUT(fixture.m_requests.count(), 1, 60000);
  const auto oldRequest = fixture.m_requests.last();
  QVERIFY(fixture.deliver(oldRequest));
  QTRY_COMPARE(fixture.m_publications, 1);
  verifyGraphCanvas(fixture.render(), c_graphCanvas);

  fixture.m_helper.invalidatePreviews();
  const QColor newCanvas(QStringLiteral("#f2efe7"));
  fixture.m_editor.setConfig(graphCanvasConfig(newCanvas));
  fixture.m_editor.getHighlighter()->updateHighlight();
  QTRY_COMPARE_WITH_TIMEOUT(fixture.m_requests.count(), 2, 60000);
  QVERIFY(fixture.deliver(fixture.m_requests.last()));
  QTRY_COMPARE(fixture.m_publications, 2);
  const auto image = fixture.render();
  verifyGraphCanvas(image, newCanvas);
  QCOMPARE(fixture.m_editor.getText(), source);

  // Different ink makes an incorrectly accepted retired result observable even
  // if it picks up the new theme's canvas while being decoded.
  QVERIFY(fixture.deliver(oldRequest, false, Qt::blue));
  QTest::qWait(100);
  QCOMPARE(fixture.render(), image);
  QCOMPARE(fixture.m_editor.getText(), source);
}

void TestMarkdownViewerAdapterAnchor::numberingStatusTracksDocumentReplacement() {
  MarkdownViewerAdapter adapter;
  adapter.setReady(true);
  adapter.setSectionNumberOptions(true, QStringLiteral("1.1)"), true);
  QSignalSpy optionsSpy(&adapter, &MarkdownViewerAdapter::sectionNumberOptionsChanged);
  adapter.setSectionNumberOptions(true, QStringLiteral("1.1)"), false);
  QCOMPARE(optionsSpy.count(), 1);
  const QJsonObject options{
      {"enabled", true}, {"pattern", "1.1)"}, {"detectHeading1ForSectionNumber", false}};
  QCOMPARE(adapter.property("sectionNumberOptions").toJsonObject(), options);
  adapter.setSectionNumberOptions(true, QStringLiteral("1.1)"), false);
  QCOMPARE(optionsSpy.count(), 1);
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
  QCOMPARE(adapter.property("sectionNumberOptions").toJsonObject(), options);
  adapter.setSectionNumberOptions(true, QStringLiteral("1.1)"), false);
  QCOMPARE(optionsSpy.count(), 1);
}

} // namespace tests

QTEST_MAIN(tests::TestMarkdownViewerAdapterAnchor)
#include "test_markdownvieweradapter_anchor.moc"
