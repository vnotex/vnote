#include "vxnotebookconfigmgr.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <QSet>

#include <notebookbackend/inotebookbackend.h>
#include <notebook/notebookparameters.h>
#include <notebook/vxnode.h>
#include <notebook/externalnode.h>
#include <notebook/bundlenotebook.h>
#include <notebook/notebookdatabaseaccess.h>
#include <utils/utils.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <exception.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <core/coreconfig.h>

#include <utils/contentmediautils.h>

#include "vxnodeconfig.h"
#include "vxnotebookconfigmgrfactory.h"

using namespace vnotex;

using namespace vnotex::vx_node_config;

const QString VXNotebookConfigMgr::c_nodeConfigName = "vx.json";

bool VXNotebookConfigMgr::s_initialized = false;

QVector<QRegExp> VXNotebookConfigMgr::s_externalNodeExcludePatterns;

VXNotebookConfigMgr::VXNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend, QObject *p_parent)
    : BundleNotebookConfigMgr(p_backend, p_parent)
{
    if (!s_initialized) {
        s_initialized = true;

        const auto &patterns = ConfigMgr::getInst().getCoreConfig().getExternalNodeExcludePatterns();
        s_externalNodeExcludePatterns.reserve(patterns.size());
        for (const auto &pat : patterns) {
            if (!pat.isEmpty()) {
                s_externalNodeExcludePatterns.push_back(QRegExp(pat, Qt::CaseInsensitive, QRegExp::Wildcard));
            }
        }
    }
}

QString VXNotebookConfigMgr::getName() const
{
    return VXNotebookConfigMgrFactory::c_name;
}

QString VXNotebookConfigMgr::getDisplayName() const
{
    return VXNotebookConfigMgrFactory::c_displayName;
}

QString VXNotebookConfigMgr::getDescription() const
{
    return VXNotebookConfigMgrFactory::c_description;
}

void VXNotebookConfigMgr::createEmptySkeleton(const NotebookParameters &p_paras)
{
    BundleNotebookConfigMgr::createEmptySkeleton(p_paras);

    createEmptyRootNode();
}

void VXNotebookConfigMgr::createEmptyRootNode()
{
    auto currentTime = QDateTime::currentDateTimeUtc();
    NodeConfig node(getCodeVersion(),
                    Node::InvalidId,
                    Node::InvalidId,
                    currentTime,
                    currentTime);
    writeNodeConfig(c_nodeConfigName, node);
}

QSharedPointer<Node> VXNotebookConfigMgr::loadRootNode()
{
    auto nodeConfig = readNodeConfig("");
    QSharedPointer<Node> root = nodeConfigToNode(*nodeConfig, "", nullptr);
    root->setUse(Node::Use::Root);
    root->setExists(true);
    Q_ASSERT(root->isLoaded());

    if (static_cast<BundleNotebook *>(getNotebook())->getConfigVersion() < 3) {
        removeLegacyRecycleBinNode(root);
    }

    return root;
}

void VXNotebookConfigMgr::removeLegacyRecycleBinNode(const QSharedPointer<Node> &p_root)
{
    // Do not support recycle bin node as it complicates everything.
    auto node = p_root->findChild(QStringLiteral("vx_recycle_bin"),
                                  FileUtils::isPlatformNameCaseSensitive());
    if (node) {
        removeNode(node, true, true);
    }
}

void VXNotebookConfigMgr::markNodeReadOnly(Node *p_node) const
{
    if (p_node->isReadOnly()) {
        return;
    }

    p_node->setReadOnly(true);
    for (const auto &child : p_node->getChildrenRef()) {
        markNodeReadOnly(child.data());
    }
}

QSharedPointer<NodeConfig> VXNotebookConfigMgr::readNodeConfig(const QString &p_path) const
{
    auto backend = getBackend();
    if (!backend->exists(p_path)) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("node path (%1) does not exist").arg(p_path));
    }

    if (backend->isFile(p_path)) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("node (%1) is a file node without config").arg(p_path));
    } else {
        auto configPath = PathUtils::concatenateFilePath(p_path, c_nodeConfigName);
        auto data = backend->readFile(configPath);
        auto nodeConfig = QSharedPointer<NodeConfig>::create();
        nodeConfig->fromJson(QJsonDocument::fromJson(data).object());
        return nodeConfig;
    }

    return nullptr;
}

QString VXNotebookConfigMgr::getNodeConfigFilePath(const Node *p_node) const
{
    Q_ASSERT(p_node->isContainer());
    return PathUtils::concatenateFilePath(p_node->fetchPath(), c_nodeConfigName);
}

void VXNotebookConfigMgr::writeNodeConfig(const QString &p_path, const NodeConfig &p_config) const
{
    getBackend()->writeFile(p_path, p_config.toJson());
}

void VXNotebookConfigMgr::writeNodeConfig(const Node *p_node)
{
    auto config = nodeToNodeConfig(p_node);
    writeNodeConfig(getNodeConfigFilePath(p_node), *config);
}

QSharedPointer<Node> VXNotebookConfigMgr::nodeConfigToNode(const NodeConfig &p_config,
                                                           const QString &p_name,
                                                           Node *p_parent)
{
    auto node = QSharedPointer<VXNode>::create(p_name, getNotebook(), p_parent);
    loadFolderNode(node.data(), p_config);
    return node;
}

void VXNotebookConfigMgr::loadFolderNode(Node *p_node, const NodeConfig &p_config)
{
    QSet<QString> seenNames;

    QVector<QSharedPointer<Node>> children;
    children.reserve(p_config.m_files.size() + p_config.m_folders.size());
    const auto basePath = p_node->fetchPath();

    bool needUpdateConfig = false;

    for (const auto &folder : p_config.m_folders) {
        if (folder.m_name.isEmpty()) {
            // Skip empty name node.
            qWarning() << "skipped loading node with empty name under" << p_node->fetchPath();
            continue;
        }

        if (seenNames.contains(folder.m_name)) {
            qWarning() << "skipped loading node with duplicated name under" << p_node->fetchPath();
            continue;
        }
        seenNames.insert(folder.m_name);

        auto folderNode = QSharedPointer<VXNode>::create(folder.m_name,
                                                         getNotebook(),
                                                         p_node);
        inheritNodeFlags(p_node, folderNode.data());
        folderNode->setExists(getBackend()->existsDir(PathUtils::concatenateFilePath(basePath, folder.m_name)));
        children.push_back(folderNode);
    }

    for (const auto &file : p_config.m_files) {
        if (file.m_name.isEmpty()) {
            // Skip empty name node.
            qWarning() << "skipped loading node with empty name under" << p_node->fetchPath();
            continue;
        }

        if (seenNames.contains(file.m_name)) {
            qWarning() << "skipped loading node with duplicated name under" << p_node->fetchPath();
            continue;
        }
        seenNames.insert(file.m_name);

        // For compability only.
        needUpdateConfig = needUpdateConfig || file.m_signature == Node::InvalidId;

        auto fileNode = QSharedPointer<VXNode>::create(file.m_name,
                                                       file.toNodeParameters(),
                                                       getNotebook(),
                                                       p_node);
        inheritNodeFlags(p_node, fileNode.data());
        fileNode->setExists(getBackend()->existsFile(PathUtils::concatenateFilePath(basePath, file.m_name)));
        children.push_back(fileNode);
    }

    p_node->loadCompleteInfo(p_config.toNodeParameters(), children);

    needUpdateConfig = needUpdateConfig || p_config.m_signature == Node::InvalidId;
    if (needUpdateConfig) {
        writeNodeConfig(p_node);
    }
}

QSharedPointer<Node> VXNotebookConfigMgr::newNode(Node *p_parent,
                                                  Node::Flags p_flags,
                                                  const QString &p_name,
                                                  const QString &p_content)
{
    Q_ASSERT(p_parent && p_parent->isContainer() && !p_name.isEmpty());

    QSharedPointer<Node> node;

    if (p_flags & Node::Flag::Content) {
        Q_ASSERT(!(p_flags & Node::Flag::Container));
        node = newFileNode(p_parent, p_name, p_content, true, NodeParameters());
    } else {
        node = newFolderNode(p_parent, p_name, true, NodeParameters());
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::addAsNode(Node *p_parent,
                                                    Node::Flags p_flags,
                                                    const QString &p_name,
                                                    const NodeParameters &p_paras)
{
    Q_ASSERT(p_parent && p_parent->isContainer());

    // TODO: reuse the config if available.
    QSharedPointer<Node> node;
    if (p_flags & Node::Flag::Content) {
        Q_ASSERT(!(p_flags & Node::Flag::Container));
        node = newFileNode(p_parent, p_name, "", false, p_paras);
    } else {
        node = newFolderNode(p_parent, p_name, false, p_paras);
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyAsNode(Node *p_parent,
                                                     Node::Flags p_flags,
                                                     const QString &p_path)
{
    Q_ASSERT(p_parent && p_parent->isContainer());

    QSharedPointer<Node> node;
    if (p_flags & Node::Flag::Content) {
        Q_ASSERT(!(p_flags & Node::Flag::Container));
        node = copyFileAsChildOf(p_path, p_parent);
    } else {
        node = copyFolderAsChildOf(p_path, p_parent);
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::newFileNode(Node *p_parent,
                                                      const QString &p_name,
                                                      const QString &p_content,
                                                      bool p_create,
                                                      const NodeParameters &p_paras)
{
    ensureNodeInDatabase(p_parent);

    auto notebook = getNotebook();

    // Create file node.
    auto node = QSharedPointer<VXNode>::create(p_name,
                                               p_paras,
                                               notebook,
                                               p_parent);

    // Write empty file.
    if (p_create) {
        if (getBackend()->childExistsCaseInsensitive(p_parent->fetchPath(), p_name)) {
            // File already exists. Exception.
            Exception::throwOne(Exception::Type::FileExistsOnCreate,
                                QString("file (%1) already exists when creating new node").arg(node->fetchPath()));
            return nullptr;
        }

        getBackend()->writeFile(node->fetchPath(), p_content);
        node->setExists(true);
    } else {
        node->setExists(getBackend()->existsFile(node->fetchPath()));
    }

    addChildNode(p_parent, node);
    writeNodeConfig(p_parent);

    addNodeToDatabase(node.data());

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::newFolderNode(Node *p_parent,
                                                        const QString &p_name,
                                                        bool p_create,
                                                        const NodeParameters &p_paras)
{
    ensureNodeInDatabase(p_parent);

    auto notebook = getNotebook();

    // Create folder node.
    auto node = QSharedPointer<VXNode>::create(p_name, notebook, p_parent);
    node->loadCompleteInfo(p_paras, QVector<QSharedPointer<Node>>());

    // Make folder.
    if (p_create) {
        if (getBackend()->childExistsCaseInsensitive(p_parent->fetchPath(), p_name)) {
            // Dir already exists. Exception.
            Exception::throwOne(Exception::Type::DirExistsOnCreate,
                                QString("dir (%1) already exists when creating new node").arg(node->fetchPath()));
            return nullptr;
        }

        getBackend()->makePath(node->fetchPath());
        node->setExists(true);
    } else {
        node->setExists(getBackend()->existsDir(node->fetchPath()));
    }

    writeNodeConfig(node.data());

    addChildNode(p_parent, node);
    writeNodeConfig(p_parent);

    addNodeToDatabase(node.data());

    return node;
}

QSharedPointer<NodeConfig> VXNotebookConfigMgr::nodeToNodeConfig(const Node *p_node) const
{
    Q_ASSERT(p_node->isContainer());
    Q_ASSERT(p_node->isLoaded());

    auto config = QSharedPointer<NodeConfig>::create(getCodeVersion(),
                                                     p_node->getId(),
                                                     p_node->getSignature(),
                                                     p_node->getCreatedTimeUtc(),
                                                     p_node->getModifiedTimeUtc());

    for (const auto &child : p_node->getChildrenRef()) {
        if (child->hasContent()) {
            NodeFileConfig fileConfig;
            fileConfig.m_name = child->getName();
            fileConfig.m_id = child->getId();
            fileConfig.m_signature = child->getSignature();
            fileConfig.m_createdTimeUtc = child->getCreatedTimeUtc();
            fileConfig.m_modifiedTimeUtc = child->getModifiedTimeUtc();
            fileConfig.m_attachmentFolder = child->getAttachmentFolder();
            fileConfig.m_tags = child->getTags();

            config->m_files.push_back(fileConfig);
        } else {
            Q_ASSERT(child->isContainer());
            NodeFolderConfig folderConfig;
            folderConfig.m_name = child->getName();

            config->m_folders.push_back(folderConfig);
        }
    }

    return config;
}

void VXNotebookConfigMgr::loadNode(Node *p_node)
{
    if (p_node->isLoaded() || !p_node->exists()) {
        return;
    }

    auto config = readNodeConfig(p_node->fetchPath());
    Q_ASSERT(p_node->isContainer());
    loadFolderNode(p_node, *config);
}

void VXNotebookConfigMgr::saveNode(const Node *p_node)
{
    if (p_node->isContainer()) {
        writeNodeConfig(p_node);
    } else {
        Q_ASSERT(!p_node->isRoot());
        writeNodeConfig(p_node->getParent());
    }
}

void VXNotebookConfigMgr::renameNode(Node *p_node, const QString &p_name)
{
    Q_ASSERT(!p_node->isRoot());

    if (p_node->isContainer()) {
        getBackend()->renameDir(p_node->fetchPath(), p_name);
    } else {
        getBackend()->renameFile(p_node->fetchPath(), p_name);
    }

    p_node->setName(p_name);
    writeNodeConfig(p_node->getParent());

    ensureNodeInDatabase(p_node);
    updateNodeInDatabase(p_node);
}

// Do not touch DB here since it will be called at different scenarios.
void VXNotebookConfigMgr::addChildNode(Node *p_parent, const QSharedPointer<Node> &p_child) const
{
    if (p_child->isContainer()) {
        int idx = 0;
        const auto &children = p_parent->getChildrenRef();
        for (; idx < children.size(); ++idx) {
            if (!children[idx]->isContainer()) {
                break;
            }
        }

        p_parent->insertChild(idx, p_child);
    } else {
        p_parent->addChild(p_child);
    }

    inheritNodeFlags(p_parent, p_child.data());
}

QSharedPointer<Node> VXNotebookConfigMgr::loadNodeByPath(const QSharedPointer<Node> &p_root, const QString &p_relativePath)
{
    auto p = PathUtils::cleanPath(p_relativePath);
    if (p == ".") {
        return p_root;
    }

    auto paths = p.split('/', Qt::SkipEmptyParts);
    auto node = p_root;
    for (auto &pa : paths) {
        // Find child @pa in @node.
        if (!node->isLoaded()) {
            loadNode(node.data());
        }

        auto child = node->findChild(pa, FileUtils::isPlatformNameCaseSensitive());
        if (!child) {
            return nullptr;
        }

        node = child;
    }

    return node;
}

// @p_src may belong to different notebook or different kind of configmgr.
// TODO: we could constrain @p_src within the same configrmgr?
QSharedPointer<Node> VXNotebookConfigMgr::copyNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                                            Node *p_dest,
                                                            bool p_move)
{
    return copyNodeAsChildOf(p_src, p_dest, p_move, true);
}

QSharedPointer<Node> VXNotebookConfigMgr::copyNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                                            Node *p_dest,
                                                            bool p_move,
                                                            bool p_updateDatabase)
{
    Q_ASSERT(p_dest->isContainer());

    if (!p_src->exists()) {
        if (p_move) {
            // It is OK to always update the database.
            p_src->getNotebook()->removeNode(p_src);
        }
        return nullptr;
    }

    QSharedPointer<Node> node;
    if (p_src->isContainer()) {
        node = copyFolderNodeAsChildOf(p_src, p_dest, p_move, p_updateDatabase);
    } else {
        node = copyFileNodeAsChildOf(p_src, p_dest, p_move, p_updateDatabase);
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFileNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                                                Node *p_dest,
                                                                bool p_move,
                                                                bool p_updateDatabase)
{
    QString destFilePath;
    QString attachmentFolder;
    copyFilesOfFileNode(p_src, p_dest->fetchPath(), destFilePath, attachmentFolder);

    // Create a file node.
    auto notebook = getNotebook();
    const bool sameNotebook = p_src->getNotebook() == notebook;

    if (p_updateDatabase) {
        ensureNodeInDatabase(p_dest);
        if (sameNotebook) {
            ensureNodeInDatabase(p_src.data());
        }
    }

    NodeParameters paras;
    if (p_move && sameNotebook) {
        paras.m_id = p_src->getId();
        paras.m_signature = p_src->getSignature();
    }
    paras.m_createdTimeUtc = p_src->getCreatedTimeUtc();
    paras.m_modifiedTimeUtc = p_src->getModifiedTimeUtc();
    paras.m_tags = p_src->getTags();
    paras.m_attachmentFolder = attachmentFolder;
    auto destNode = QSharedPointer<VXNode>::create(PathUtils::fileName(destFilePath),
                                                   paras,
                                                   notebook,
                                                   p_dest);
    destNode->setExists(true);

    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    if (p_updateDatabase) {
        if (p_move && sameNotebook) {
            updateNodeInDatabase(destNode.data());
        } else {
            addNodeToDatabase(destNode.data());
        }

        Q_ASSERT(nodeExistsInDatabase(destNode.data()));
    }

    if (p_move) {
        if (sameNotebook) {
            // The same notebook. Do not directly call removeNode() since we already update the record
            // in database directly.
            removeNode(p_src, false, false, false);
        } else {
            p_src->getNotebook()->removeNode(p_src);
        }
    }

    return destNode;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFolderNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                                                  Node *p_dest,
                                                                  bool p_move,
                                                                  bool p_updateDatabase)
{
    auto destFolderPath = PathUtils::concatenateFilePath(p_dest->fetchPath(), p_src->getName());
    destFolderPath = getBackend()->renameIfExistsCaseInsensitive(destFolderPath);

    // Make folder.
    getBackend()->makePath(destFolderPath);

    // Create a folder node.
    auto notebook = getNotebook();
    const bool sameNotebook = p_src->getNotebook() == notebook;

    if (p_updateDatabase) {
        ensureNodeInDatabase(p_dest);
        if (sameNotebook) {
            ensureNodeInDatabase(p_src.data());
        }
    }

    auto destNode = QSharedPointer<VXNode>::create(PathUtils::fileName(destFolderPath),
                                                   notebook,
                                                   p_dest);
    {
        NodeParameters paras;
        if (p_move && sameNotebook) {
            paras.m_id = p_src->getId();
            paras.m_signature = p_src->getSignature();
        }
        paras.m_createdTimeUtc = p_src->getCreatedTimeUtc();
        paras.m_modifiedTimeUtc = p_src->getModifiedTimeUtc();
        destNode->loadCompleteInfo(paras, QVector<QSharedPointer<Node>>());
    }

    destNode->setExists(true);
    writeNodeConfig(destNode.data());

    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    if (p_updateDatabase) {
        if (p_move && sameNotebook) {
            p_updateDatabase = false;
            updateNodeInDatabase(destNode.data());
        } else {
            addNodeToDatabase(destNode.data());
        }
    }

    // Copy children node.
    auto children = p_src->getChildren();
    for (const auto &childNode : children) {
        copyNodeAsChildOf(childNode, destNode.data(), p_move, p_updateDatabase);
    }

    if (p_move) {
        if (sameNotebook) {
            removeNode(p_src, false, false, false);
        } else {
            p_src->getNotebook()->removeNode(p_src);
        }
    }

    return destNode;
}

void VXNotebookConfigMgr::removeNode(const QSharedPointer<Node> &p_node, bool p_force, bool p_configOnly)
{
    removeNode(p_node, p_force, p_configOnly, true);
}

void VXNotebookConfigMgr::removeNode(const QSharedPointer<Node> &p_node,
                                     bool p_force,
                                     bool p_configOnly,
                                     bool p_updateDatabase)
{
    if (!p_configOnly && p_node->exists()) {
        // Remove all children.
        auto children = p_node->getChildren();
        for (const auto &childNode : children) {
            // With DELETE CASCADE, we could just touch the DB at parent level.
            removeNode(childNode, p_force, p_configOnly, false);
        }

        try {
            removeFilesOfNode(p_node.data(), p_force);
        } catch (Exception &p_e) {
            qWarning() << "failed to remove files of node" << p_node->fetchPath() << p_e.what();
        }
    }

    if (p_updateDatabase) {
        // Remove it from data base before modifying the parent.
        removeNodeFromDatabase(p_node.data());
    }

    if (auto parentNode = p_node->getParent()) {
        parentNode->removeChild(p_node);
        writeNodeConfig(parentNode);
    }
}

void VXNotebookConfigMgr::removeFilesOfNode(Node *p_node, bool p_force)
{
    Q_ASSERT(p_node->getNotebook() == getNotebook());
    if (!p_node->isContainer()) {
        // Delete attachment.
        if (!p_node->getAttachmentFolder().isEmpty()) {
            getBackend()->removeDir(p_node->fetchAttachmentFolderPath());
        }

        // Delete media files fetched from content.
        ContentMediaUtils::removeMediaFiles(p_node);

        // Delete node file itself.
        auto filePath = p_node->fetchPath();
        getBackend()->removeFile(filePath);
    } else {
        Q_ASSERT(p_node->getChildrenCount() == 0);
        // Delete node config file and the dir if it is empty.
        auto configFilePath = getNodeConfigFilePath(p_node);
        getBackend()->removeFile(configFilePath);
        auto folderPath = p_node->fetchPath();
        if (p_force) {
            getBackend()->removeDir(folderPath);
        } else {
            getBackend()->removeEmptyDir(folderPath);
            bool deleted = getBackend()->removeDirIfEmpty(folderPath);
            if (!deleted) {
                qWarning() << "folder is not deleted since it is not empty" << folderPath;
            }
        }
    }
}

void VXNotebookConfigMgr::removeNodeToFolder(const QSharedPointer<Node> &p_node, const QString &p_destFolder)
{
    if (p_node->isContainer()) {
        removeFolderNodeToFolder(p_node, p_destFolder);
    } else {
        removeFileNodeToFolder(p_node, p_destFolder);
    }
}

void VXNotebookConfigMgr::removeFolderNodeToFolder(const QSharedPointer<Node> &p_node, const QString &p_destFolder)
{
    auto destFolderPath = PathUtils::concatenateFilePath(p_destFolder, p_node->getName());
    destFolderPath = getBackend()->renameIfExistsCaseInsensitive(destFolderPath);

    // Make folder.
    getBackend()->makePath(destFolderPath);

    // Children.
    auto children = p_node->getChildren();
    for (const auto &child : children) {
        removeNodeToFolder(child, destFolderPath);
    }

    removeNode(p_node, false, false);
}

void VXNotebookConfigMgr::removeFileNodeToFolder(const QSharedPointer<Node> &p_node, const QString &p_destFolder)
{
    // Use a wrapper folder.
    auto destFolderPath = PathUtils::concatenateFilePath(p_destFolder, p_node->getName());
    destFolderPath = getBackend()->renameIfExistsCaseInsensitive(destFolderPath);

    // Make folder.
    getBackend()->makePath(destFolderPath);

    QString destFilePath;
    QString attachmentFolder;
    copyFilesOfFileNode(p_node, destFolderPath, destFilePath, attachmentFolder);

    removeNode(p_node, false, false);
}

void VXNotebookConfigMgr::copyFilesOfFileNode(const QSharedPointer<Node> &p_node,
                                              const QString &p_destFolder,
                                              QString &p_destFilePath,
                                              QString &p_attachmentFolder)
{
    // Copy source file itself.
    auto nodeFilePath = p_node->fetchAbsolutePath();
    p_destFilePath = PathUtils::concatenateFilePath(p_destFolder, PathUtils::fileName(nodeFilePath));
    p_destFilePath = getBackend()->renameIfExistsCaseInsensitive(p_destFilePath);
    getBackend()->copyFile(nodeFilePath, p_destFilePath);

    // Copy media files fetched from content.
    ContentMediaUtils::copyMediaFiles(p_node.data(), getBackend().data(), p_destFilePath);

    // Copy attachment folder. Rename attachment folder if conflicts.
    p_attachmentFolder = p_node->getAttachmentFolder();
    if (!p_attachmentFolder.isEmpty()) {
        auto destAttachmentFolderPath = fetchNodeAttachmentFolder(p_destFilePath, p_attachmentFolder);
        ContentMediaUtils::copyAttachment(p_node.data(), getBackend().data(), p_destFilePath, destAttachmentFolderPath);
    }
}

QString VXNotebookConfigMgr::fetchNodeImageFolderPath(Node *p_node)
{
    auto pa = PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_node->fetchAbsolutePath()),
            getNotebook()->getImageFolder());
    // Do not make the folder when it is a folder node request.
    if (p_node->hasContent()) {
        getBackend()->makePath(pa);
    }
    return pa;
}

QString VXNotebookConfigMgr::fetchNodeAttachmentFolderPath(Node *p_node)
{
    auto notebookFolder = PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_node->fetchAbsolutePath()),
            getNotebook()->getAttachmentFolder());
    if (p_node->hasContent()) {
        auto nodeFolder = p_node->getAttachmentFolder();
        if (nodeFolder.isEmpty()) {
            auto folderPath = fetchNodeAttachmentFolder(p_node->fetchAbsolutePath(), nodeFolder);
            p_node->setAttachmentFolder(nodeFolder);
            saveNode(p_node);

            getBackend()->makePath(folderPath);
            return folderPath;
        } else {
            return PathUtils::concatenateFilePath(notebookFolder, nodeFolder);
        }
    } else {
        // Do not make the folder when it is a folder node request.
        return notebookFolder;
    }
}

QString VXNotebookConfigMgr::fetchNodeAttachmentFolder(const QString &p_nodePath, QString &p_folderName)
{
    auto notebookFolder = PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_nodePath),
                                                         getNotebook()->getAttachmentFolder());
    if (p_folderName.isEmpty()) {
        p_folderName = FileUtils::generateUniqueFileName(notebookFolder, QString(), QString());
    } else if (FileUtils::childExistsCaseInsensitive(notebookFolder, p_folderName)) {
        p_folderName = FileUtils::generateFileNameWithSequence(notebookFolder, p_folderName, QString());
    }
    return PathUtils::concatenateFilePath(notebookFolder, p_folderName);
}

bool VXNotebookConfigMgr::isBuiltInFile(const Node *p_node, const QString &p_name) const
{
    static const QString backupFileExtension = ConfigMgr::getInst().getEditorConfig().getBackupFileExtension().toLower();

    const auto name = p_name.toLower();
    if (name == c_nodeConfigName
        || name == QStringLiteral("_vnote.json")
        || name.endsWith(backupFileExtension)) {
        return true;
    }
    return BundleNotebookConfigMgr::isBuiltInFile(p_node, p_name);
}

bool VXNotebookConfigMgr::isBuiltInFolder(const Node *p_node, const QString &p_name) const
{
    const auto name = p_name.toLower();
    const auto &nb = getNotebook();
    if (name == nb->getImageFolder().toLower()
        || name == nb->getAttachmentFolder().toLower()
        || name == QStringLiteral("_v_images")
        || name == QStringLiteral("_v_attachments")
        || name == QStringLiteral("vx_images")
        || name == QStringLiteral("vx_attachments")) {
        return true;
    }
    return BundleNotebookConfigMgr::isBuiltInFolder(p_node, p_name);
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFileAsChildOf(const QString &p_srcPath, Node *p_dest)
{
    // Skip copy if it already locates in dest folder.
    auto destFilePath = PathUtils::concatenateFilePath(p_dest->fetchAbsolutePath(),
                                                       PathUtils::fileName(p_srcPath));
    if (!PathUtils::areSamePaths(p_srcPath, destFilePath)) {
        // Copy source file itself.
        destFilePath = getBackend()->renameIfExistsCaseInsensitive(destFilePath);
        getBackend()->copyFile(p_srcPath, destFilePath);

        // Copy media files fetched from content.
        ContentMediaUtils::copyMediaFiles(p_srcPath, getBackend().data(), destFilePath);
    }

    ensureNodeInDatabase(p_dest);

    const auto name = PathUtils::fileName(destFilePath);
    auto destNode = p_dest->findChild(name, true);
    if (destNode) {
        // Already have the node.
        ensureNodeInDatabase(destNode.data());
        return destNode;
    }

    // Create a file node.
    destNode = QSharedPointer<VXNode>::create(name,
                                              NodeParameters(),
                                              getNotebook(),
                                              p_dest);
    destNode->setExists(true);
    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    addNodeToDatabase(destNode.data());

    return destNode;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFolderAsChildOf(const QString &p_srcPath, Node *p_dest)
{
    // Skip copy if it already locates in dest folder.
    auto destFolderPath = PathUtils::concatenateFilePath(p_dest->fetchAbsolutePath(),
                                                         PathUtils::fileName(p_srcPath));
    if (!PathUtils::areSamePaths(p_srcPath, destFolderPath)) {
        destFolderPath = getBackend()->renameIfExistsCaseInsensitive(destFolderPath);

        // Copy folder.
        getBackend()->copyDir(p_srcPath, destFolderPath);
    }

    ensureNodeInDatabase(p_dest);

    const auto name = PathUtils::fileName(destFolderPath);
    auto destNode = p_dest->findChild(name, true);
    if (destNode) {
        // Already have the node.
        ensureNodeInDatabase(destNode.data());
        return destNode;
    }

    // Create a folder node.
    auto notebook = getNotebook();
    destNode = QSharedPointer<VXNode>::create(name, notebook, p_dest);
    destNode->loadCompleteInfo(NodeParameters(), QVector<QSharedPointer<Node>>());
    destNode->setExists(true);

    writeNodeConfig(destNode.data());

    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    addNodeToDatabase(destNode.data());

    return destNode;
}

void VXNotebookConfigMgr::inheritNodeFlags(const Node *p_node, Node *p_child) const
{
    if (p_node->isReadOnly()) {
        markNodeReadOnly(p_child);
    }
}

QVector<QSharedPointer<ExternalNode>> VXNotebookConfigMgr::fetchExternalChildren(Node *p_node) const
{
    Q_ASSERT(p_node->isContainer());
    QVector<QSharedPointer<ExternalNode>> externalNodes;

    auto dir = p_node->toDir();

    // Folders.
    {
        const auto folders = dir.entryList(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
        for (const auto &folder : folders) {
            if (isBuiltInFolder(p_node, folder)) {
                continue;
            }

            if (isExcludedFromExternalNode(folder)) {
                continue;
            }

            if (p_node->containsContainerChild(folder)) {
                continue;
            }

            externalNodes.push_back(QSharedPointer<ExternalNode>::create(p_node, folder, ExternalNode::Type::Folder));
        }
    }

    // Files.
    {
        const auto files = dir.entryList(QDir::Files);
        for (const auto &file : files) {
            if (isBuiltInFile(p_node, file)) {
                continue;
            }

            if (isExcludedFromExternalNode(file)) {
                continue;
            }

            if (p_node->containsContentChild(file)) {
                continue;
            }

            externalNodes.push_back(QSharedPointer<ExternalNode>::create(p_node, file, ExternalNode::Type::File));
        }
    }

    return externalNodes;
}

bool VXNotebookConfigMgr::isExcludedFromExternalNode(const QString &p_name) const
{
    for (const auto &regExp : s_externalNodeExcludePatterns) {
        if (regExp.exactMatch(p_name)) {
            return true;
        }
    }
    return false;
}

bool VXNotebookConfigMgr::checkNodeExists(Node *p_node)
{
    bool exists = getBackend()->exists(p_node->fetchPath());
    p_node->setExists(exists);
    return exists;
}

QStringList VXNotebookConfigMgr::scanAndImportExternalFiles(Node *p_node)
{
    QStringList files;
    if (!p_node->isContainer()) {
        return files;
    }

    // External nodes.
    auto dir = p_node->toDir();
    auto externalNodes = fetchExternalChildren(p_node);
    for (const auto &node : externalNodes) {
        Node::Flags flags = Node::Flag::Content;
        if (node->isFolder()) {
            if (isLikelyImageFolder(dir.filePath(node->getName()))) {
                qWarning() << "skip importing folder containing only images" << node->getName();
                continue;
            }
            flags = Node::Flag::Container;
        }

        addAsNode(p_node, flags, node->getName(), NodeParameters());
        files << dir.filePath(node->getName());
    }

    // Children folders (including newly-added external nodes).
    for (const auto &child : p_node->getChildrenRef()) {
        if (child->isContainer()) {
            files << scanAndImportExternalFiles(child.data());
        }
    }

    return files;
}

bool VXNotebookConfigMgr::isLikelyImageFolder(const QString &p_dirPath)
{
    QDir dir(p_dirPath);
    const auto folders = dir.entryList(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    if (!folders.isEmpty()) {
        return false;
    }

    const auto files = dir.entryList(QDir::Files);
    if (files.isEmpty()) {
        return false;
    }

    for (const auto &file : files) {
        if (!FileUtils::isImage(dir.filePath(file))) {
            return false;
        }
    }

    return true;
}

NotebookDatabaseAccess *VXNotebookConfigMgr::getDatabaseAccess() const
{
    return static_cast<BundleNotebook *>(getNotebook())->getDatabaseAccess();
}

void VXNotebookConfigMgr::updateNodeInDatabase(Node *p_node)
{
    Q_ASSERT(sameNotebook(p_node));
    getDatabaseAccess()->updateNode(p_node);
}

void VXNotebookConfigMgr::ensureNodeInDatabase(Node *p_node)
{
    if (!p_node) {
        return;
    }

    Q_ASSERT(sameNotebook(p_node));

    auto db = getDatabaseAccess();
    if (db->existsNode(p_node)) {
        return;
    }

    ensureNodeInDatabase(p_node->getParent());
    db->addNode(p_node, false);
    db->clearObsoleteNodes();
}

void VXNotebookConfigMgr::addNodeToDatabase(Node *p_node)
{
    Q_ASSERT(sameNotebook(p_node));
    auto db = getDatabaseAccess();
    db->addNode(p_node, false);
    db->clearObsoleteNodes();
}

bool VXNotebookConfigMgr::nodeExistsInDatabase(const Node *p_node)
{
    Q_ASSERT(sameNotebook(p_node));
    return getDatabaseAccess()->existsNode(p_node);
}

void VXNotebookConfigMgr::removeNodeFromDatabase(const Node *p_node)
{
    Q_ASSERT(sameNotebook(p_node));
    getDatabaseAccess()->removeNode(p_node);
}

bool VXNotebookConfigMgr::sameNotebook(const Node *p_node) const
{
    return p_node ? p_node->getNotebook() == getNotebook() : true;
}
