#include "versioncontrollerserver.h"

#include <QDebug>

#include "iversioncontroller.h"
#include "iversioncontrollerfactory.h"

using namespace vnotex;

VersionControllerServer::VersionControllerServer()
{
}

bool VersionControllerServer::registerFactory(const QSharedPointer<IVersionControllerFactory> &p_factory)
{
    Q_ASSERT(p_factory);
    if (m_factories.contains(p_factory->getName())) {
        qWarning() << "VersionControllerFactory to register already exists"
                   << p_factory->getName()
                   << p_factory->getDisplayName();
        return false;
    }

    qDebug() << "VersionControllerFactory" << p_factory->getName() << "registered";
    m_factories.insert(p_factory->getName(), p_factory);
    return true;
}

QSharedPointer<IVersionController> VersionControllerServer::createVersionController(const QString &p_name)
{
    auto it = m_factories.find(p_name);
    if (it != m_factories.end()) {
        auto &factory = it.value();
        return factory->createVersionController();
    }

    return nullptr;
}
