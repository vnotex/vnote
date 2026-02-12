#include "imageutils.h"

#include <QMimeDatabase>
#include <QPainter>
#include <QSvgRenderer>

#include <utils/fileutils2.h>

using namespace vnotex;

QImage::Format ImageUtils::guessImageFormat(const QByteArray &p_data) {
  auto image = QImage::fromData(p_data);
  return image.format();
}

QString ImageUtils::guessImageSuffix(const QByteArray &p_data) {
  QMimeDatabase mimeDb;
  auto mimeType = mimeDb.mimeTypeForData(p_data);
  return mimeType.preferredSuffix();
}

QImage ImageUtils::imageFromFile(const QString &p_filePath) {
  QImage img(p_filePath);
  if (!img.isNull()) {
    return img;
  }

  // @p_filePath may has a wrong suffix which indicates a wrong image format.
  QByteArray data;
  if (FileUtils2::readFile(p_filePath, &data)) {
    // Error reading file, return null image.
    return QImage();
  }
  img.loadFromData(data);
  return img;
}

QPixmap ImageUtils::pixmapFromFile(const QString &p_filePath) {
  QPixmap pixmap;
  QByteArray data;
  if (FileUtils2::readFile(p_filePath, &data)) {
    // Error reading file, return null pixmap.
    return pixmap;
  }
  pixmap.loadFromData(data);
  return pixmap;
}

QPixmap ImageUtils::svgToPixmap(const QByteArray &p_content, QRgb p_background, qreal p_scaleFactor) {
  QSvgRenderer renderer(p_content);
  QSize deSz = renderer.defaultSize();
  if (p_scaleFactor > 0) {
    deSz *= p_scaleFactor;
  }

  QPixmap pm(deSz);
  if (p_background == 0x0) {
    // Fill a transparent background to avoid glitchy preview.
    pm.fill(QColor(255, 255, 255, 0));
  } else {
    pm.fill(p_background);
  }

  QPainter painter(&pm);
  renderer.render(&painter);
  return pm;
}

