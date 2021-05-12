#ifndef IMAGEUTILS_H
#define IMAGEUTILS_H

#include <QImage>

namespace vnotex
{
    class ImageUtils
    {
    public:
        ImageUtils() = delete;

        static QImage::Format guessImageFormat(const QByteArray &p_data);

        static QString guessImageSuffix(const QByteArray &p_data);
    };
}

#endif // IMAGEUTILS_H
