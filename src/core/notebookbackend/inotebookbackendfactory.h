#ifndef INOTEBOOKBACKENDFACTORY_H
#define INOTEBOOKBACKENDFACTORY_H

#include <QSharedPointer>

namespace vnotex
{
    class INotebookBackend;

    class INotebookBackendFactory
    {
    public:
        INotebookBackendFactory()
        {
        }

        virtual ~INotebookBackendFactory()
        {
        }

        virtual QString getName() const = 0;

        virtual QString getDisplayName() const = 0;

        virtual QString getDescription() const = 0;

        virtual QSharedPointer<INotebookBackend> createNotebookBackend(const QString &p_rootPath) = 0;
    };
} // ns vnotex

#endif // INOTEBOOKBACKENDFACTORY_H
