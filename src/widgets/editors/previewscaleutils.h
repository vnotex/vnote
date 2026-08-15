#ifndef PREVIEWSCALEUTILS_H
#define PREVIEWSCALEUTILS_H

#include <QtGlobal>

namespace vnotex {
// Dependency-free scale arithmetic of the in-place preview pipeline.
// Kept out of PreviewHelper so it is unit-testable without the editor stack.
//
// There are two distinct quantities, and they must never be conflated:
// - the zoom ratio, unitless and clamped, which is what the web side receives;
// - the C++ raster factor, the DPI scale factor multiplied by the zoom ratio,
//   which is what the C++ rasterizer receives.
namespace PreviewScaleUtils {
// Lower/upper bound of the zoom ratio.
constexpr qreal c_minZoomRatio = 0.25;
constexpr qreal c_maxZoomRatio = 4.0;

// The zoom ratio of an editor whose current font is @p_fontPointSize and whose
// unzoomed base font is @p_baseFontPointSize. Returns 1 when the base is not
// positive.
qreal zoomRatio(int p_fontPointSize, int p_baseFontPointSize);

// The factor to rasterize with on the C++ side.
qreal rasterFactor(qreal p_dpiScaleFactor, qreal p_zoomRatio);

// Whether a factor is materially different from 1 and thus needs scaling.
bool needsScaling(qreal p_rasterFactor);

// Whether an entry rendered at @p_appliedZoomRatio is stale for @p_zoomRatio.
bool isZoomRatioStale(qreal p_appliedZoomRatio, qreal p_zoomRatio);

// What to do with a cache entry found for the current generation.
enum class CacheAction {
  // Fresh enough: use it as a hit.
  Reuse,
  // Stale but locally re-renderable from the retained payload.
  Rerasterize,
  // Stale and rasterized on the web side: it must be re-requested.
  Miss
};

// @p_needScale: whether the entry retains a payload we can re-render locally.
CacheAction cacheAction(bool p_needScale, qreal p_appliedZoomRatio, qreal p_zoomRatio);
} // namespace PreviewScaleUtils
} // namespace vnotex

#endif // PREVIEWSCALEUTILS_H
