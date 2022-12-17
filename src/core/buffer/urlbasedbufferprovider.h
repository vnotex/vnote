#ifndef URLBASEDBUFFERPROVIDER_H
#define URLBASEDBUFFERPROVIDER_H

#include "bufferprovider.h"

#include <QSharedPointer>

namespace vnotex
{
    // A wrapper provider to provide URL-based buffer (instead of content-based).
    class UrlBasedBufferProvider : public BufferProvider
    {
        Q_OBJECT
    public:
        // Will own @p_provider.
        UrlBasedBufferProvider(const QSharedPointer<BufferProvider> &p_provider, QObject *p_parent = nullptr)
            : BufferProvider(p_parent),
              m_provider(p_provider)
        {
        }

        Buffer::ProviderType getType() const Q_DECL_OVERRIDE {
            return m_provider->getType();
        }

        bool match(const Node *p_node) const Q_DECL_OVERRIDE {
            return m_provider->match(p_node);
        }

        bool match(const QString &p_filePath) const Q_DECL_OVERRIDE {
            return m_provider->match(p_filePath);
        }

        QString getName() const Q_DECL_OVERRIDE {
            return m_provider->getName();
        }

        QString getPath() const Q_DECL_OVERRIDE {
            return m_provider->getPath();
        }

        QString getContentPath() const Q_DECL_OVERRIDE {
            return m_provider->getContentPath();
        }

        QString getResourcePath() const Q_DECL_OVERRIDE {
            return m_provider->getResourcePath();
        }

        void write(const QString &p_content) Q_DECL_OVERRIDE {
            Q_UNUSED(p_content);
        }

        QString read() const Q_DECL_OVERRIDE {
            const_cast<UrlBasedBufferProvider *>(this)->m_lastModified = getLastModifiedFromFile();
            return QString();
        }

        QString fetchImageFolderPath() Q_DECL_OVERRIDE {
            return m_provider->fetchImageFolderPath();
        }

        bool isChildOf(const Node *p_node) const Q_DECL_OVERRIDE {
            return m_provider->isChildOf(p_node);
        }

        Node *getNode() const Q_DECL_OVERRIDE {
            return m_provider->getNode();
        }

        QString getAttachmentFolder() const Q_DECL_OVERRIDE {
            return m_provider->getAttachmentFolder();
        }

        QString fetchAttachmentFolderPath() Q_DECL_OVERRIDE {
            return m_provider->fetchAttachmentFolderPath();
        }

        QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files) Q_DECL_OVERRIDE {
            return m_provider->addAttachment(p_destFolderPath, p_files);
        }

        QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE {
            return m_provider->newAttachmentFile(p_destFolderPath, p_name);
        }

        QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE {
            return m_provider->newAttachmentFolder(p_destFolderPath, p_name);
        }

        QString renameAttachment(const QString &p_path, const QString &p_name) Q_DECL_OVERRIDE {
            return m_provider->renameAttachment(p_path, p_name);
        }

        void removeAttachment(const QStringList &p_paths) Q_DECL_OVERRIDE {
            m_provider->removeAttachment(p_paths);
        }

        QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) Q_DECL_OVERRIDE {
            return m_provider->insertImage(p_srcImagePath, p_imageFileName);
        }

        QString insertImage(const QImage &p_image, const QString &p_imageFileName) Q_DECL_OVERRIDE {
            return m_provider->insertImage(p_image, p_imageFileName);
        }

        void removeImage(const QString &p_imagePath) Q_DECL_OVERRIDE {
            m_provider->removeImage(p_imagePath);
        }

        bool isAttachmentSupported() const Q_DECL_OVERRIDE {
            return m_provider->isAttachmentSupported();
        }

        bool isTagSupported() const Q_DECL_OVERRIDE {
            return m_provider->isTagSupported();
        }

        bool isReadOnly() const Q_DECL_OVERRIDE {
            return true;
        }

        QSharedPointer<File> getFile() const Q_DECL_OVERRIDE {
            return m_provider->getFile();
        }

    private:
        QSharedPointer<BufferProvider> m_provider;
    };
}

#endif // URLBASEDBUFFERPROVIDER_H
