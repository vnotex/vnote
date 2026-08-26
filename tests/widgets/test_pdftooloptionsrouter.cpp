// PdfToolOptionsRouter: the normalize -> persist -> push-to-adapter routing
// that every per-tool settings change goes through.
//
// Gate 6 (startup hydration) is the one that fails SILENTLY in production:
// menu picks persist to JSON, then a newly opened PDF window comes up on the
// adapter/JS defaults and the settings appear forgotten. The reload latch
// cannot compensate — it republishes only what the adapter already holds.
//
// Gate 7 (context-menu route) proves one pick updates the adapter, persists the
// same normalized value, AND issues captureSelection with it.

#include <QtTest>

#include <QHash>
#include <QJsonObject>
#include <QSignalSpy>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mainconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/services/commenttypes.h>
#include <core/services/configcoreservice.h>
#include <widgets/editors/pdfvieweradapter.h>
#include <widgets/pdftooloptionsrouter.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

namespace {

QHash<QString, QJsonObject> byTool(const QSignalSpy &p_spy) {
  QHash<QString, QJsonObject> map;
  for (const auto &call : p_spy) {
    map.insert(call.at(0).toString(), call.at(1).toJsonObject());
  }
  return map;
}

} // namespace

class TestPdfToolOptionsRouter : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void persistedOptionsAreHydratedOntoAFreshAdapter();

  void hydratedOptionsSurviveTheFirstReadyTransition();

  void theContextMenuRoutePersistsPushesAndCaptures();

  void theRouterNormalizesExactlyLikeTheConfig();

private:
  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
};

void TestPdfToolOptionsRouter::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_configService = new ConfigCoreService(m_context);
  m_configMgr = new ConfigMgr2(m_configService);
}

void TestPdfToolOptionsRouter::cleanupTestCase() {
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configService;
  m_configService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestPdfToolOptionsRouter::persistedOptionsAreHydratedOntoAFreshAdapter() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  // Non-default options for ALL THREE tools, so a hydration that only handles
  // the active tool (or drops one) is visible.
  PdfViewerConfig::ToolOptions highlight;
  highlight.m_color = QStringLiteral("green");
  pdfConfig.setToolOptions(PdfToolOptions::highlightTool(), highlight);

  PdfViewerConfig::ToolOptions ink;
  ink.m_color = QStringLiteral("blue");
  ink.m_width = 3.0;
  pdfConfig.setToolOptions(PdfToolOptions::inkTool(), ink);

  PdfViewerConfig::ToolOptions freetext;
  freetext.m_color = QStringLiteral("purple");
  freetext.m_fontSize = 16.0;
  pdfConfig.setToolOptions(PdfToolOptions::freeTextTool(), freetext);

  PdfViewerAdapter adapter;
  // A fresh adapter starts on ITS OWN defaults; that is exactly the state a
  // newly opened window would be stuck in without hydration.
  QCOMPARE(adapter.getToolOptions(PdfToolOptions::inkTool())
               .value(PdfToolOptions::colorKey())
               .toString(),
           CommentColor::defaultToken());

  PdfToolOptionsRouter::hydrate(pdfConfig, &adapter);

  QCOMPARE(adapter.getToolOptions(PdfToolOptions::highlightTool())
               .value(PdfToolOptions::colorKey())
               .toString(),
           QStringLiteral("green"));
  QCOMPARE(adapter.getToolOptions(PdfToolOptions::inkTool())
               .value(PdfToolOptions::colorKey())
               .toString(),
           QStringLiteral("blue"));
  QCOMPARE(adapter.getToolOptions(PdfToolOptions::inkTool())
               .value(PdfToolOptions::widthKey())
               .toDouble(),
           3.0);
  QCOMPARE(adapter.getToolOptions(PdfToolOptions::freeTextTool())
               .value(PdfToolOptions::colorKey())
               .toString(),
           QStringLiteral("purple"));
  QCOMPARE(adapter.getToolOptions(PdfToolOptions::freeTextTool())
               .value(PdfToolOptions::fontSizeKey())
               .toDouble(),
           16.0);

  // A null adapter is a no-op, not a crash: setupComments() runs before the
  // viewer is guaranteed to exist in every path.
  PdfToolOptionsRouter::hydrate(pdfConfig, nullptr);
}

void TestPdfToolOptionsRouter::hydratedOptionsSurviveTheFirstReadyTransition() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  PdfViewerConfig::ToolOptions ink;
  ink.m_color = QStringLiteral("pink");
  ink.m_width = 0.75;
  pdfConfig.setToolOptions(PdfToolOptions::inkTool(), ink);

  PdfViewerAdapter adapter;
  QSignalSpy options(&adapter, &PdfViewerAdapter::toolOptionsChanged);
  QSignalSpy tools(&adapter, &PdfViewerAdapter::toolChanged);

  // Hydrate BEFORE the page is ready, which is the real ordering.
  PdfToolOptionsRouter::hydrate(pdfConfig, &adapter);
  adapter.setTool(PdfViewerAdapter::Tool::Ink);
  QCOMPARE(options.count(), 0);
  QCOMPARE(tools.count(), 0);

  adapter.setReady(true);

  // Every tool, plus the active tool.
  const auto published = byTool(options);
  QCOMPARE(published.size(), PdfViewerConfig::toolNames().size());
  QCOMPARE(published.value(PdfToolOptions::inkTool()).value(PdfToolOptions::colorKey()).toString(),
           QStringLiteral("pink"));
  QCOMPARE(published.value(PdfToolOptions::inkTool()).value(PdfToolOptions::widthKey()).toDouble(),
           0.75);
  QCOMPARE(tools.count(), 1);
  QCOMPARE(tools.at(0).at(0).toString(), PdfToolOptions::inkTool());
}

void TestPdfToolOptionsRouter::theContextMenuRoutePersistsPushesAndCaptures() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  PdfViewerAdapter adapter;
  adapter.setReady(true);

  QSignalSpy captures(&adapter, &PdfViewerAdapter::captureSelectionRequested);
  QSignalSpy options(&adapter, &PdfViewerAdapter::toolOptionsChanged);

  PdfToolOptionsRouter::captureHighlight(pdfConfig, &adapter, QStringLiteral("green"));

  // 1. persisted
  QCOMPARE(pdfConfig.getToolOptions(PdfToolOptions::highlightTool()).m_color,
           QStringLiteral("green"));
  // 2. pushed to the adapter
  QCOMPARE(adapter.getToolOptions(PdfViewerAdapter::Tool::Highlight)
               .value(PdfToolOptions::colorKey())
               .toString(),
           QStringLiteral("green"));
  // 3. captured with it
  QCOMPARE(captures.count(), 1);
  QCOMPARE(captures.at(0).at(0).toString(), QStringLiteral("green"));

  // Only the HIGHLIGHT tool moved.
  QCOMPARE(options.count(), 1);
  QCOMPARE(options.at(0).at(0).toString(), PdfToolOptions::highlightTool());
  QCOMPARE(pdfConfig.getToolOptions(PdfToolOptions::inkTool()).m_color,
           CommentColor::defaultToken());

  // An invalid token normalizes ONCE, and all three effects see the SAME
  // normalized value -- capturing with the raw pick would let the highlight
  // painted on the page differ from the one the toolbar shows.
  captures.clear();
  PdfToolOptionsRouter::captureHighlight(pdfConfig, &adapter, QStringLiteral("#ff00ff"));
  QCOMPARE(pdfConfig.getToolOptions(PdfToolOptions::highlightTool()).m_color,
           CommentColor::defaultToken());
  QCOMPARE(adapter.getToolOptions(PdfViewerAdapter::Tool::Highlight)
               .value(PdfToolOptions::colorKey())
               .toString(),
           CommentColor::defaultToken());
  QCOMPARE(captures.count(), 1);
  QCOMPARE(captures.at(0).at(0).toString(), CommentColor::defaultToken());
}

void TestPdfToolOptionsRouter::theRouterNormalizesExactlyLikeTheConfig() {
  MainConfig config(m_configMgr);
  auto &pdfConfig = config.getEditorConfig().getPdfViewerConfig();

  PdfViewerAdapter adapter;
  adapter.setReady(true);

  // Out of range CLAMPS, and the clamped value is what BOTH the store and the
  // adapter end up holding.
  QCOMPARE(PdfToolOptionsRouter::applyScalar(pdfConfig, &adapter, PdfToolOptions::inkTool(), 1.0e9),
           PdfInkAnchor::maxWidth());
  QCOMPARE(pdfConfig.getToolOptions(PdfToolOptions::inkTool()).m_width, PdfInkAnchor::maxWidth());
  QCOMPARE(adapter.getToolOptions(PdfViewerAdapter::Tool::Ink)
               .value(PdfToolOptions::widthKey())
               .toDouble(),
           PdfInkAnchor::maxWidth());

  QCOMPARE(
      PdfToolOptionsRouter::applyScalar(pdfConfig, &adapter, PdfToolOptions::freeTextTool(), -1.0),
      PdfFreeTextAnchor::minFontSize());

  // A tool that carries no scalar is a no-op.
  QCOMPARE(
      PdfToolOptionsRouter::applyScalar(pdfConfig, &adapter, PdfToolOptions::highlightTool(), 5.0),
      0.0);

  // An unknown tool cannot invent an entry.
  QCOMPARE(PdfToolOptionsRouter::applyColor(pdfConfig, &adapter, QStringLiteral("bogus"),
                                            QStringLiteral("blue")),
           QString());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfToolOptionsRouter)
#include "test_pdftooloptionsrouter.moc"
