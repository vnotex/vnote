#ifndef FILENODE_H
#define FILENODE_H

#include "node.h"

namespace vnotex
{
    // File node of notebook.
    class FileNode : public Node
    {
    public:
        FileNode(ID p_id,
                 const QString &p_name,
                 const QDateTime &p_createdTimeUtc,
                 const QDateTime &p_modifiedTimeUtc,
                 const QString &p_attachmentFolder,
                 const QStringList &p_tags,
                 Notebook *p_notebook,
                 Node *p_parent = nullptr);

        QVector<QSharedPointer<Node>> getChildren() const Q_DECL_OVERRIDE;

        int getChildrenCount() const Q_DECL_OVERRIDE;

        void addChild(const QSharedPointer<Node> &p_node) Q_DECL_OVERRIDE;

        void insertChild(int p_idx, const QSharedPointer<Node> &p_node) Q_DECL_OVERRIDE;

        void removeChild(const QSharedPointer<Node> &p_child) Q_DECL_OVERRIDE;

        QDateTime getModifiedTimeUtc() const Q_DECL_OVERRIDE;

        void setModifiedTimeUtc() Q_DECL_OVERRIDE;

        QString getAttachmentFolder() const Q_DECL_OVERRIDE;

        void setAttachmentFolder(const QString &p_folder) Q_DECL_OVERRIDE;

        QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files) Q_DECL_OVERRIDE;

        QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

        QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

        QString renameAttachment(const QString &p_path, const QString &p_name) Q_DECL_OVERRIDE;

        void removeAttachment(const QStringList &p_paths) Q_DECL_OVERRIDE;

        QStringList getTags() const Q_DECL_OVERRIDE;

    private:
        QDateTime m_modifiedTimeUtc;

        QString m_attachmentFolder;

        QStringList m_tags;
    };
}

#endif // FILENODE_H
