// In-place previews must follow the editor zoom. The scale arithmetic behind
// that lives in PreviewScaleUtils so it can be pinned without the editor stack.
//
// The two quantities MUST stay distinct: the web side receives the bare zoom
// ratio (it multiplies by devicePixelRatio itself), while the C++ rasterizer
// receives dpiScaleFactor * zoomRatio. Collapsing them either double-scales on
// a high-DPI screen or drops DPI scaling entirely.
#include <QBuffer>
#include <QtTest>

#include <widgets/editors/graphpreviewdata.h>
#include <widgets/editors/previewscaleutils.h>

using namespace vnotex;

namespace tests {

namespace {
// A 40x20 SVG with an absolute size, like a MathJax/Graphviz payload.
QByteArray svgPayload() {
  return QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
                           "<rect width=\"40\" height=\"20\" fill=\"#123456\"/></svg>");
}

// A 40x20 PNG, like a web PlantUML payload (resampled, decision 6).
QByteArray pngPayload() {
  QPixmap pm(40, 20);
  pm.fill(Qt::red);
  QByteArray data;
  QBuffer buffer(&data);
  buffer.open(QIODevice::WriteOnly);
  pm.save(&buffer, "PNG");
  return data;
}
} // namespace

class TestPreviewScaleUtils : public QObject {
  Q_OBJECT

private slots:
  void zoomRatioNoZoom() { QCOMPARE(PreviewScaleUtils::zoomRatio(12, 12), 1.0); }

  void zoomRatioPositiveDelta() { QCOMPARE(PreviewScaleUtils::zoomRatio(24, 12), 2.0); }

  void zoomRatioNegativeDelta() { QCOMPARE(PreviewScaleUtils::zoomRatio(6, 12), 0.5); }

  void zoomRatioClampsBothEnds() {
    QCOMPARE(PreviewScaleUtils::zoomRatio(1000, 12), 4.0);
    QCOMPARE(PreviewScaleUtils::zoomRatio(1, 100), 0.25);
  }

  void zoomRatioWithoutBase() {
    QCOMPARE(PreviewScaleUtils::zoomRatio(12, 0), 1.0);
    QCOMPARE(PreviewScaleUtils::zoomRatio(12, -3), 1.0);
  }

  // VTextEditor::zoom() clamps the font at 2pt but still stores the requested
  // delta, so the ratio must come from the actual font size, not the delta.
  void zoomRatioUsesClampedFontSize() {
    // Base 10pt, zoom(-20) leaves the font at 2pt.
    QCOMPARE(PreviewScaleUtils::zoomRatio(2, 10), 0.25); // 0.2 clamped to the floor.
    // The delta-derived value (10 / 30) would be a different, wrong number.
    QVERIFY(!qFuzzyCompare(PreviewScaleUtils::zoomRatio(2, 10), 10 / 30.0));
  }

  // The unit contract: rasterFactor keeps the DPI factor, the JS scale does not.
  void rasterFactorKeepsDpiFactor() {
    QCOMPARE(PreviewScaleUtils::rasterFactor(2.0, 1.5), 3.0);
    QCOMPARE(PreviewScaleUtils::rasterFactor(1.0, 1.5), 1.5);
    // A ratio of 1 on a high-DPI screen must not change today's behaviour.
    QCOMPARE(PreviewScaleUtils::rasterFactor(2.0, 1.0), 2.0);
  }

  void rasterFactorGuardsNonPositiveInputs() {
    QCOMPARE(PreviewScaleUtils::rasterFactor(0, 2.0), 2.0);
    QCOMPARE(PreviewScaleUtils::rasterFactor(2.0, 0), 2.0);
  }

  // Zoom-out must re-render too: the old "> 1.01" upscale-only guard is gone.
  void needsScalingCoversZoomOut() {
    QVERIFY(PreviewScaleUtils::needsScaling(0.5));
    QVERIFY(PreviewScaleUtils::needsScaling(2.0));
    QVERIFY(!PreviewScaleUtils::needsScaling(1.0));
    QVERIFY(!PreviewScaleUtils::needsScaling(0));
  }

  void staleZoomRatioDetection() {
    QVERIFY(!PreviewScaleUtils::isZoomRatioStale(1.0, 1.0));
    QVERIFY(PreviewScaleUtils::isZoomRatioStale(1.0, 1.1));
    QVERIFY(PreviewScaleUtils::isZoomRatioStale(2.0, 1.0));
  }

  // Cache policy: a stale needScale=true entry is re-rasterized and reused; a
  // stale needScale=false entry (rasterized on the web side) is a miss.
  void cacheActionPolicy() {
    QCOMPARE(PreviewScaleUtils::cacheAction(true, 1.0, 1.0), PreviewScaleUtils::CacheAction::Reuse);
    QCOMPARE(PreviewScaleUtils::cacheAction(false, 1.0, 1.0),
             PreviewScaleUtils::CacheAction::Reuse);
    QCOMPARE(PreviewScaleUtils::cacheAction(true, 1.0, 2.0),
             PreviewScaleUtils::CacheAction::Rerasterize);
    QCOMPARE(PreviewScaleUtils::cacheAction(true, 2.0, 0.5),
             PreviewScaleUtils::CacheAction::Rerasterize);
    QCOMPARE(PreviewScaleUtils::cacheAction(false, 1.0, 2.0), PreviewScaleUtils::CacheAction::Miss);
  }

  // needScale=false is rasterized by the web side at the right scale already;
  // applying the C++ factor to it would double-scale.
  void rasterizeIgnoresFactorWhenNotNeedScale() {
    GraphPreviewData data(1, QStringLiteral("png"), pngPayload(), false, 0x0, 3.0, 1.0);
    QCOMPARE(data.m_image.width(), 40);
    QCOMPARE(data.m_image.height(), 20);
    // No payload is retained for an entry that can only be refreshed by a request.
    QVERIFY(data.m_data.isEmpty());
  }

  void rasterizeSvgUpAndDown() {
    GraphPreviewData data(1, QStringLiteral("svg"), svgPayload(), true, 0x0, 1.0, 1.0);
    QCOMPARE(data.m_image.width(), 40);
    QVERIFY(!data.m_data.isEmpty());

    data.rasterize(2.0);
    QCOMPARE(data.m_image.width(), 80);

    // Zoom-out must re-render too: the old upscale-only guard is gone.
    data.rasterize(0.5);
    QCOMPARE(data.m_image.width(), 20);
  }

  // Always from the ORIGINAL bytes, so repeated zooming never accumulates
  // resampling artifacts nor drifts in size.
  void rasterizePngFromOriginalBytes() {
    GraphPreviewData data(1, QStringLiteral("png"), pngPayload(), true, 0x0, 1.0, 1.0);
    QCOMPARE(data.m_image.width(), 40);

    data.rasterize(2.0);
    QCOMPARE(data.m_image.width(), 80);

    data.rasterize(2.0);
    QCOMPARE(data.m_image.width(), 80); // Not 160.

    data.rasterize(1.0);
    QCOMPARE(data.m_image.width(), 40);
  }

  // PreviewMgr::imageResourceNameForSource() early-returns on an existing name,
  // so a re-render under the old name would never be registered.
  void rasterizeMintsANewName() {
    GraphPreviewData data(1, QStringLiteral("svg"), svgPayload(), true, 0x0, 1.0, 1.0);
    const auto first = data.m_name;
    QVERIFY(!first.isEmpty());

    data.rasterize(2.0);
    QVERIFY(data.m_name != first);

    const auto second = data.m_name;
    data.rasterize(2.0);
    QVERIFY(data.m_name != second);
  }

  void emptyPayloadRastersNothing() {
    GraphPreviewData data(1, QStringLiteral("svg"), QByteArray(), true, 0x0, 2.0, 1.0);
    QVERIFY(data.m_image.isNull());
    QVERIFY(data.m_name.isEmpty());
  }
};
} // namespace tests

QTEST_MAIN(tests::TestPreviewScaleUtils)

#include "test_previewscaleutils.moc"
