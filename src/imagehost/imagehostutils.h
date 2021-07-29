#ifndef IMAGEHOSTUTILS_H
#define IMAGEHOSTUTILS_H

#include <QString>

class QImage;
class QWidget;

namespace vnotex
{
    class Buffer;

    class ImageHostUtils
    {
    public:
        ImageHostUtils() = delete;

        // According to @p_buffer, generate the relative path on image host for images.
        // Return the relative path folder.
        static QString generateRelativePath(const Buffer *p_buffer);
    };
}

#endif // IMAGEHOSTUTILS_H
