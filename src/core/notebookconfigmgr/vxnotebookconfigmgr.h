#ifndef VXNOTEBOOKCONFIGMGR_H
#define VXNOTEBOOKCONFIGMGR_H

#include "bundlenotebookconfigmgr.h"

#include <QDateTime>
#include <QVector>

#include "../global.h"

class QJsonObject;

namespace vnotex
{
    // Config manager for VNoteX's bundle notebook.
    class VXNotebookConfigMgr : public BundleNotebookConfigMgr
    {
        Q_OBJECT
    public:
        explicit VXNotebookConfigMgr(const QString &p_name,
                                     const QString &p_displayName,
                                     const QString &p_description,
                                     const QSharedPointer<INotebookBackend> &p_backend,
                                     QObject *p_parent = nullptr);

        QString getName() const Q_DECL_OVERRIDE;

        QString getDisplayName() const Q_DECL_OVERRIDE;

        QString getDescription() const Q_DECL_OVERRIDE;

        void createEmptySkeleton(const NotebookParameters &p_paras) Q_DECL_OVERRIDE;

        QSharedPointer<Node> loadRootNode() const Q_DECL_OVERRIDE;

        void loadNode(Node *p_node) const Q_DECL_OVERRIDE;
        void saveNode(const Node *p_node) Q_DECL_OVERRIDE;

        void renameNode(Node *p_node, const QString &p_name) Q_DECL_OVERRIDE;

        QSharedPointer<Node> newNode(Node *p_parent,
                                     Node::Flags p_flags,
                                     const QString &p_name) Q_DECL_OVERRIDE;

        QSharedPointer<Node> addAsNode(Node *p_parent,
                                       Node::Flags p_flags,
                                       const QString &p_name,
                                       const NodeParameters &p_paras) Q_DECL_OVERRIDE;

        QSharedPointer<Node> copyAsNode(Node *p_parent,
                                        Node::Flags p_flags,
                                        const QString &p_path) Q_DECL_OVERRIDE;

        QSharedPointer<Node> loadNodeByPath(const QSharedPointer<Node> &p_root,
                                            const QString &p_relativePath) Q_DECL_OVERRIDE;

        QSharedPointer<Node> copyNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                               Node *p_dest,
                                               bool p_move) Q_DECL_OVERRIDE;

        void removeNode(const QSharedPointer<Node> &p_node, bool p_force = false, bool p_configOnly = false) Q_DECL_OVERRIDE;

        bool isBuiltInFile(const Node *p_node, const QString &p_name) const Q_DECL_OVERRIDE;

        bool isBuiltInFolder(const Node *p_node, const QString &p_name) const Q_DECL_OVERRIDE;

        QString fetchNodeImageFolderPath(Node *p_node);

        QString fetchNodeAttachmentFolderPath(Node *p_node) Q_DECL_OVERRIDE;

    private:
        // Config of a file child.
        struct NodeFileConfig
        {
            QJsonObject toJson() const;

            void fromJson(const QJsonObject &p_jobj);

            QString m_name;
            ID m_id = Node::InvalidId;
            QDateTime m_createdTimeUtc;
            QDateTime m_modifiedTimeUtc;
            QString m_attachmentFolder;
            QStringList m_tags;
        };

        // Config of a folder child.
        struct NodeFolderConfig
        {
            QJsonObject toJson() const;

            void fromJson(const QJsonObject &p_jobj);

            QString m_name;
        };

        // Config of a folder node.
        struct NodeConfig
        {
            NodeConfig();

            NodeConfig(const QString &p_version,
                       ID p_id,
                       const QDateTime &p_createdTimeUtc,
                       const QDateTime &p_modifiedTimeUtc);

            QJsonObject toJson() const;

            void fromJson(const QJsonObject &p_jobj);

            QString m_version;
            ID m_id = Node::InvalidId;
            QDateTime m_createdTimeUtc;
            QDateTime m_modifiedTimeUtc;
            QVector<NodeFileConfig> m_files;
            QVector<NodeFolderConfig> m_folders;

            static const QString c_version;

            static const QString c_id;

            static const QString c_createdTimeUtc;

            static const QString c_files;

            static const QString c_folders;

            static const QString c_name;

            static const QString c_modifiedTimeUtc;

            static const QString c_attachmentFolder;

            static const QString c_tags;
        };

        void createEmptyRootNode();

        QSharedPointer<VXNotebookConfigMgr::NodeConfig> readNodeConfig(const QString &p_path) const;
        void writeNodeConfig(const QString &p_path, const NodeConfig &p_config) const;

        void writeNodeConfig(const Node *p_node);

        QSharedPointer<Node> nodeConfigToNode(const NodeConfig &p_config,
                                              const QString &p_name,
                                              Node *p_parent = nullptr) const;

        void loadFolderNode(Node *p_node, const NodeConfig &p_config) const;

        QSharedPointer<VXNotebookConfigMgr::NodeConfig> nodeToNodeConfig(const Node *p_node) const;

        QSharedPointer<Node> newFileNode(Node *p_parent,
                                         const QString &p_name,
                                         bool p_create,
                                         const NodeParameters &p_paras);

        QSharedPointer<Node> newFolderNode(Node *p_parent,
                                           const QString &p_name,
                                           bool p_create,
                                           const NodeParameters &p_paras);

        QString getNodeConfigFilePath(const Node *p_node) const;

        void addChildNode(Node *p_parent, const QSharedPointer<Node> &p_child) const;

        QSharedPointer<Node> copyFileNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest, bool p_move);

        QSharedPointer<Node> copyFolderNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest, bool p_move);

        QSharedPointer<Node> copyFileAsChildOf(const QString &p_srcPath, Node *p_dest);

        QSharedPointer<Node> copyFolderAsChildOf(const QString &p_srcPath, Node *p_dest);

        void removeFilesOfNode(Node *p_node, bool p_force);

        bool markRecycleBinNode(const QSharedPointer<Node> &p_root) const;

        void markNodeReadOnly(Node *p_node) const;

        void createRecycleBinNode(const QSharedPointer<Node> &p_root);

        // Generate node attachment folder.
        // @p_folderName: suggested folder name if not empty, may be renamed due to conflicts.
        // Return the attachment folder path.
        QString fetchNodeAttachmentFolder(const QString &p_nodePath, QString &p_folderName);

        void inheritNodeFlags(const Node *p_node, Node *p_child) const;

        Info m_info;

        // Name of the node's config file.
        static const QString c_nodeConfigName;

        // Name of the recycle bin folder which should be a child of the root node.
        static const QString c_recycleBinFolderName;
    };
} // ns vnotex

#endif // VXNOTEBOOKCONFIGMGR_H
