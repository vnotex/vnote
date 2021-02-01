#include "notebook.h"

#include <QFileInfo>

#include <versioncontroller/iversioncontroller.h>
#include <notebookbackend/inotebookbackend.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include "exception.h"

using namespace vnotex;

const QString Notebook::c_defaultAttachmentFolder = QStringLiteral("vx_attachments");

const QString Notebook::c_defaultImageFolder = QStringLiteral("vx_images");

static vnotex::ID generateNotebookID()
{
    static vnotex::ID id = Notebook::InvalidId;
    return ++id;
}

Notebook::Notebook(const NotebookParameters &p_paras,
                   QObject *p_parent)
    : QObject(p_parent),
      m_id(generateNotebookID()),
      m_type(p_paras.m_type),
      m_name(p_paras.m_name),
      m_description(p_paras.m_description),
      m_rootFolderPath(p_paras.m_rootFolderPath),
      m_icon(p_paras.m_icon),
      m_imageFolder(p_paras.m_imageFolder),
      m_attachmentFolder(p_paras.m_attachmentFolder),
      m_createdTimeUtc(p_paras.m_createdTimeUtc),
      m_backend(p_paras.m_notebookBackend),
      m_versionController(p_paras.m_versionController),
      m_configMgr(p_paras.m_notebookConfigMgr)
{
    if (m_imageFolder.isEmpty()) {
        m_imageFolder = c_defaultImageFolder;
    }
    if (m_attachmentFolder.isEmpty()) {
        m_attachmentFolder = c_defaultAttachmentFolder;
    }
    m_configMgr->setNotebook(this);
}

Notebook::~Notebook()
{
}

vnotex::ID Notebook::getId() const
{
    return m_id;
}

const QString &Notebook::getType() const
{
    return m_type;
}

const QString &Notebook::getName() const
{
    return m_name;
}

void Notebook::setName(const QString &p_name)
{
    m_name = p_name;
}

void Notebook::updateName(const QString &p_name)
{
    Q_ASSERT(!p_name.isEmpty());
    if (p_name == m_name) {
        return;
    }

    m_name = p_name;
    updateNotebookConfig();
    emit updated();
}

const QString &Notebook::getDescription() const
{
    return m_description;
}

void Notebook::setDescription(const QString &p_description)
{
    m_description = p_description;
}

void Notebook::updateDescription(const QString &p_description)
{
    if (p_description == m_description) {
        return;
    }

    m_description = p_description;
    updateNotebookConfig();
    emit updated();
}

const QString &Notebook::getRootFolderPath() const
{
    return m_rootFolderPath;
}

QString Notebook::getRootFolderAbsolutePath() const
{
    return PathUtils::absolutePath(m_rootFolderPath);
}

const QIcon &Notebook::getIcon() const
{
    return m_icon;
}

void Notebook::setIcon(const QIcon &p_icon)
{
    m_icon = p_icon;
}

const QString &Notebook::getImageFolder() const
{
    return m_imageFolder;
}

const QString &Notebook::getAttachmentFolder() const
{
    return m_attachmentFolder;
}

const QSharedPointer<INotebookBackend> &Notebook::getBackend() const
{
    return m_backend;
}

const QSharedPointer<IVersionController> &Notebook::getVersionController() const
{
    return m_versionController;
}

const QSharedPointer<INotebookConfigMgr> &Notebook::getConfigMgr() const
{
    return m_configMgr;
}

const QSharedPointer<Node> &Notebook::getRootNode() const
{
    if (!m_root) {
        const_cast<Notebook *>(this)->m_root = m_configMgr->loadRootNode();
        Q_ASSERT(m_root->isRoot());
    }

    return m_root;
}

QSharedPointer<Node> Notebook::getRecycleBinNode() const
{
    auto root = getRootNode();
    auto children = root->getChildren();
    auto it = std::find_if(children.begin(),
                           children.end(),
                           [this](const QSharedPointer<Node> &p_node) {
                               return isRecycleBinNode(p_node.data());
                           });

    if (it != children.end()) {
        return *it;
    }

    return nullptr;
}

QSharedPointer<Node> Notebook::newNode(Node *p_parent, Node::Flags p_flags, const QString &p_name)
{
    return m_configMgr->newNode(p_parent, p_flags, p_name);
}

const QDateTime &Notebook::getCreatedTimeUtc() const
{
    return m_createdTimeUtc;
}

QSharedPointer<Node> Notebook::loadNodeByPath(const QString &p_path)
{
    if (!PathUtils::pathContains(m_rootFolderPath, p_path)) {
        return nullptr;
    }

    QString relativePath;
    QFileInfo fi(p_path);
    if (fi.isAbsolute()) {
        if (!fi.exists()) {
            return nullptr;
        }

        relativePath = PathUtils::relativePath(m_rootFolderPath, p_path);
    } else {
        relativePath = p_path;
    }

    return m_configMgr->loadNodeByPath(m_root, relativePath);
}

QSharedPointer<Node> Notebook::copyNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest, bool p_move)
{
    Q_ASSERT(p_src != p_dest);
    Q_ASSERT(p_dest->getNotebook() == this);

    if (Node::isAncestor(p_src.data(), p_dest)) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("source (%1) is the ancestor of destination (%2)")
                                   .arg(p_src->fetchPath(), p_dest->fetchPath()));
        return nullptr;
    }

    if (p_src->getParent() == p_dest && p_move) {
        return p_src;
    }

    return m_configMgr->copyNodeAsChildOf(p_src, p_dest, p_move);
}

void Notebook::removeNode(const QSharedPointer<Node> &p_node, bool p_force, bool p_configOnly)
{
    Q_ASSERT(p_node->getNotebook() == this);
    m_configMgr->removeNode(p_node, p_force, p_configOnly);
}

void Notebook::removeNode(const Node *p_node, bool p_force, bool p_configOnly)
{
    Q_ASSERT(p_node && !p_node->isRoot());
    auto children = p_node->getParent()->getChildren();
    auto it = std::find(children.begin(), children.end(), p_node);
    Q_ASSERT(it != children.end());
    removeNode(*it, p_force, p_configOnly);
}

bool Notebook::isRecycleBinNode(const Node *p_node) const
{
    return p_node && p_node->getUse() == Node::Use::RecycleBin;
}

bool Notebook::isNodeInRecycleBin(const Node *p_node) const
{
    if (p_node) {
        p_node = p_node->getParent();
        while (p_node) {
            if (isRecycleBinNode(p_node)) {
                return true;
            }

            p_node = p_node->getParent();
        }
    }

    return false;
}

void Notebook::moveNodeToRecycleBin(const Node *p_node)
{
    Q_ASSERT(p_node && !p_node->isRoot());
    auto children = p_node->getParent()->getChildren();
    for (auto &child : children) {
        if (p_node == child) {
            moveNodeToRecycleBin(child);
            return;
        }
    }

    Q_ASSERT(false);
}

void Notebook::moveNodeToRecycleBin(const QSharedPointer<Node> &p_node)
{
    auto destNode = getOrCreateRecycleBinDateNode();
    copyNodeAsChildOf(p_node, destNode.data(), true);
}

QSharedPointer<Node> Notebook::getOrCreateRecycleBinDateNode()
{
    // Name after date.
    auto dateNodeName = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));

    auto recycleBinNode = getRecycleBinNode();
    auto dateNode = recycleBinNode->findChild(dateNodeName,
                                              FileUtils::isPlatformNameCaseSensitive());
    if (!dateNode) {
        // Create a date node.
        dateNode = newNode(recycleBinNode.data(), Node::Flag::Container, dateNodeName);
    }

    return dateNode;
}

void Notebook::emptyNode(const Node *p_node, bool p_force)
{
    auto children = p_node->getChildren();
    for (auto &child : children) {
        removeNode(child, p_force);
    }
}

void Notebook::moveFileToRecycleBin(const QString &p_filePath)
{
    auto node = getOrCreateRecycleBinDateNode();
    auto destFilePath = PathUtils::concatenateFilePath(node->fetchPath(),
                                                       PathUtils::fileName(p_filePath));
    destFilePath = getBackend()->renameIfExistsCaseInsensitive(destFilePath);
    m_backend->copyFile(p_filePath, destFilePath);

    getBackend()->removeFile(p_filePath);

    emit nodeUpdated(node.data());
}

void Notebook::moveDirToRecycleBin(const QString &p_dirPath)
{
    auto node = getOrCreateRecycleBinDateNode();
    auto destDirPath = PathUtils::concatenateFilePath(node->fetchPath(),
                                                      PathUtils::fileName(p_dirPath));
    destDirPath = getBackend()->renameIfExistsCaseInsensitive(destDirPath);
    m_backend->copyDir(p_dirPath, destDirPath);

    getBackend()->removeDir(p_dirPath);

    emit nodeUpdated(node.data());
}

QSharedPointer<Node> Notebook::addAsNode(Node *p_parent,
                                         Node::Flags p_flags,
                                         const QString &p_name,
                                         const NodeParameters &p_paras)
{
    return m_configMgr->addAsNode(p_parent, p_flags, p_name, p_paras);
}

bool Notebook::isBuiltInFile(const Node *p_node, const QString &p_name) const
{
    return m_configMgr->isBuiltInFile(p_node, p_name);
}

bool Notebook::isBuiltInFolder(const Node *p_node, const QString &p_name) const
{
    return m_configMgr->isBuiltInFolder(p_node, p_name);
}

QSharedPointer<Node> Notebook::copyAsNode(Node *p_parent,
                                          Node::Flags p_flags,
                                          const QString &p_path)
{
    return m_configMgr->copyAsNode(p_parent, p_flags, p_path);
}
