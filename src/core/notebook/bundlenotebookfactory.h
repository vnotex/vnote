#ifndef BUNDLENOTEBOOKFACTORY_H
#define BUNDLENOTEBOOKFACTORY_H

#include "inotebookfactory.h"


namespace vnotex
{
    class BundleNotebookFactory : public INotebookFactory
    {
    public:
        BundleNotebookFactory();

        // Get the name of this factory.
        QString getName() const Q_DECL_OVERRIDE;

        // Get the display name of this factory.
        QString getDisplayName() const Q_DECL_OVERRIDE;

        // Get the description of this factory.
        QString getDescription() const Q_DECL_OVERRIDE;

        // New a notebook with given information and return an instance of that notebook.
        QSharedPointer<Notebook> newNotebook(const NotebookParameters &p_paras) Q_DECL_OVERRIDE;

        // Create a Notebook instance from existing root folder.
        QSharedPointer<Notebook> createNotebook(const NotebookMgr &p_mgr,
                                                const QString &p_rootFolderPath,
                                                const QSharedPointer<INotebookBackend> &p_backend) Q_DECL_OVERRIDE;

        bool checkRootFolder(const QSharedPointer<INotebookBackend> &p_backend) Q_DECL_OVERRIDE;

    private:
        void checkParameters(const NotebookParameters &p_paras) const;
    };
} // ns vnotex

#endif // BUNDLENOTEBOOKFACTORY_H
