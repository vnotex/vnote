#ifndef INOTEBOOKFACTORY_H
#define INOTEBOOKFACTORY_H

#include <QSharedPointer>
#include <QIcon>

namespace vnotex
{
    class Notebook;
    class NotebookParameters;
    class INotebookBackend;
    class NotebookMgr;

    // Abstract factory to create notebook.
    class INotebookFactory
    {
    public:
        virtual ~INotebookFactory()
        {
        }

        // Get the name of this factory.
        virtual QString getName() const = 0;

        // Get the display name of this factory.
        virtual QString getDisplayName() const = 0;

        // Get the description of this factory.
        virtual QString getDescription() const = 0;

        // New a notebook with given information and return an instance of that notebook.
        // The root folder should be empty.
        virtual QSharedPointer<Notebook> newNotebook(const NotebookParameters &p_paras) = 0;

        // Create a Notebook instance from existing root folder.
        virtual QSharedPointer<Notebook> createNotebook(const NotebookMgr &p_mgr,
                                                        const QString &p_rootFolderPath,
                                                        const QSharedPointer<INotebookBackend> &p_backend) = 0;

        // Check if @p_rootFolderPath is a valid root folder to use by this factory
        // to create a notebook.
        virtual bool checkRootFolder(const QSharedPointer<INotebookBackend> &p_backend) = 0;
    };
} // ns vnotex

#endif // INOTEBOOKFACTORY_H
