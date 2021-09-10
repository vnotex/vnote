#include "dummynode.h"

#include <utils/pathutils.h>
#include <notebook/nodeparameters.h>

using namespace tests;

using namespace vnotex;

DummyNode::DummyNode(Flags p_flags, ID p_id, const QString &p_name, Notebook *p_notebook, Node *p_parent)
    : Node(p_flags,
           p_name,
           NodeParameters(p_id),
           p_notebook,
           p_parent)
{
}

QString DummyNode::fetchAbsolutePath() const
{
    return PathUtils::concatenateFilePath("/", fetchPath());
}

QSharedPointer<File> DummyNode::getContentFile()
{
    return nullptr;
}

QStringList DummyNode::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_files);
    return QStringList();
}

QString DummyNode::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_name);
    return QString();
}

QString DummyNode::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_name);
    return QString();
}

QString DummyNode::renameAttachment(const QString &p_path, const QString &p_name)
{
    Q_UNUSED(p_path);
    Q_UNUSED(p_name);
    return QString();
}

void DummyNode::removeAttachment(const QStringList &p_paths)
{
    Q_UNUSED(p_paths);
}

void DummyNode::load()
{
    m_loaded = true;
}

void DummyNode::save()
{
}
