#ifndef IMAGEUTILS_H
#define IMAGEUTILS_H

#include <QImage>
#include <QPixmap>

namespace vnotex {
class ImageUtils {
public:
  ImageUtils() = delete;

  static QImage::Format guessImageFormat(const QByteArray &p_data);

  static QString guessImageSuffix(const QByteArray &p_data);

  static QImage imageFromFile(const QString &p_filePath);

  static QPixmap pixmapFromFile(const QString &p_filePath);

  static QPixmap svgToPixmap(const QByteArray &p_content, QRgb p_background, qreal p_scaleFactor);
};
} // namespace vnotex

#endif // IMAGEUTILS_H
