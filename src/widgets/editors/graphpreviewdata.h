#ifndef GRAPHPREVIEWDATA_H
#define GRAPHPREVIEWDATA_H

#include <QByteArray>
#include <QPixmap>
#include <QString>

#include <core/global.h>

namespace vnotex {
// Data of an in-place preview result.
//
// Deliberately free of any editor dependency: the rasterization is the part of
// the zoom pipeline that is easiest to get wrong (double-scaling, cumulative
// resampling, a mutated pixmap under a reused resource name), so it must be
// unit-testable on its own.
struct GraphPreviewData {
  GraphPreviewData() = default;

  // @p_needScale: whether the payload still needs to be scaled on the C++ side.
  //               When false, the web side already rasterized it at the right
  //               scale and the raster factor is ignored.
  // @p_cppRasterFactor: DPI scale factor multiplied by the zoom ratio.
  // @p_zoomRatio: the zoom ratio this data corresponds to.
  GraphPreviewData(TimeStamp p_timeStamp, const QString &p_format, const QByteArray &p_data,
                   bool p_needScale = false, QRgb p_background = 0x0, qreal p_cppRasterFactor = 1,
                   qreal p_zoomRatio = 1);

  // Re-render m_image from the retained payload, always from the ORIGINAL bytes
  // so repeated zooming never resamples an already resampled pixmap.
  // Mints a new m_name: PreviewMgr::imageResourceNameForSource() early-returns
  // when the generated name already exists, so a mutated pixmap under the same
  // name would never be registered.
  void rasterize(qreal p_cppRasterFactor);

  bool isNull() const;

  TimeStamp m_timeStamp = 0;

  QPixmap m_image;

  // Name of the image for identification in resource manager.
  QString m_name;

  // Background color to override.
  // 0x0 indicates it is not specified.
  QRgb m_background = 0x0;

  // Format of the payload, like "svg" or "png".
  QString m_format;

  // Whether the payload should be scaled on the C++ side.
  bool m_needScale = false;

  // The original payload. Retained only when m_needScale is true, since a
  // web-rasterized entry can only be refreshed by a new request anyway.
  QByteArray m_data;

  // The zoom ratio m_image corresponds to.
  qreal m_appliedZoomRatio = 1;

  // An increasing index to used as the image name.
  static int s_imageIndex;
};
} // namespace vnotex

#endif // GRAPHPREVIEWDATA_H
