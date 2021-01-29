#ifndef VXNODEFILE_H
#define VXNODEFILE_H

#include <QSharedPointer>

#include <core/file.h>

namespace vnotex
{
    class VXNode;

    // File from VXNode.
    class VXNodeFile : public File, public IFileWithImage
    {
    public:
        explicit VXNodeFile(const QSharedPointer<VXNode> &p_node);

        QString read() const Q_DECL_OVERRIDE;

        void write(const QString &p_content) Q_DECL_OVERRIDE;

        QString getName() const Q_DECL_OVERRIDE;

        QString getFilePath() const Q_DECL_OVERRIDE;

        QString getContentPath() const Q_DECL_OVERRIDE;

        QString getResourcePath() const Q_DECL_OVERRIDE;

        IFileWithImage *getImageInterface() Q_DECL_OVERRIDE;

        // Get the corresponding node if available.
        Node *getNode() const Q_DECL_OVERRIDE;

        // IFileWithImage interfaces.
    public:
        QString fetchImageFolderPath() Q_DECL_OVERRIDE;

        // Insert image from @p_srcImagePath.
        // Return inserted image file path.
        QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        QString insertImage(const QImage &p_image, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        void removeImage(const QString &p_imagePath) Q_DECL_OVERRIDE;

    private:
        QSharedPointer<VXNode> m_node;
    };
}

#endif // VXNODEFILE_H
