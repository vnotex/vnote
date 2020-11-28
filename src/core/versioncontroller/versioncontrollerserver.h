#ifndef VERSIONCONTROLLERSERVER_H
#define VERSIONCONTROLLERSERVER_H

#include <QHash>
#include <QSharedPointer>

namespace vnotex
{
    class IVersionController;
    class IVersionControllerFactory;

    class VersionControllerServer
    {
    public:
        VersionControllerServer();

        // Register a factory.
        bool registerFactory(const QSharedPointer<IVersionControllerFactory> &p_factory);

        // @p_name: Name of the version controller to create.
        QSharedPointer<IVersionController> createVersionController(const QString &p_name);

    private:
        // Name to factory mapping.
        QHash<QString, QSharedPointer<IVersionControllerFactory>> m_factories;
    };
} // ns vnotex

#endif // VERSIONCONTROLLERSERVER_H
