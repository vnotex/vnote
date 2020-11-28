#ifndef DUMMYVERSIONCONTROLLERFACTORY_H
#define DUMMYVERSIONCONTROLLERFACTORY_H

#include "iversioncontrollerfactory.h"


namespace vnotex
{
    class DummyVersionControllerFactory : public IVersionControllerFactory
    {
    public:
        DummyVersionControllerFactory();

        QString getName() const Q_DECL_OVERRIDE;

        QString getDisplayName() const Q_DECL_OVERRIDE;

        QString getDescription()const Q_DECL_OVERRIDE;

        QSharedPointer<IVersionController> createVersionController() Q_DECL_OVERRIDE;
    };
} // ns vnotex

#endif // DUMMYVERSIONCONTROLLERFACTORY_H
