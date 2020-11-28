#ifndef MARKDOWNBUFFER_H
#define MARKDOWNBUFFER_H

#include "buffer.h"

#include <QVector>
#include <QSet>

#include <vtextedit/markdownutils.h>

namespace vnotex
{
    class MarkdownBuffer : public Buffer
    {
        Q_OBJECT
    public:
        MarkdownBuffer(const BufferParameters &p_parameters,
                       QObject *p_parent = nullptr);

        QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        QString insertImage(const QImage &p_image, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        void removeImage(const QString &p_imagePath) Q_DECL_OVERRIDE;

        void addInsertedImage(const QString &p_imagePath, const QString &p_urlInLink);

        // Clear obsolete images.
        // Won't delete images, just return a list of obsolete images path.
        // Will re-init m_initialImages and clear m_insertedImages.
        QSet<QString> clearObsoleteImages();

    protected:
        ViewWindow *createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent) Q_DECL_OVERRIDE;

    private:
        void fetchInitialImages();

        // Images referenced in the file before opening this buffer.
        QVector<vte::MarkdownLink> m_initialImages;

        // Images newly inserted during this buffer's lifetime.
        QVector<vte::MarkdownLink> m_insertedImages;
    };
} // ns vnotex

#endif // MARKDOWNBUFFER_H
