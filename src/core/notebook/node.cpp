#include "node.h"

#include <QDir>

#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <notebookbackend/inotebookbackend.h>
#include <utils/pathutils.h>
#include <core/exception.h>
#include "notebook.h"

using namespace vnotex;

Node::Node(Type p_type,
           ID p_id,
           const QString &p_name,
           const QDateTime &p_createdTimeUtc,
           Notebook *p_notebook,
           Node *p_parent)
    : m_notebook(p_notebook),
      m_type(p_type),
      m_id(p_id),
      m_name(p_name),
      m_createdTimeUtc(p_createdTimeUtc),
      m_loaded(true),
      m_parent(p_parent)
{
    if (m_notebook) {
        m_configMgr = m_notebook->getConfigMgr();
        m_backend = m_notebook->getBackend();
    }
}

Node::Node(Type p_type,
           const QString &p_name,
           Notebook *p_notebook,
           Node *p_parent)
    : m_notebook(p_notebook),
      m_type(p_type),
      m_name(p_name),
      m_parent(p_parent)
{
    if (m_notebook) {
        m_configMgr = m_notebook->getConfigMgr();
        m_backend = m_notebook->getBackend();
    }
}

Node::~Node()
{
}

bool Node::isLoaded() const
{
    return m_loaded;
}

void Node::setLoaded(bool p_loaded)
{
    m_loaded = p_loaded;
}

void Node::loadInfo(ID p_id, const QDateTime &p_createdTimeUtc)
{
    Q_ASSERT(!m_loaded);

    m_id = p_id;
    m_createdTimeUtc = p_createdTimeUtc;
    m_loaded = true;
}

bool Node::isRoot() const
{
    return !m_parent;
}

const QString &Node::getName() const
{
    return m_name;
}

bool Node::hasChild(const QString &p_name, bool p_caseSensitive) const
{
    return findChild(p_name, p_caseSensitive) != nullptr;
}

bool Node::hasChild(const QSharedPointer<Node> &p_node) const
{
    return getChildren().indexOf(p_node) != -1;
}

QSharedPointer<Node> Node::findChild(const QString &p_name, bool p_caseSensitive) const
{
    auto targetName = p_caseSensitive ? p_name : p_name.toLower();
    for (auto &child : getChildren()) {
        if (p_caseSensitive ? child->getName() == targetName
                            : child->getName().toLower() == targetName) {
            return child;
        }
    }

    return nullptr;
}

void Node::setParent(Node *p_parent)
{
    m_parent = p_parent;
}

Node *Node::getParent() const
{
    return m_parent;
}

Node::Type Node::getType() const
{
    return m_type;
}

Node::Flags Node::getFlags() const
{
    return m_flags;
}

void Node::setFlags(Node::Flags p_flags)
{
    m_flags = p_flags;
}

Node::Use Node::getUse() const
{
    return m_use;
}

void Node::setUse(Node::Use p_use)
{
    m_use = p_use;
}

ID Node::getId() const
{
    return m_id;
}

const QDateTime &Node::getCreatedTimeUtc() const
{
    return m_createdTimeUtc;
}

Notebook *Node::getNotebook() const
{
    return m_notebook;
}

QString Node::fetchRelativePath() const
{
    if (!m_parent) {
        return QString();
    } else {
        return PathUtils::concatenateFilePath(m_parent->fetchRelativePath(), m_name);
    }
}

QString Node::fetchAbsolutePath() const
{
    return PathUtils::concatenateFilePath(m_notebook->getRootFolderAbsolutePath(),
                                          fetchRelativePath());
}

QString Node::fetchContentPath() const
{
    return fetchAbsolutePath();
}

void Node::load()
{
    Q_ASSERT(m_notebook);
    m_notebook->load(this);
}

void Node::save()
{
    Q_ASSERT(m_notebook);
    m_notebook->save(this);
}

void Node::setName(const QString &p_name)
{
    m_name = p_name;
}

void Node::updateName(const QString &p_name)
{
    if (m_name == p_name) {
        return;
    }

    m_notebook->rename(this, p_name);
    Q_ASSERT(m_name == p_name);
}

bool Node::isAncestor(const Node *p_ancestor, const Node *p_child)
{
    if (!p_ancestor || !p_child) {
        return false;
    }

    while (p_child) {
        p_child = p_child->getParent();
        if (p_child == p_ancestor) {
            return true;
        }
    }

    return false;
}

bool Node::existsOnDisk() const
{
    return m_configMgr->nodeExistsOnDisk(this);
}

QString Node::read() const
{
    return m_configMgr->readNode(this);
}

void Node::write(const QString &p_content)
{
    m_configMgr->writeNode(this, p_content);
}

QString Node::fetchImageFolderPath()
{
    return m_configMgr->fetchNodeImageFolderPath(this);
}

QString Node::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = m_backend->renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    m_backend->copyFile(p_srcImagePath, destFilePath);
    return destFilePath;
}

QString Node::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = m_backend->renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    p_image.save(destFilePath);
    m_backend->addFile(destFilePath);
    return destFilePath;
}

void Node::removeImage(const QString &p_imagePath)
{
    // Just move it to recycle bin but not added as a child node of recycle bin.
    m_notebook->moveFileToRecycleBin(p_imagePath);
}

QString Node::getAttachmentFolder() const
{
    Q_ASSERT(false);
    return QString();
}

void Node::setAttachmentFolder(const QString &p_folder)
{
    Q_UNUSED(p_folder);
    Q_ASSERT(false);
}

QString Node::fetchAttachmentFolderPath()
{
    return m_configMgr->fetchNodeAttachmentFolderPath(this);
}

QStringList Node::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_files);
    Q_ASSERT(false);
    return QStringList();
}

QString Node::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_name);
    Q_ASSERT(false);
    return QString();
}

QString Node::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_name);
    Q_ASSERT(false);
    return QString();
}

QString Node::renameAttachment(const QString &p_path, const QString &p_name)
{
    Q_UNUSED(p_path);
    Q_UNUSED(p_name);
    Q_ASSERT(false);
    return QString();
}

void Node::removeAttachment(const QStringList &p_paths)
{
    Q_UNUSED(p_paths);
    Q_ASSERT(false);
}

QStringList Node::getTags() const
{
    Q_ASSERT(false);
    return QStringList();
}

QDir Node::toDir() const
{
    Q_ASSERT(false);
    return QDir();
}

INotebookBackend *Node::getBackend() const
{
    return m_backend.data();
}

bool Node::isReadOnly() const
{
    return m_flags & Flag::ReadOnly;
}
