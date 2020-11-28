#include "localnotebookbackendfactory.h"

#include <QObject>

#include "localnotebookbackend.h"

using namespace vnotex;

LocalNotebookBackendFactory::LocalNotebookBackendFactory()
{
}

QString LocalNotebookBackendFactory::getName() const
{
    return QStringLiteral("local.vnotex");
}

QString LocalNotebookBackendFactory::getDisplayName() const
{
    return QObject::tr("Local Notebook Backend");
}

QString LocalNotebookBackendFactory::getDescription() const
{
    return QObject::tr("Local file system");
}

QSharedPointer<INotebookBackend> LocalNotebookBackendFactory::createNotebookBackend(const QString &p_rootPath)
{
    return QSharedPointer<LocalNotebookBackend>::create(getName(),
                                                        getDisplayName(),
                                                        getDescription(),
                                                        p_rootPath);
}
