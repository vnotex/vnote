#ifndef VXNOTEBOOKCONFIGMGRFACTORY_H
#define VXNOTEBOOKCONFIGMGRFACTORY_H


#include "inotebookconfigmgrfactory.h"


namespace vnotex
{
    class VXNotebookConfigMgrFactory : public INotebookConfigMgrFactory
    {
    public:
        VXNotebookConfigMgrFactory();

        QString getName() const Q_DECL_OVERRIDE;

        QString getDisplayName() const Q_DECL_OVERRIDE;

        QString getDescription()const Q_DECL_OVERRIDE;

        QSharedPointer<INotebookConfigMgr> createNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend) Q_DECL_OVERRIDE;
    };
} // ns vnotex

#endif // VXNOTEBOOKCONFIGMGRFACTORY_H
