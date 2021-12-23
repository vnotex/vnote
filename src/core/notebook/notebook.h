#ifndef NOTEBOOK_H
#define NOTEBOOK_H

#include <QObject>
#include <QIcon>
#include <QSharedPointer>

#include "notebookparameters.h"
#include <core/global.h>
#include "node.h"

namespace vnotex
{
    class INotebookBackend;
    class IVersionController;
    class INotebookConfigMgr;
    class NodeParameters;
    class File;
    class HistoryI;
    class TagI;

    // Base class of notebook.
    class Notebook : public QObject
    {
        Q_OBJECT
    public:
        Notebook(const NotebookParameters &p_paras,
                 QObject *p_parent = nullptr);

        // Used for UT only.
        Notebook(const QString &p_name, QObject *p_parent = nullptr);

        virtual ~Notebook();

        void initialize();

        enum { InvalidId = 0 };

        ID getId() const;

        const QString &getType() const;

        const QString &getName() const;
        void setName(const QString &p_name);
        // Change the config and backend file as well.
        void updateName(const QString &p_name);

        const QString &getDescription() const;
        void setDescription(const QString &p_description);
        void updateDescription(const QString &p_description);

        // Use getRootFolderAbsolutePath() instead for access.
        const QString &getRootFolderPath() const;

        QString getRootFolderAbsolutePath() const;

        // Get the absolute path of the config folder if applicable.
        QString getConfigFolderAbsolutePath() const;

        const QIcon &getIcon() const;
        void setIcon(const QIcon &p_icon);

        const QString &getImageFolder() const;

        const QString &getAttachmentFolder() const;

        const QString &getRecycleBinFolder() const;

        QString getRecycleBinFolderAbsolutePath() const;

        const QDateTime &getCreatedTimeUtc() const;

        const QSharedPointer<INotebookBackend> &getBackend() const;

        const QSharedPointer<IVersionController> &getVersionController() const;

        const QSharedPointer<INotebookConfigMgr> &getConfigMgr() const;

        const QSharedPointer<Node> &getRootNode() const;

        QSharedPointer<Node> newNode(Node *p_parent,
                                     Node::Flags p_flags,
                                     const QString &p_name,
                                     const QString &p_content = QString());

        // Add @p_name under @p_parent to add as a new node @p_type.
        QSharedPointer<Node> addAsNode(Node *p_parent,
                                       Node::Flags p_flags,
                                       const QString &p_name,
                                       const NodeParameters &p_paras);

        // Copy @p_path to @p_parent and add as a new node @p_type.
        QSharedPointer<Node> copyAsNode(Node *p_parent,
                                        Node::Flags p_flags,
                                        const QString &p_path);

        virtual void updateNotebookConfig() = 0;

        virtual void removeNotebookConfig() = 0;

        // @p_path could be absolute or relative.
        virtual QSharedPointer<Node> loadNodeByPath(const QString &p_path);

        // Copy @p_src as a child of @p_dest. They may belong to different notebooks.
        virtual QSharedPointer<Node> copyNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest, bool p_move);

        // Remove @p_node and delete all related files from disk.
        // @p_force: if true, will delete all files including files not tracked by configmgr.
        // @p_configOnly: if true, will just remove node from config.
        void removeNode(const QSharedPointer<Node> &p_node, bool p_force = false, bool p_configOnly = false);

        void removeNode(Node *p_node, bool p_force = false, bool p_configOnly = false);

        void moveNodeToRecycleBin(const QSharedPointer<Node> &p_node);

        void moveNodeToRecycleBin(Node *p_node);

        // Move @p_filePath to the recycle bin, without adding it as a child node.
        void moveFileToRecycleBin(const QString &p_filePath);

        // Move @p_dirPath to the recycle bin, without adding it as a child node.
        void moveDirToRecycleBin(const QString &p_dirPath);

        virtual void emptyRecycleBin();

        // Remove all files of this notebook from disk.
        virtual void remove() = 0;

        // Whether @p_name is a built-in file under @p_node.
        bool isBuiltInFile(const Node *p_node, const QString &p_name) const;

        bool isBuiltInFolder(const Node *p_node, const QString &p_name) const;

        void reloadNodes();

        // Hold extra 3rd party configs.
        virtual const QJsonObject &getExtraConfigs() const = 0;
        QJsonObject getExtraConfig(const QString &p_key) const;
        virtual void setExtraConfig(const QString &p_key, const QJsonObject &p_obj) = 0;

        // Get content files recursively.
        QList<QSharedPointer<File>> collectFiles();

        QStringList scanAndImportExternalFiles();

        virtual bool rebuildDatabase();

        static const QString c_defaultAttachmentFolder;

        static const QString c_defaultImageFolder;

        static const QString c_defaultRecycleBinFolder;

    public:
        // Return null if history is not suported.
        virtual HistoryI *history();

        // Return null if tag is not suported.
        virtual TagI *tag();

    signals:
        void updated();

        void nodeUpdated(const Node *p_node);

        void tagsUpdated();

    protected:
        virtual void initializeInternal() = 0;

    private:
        QString getOrCreateRecycleBinDateFolder();

        bool m_initialized = false;

        // ID of this notebook.
        // Will be assigned uniquely once loaded.
        ID m_id;

        // Type of this notebook.
        QString m_type;

        // Name of this notebook.
        QString m_name;

        // Description of this notebook.
        QString m_description;

        // Path of the notebook root folder.
        QString m_rootFolderPath;

        QIcon m_icon;

        // Name of the folder to hold images.
        QString m_imageFolder;

        // Name of the folder to hold attachments.
        QString m_attachmentFolder;

        // Name or path of the folder to hold deleted files.
        QString m_recycleBinFolder;

        QDateTime m_createdTimeUtc;

        // Backend for file access and synchronization.
        QSharedPointer<INotebookBackend> m_backend;

        // Version controller.
        QSharedPointer<IVersionController> m_versionController;

        // Config manager to read/wirte config files.
        QSharedPointer<INotebookConfigMgr> m_configMgr;

        QSharedPointer<Node> m_root;
    };
} // ns vnotex

#endif // NOTEBOOK_H
