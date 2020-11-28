#ifndef NODE_H
#define NODE_H

#include <QDateTime>
#include <QVector>
#include <QSharedPointer>
#include <QDir>

#include <global.h>

namespace vnotex
{
    class Notebook;
    class INotebookConfigMgr;
    class INotebookBackend;

    // Used when add/new a node.
    struct NodeParameters
    {
        QDateTime m_createdTimeUtc = QDateTime::currentDateTimeUtc();
        QDateTime m_modifiedTimeUtc = QDateTime::currentDateTimeUtc();
        QString m_attachmentFolder;
        QStringList m_tags;
    };

    // Node of notebook.
    class Node
    {
    public:
        enum Type {
            Folder,
            File
        };

        enum Flag {
            None = 0,
            ReadOnly = 0x1
        };
        Q_DECLARE_FLAGS(Flags, Flag)

        enum Use {
            Normal,
            RecycleBin
        };

        // Constructor with all information loaded.
        Node(Type p_type,
             ID p_id,
             const QString &p_name,
             const QDateTime &p_createdTimeUtc,
             Notebook *p_notebook,
             Node *p_parent = nullptr);

        // Constructor not loaded.
        Node(Type p_type,
             const QString &p_name,
             Notebook *p_notebook,
             Node *p_parent = nullptr);

        virtual ~Node();

        enum { InvalidId = 0 };

        bool isLoaded() const;

        bool isRoot() const;

        const QString &getName() const;
        void setName(const QString &p_name);

        // Change the config and backend file as well.
        void updateName(const QString &p_name);

        Node::Type getType() const;

        Node::Flags getFlags() const;
        void setFlags(Node::Flags p_flags);

        Node::Use getUse() const;
        void setUse(Node::Use p_use);

        ID getId() const;

        const QDateTime &getCreatedTimeUtc() const;

        virtual QDateTime getModifiedTimeUtc() const = 0;
        virtual void setModifiedTimeUtc() = 0;

        virtual QString getAttachmentFolder() const;
        virtual void setAttachmentFolder(const QString &p_folder);

        virtual QVector<QSharedPointer<Node>> getChildren() const = 0;

        virtual int getChildrenCount() const = 0;

        QSharedPointer<Node> findChild(const QString &p_name, bool p_caseSensitive = true) const;

        bool hasChild(const QString &p_name, bool p_caseSensitive = true) const;

        bool hasChild(const QSharedPointer<Node> &p_node) const;

        virtual void addChild(const QSharedPointer<Node> &p_node) = 0;

        virtual void insertChild(int p_idx, const QSharedPointer<Node> &p_node) = 0;

        virtual void removeChild(const QSharedPointer<Node> &p_node) = 0;

        void setParent(Node *p_parent);
        Node *getParent() const;

        Notebook *getNotebook() const;

        // Path to the node.
        QString fetchRelativePath() const;

        QString fetchAbsolutePath() const;

        // A node may be a container of all the stuffs, so the node's path may not be identical with
        // the content file path, like TextBundle.
        virtual QString fetchContentPath() const;

        // Get image folder path.
        virtual QString fetchImageFolderPath();

        virtual void load();
        virtual void save();

        static bool isAncestor(const Node *p_ancestor, const Node *p_child);

        bool existsOnDisk() const;

        QString read() const;
        void write(const QString &p_content);

        // Insert image from @p_srcImagePath.
        // Return inserted image file path.
        virtual QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName);

        virtual QString insertImage(const QImage &p_image, const QString &p_imageFileName);

        virtual void removeImage(const QString &p_imagePath);

        // Get attachment folder path.
        virtual QString fetchAttachmentFolderPath();

        virtual QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files);

        virtual QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name);

        virtual QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name);

        virtual QString renameAttachment(const QString &p_path, const QString &p_name);

        virtual void removeAttachment(const QStringList &p_paths);

        virtual QStringList getTags() const;

        virtual QDir toDir() const;

        INotebookBackend *getBackend() const;

        bool isReadOnly() const;

    protected:
        void loadInfo(ID p_id, const QDateTime &p_createdTimeUtc);

        void setLoaded(bool p_loaded);

        Notebook *m_notebook = nullptr;

        QSharedPointer<INotebookConfigMgr> m_configMgr;

        QSharedPointer<INotebookBackend> m_backend;

    private:
        Type m_type = Type::Folder;

        Flags m_flags = Flag::None;

        Use m_use = Use::Normal;

        ID m_id = InvalidId;

        QString m_name;

        QDateTime m_createdTimeUtc;

        bool m_loaded = false;

        Node *m_parent = nullptr;
    };

    Q_DECLARE_OPERATORS_FOR_FLAGS(Node::Flags)
} // ns vnotex

#endif // NODE_H
