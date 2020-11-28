#ifndef BUNDLENOTEBOOKCONFIGMGR_H
#define BUNDLENOTEBOOKCONFIGMGR_H

#include "inotebookconfigmgr.h"

namespace vnotex
{
    class BundleNotebook;

    class BundleNotebookConfigMgr : public INotebookConfigMgr
    {
        Q_OBJECT
    public:
        BundleNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend,
                                QObject *p_parent = nullptr);

        // Create an empty skeleton for an empty notebook.
        virtual void createEmptySkeleton(const NotebookParameters &p_paras) Q_DECL_OVERRIDE;

        QSharedPointer<NotebookConfig> readNotebookConfig() const;
        void writeNotebookConfig();

        void removeNotebookConfig();

        bool isBuiltInFile(const Node *p_node, const QString &p_name) const Q_DECL_OVERRIDE;

        bool isBuiltInFolder(const Node *p_node, const QString &p_name) const Q_DECL_OVERRIDE;

        static const QString &getConfigFolderName();

        static const QString &getConfigName();

        static QString getConfigFilePath();

        static QSharedPointer<NotebookConfig> readNotebookConfig(const QSharedPointer<INotebookBackend> &p_backend);

        enum { RootNodeId = 1 };

    protected:
        BundleNotebook *getBundleNotebook() const;

    private:
        void writeNotebookConfig(const NotebookConfig &p_config);

        // Folder name to store the notebook's config.
        // This folder locates in the root folder of the notebook.
        static const QString c_configFolderName;

        // Name of the notebook's config file.
        static const QString c_configName;
    };
} // ns vnotex

#endif // BUNDLENOTEBOOKCONFIGMGR_H
