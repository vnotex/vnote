#include "imageutils.h"

#include <QBuffer>
#include <QHash>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSvgRenderer>
#include <QXmlStreamReader>

#include <utils/fileutils2.h>

using namespace vnotex;

namespace {
constexpr int c_protectedImageByteLimit = 32 * 1024 * 1024;
constexpr int c_protectedImageDimensionLimit = 8192;
constexpr qint64 c_protectedImagePixelLimit = 16 * 1024 * 1024;

bool protectedImageSize(const QSize &p_size) {
  return p_size.width() > 0 && p_size.height() > 0 &&
         p_size.width() <= c_protectedImageDimensionLimit &&
         p_size.height() <= c_protectedImageDimensionLimit &&
         qint64(p_size.width()) * p_size.height() <= c_protectedImagePixelLimit;
}

void wipeImage(QImage &p_image) {
  volatile uchar *bytes = p_image.bits();
  const auto size = p_image.sizeInBytes();
  for (qint64 idx = 0; idx < size; ++idx) {
    bytes[idx] = 0;
  }
}

bool passiveSvg(const QByteArray &p_data) {
  // Parse before constructing QSvgRenderer: never let an XML entity or external
  // SVG reference reach a decoder capable of opening a URL or local file.
  static const QSet<QString> c_elements = {QStringLiteral("svg"),
                                           QStringLiteral("g"),
                                           QStringLiteral("defs"),
                                           QStringLiteral("title"),
                                           QStringLiteral("desc"),
                                           QStringLiteral("path"),
                                           QStringLiteral("rect"),
                                           QStringLiteral("circle"),
                                           QStringLiteral("ellipse"),
                                           QStringLiteral("line"),
                                           QStringLiteral("polyline"),
                                           QStringLiteral("polygon"),
                                           QStringLiteral("text"),
                                           QStringLiteral("tspan"),
                                           QStringLiteral("textPath"),
                                           QStringLiteral("use"),
                                           QStringLiteral("symbol"),
                                           QStringLiteral("clipPath"),
                                           QStringLiteral("mask"),
                                           QStringLiteral("pattern"),
                                           QStringLiteral("marker"),
                                           QStringLiteral("linearGradient"),
                                           QStringLiteral("radialGradient"),
                                           QStringLiteral("stop")};
  static const QRegularExpression c_localReference(QStringLiteral("^#[A-Za-z_][A-Za-z0-9_.:-]*$"));
  static const QRegularExpression c_localPaint(
      QStringLiteral("url\\(\\s*#[A-Za-z_][A-Za-z0-9_.:-]*\\s*\\)"));
  QXmlStreamReader xml(p_data);
  int depth = 0;
  int elements = 0;
  bool rootSeen = false;
  while (!xml.atEnd()) {
    const auto type = xml.readNext();
    if (type == QXmlStreamReader::DTD || type == QXmlStreamReader::EntityReference ||
        type == QXmlStreamReader::ProcessingInstruction) {
      return false;
    }
    if (type == QXmlStreamReader::EndElement) {
      --depth;
    } else if (type == QXmlStreamReader::StartElement) {
      if (++depth > 128 || ++elements > 100000) {
        return false;
      }
      const auto name = xml.name().toString();
      const auto ns = xml.namespaceUri().toString();
      if ((!ns.isEmpty() && ns != QStringLiteral("http://www.w3.org/2000/svg")) ||
          !c_elements.contains(name) || (!rootSeen && name != QStringLiteral("svg"))) {
        return false;
      }
      rootSeen = true;
      for (const auto &attr : xml.attributes()) {
        const auto attrName = attr.name().toString().toLower();
        const auto attrNs = attr.namespaceUri().toString();
        if (attrName.startsWith(QStringLiteral("on")) || attrName == QStringLiteral("base") ||
            (!attrNs.isEmpty() && attrNs != QStringLiteral("http://www.w3.org/1999/xlink") &&
             attrNs != QStringLiteral("http://www.w3.org/XML/1998/namespace"))) {
          return false;
        }
        auto value = attr.value().toString();
        if (attrName == QStringLiteral("href") && !c_localReference.match(value).hasMatch()) {
          return false;
        }
        // No CSS escapes, imports, expression(), or non-local paint servers.
        // SVG <style>, <image>, SMIL and foreignObject are outside the allowlist.
        if (value.contains(QLatin1Char('\\')) || value.contains(QLatin1Char('@')) ||
            value.contains(QLatin1Char('<')) || value.contains(QLatin1Char('>')) ||
            value.contains(QStringLiteral("expression"), Qt::CaseInsensitive)) {
          return false;
        }
        value.remove(c_localPaint);
        if (value.contains(QStringLiteral("url"), Qt::CaseInsensitive)) {
          return false;
        }
      }
    }
  }
  return rootSeen && !xml.hasError() && depth == 0;
}
} // namespace

bool ImageUtils::protectedImageData(const QByteArray &p_input, QByteArray &p_output,
                                    QByteArray &p_mime) {
  p_output.clear();
  p_mime.clear();
  if (p_input.isEmpty() || p_input.size() > c_protectedImageByteLimit) {
    return false;
  }

  QBuffer input;
  input.setData(p_input);
  if (!input.open(QIODevice::ReadOnly)) {
    return false;
  }
  QImageReader reader(&input);
  reader.setDecideFormatFromContent(true);
  const auto format = reader.format().toLower();
  if (format == QByteArrayLiteral("svg") || format == QByteArrayLiteral("svgz")) {
    // Compressed SVG is intentionally not accepted: the XML bound is applied to
    // the actual bytes reviewed before Qt's SVG implementation sees them.
    if (format != QByteArrayLiteral("svg") || !passiveSvg(p_input)) {
      return false;
    }
    QSvgRenderer renderer(p_input);
    if (!renderer.isValid() || renderer.animated() || !protectedImageSize(renderer.defaultSize())) {
      return false;
    }
    QImage image(renderer.defaultSize(), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
      return false;
    }
    image.fill(Qt::transparent);
    {
      QPainter painter(&image);
      renderer.render(&painter);
    }
    QBuffer output(&p_output);
    const bool ok = output.open(QIODevice::WriteOnly) && image.save(&output, "PNG") &&
                    p_output.size() <= c_protectedImageByteLimit;
    wipeImage(image);
    if (!ok) {
      p_output.fill('\0');
      p_output.clear();
      return false;
    }
    p_mime = QByteArrayLiteral("image/png");
    return true;
  }

  static const QHash<QByteArray, QByteArray> c_passiveTypes = {
      {QByteArrayLiteral("png"), QByteArrayLiteral("image/png")},
      {QByteArrayLiteral("jpeg"), QByteArrayLiteral("image/jpeg")},
      {QByteArrayLiteral("jpg"), QByteArrayLiteral("image/jpeg")},
      {QByteArrayLiteral("gif"), QByteArrayLiteral("image/gif")},
      {QByteArrayLiteral("webp"), QByteArrayLiteral("image/webp")},
      {QByteArrayLiteral("xpm"), QByteArrayLiteral("image/png")},
      {QByteArrayLiteral("bmp"), QByteArrayLiteral("image/bmp")}};
  const auto mime = c_passiveTypes.constFind(format);
  if (mime == c_passiveTypes.cend() || !protectedImageSize(reader.size())) {
    return false;
  }
  auto image = reader.read();
  if (image.isNull() || !protectedImageSize(image.size())) {
    wipeImage(image);
    return false;
  }
  if (format == QByteArrayLiteral("xpm")) {
    // XPM is supported by the image picker but not by browsers. Preserve its
    // pixels in a lossless, passive format using the same bounds as other images.
    QBuffer output(&p_output);
    const bool ok = output.open(QIODevice::WriteOnly) && image.save(&output, "PNG") &&
                    p_output.size() <= c_protectedImageByteLimit;
    wipeImage(image);
    if (!ok) {
      p_output.fill('\0');
      p_output.clear();
      return false;
    }
    p_mime = mime.value();
    return true;
  }
  wipeImage(image);
  // Preserve passive raster bytes (including GIF/WebP animation), but give the
  // caller a separate owner that can be wiped independently of the input.
  p_output = QByteArray(p_input.constData(), p_input.size());
  p_mime = mime.value();
  return true;
}

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

QPixmap ImageUtils::svgToPixmap(const QByteArray &p_content, QRgb p_background,
                                qreal p_scaleFactor) {
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
