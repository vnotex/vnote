#include "node.h"

#include <QDir>

#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <notebookbackend/inotebookbackend.h>
#include <utils/pathutils.h>
#include <core/exception.h>
#include "notebook.h"

using namespace vnotex;

Node::Node(Flags p_flags,
           ID p_id,
           const QString &p_name,
           const QDateTime &p_createdTimeUtc,
           const QDateTime &p_modifiedTimeUtc,
           const QStringList &p_tags,
           const QString &p_attachmentFolder,
           Notebook *p_notebook,
           Node *p_parent)
    : m_notebook(p_notebook),
      m_loaded(true),
      m_flags(p_flags),
      m_id(p_id),
      m_name(p_name),
      m_createdTimeUtc(p_createdTimeUtc),
      m_modifiedTimeUtc(p_modifiedTimeUtc),
      m_tags(p_tags),
      m_attachmentFolder(p_attachmentFolder),
      m_parent(p_parent)
{
    Q_ASSERT(m_notebook);
}

Node::Node(Flags p_flags,
           const QString &p_name,
           Notebook *p_notebook,
           Node *p_parent)
    : m_notebook(p_notebook),
      m_flags(p_flags),
      m_name(p_name),
      m_parent(p_parent)
{
    Q_ASSERT(m_notebook);
}

Node::~Node()
{
}

bool Node::isLoaded() const
{
    return m_loaded;
}

void Node::loadCompleteInfo(ID p_id,
                            const QDateTime &p_createdTimeUtc,
                            const QDateTime &p_modifiedTimeUtc,
                            const QStringList &p_tags,
                            const QVector<QSharedPointer<Node>> &p_children)
{
    Q_ASSERT(!m_loaded);
    m_id = p_id;
    m_createdTimeUtc = p_createdTimeUtc;
    m_modifiedTimeUtc = p_modifiedTimeUtc;
    m_tags = p_tags;
    m_children = p_children;
    m_loaded = true;
}

bool Node::isRoot() const
{
    return !m_parent && m_use == Use::Root;
}

const QString &Node::getName() const
{
    return m_name;
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

    getConfigMgr()->renameNode(this, p_name);
    Q_ASSERT(m_name == p_name);

    emit m_notebook->nodeUpdated(this);
}

bool Node::containsChild(const QString &p_name, bool p_caseSensitive) const
{
    return findChild(p_name, p_caseSensitive) != nullptr;
}

bool Node::containsChild(const QSharedPointer<Node> &p_node) const
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

Node::Flags Node::getFlags() const
{
    return m_flags;
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

const QDateTime &Node::getModifiedTimeUtc() const
{
    return m_modifiedTimeUtc;
}

void Node::setModifiedTimeUtc()
{
    m_modifiedTimeUtc = QDateTime::currentDateTimeUtc();
}

const QVector<QSharedPointer<Node>> &Node::getChildren() const
{
    return m_children;
}

int Node::getChildrenCount() const
{
    return m_children.size();
}

void Node::addChild(const QSharedPointer<Node> &p_node)
{
    insertChild(m_children.size(), p_node);
}

void Node::insertChild(int p_idx, const QSharedPointer<Node> &p_node)
{
    Q_ASSERT(isContainer());

    p_node->setParent(this);

    m_children.insert(p_idx, p_node);
}

void Node::removeChild(const QSharedPointer<Node> &p_child)
{
    if (m_children.removeOne(p_child)) {
        p_child->setParent(nullptr);
    }
}

Notebook *Node::getNotebook() const
{
    return m_notebook;
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

const QStringList &Node::getTags() const
{
    return m_tags;
}

bool Node::isReadOnly() const
{
    return m_flags & Flag::ReadOnly;
}

void Node::setReadOnly(bool p_readOnly)
{
    if (p_readOnly) {
        m_flags |= Flag::ReadOnly;
    } else {
        m_flags &= ~Flag::ReadOnly;
    }
}

QString Node::fetchPath() const
{
    if (!m_parent) {
        return QString();
    } else {
        return PathUtils::concatenateFilePath(m_parent->fetchPath(), m_name);
    }
}

bool Node::isContainer() const
{
    return m_flags & Flag::Container;
}

bool Node::hasContent() const
{
    return m_flags & Flag::Content;
}

QDir Node::toDir() const
{
    if (isContainer()) {
        return QDir(fetchAbsolutePath());
    }
    Q_ASSERT(false);
    return QDir();
}

void Node::load()
{
    if (isLoaded()) {
        return;
    }

    getConfigMgr()->loadNode(this);
}

void Node::save()
{
    getConfigMgr()->saveNode(this);
}

INotebookConfigMgr *Node::getConfigMgr() const
{
    return m_notebook->getConfigMgr().data();
}

const QString &Node::getAttachmentFolder() const
{
    return m_attachmentFolder;
}

void Node::setAttachmentFolder(const QString &p_attachmentFolder)
{
    m_attachmentFolder = p_attachmentFolder;
}

QString Node::fetchAttachmentFolderPath()
{
    return getConfigMgr()->fetchNodeAttachmentFolderPath(this);
}

INotebookBackend *Node::getBackend() const
{
    return m_notebook->getBackend().data();
}
