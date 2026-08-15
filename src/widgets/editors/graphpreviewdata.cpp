#include "graphpreviewdata.h"

#include "previewscaleutils.h"
#include <gui/utils/imageutils.h>

using namespace vnotex;

int GraphPreviewData::s_imageIndex = 0;

GraphPreviewData::GraphPreviewData(TimeStamp p_timeStamp, const QString &p_format,
                                   const QByteArray &p_data, bool p_needScale, QRgb p_background,
                                   qreal p_cppRasterFactor, qreal p_zoomRatio)
    : m_timeStamp(p_timeStamp), m_background(p_background), m_format(p_format),
      m_needScale(p_needScale), m_data(p_data), m_appliedZoomRatio(p_zoomRatio) {
  if (m_data.isEmpty()) {
    return;
  }

  rasterize(p_cppRasterFactor);

  if (!m_needScale) {
    // Retain the payload only for the entries we can re-render locally.
    m_data.clear();
  }
}

void GraphPreviewData::rasterize(qreal p_cppRasterFactor) {
  if (m_data.isEmpty()) {
    return;
  }

  m_name = QString::number(++s_imageIndex);

  // The web side already rasterized it at the right scale.
  if (!m_needScale || !PreviewScaleUtils::needsScaling(p_cppRasterFactor)) {
    m_image.loadFromData(m_data, m_format.toLocal8Bit().data());
    return;
  }

  if (m_format == QStringLiteral("svg")) {
    m_image = ImageUtils::svgToPixmap(m_data, m_background, p_cppRasterFactor);
  } else {
    QPixmap tmpImg;
    tmpImg.loadFromData(m_data, m_format.toLocal8Bit().data());
    const int width = qMax(1, static_cast<int>(tmpImg.width() * p_cppRasterFactor));
    m_image = tmpImg.scaledToWidth(width, Qt::SmoothTransformation);
  }
}

bool GraphPreviewData::isNull() const { return m_timeStamp == 0; }
