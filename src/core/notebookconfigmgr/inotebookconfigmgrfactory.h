#ifndef INOTEBOOKCONFIGMGRFACTORY_H
#define INOTEBOOKCONFIGMGRFACTORY_H

#include <QSharedPointer>

namespace vnotex
{
    class INotebookConfigMgr;
    class INotebookBackend;

    class INotebookConfigMgrFactory
    {
    public:
        INotebookConfigMgrFactory()
        {
        }

        virtual ~INotebookConfigMgrFactory()
        {
        }

        virtual QString getName() const = 0;

        virtual QString getDisplayName() const = 0;

        virtual QString getDescription() const = 0;

        virtual QSharedPointer<INotebookConfigMgr> createNotebookConfigMgr(
            const QSharedPointer<INotebookBackend> &p_backend) = 0;
    };
} // ns vnotex

#endif // INOTEBOOKCONFIGMGRFACTORY_H
