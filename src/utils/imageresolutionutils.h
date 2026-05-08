#ifndef IMAGERESOLUTIONUTILS_H
#define IMAGERESOLUTIONUTILS_H

#include <QString>
#include <QVector>

namespace vnotex {

struct RelativeImageInfo {
  QString srcAbsolutePath; // Resolved absolute path of source image
  QString urlInLink;       // Original URL string in markdown (e.g., "vx_images/img.png")
  int urlInLinkPos = -1;   // QChar position of URL in text
  int regionStart = -1;    // QChar position of "![" in text
  int regionEnd = -1;      // QChar position past closing ")"
  QString alt;             // Alt text from image link
  QString title;           // Title from image link
  bool exists = false;     // Whether the source file exists on disk
};

class ImageResolutionUtils {
public:
  ImageResolutionUtils() = delete;

  // Parse @p_markdownText for image links, resolve relative ones against @p_sourceBasePath.
  // Returns results sorted DESCENDING by urlInLinkPos (for safe in-place text rewriting).
  // Only includes images with relative URLs (skips absolute paths, http/https URLs, Qt resource paths).
  static QVector<RelativeImageInfo> resolveRelativeImages(
      const QString &p_markdownText,
      const QString &p_sourceBasePath);
};

} // namespace vnotex

#endif // IMAGERESOLUTIONUTILS_H
