#ifndef NODEBUFFERPROVIDER_H
#define NODEBUFFERPROVIDER_H

#include <QSharedPointer>

#include "bufferprovider.h"

namespace vnotex
{
    class File;
    class IFileWithImage;

    // Buffer provider based on an internal node.
    class NodeBufferProvider : public BufferProvider
    {
        Q_OBJECT
    public:
        NodeBufferProvider(const QSharedPointer<Node> &p_node, QObject *p_parent = nullptr);

        Buffer::ProviderType getType() const Q_DECL_OVERRIDE;

        bool match(const Node *p_node) const Q_DECL_OVERRIDE;

        bool match(const QString &p_filePath) const Q_DECL_OVERRIDE;

        QString getName() const Q_DECL_OVERRIDE;

        QString getPath() const Q_DECL_OVERRIDE;

        QString getContentPath() const Q_DECL_OVERRIDE;

        QString getResourcePath() const Q_DECL_OVERRIDE;

        void write(const QString &p_content) Q_DECL_OVERRIDE;

        QString read() const Q_DECL_OVERRIDE;

        QString fetchImageFolderPath() Q_DECL_OVERRIDE;

        bool isChildOf(const Node *p_node) const Q_DECL_OVERRIDE;

        Node *getNode() const Q_DECL_OVERRIDE;

        QString getAttachmentFolder() const Q_DECL_OVERRIDE;

        QString fetchAttachmentFolderPath() Q_DECL_OVERRIDE;

        QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files) Q_DECL_OVERRIDE;

        QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

        QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

        QString renameAttachment(const QString &p_path, const QString &p_name) Q_DECL_OVERRIDE;

        void removeAttachment(const QStringList &p_paths) Q_DECL_OVERRIDE;

        QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        QString insertImage(const QImage &p_image, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        void removeImage(const QString &p_imagePath) Q_DECL_OVERRIDE;

        bool isAttachmentSupported() const Q_DECL_OVERRIDE;

        bool isReadOnly() const Q_DECL_OVERRIDE;

    private:
        QSharedPointer<Node> m_node;

        QSharedPointer<File> m_nodeFile;
    };
}

#endif // NODEBUFFERPROVIDER_H
