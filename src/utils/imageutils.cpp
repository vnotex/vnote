#include "imageutils.h"

#include <QMimeDatabase>

using namespace vnotex;

QImage::Format ImageUtils::guessImageFormat(const QByteArray &p_data)
{
    auto image = QImage::fromData(p_data);
    return image.format();
}

QString ImageUtils::guessImageSuffix(const QByteArray &p_data)
{
    QMimeDatabase mimeDb;
    auto mimeType = mimeDb.mimeTypeForData(p_data);
    return mimeType.preferredSuffix();
}
