#ifndef IMAGEUTILS_H
#define IMAGEUTILS_H

#include <QImage>
#include <QPixmap>

namespace vnotex {
class ImageUtils {
public:
  ImageUtils() = delete;

  // Decode bounded passive image bytes only; SVG is validated then rasterized.
  // Output is cleared on failure and never contains an executable SVG document.
  static bool protectedImageData(const QByteArray &p_input, QByteArray &p_output,
                                 QByteArray &p_mime);

  static QImage::Format guessImageFormat(const QByteArray &p_data);

  static QString guessImageSuffix(const QByteArray &p_data);

  static QImage imageFromFile(const QString &p_filePath);

  static QPixmap pixmapFromFile(const QString &p_filePath);

  static QPixmap svgToPixmap(const QByteArray &p_content, QRgb p_background, qreal p_scaleFactor);
};
} // namespace vnotex

#endif // IMAGEUTILS_H
