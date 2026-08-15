#include "previewscaleutils.h"

namespace vnotex {
namespace PreviewScaleUtils {

qreal zoomRatio(int p_fontPointSize, int p_baseFontPointSize) {
  if (p_baseFontPointSize <= 0) {
    return 1;
  }

  return qBound(c_minZoomRatio, p_fontPointSize / static_cast<qreal>(p_baseFontPointSize),
                c_maxZoomRatio);
}

qreal rasterFactor(qreal p_dpiScaleFactor, qreal p_zoomRatio) {
  const qreal dpi = p_dpiScaleFactor > 0 ? p_dpiScaleFactor : 1;
  const qreal ratio = p_zoomRatio > 0 ? p_zoomRatio : 1;
  return dpi * ratio;
}

bool needsScaling(qreal p_rasterFactor) {
  return p_rasterFactor > 0 && qAbs(p_rasterFactor - 1) > 0.01;
}

bool isZoomRatioStale(qreal p_appliedZoomRatio, qreal p_zoomRatio) {
  return qAbs(p_appliedZoomRatio - p_zoomRatio) > 0.001;
}

CacheAction cacheAction(bool p_needScale, qreal p_appliedZoomRatio, qreal p_zoomRatio) {
  if (!isZoomRatioStale(p_appliedZoomRatio, p_zoomRatio)) {
    return CacheAction::Reuse;
  }

  return p_needScale ? CacheAction::Rerasterize : CacheAction::Miss;
}

} // namespace PreviewScaleUtils
} // namespace vnotex
