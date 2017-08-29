#ifndef VPREVIEWUTILS_H
#define VPREVIEWUTILS_H

#include <QTextImageFormat>
#include "vconstants.h"

class QTextDocument;

class VPreviewUtils
{
public:
    // Fetch the text image format from an image preview position.
    static QTextImageFormat fetchFormatFromPosition(QTextDocument *p_doc,
                                                    int p_position);

    static PreviewImageType getPreviewImageType(const QTextImageFormat &p_format);

    static PreviewImageSource getPreviewImageSource(const QTextImageFormat &p_format);

    // Fetch the ImageID from an image format.
    // Returns -1 if not valid.
    static long long getPreviewImageID(const QTextImageFormat &p_format);

private:
    VPreviewUtils() {}
};

#endif // VPREVIEWUTILS_H
