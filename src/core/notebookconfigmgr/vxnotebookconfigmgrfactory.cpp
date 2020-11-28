#include "vxnotebookconfigmgrfactory.h"

#include <QObject>

#include "vxnotebookconfigmgr.h"
#include "../notebookbackend/inotebookbackend.h"

using namespace vnotex;

VXNotebookConfigMgrFactory::VXNotebookConfigMgrFactory()
{
}

QString VXNotebookConfigMgrFactory::getName() const
{
    return QStringLiteral("vx.vnotex");
}

QString VXNotebookConfigMgrFactory::getDisplayName() const
{
    return QObject::tr("VNoteX Notebook Configuration");
}

QString VXNotebookConfigMgrFactory::getDescription() const
{
    return QObject::tr("Built-in VNoteX notebook configuration");
}

QSharedPointer<INotebookConfigMgr> VXNotebookConfigMgrFactory::createNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend)
{
    return QSharedPointer<VXNotebookConfigMgr>::create(getName(),
                                                       getDisplayName(),
                                                       getDescription(),
                                                       p_backend);
}
