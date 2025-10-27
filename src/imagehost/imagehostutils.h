#ifndef IMAGEHOSTUTILS_H
#define IMAGEHOSTUTILS_H

#include <QString>

class QImage;
class QWidget;

namespace vnotex {
class Buffer;

class ImageHostUtils {
public:
  ImageHostUtils() = delete;

  // According to @p_buffer, generate the relative path on image host for images.
  static QString generateRelativePath(const Buffer *p_buffer);
};
} // namespace vnotex

#endif // IMAGEHOSTUTILS_H
