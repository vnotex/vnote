#ifndef NOTEBOOKMGR_H
#define NOTEBOOKMGR_H

#include <QObject>
#include <QScopedPointer>
#include <QList>
#include <QVector>

#include "namebasedserver.h"
#include "sessionconfig.h"
#include "global.h"
#include "notebook/notebook.h"

namespace vnotex
{
    class IVersionController;
    class IVersionControllerFactory;
    class INotebookConfigMgr;
    class INotebookConfigMgrFactory;
    class INotebookBackend;
    class INotebookBackendFactory;
    class INotebookFactory;
    class NotebookParameters;

    class NotebookMgr : public QObject
    {
        Q_OBJECT
    public:
        explicit NotebookMgr(QObject *p_parent = nullptr);

        void init();

        QSharedPointer<INotebookFactory> getBundleNotebookFactory() const;

        QList<QSharedPointer<INotebookFactory>> getAllNotebookFactories() const;

        QList<QSharedPointer<IVersionControllerFactory>> getAllVersionControllerFactories() const;

        QList<QSharedPointer<INotebookConfigMgrFactory>> getAllNotebookConfigMgrFactories() const;

        QList<QSharedPointer<INotebookBackendFactory>> getAllNotebookBackendFactories() const;

        QSharedPointer<INotebookBackend> createNotebookBackend(const QString &p_backendName,
                                                               const QString &p_rootFolderPath) const;

        QSharedPointer<IVersionController> createVersionController(const QString &p_controllerName) const;

        QSharedPointer<INotebookConfigMgr> createNotebookConfigMgr(const QString &p_mgrName,
                                                                   const QSharedPointer<INotebookBackend> &p_backend) const;

        void loadNotebooks();

        QSharedPointer<Notebook> newNotebook(const QSharedPointer<NotebookParameters> &p_parameters);

        void importNotebook(const QSharedPointer<Notebook> &p_notebook);

        const QVector<QSharedPointer<Notebook>> &getNotebooks() const;

        ID getCurrentNotebookId() const;

        // Find the notebook with the same directory as root folder.
        QSharedPointer<Notebook> findNotebookByRootFolderPath(const QString &p_rootFolderPath) const;

        QSharedPointer<Notebook> findNotebookById(ID p_id) const;

        void closeNotebook(ID p_id);

        void removeNotebook(ID p_id);

    public slots:
        void setCurrentNotebook(ID p_notebookId);

    signals:
        void notebooksUpdated();

        void notebookUpdated(const Notebook *p_notebook);

        void notebookAboutToClose(const Notebook *p_notebook);

        void notebookAboutToRemove(const Notebook *p_notebook);

        void currentNotebookChanged(const QSharedPointer<Notebook> &p_notebook);

    private:
        void initVersionControllerServer();

        void initConfigMgrServer();

        void initBackendServer();

        void initNotebookServer();

        void saveNotebooksToConfig() const;
        void readNotebooksFromConfig();

        void loadCurrentNotebookId();

        QSharedPointer<Notebook> readNotebookFromConfig(const SessionConfig::NotebookItem &p_item);

        void setCurrentNotebookAfterUpdate();

        void addNotebook(const QSharedPointer<Notebook> &p_notebook);

        QSharedPointer<NameBasedServer<IVersionControllerFactory>> m_versionControllerServer;

        QSharedPointer<NameBasedServer<INotebookConfigMgrFactory>> m_configMgrServer;

        QSharedPointer<NameBasedServer<INotebookBackendFactory>> m_backendServer;

        QSharedPointer<NameBasedServer<INotebookFactory>> m_notebookServer;

        QVector<QSharedPointer<Notebook>> m_notebooks;

        ID m_currentNotebookId = 0;
    };
} // ns vnotex

#endif // NOTEBOOKMGR_H
