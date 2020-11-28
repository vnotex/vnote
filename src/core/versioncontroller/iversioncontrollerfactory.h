#ifndef IVERSIONCONTROLLERFACTORY_H
#define IVERSIONCONTROLLERFACTORY_H

#include <QSharedPointer>

namespace vnotex
{
    class IVersionController;

    class IVersionControllerFactory
    {
    public:
        IVersionControllerFactory()
        {
        }

        virtual ~IVersionControllerFactory()
        {
        }

        virtual QString getName() const = 0;

        virtual QString getDisplayName() const = 0;

        virtual QString getDescription() const = 0;

        virtual QSharedPointer<IVersionController> createVersionController() = 0;
    };
} // ns vnotex

#endif // IVERSIONCONTROLLERFACTORY_H
