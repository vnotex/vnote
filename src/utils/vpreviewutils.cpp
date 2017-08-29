#include "vpreviewutils.h"

#include <QTextDocument>
#include <QTextCursor>

QTextImageFormat VPreviewUtils::fetchFormatFromPosition(QTextDocument *p_doc,
                                                        int p_position)
{
    if (p_doc->characterAt(p_position) != QChar::ObjectReplacementCharacter) {
        return QTextImageFormat();
    }

    QTextCursor cursor(p_doc);
    cursor.setPosition(p_position);
    if (cursor.atBlockEnd()) {
        return QTextImageFormat();
    }

    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);

    return cursor.charFormat().toImageFormat();
}

PreviewImageType VPreviewUtils::getPreviewImageType(const QTextImageFormat &p_format)
{
    Q_ASSERT(p_format.isValid());
    bool ok = true;
    int type = p_format.property((int)ImageProperty::ImageType).toInt(&ok);
    if (ok) {
        return (PreviewImageType)type;
    } else {
        return PreviewImageType::Invalid;
    }
}

PreviewImageSource VPreviewUtils::getPreviewImageSource(const QTextImageFormat &p_format)
{
    Q_ASSERT(p_format.isValid());
    bool ok = true;
    int src = p_format.property((int)ImageProperty::ImageSource).toInt(&ok);
    if (ok) {
        return (PreviewImageSource)src;
    } else {
        return PreviewImageSource::Invalid;
    }
}

long long VPreviewUtils::getPreviewImageID(const QTextImageFormat &p_format)
{
    if (!p_format.isValid()) {
        return -1;
    }

    bool ok = true;
    long long id = p_format.property((int)ImageProperty::ImageID).toLongLong(&ok);
    if (ok) {
        return id;
    } else {
        return -1;
    }
}
