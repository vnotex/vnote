#ifndef DUMMYNODE_H
#define DUMMYNODE_H

#include <notebook/node.h>

namespace tests
{
    class DummyNode : public vnotex::Node
    {
    public:
        DummyNode(Flags p_flags, vnotex::ID p_id, const QString &p_name, vnotex::Notebook *p_notebook, Node *p_parent);

        QString fetchAbsolutePath() const Q_DECL_OVERRIDE;

        QSharedPointer<vnotex::File> getContentFile() Q_DECL_OVERRIDE;

        QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files) Q_DECL_OVERRIDE;

        QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

        QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

        QString renameAttachment(const QString &p_path, const QString &p_name) Q_DECL_OVERRIDE;

        void removeAttachment(const QStringList &p_paths) Q_DECL_OVERRIDE;

        void load() Q_DECL_OVERRIDE;

        void save() Q_DECL_OVERRIDE;
    };
}

#endif // DUMMYNODE_H
