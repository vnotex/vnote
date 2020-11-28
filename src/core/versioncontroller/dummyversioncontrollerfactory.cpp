#include "dummyversioncontrollerfactory.h"

using namespace vnotex;

#include <QObject>

#include "dummyversioncontroller.h"

DummyVersionControllerFactory::DummyVersionControllerFactory()
{
}

QString DummyVersionControllerFactory::getName() const
{
    return QStringLiteral("dummy.vnotex");
}

QString DummyVersionControllerFactory::getDisplayName() const
{
    return QObject::tr("Dummy Version Control");
}

QString DummyVersionControllerFactory::getDescription() const
{
    return QObject::tr("Disable version control");
}

QSharedPointer<IVersionController> DummyVersionControllerFactory::createVersionController()
{
    return QSharedPointer<DummyVersionController>::create(getName(),
                                                          getDisplayName(),
                                                          getDescription());
}
