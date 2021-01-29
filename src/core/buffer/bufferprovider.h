#ifndef BUFFERPROVIDER_H
#define BUFFERPROVIDER_H

#include <QObject>
#include <QDateTime>

#include "buffer.h"

namespace vnotex
{
    class Node;

    // Content provider for Buffer.
    class BufferProvider : public QObject
    {
        Q_OBJECT
    public:
        BufferProvider(QObject *p_parent = nullptr)
            : QObject(p_parent)
        {
        }

        virtual ~BufferProvider() {}

        virtual Buffer::ProviderType getType() const = 0;

        virtual bool match(const Node *p_node) const = 0;

        virtual bool match(const QString &p_filePath) const = 0;

        virtual QString getName() const = 0;

        virtual QString getPath() const = 0;

        virtual QString getContentPath() const = 0;

        virtual QString getResourcePath() const = 0;

        virtual void write(const QString &p_content) = 0;

        virtual QString read() const = 0;

        virtual QString fetchImageFolderPath() = 0;

        virtual bool isChildOf(const Node *p_node) const = 0;

        virtual Node *getNode() const = 0;

        virtual QString getAttachmentFolder() const = 0;

        virtual QString fetchAttachmentFolderPath() = 0;

        virtual QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files) = 0;

        virtual QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) = 0;

        virtual QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) = 0;

        virtual QString renameAttachment(const QString &p_path, const QString &p_name) = 0;

        virtual void removeAttachment(const QStringList &p_paths) = 0;

        virtual QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) = 0;

        virtual QString insertImage(const QImage &p_image, const QString &p_imageFileName) = 0;

        virtual void removeImage(const QString &p_imagePath) = 0;

        virtual bool isAttachmentSupported() const = 0;

        virtual bool checkFileExistsOnDisk() const;

        virtual bool checkFileChangedOutside() const;

        virtual bool isReadOnly() const = 0;

    protected:
        virtual QDateTime getLastModifiedFromFile() const;

        QDateTime m_lastModified;
    };
}

#endif // BUFFERPROVIDER_H
