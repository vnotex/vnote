#ifndef LOCALNOTEBOOKBACKENDFACTORY_H
#define LOCALNOTEBOOKBACKENDFACTORY_H

#include "inotebookbackendfactory.h"


namespace vnotex
{
    class LocalNotebookBackendFactory : public INotebookBackendFactory
    {
    public:
        LocalNotebookBackendFactory();

        QString getName() const Q_DECL_OVERRIDE;

        QString getDisplayName() const Q_DECL_OVERRIDE;

        QString getDescription()const Q_DECL_OVERRIDE;

        QSharedPointer<INotebookBackend> createNotebookBackend(const QString &p_rootPath) Q_DECL_OVERRIDE;
    };
} // ns vnotex

#endif // LOCALNOTEBOOKBACKENDFACTORY_H
