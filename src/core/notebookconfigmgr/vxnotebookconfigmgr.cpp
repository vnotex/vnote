#include "vxnotebookconfigmgr.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

#include <notebookbackend/inotebookbackend.h>
#include <notebook/notebookparameters.h>
#include <notebook/filenode.h>
#include <notebook/foldernode.h>
#include <notebook/bundlenotebook.h>
#include <utils/utils.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <exception.h>

#include "nodecontentmediautils.h"

using namespace vnotex;

const QString VXNotebookConfigMgr::NodeConfig::c_version = "version";

const QString VXNotebookConfigMgr::NodeConfig::c_id = "id";

const QString VXNotebookConfigMgr::NodeConfig::c_createdTimeUtc = "created_time";

const QString VXNotebookConfigMgr::NodeConfig::c_files = "files";

const QString VXNotebookConfigMgr::NodeConfig::c_folders = "folders";

const QString VXNotebookConfigMgr::NodeConfig::c_name = "name";

const QString VXNotebookConfigMgr::NodeConfig::c_modifiedTimeUtc = "modified_time";

const QString VXNotebookConfigMgr::NodeConfig::c_attachmentFolder = "attachment_folder";

const QString VXNotebookConfigMgr::NodeConfig::c_tags = "tags";

QJsonObject VXNotebookConfigMgr::NodeFileConfig::toJson() const
{
    QJsonObject jobj;

    jobj[NodeConfig::c_name] = m_name;
    jobj[NodeConfig::c_id] = QString::number(m_id);
    jobj[NodeConfig::c_createdTimeUtc] = Utils::dateTimeStringUniform(m_createdTimeUtc);
    jobj[NodeConfig::c_modifiedTimeUtc] = Utils::dateTimeStringUniform(m_modifiedTimeUtc);
    jobj[NodeConfig::c_attachmentFolder] = m_attachmentFolder;
    jobj[NodeConfig::c_tags] = QJsonArray::fromStringList(m_tags);

    return jobj;
}

void VXNotebookConfigMgr::NodeFileConfig::fromJson(const QJsonObject &p_jobj)
{
    m_name = p_jobj[NodeConfig::c_name].toString();

    {
        auto idStr = p_jobj[NodeConfig::c_id].toString();
        bool ok;
        m_id = idStr.toULongLong(&ok);
        if (!ok) {
            m_id = Node::InvalidId;
        }
    }

    m_createdTimeUtc = Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_createdTimeUtc].toString());
    m_modifiedTimeUtc = Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_modifiedTimeUtc].toString());

    m_attachmentFolder = p_jobj[NodeConfig::c_attachmentFolder].toString();

    {
        auto arr = p_jobj[NodeConfig::c_tags].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            m_tags << arr[i].toString();
        }
    }
}

QJsonObject VXNotebookConfigMgr::NodeFolderConfig::toJson() const
{
    QJsonObject jobj;

    jobj[NodeConfig::c_name] = m_name;

    return jobj;
}

void VXNotebookConfigMgr::NodeFolderConfig::fromJson(const QJsonObject &p_jobj)
{
    m_name = p_jobj[NodeConfig::c_name].toString();
}

VXNotebookConfigMgr::NodeConfig::NodeConfig()
{
}

VXNotebookConfigMgr::NodeConfig::NodeConfig(const QString &p_version,
                                            ID p_id,
                                            const QDateTime &p_createdTimeUtc)
    : m_version(p_version),
      m_id(p_id),
      m_createdTimeUtc(p_createdTimeUtc)
{
}

QJsonObject VXNotebookConfigMgr::NodeConfig::toJson() const
{
    QJsonObject jobj;

    jobj[NodeConfig::c_version] = m_version;
    jobj[NodeConfig::c_id] = QString::number(m_id);
    jobj[NodeConfig::c_createdTimeUtc] = Utils::dateTimeStringUniform(m_createdTimeUtc);

    QJsonArray files;
    for (const auto &file : m_files) {
        files.append(file.toJson());
    }
    jobj[NodeConfig::c_files] = files;

    QJsonArray folders;
    for (const auto& folder : m_folders) {
        folders.append(folder.toJson());
    }
    jobj[NodeConfig::c_folders] = folders;

    return jobj;
}

void VXNotebookConfigMgr::NodeConfig::fromJson(const QJsonObject &p_jobj)
{
    m_version = p_jobj[NodeConfig::c_version].toString();

    {
    auto idStr = p_jobj[NodeConfig::c_id].toString();
    bool ok;
    m_id = idStr.toULongLong(&ok);
    if (!ok) {
        m_id = Node::InvalidId;
    }
    }

    m_createdTimeUtc = Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_createdTimeUtc].toString());

    auto filesJson = p_jobj[NodeConfig::c_files].toArray();
    m_files.resize(filesJson.size());
    for (int i = 0; i < filesJson.size(); ++i) {
        m_files[i].fromJson(filesJson[i].toObject());
    }

    auto foldersJson = p_jobj[NodeConfig::c_folders].toArray();
    m_folders.resize(foldersJson.size());
    for (int i = 0; i < foldersJson.size(); ++i) {
        m_folders[i].fromJson(foldersJson[i].toObject());
    }
}


const QString VXNotebookConfigMgr::c_nodeConfigName = "vx.json";

const QString VXNotebookConfigMgr::c_recycleBinFolderName = "vx_recycle_bin";

VXNotebookConfigMgr::VXNotebookConfigMgr(const QString &p_name,
                                         const QString &p_displayName,
                                         const QString &p_description,
                                         const QSharedPointer<INotebookBackend> &p_backend,
                                         QObject *p_parent)
    : BundleNotebookConfigMgr(p_backend, p_parent),
      m_info(p_name, p_displayName, p_description)
{
}

QString VXNotebookConfigMgr::getName() const
{
    return m_info.m_name;
}

QString VXNotebookConfigMgr::getDisplayName() const
{
    return m_info.m_displayName;
}

QString VXNotebookConfigMgr::getDescription() const
{
    return m_info.m_description;
}

void VXNotebookConfigMgr::createEmptySkeleton(const NotebookParameters &p_paras)
{
    BundleNotebookConfigMgr::createEmptySkeleton(p_paras);

    createEmptyRootNode();
}

void VXNotebookConfigMgr::createEmptyRootNode()
{
    NodeConfig node(getCodeVersion(),
                    BundleNotebookConfigMgr::RootNodeId,
                    QDateTime::currentDateTimeUtc());
    writeNodeConfig(c_nodeConfigName, node);
}

QSharedPointer<Node> VXNotebookConfigMgr::loadRootNode() const
{
    auto nodeConfig = readNodeConfig("");
    QSharedPointer<Node> root = nodeConfigToNode(*nodeConfig, "", nullptr);
    Q_ASSERT(root->isLoaded());

    if (!markRecycleBinNode(root)) {
        const_cast<VXNotebookConfigMgr *>(this)->createRecycleBinNode(root);
    }

    return root;
}

bool VXNotebookConfigMgr::markRecycleBinNode(const QSharedPointer<Node> &p_root) const
{
    auto node = p_root->findChild(c_recycleBinFolderName,
                                  FileUtils::isPlatformNameCaseSensitive());
    if (node) {
        node->setUse(Node::Use::RecycleBin);
        markNodeReadOnly(node.data());
        return true;
    }

    return false;
}

void VXNotebookConfigMgr::markNodeReadOnly(Node *p_node) const
{
    auto flags = p_node->getFlags();
    if (flags & Node::Flag::ReadOnly) {
        return;
    }

    p_node->setFlags(flags | Node::Flag::ReadOnly);
    for (auto &child : p_node->getChildren()) {
        markNodeReadOnly(child.data());
    }
}

void VXNotebookConfigMgr::createRecycleBinNode(const QSharedPointer<Node> &p_root)
{
    Q_ASSERT(p_root->isRoot());

    auto node = newNode(p_root.data(), Node::Type::Folder, c_recycleBinFolderName);
    node->setUse(Node::Use::RecycleBin);
    markNodeReadOnly(node.data());
}

QSharedPointer<VXNotebookConfigMgr::NodeConfig> VXNotebookConfigMgr::readNodeConfig(const QString &p_path) const
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
    Q_ASSERT(p_node->getType() == Node::Type::Folder);
    return PathUtils::concatenateFilePath(p_node->fetchRelativePath(), c_nodeConfigName);
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
                                                           Node *p_parent) const
{
    auto node = QSharedPointer<FolderNode>::create(p_name, getNotebook(), p_parent);
    loadFolderNode(node.data(), p_config);
    return node;
}

void VXNotebookConfigMgr::loadFolderNode(FolderNode *p_node, const NodeConfig &p_config) const
{
    QVector<QSharedPointer<Node>> children;
    children.reserve(p_config.m_files.size() + p_config.m_folders.size());

    for (const auto &folder : p_config.m_folders) {
        auto folderNode = QSharedPointer<FolderNode>::create(folder.m_name,
                                                             getNotebook(),
                                                             p_node);
        inheritNodeFlags(p_node, folderNode.data());
        children.push_back(folderNode);
    }

    for (const auto &file : p_config.m_files) {
        auto fileNode = QSharedPointer<FileNode>::create(file.m_id,
                                                         file.m_name,
                                                         file.m_createdTimeUtc,
                                                         file.m_modifiedTimeUtc,
                                                         file.m_attachmentFolder,
                                                         file.m_tags,
                                                         getNotebook(),
                                                         p_node);
        inheritNodeFlags(p_node, fileNode.data());
        children.push_back(fileNode);
    }

    p_node->loadFolder(p_config.m_id, p_config.m_createdTimeUtc, children);
}

QSharedPointer<Node> VXNotebookConfigMgr::newNode(Node *p_parent,
                                                  Node::Type p_type,
                                                  const QString &p_name)
{
    Q_ASSERT(p_parent && p_parent->getType() == Node::Type::Folder);

    QSharedPointer<Node> node;

    switch (p_type) {
    case Node::Type::File:
        node = newFileNode(p_parent, p_name, true, NodeParameters());
        break;

    case Node::Type::Folder:
        node = newFolderNode(p_parent, p_name, true, NodeParameters());
        break;
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::addAsNode(Node *p_parent,
                                                    Node::Type p_type,
                                                    const QString &p_name,
                                                    const NodeParameters &p_paras)
{
    Q_ASSERT(p_parent && p_parent->getType() == Node::Type::Folder);

    QSharedPointer<Node> node;
    switch (p_type) {
    case Node::Type::File:
        node = newFileNode(p_parent, p_name, false, p_paras);
        break;

    case Node::Type::Folder:
        node = newFolderNode(p_parent, p_name, false, p_paras);
        break;
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyAsNode(Node *p_parent,
                                                     Node::Type p_type,
                                                     const QString &p_path)
{
    Q_ASSERT(p_parent && p_parent->getType() == Node::Type::Folder);

    QSharedPointer<Node> node;
    switch (p_type) {
    case Node::Type::File:
        node = copyFileAsChildOf(p_path, p_parent);
        break;

    case Node::Type::Folder:
        node = copyFolderAsChildOf(p_path, p_parent);
        break;
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::newFileNode(Node *p_parent,
                                                      const QString &p_name,
                                                      bool p_create,
                                                      const NodeParameters &p_paras)
{
    auto notebook = getNotebook();

    // Create file node.
    auto node = QSharedPointer<FileNode>::create(Node::InvalidId,
                                                 p_name,
                                                 p_paras.m_createdTimeUtc,
                                                 p_paras.m_modifiedTimeUtc,
                                                 p_paras.m_attachmentFolder,
                                                 p_paras.m_tags,
                                                 notebook,
                                                 p_parent);

    // Write empty file.
    if (p_create) {
        getBackend()->writeFile(node->fetchRelativePath(), QString());
    }

    addChildNode(p_parent, node);
    writeNodeConfig(p_parent);

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::newFolderNode(Node *p_parent,
                                                        const QString &p_name,
                                                        bool p_create,
                                                        const NodeParameters &p_paras)
{
    auto notebook = getNotebook();

    // Create folder node.
    auto node = QSharedPointer<FolderNode>::create(p_name, notebook, p_parent);
    node->loadFolder(Node::InvalidId,
                     p_paras.m_createdTimeUtc,
                     QVector<QSharedPointer<Node>>());

    // Make folder.
    if (p_create) {
        getBackend()->makePath(node->fetchRelativePath());
    }

    writeNodeConfig(node.data());

    addChildNode(p_parent, node);
    writeNodeConfig(p_parent);

    return node;
}

QSharedPointer<VXNotebookConfigMgr::NodeConfig> VXNotebookConfigMgr::nodeToNodeConfig(const Node *p_node) const
{
    Q_ASSERT(p_node->getType() == Node::Type::Folder);

    auto config = QSharedPointer<NodeConfig>::create(getCodeVersion(),
                                                     p_node->getId(),
                                                     p_node->getCreatedTimeUtc());

    for (const auto &child : p_node->getChildren()) {
        switch (child->getType()) {
        case Node::Type::File:
        {
            NodeFileConfig fileConfig;
            fileConfig.m_name = child->getName();
            fileConfig.m_id = child->getId();
            fileConfig.m_createdTimeUtc = child->getCreatedTimeUtc();
            fileConfig.m_modifiedTimeUtc = child->getModifiedTimeUtc();
            fileConfig.m_attachmentFolder = child->getAttachmentFolder();
            fileConfig.m_tags = child->getTags();

            config->m_files.push_back(fileConfig);
            break;
        }

        case Node::Type::Folder:
        {
            NodeFolderConfig folderConfig;
            folderConfig.m_name = child->getName();

            config->m_folders.push_back(folderConfig);
            break;
        }
        }
    }

    return config;
}

void VXNotebookConfigMgr::loadNode(Node *p_node) const
{
    if (p_node->isLoaded()) {
        return;
    }

    auto config = readNodeConfig(p_node->fetchRelativePath());
    auto folderNode = dynamic_cast<FolderNode *>(p_node);
    loadFolderNode(folderNode, *config);
}

void VXNotebookConfigMgr::saveNode(const Node *p_node)
{
    Q_ASSERT(!p_node->isRoot());

    if (p_node->getType() == Node::Type::Folder) {
        writeNodeConfig(p_node);
    } else {
        writeNodeConfig(p_node->getParent());
    }
}

void VXNotebookConfigMgr::renameNode(Node *p_node, const QString &p_name)
{
    Q_ASSERT(!p_node->isRoot());
    switch (p_node->getType()) {
    case Node::Type::Folder:
        getBackend()->renameDir(p_node->fetchRelativePath(), p_name);
        break;

    case Node::Type::File:
        getBackend()->renameFile(p_node->fetchRelativePath(), p_name);
        break;
    }

    p_node->setName(p_name);
    writeNodeConfig(p_node->getParent());
}

void VXNotebookConfigMgr::addChildNode(Node *p_parent, const QSharedPointer<Node> &p_child) const
{
    // Add @p_child after the last node of same type.
    const auto type = p_child->getType();
    switch (type) {
    case Node::Type::Folder:
    {
        int idx = 0;
        auto children = p_parent->getChildren();
        for (; idx < children.size(); ++idx) {
            if (children[idx]->getType() != type) {
                break;
            }
        }

        p_parent->insertChild(idx, p_child);
        break;
    }

    case Node::Type::File:
        p_parent->addChild(p_child);
        break;
    }

    inheritNodeFlags(p_parent, p_child.data());
}

QSharedPointer<Node> VXNotebookConfigMgr::loadNodeByPath(const QSharedPointer<Node> &p_root, const QString &p_relativePath)
{
    auto p = PathUtils::cleanPath(p_relativePath);
    auto paths = p.split('/', QString::SkipEmptyParts);
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
    Q_ASSERT(p_dest->getType() == Node::Type::Folder);
    if (!p_src->existsOnDisk()) {
        Exception::throwOne(Exception::Type::FileMissingOnDisk,
                            QString("source node missing on disk (%1)").arg(p_src->fetchAbsolutePath()));
        return nullptr;
    }

    QSharedPointer<Node> node;
    switch (p_src->getType()) {
    case Node::Type::File:
        node = copyFileNodeAsChildOf(p_src, p_dest, p_move);
        break;

    case Node::Type::Folder:
        node = copyFolderNodeAsChildOf(p_src, p_dest, p_move);
        break;
    }

    return node;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFileNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                                                Node *p_dest,
                                                                bool p_move)
{
    // Copy source file itself.
    auto srcFilePath = p_src->fetchAbsolutePath();
    auto destFilePath = PathUtils::concatenateFilePath(p_dest->fetchRelativePath(),
                                                       PathUtils::fileName(srcFilePath));
    destFilePath = getBackend()->renameIfExistsCaseInsensitive(destFilePath);
    getBackend()->copyFile(srcFilePath, destFilePath);

    // Copy media files fetched from content.
    NodeContentMediaUtils::copyMediaFiles(p_src.data(), getBackend().data(), destFilePath);

    // Copy attachment folder. Rename attachment folder if conflicts.
    QString attachmentFolder = p_src->getAttachmentFolder();
    if (!attachmentFolder.isEmpty()) {
        auto destAttachmentFolderPath = fetchNodeAttachmentFolder(destFilePath, attachmentFolder);
        NodeContentMediaUtils::copyAttachment(p_src.data(), getBackend().data(), destFilePath, destAttachmentFolderPath);
    }

    // Create a file node.
    auto notebook = getNotebook();
    auto id = p_src->getId();
    if (!p_move || p_src->getNotebook() != notebook) {
        // Use a new id.
        id = notebook->getAndUpdateNextNodeId();
    }

    auto destNode = QSharedPointer<FileNode>::create(id,
                                                     PathUtils::fileName(destFilePath),
                                                     p_src->getCreatedTimeUtc(),
                                                     p_src->getModifiedTimeUtc(),
                                                     attachmentFolder,
                                                     p_src->getTags(),
                                                     notebook,
                                                     p_dest);
    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    if (p_move) {
        // Delete src node.
        p_src->getNotebook()->removeNode(p_src);
    }

    return destNode;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFolderNodeAsChildOf(const QSharedPointer<Node> &p_src,
                                                                  Node *p_dest,
                                                                  bool p_move)
{
    auto srcFolderPath = p_src->fetchAbsolutePath();
    auto destFolderPath = PathUtils::concatenateFilePath(p_dest->fetchRelativePath(),
                                                         PathUtils::fileName(srcFolderPath));
    destFolderPath = getBackend()->renameIfExistsCaseInsensitive(destFolderPath);

    // Make folder.
    getBackend()->makePath(destFolderPath);

    // Create a folder node.
    auto notebook = getNotebook();
    auto id = p_src->getId();
    if (!p_move || p_src->getNotebook() != notebook) {
        // Use a new id.
        id = notebook->getAndUpdateNextNodeId();
    }
    auto destNode = QSharedPointer<FolderNode>::create(PathUtils::fileName(destFolderPath),
                                                       notebook,
                                                       p_dest);
    destNode->loadFolder(id, p_src->getCreatedTimeUtc(), QVector<QSharedPointer<Node>>());

    writeNodeConfig(destNode.data());

    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    // Copy children node.
    for (const auto &childNode : p_src->getChildren()) {
        copyNodeAsChildOf(childNode, destNode.data(), p_move);
    }

    if (p_move) {
        p_src->getNotebook()->removeNode(p_src);
    }

    return destNode;
}

void VXNotebookConfigMgr::removeNode(const QSharedPointer<Node> &p_node, bool p_force, bool p_configOnly)
{
    auto parentNode = p_node->getParent();
    if (!p_configOnly) {
        // Remove all children.
        for (auto &childNode : p_node->getChildren()) {
            removeNode(childNode, p_force, p_configOnly);
        }

        removeFilesOfNode(p_node.data(), p_force);
    }

    if (parentNode) {
        parentNode->removeChild(p_node);
        writeNodeConfig(parentNode);
    }
}

void VXNotebookConfigMgr::removeFilesOfNode(Node *p_node, bool p_force)
{
    Q_ASSERT(p_node->getNotebook() == getNotebook());
    switch (p_node->getType()) {
    case Node::Type::File:
    {
        // Delete attachment.
        if (!p_node->getAttachmentFolder().isEmpty()) {
            getBackend()->removeDir(p_node->fetchAttachmentFolderPath());
        }

        // Delete media files fetched from content.
        NodeContentMediaUtils::removeMediaFiles(p_node);

        // Delete node file itself.
        auto filePath = p_node->fetchRelativePath();
        getBackend()->removeFile(filePath);
        break;
    }

    case Node::Type::Folder:
    {
        Q_ASSERT(p_node->getChildrenCount() == 0);
        // Delete node config file and the dir if it is empty.
        auto configFilePath = getNodeConfigFilePath(p_node);
        getBackend()->removeFile(configFilePath);
        auto folderPath = p_node->fetchRelativePath();
        if (p_force) {
            getBackend()->removeDir(folderPath);
        } else {
            getBackend()->removeEmptyDir(folderPath);
            bool deleted = getBackend()->removeDirIfEmpty(folderPath);
            if (!deleted) {
                qWarning() << "folder is not deleted since it is not empty" << folderPath;
            }
        }
        break;
    }
    }
}

bool VXNotebookConfigMgr::nodeExistsOnDisk(const Node *p_node) const
{
    return getBackend()->exists(p_node->fetchRelativePath());
}

QString VXNotebookConfigMgr::readNode(const Node *p_node) const
{
    Q_ASSERT(p_node->getType() == Node::Type::File);
    return getBackend()->readTextFile(p_node->fetchRelativePath());
}

void VXNotebookConfigMgr::writeNode(Node *p_node, const QString &p_content)
{
    Q_ASSERT(p_node->getType() == Node::Type::File);
    getBackend()->writeFile(p_node->fetchRelativePath(), p_content);

    p_node->setModifiedTimeUtc();
    writeNodeConfig(p_node->getParent());
}

QString VXNotebookConfigMgr::fetchNodeImageFolderPath(Node *p_node)
{
    auto pa = PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_node->fetchAbsolutePath()),
                                             getNotebook()->getImageFolder());
    // Do not make the folder when it is a folder node request.
    if (p_node->getType() == Node::Type::File) {
        getBackend()->makePath(pa);
    }
    return pa;
}

QString VXNotebookConfigMgr::fetchNodeAttachmentFolderPath(Node *p_node)
{
    auto notebookFolder = PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_node->fetchAbsolutePath()),
                                                         getNotebook()->getAttachmentFolder());
    if (p_node->getType() == Node::Type::File) {
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
    const auto name = p_name.toLower();
    if (name == c_nodeConfigName) {
        return true;
    }
    return BundleNotebookConfigMgr::isBuiltInFile(p_node, p_name);
}

bool VXNotebookConfigMgr::isBuiltInFolder(const Node *p_node, const QString &p_name) const
{
    const auto name = p_name.toLower();
    if (name == c_recycleBinFolderName
        || name == getNotebook()->getImageFolder().toLower()
        || name == getNotebook()->getAttachmentFolder().toLower()) {
        return true;
    }
    return BundleNotebookConfigMgr::isBuiltInFolder(p_node, p_name);
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFileAsChildOf(const QString &p_srcPath, Node *p_dest)
{
    // Copy source file itself.
    auto destFilePath = PathUtils::concatenateFilePath(p_dest->fetchRelativePath(),
                                                       PathUtils::fileName(p_srcPath));
    destFilePath = getBackend()->renameIfExistsCaseInsensitive(destFilePath);
    getBackend()->copyFile(p_srcPath, destFilePath);

    // Copy media files fetched from content.
    NodeContentMediaUtils::copyMediaFiles(p_srcPath, getBackend().data(), destFilePath);

    // Create a file node.
    auto currentTime = QDateTime::currentDateTimeUtc();
    auto destNode = QSharedPointer<FileNode>::create(getNotebook()->getAndUpdateNextNodeId(),
                                                     PathUtils::fileName(destFilePath),
                                                     currentTime,
                                                     currentTime,
                                                     QString(),
                                                     QStringList(),
                                                     getNotebook(),
                                                     p_dest);
    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    return destNode;
}

QSharedPointer<Node> VXNotebookConfigMgr::copyFolderAsChildOf(const QString &p_srcPath, Node *p_dest)
{
    auto destFolderPath = PathUtils::concatenateFilePath(p_dest->fetchRelativePath(),
                                                         PathUtils::fileName(p_srcPath));
    destFolderPath = getBackend()->renameIfExistsCaseInsensitive(destFolderPath);

    // Copy folder.
    getBackend()->copyDir(p_srcPath, destFolderPath);

    // Create a folder node.
    auto notebook = getNotebook();
    auto destNode = QSharedPointer<FolderNode>::create(PathUtils::fileName(destFolderPath),
                                                       notebook,
                                                       p_dest);
    destNode->loadFolder(notebook->getAndUpdateNextNodeId(), QDateTime::currentDateTimeUtc(), QVector<QSharedPointer<Node>>());

    writeNodeConfig(destNode.data());

    addChildNode(p_dest, destNode);
    writeNodeConfig(p_dest);

    return destNode;
}

void VXNotebookConfigMgr::inheritNodeFlags(const Node *p_node, Node *p_child) const
{
    if (p_node->getFlags() & Node::Flag::ReadOnly) {
        markNodeReadOnly(p_child);
    }
}
