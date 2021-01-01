#include "notebookmgr.h"

#include <versioncontroller/dummyversioncontrollerfactory.h>
#include <versioncontroller/iversioncontroller.h>
#include <notebookconfigmgr/vxnotebookconfigmgrfactory.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <notebookbackend/localnotebookbackendfactory.h>
#include <notebookbackend/inotebookbackend.h>
#include <notebook/bundlenotebookfactory.h>
#include <notebook/notebook.h>
#include <notebook/notebookparameters.h>
#include "exception.h"
#include "configmgr.h"
#include <utils/pathutils.h>

using namespace vnotex;

NotebookMgr::NotebookMgr(QObject *p_parent)
    : QObject(p_parent),
      m_currentNotebookId(Notebook::InvalidId)
{
}

void NotebookMgr::init()
{
    initVersionControllerServer();

    initConfigMgrServer();

    initBackendServer();

    initNotebookServer();
}

void NotebookMgr::initVersionControllerServer()
{
    m_versionControllerServer.reset(new NameBasedServer<IVersionControllerFactory>);

    // Dummy Version Controller.
    auto dummyFactory = QSharedPointer<DummyVersionControllerFactory>::create();
    m_versionControllerServer->registerItem(dummyFactory->getName(), dummyFactory);
}

void NotebookMgr::initConfigMgrServer()
{
    m_configMgrServer.reset(new NameBasedServer<INotebookConfigMgrFactory>);

    // VX Notebook Config Manager.
    auto vxFactory = QSharedPointer<VXNotebookConfigMgrFactory>::create();
    m_configMgrServer->registerItem(vxFactory->getName(), vxFactory);

}

void NotebookMgr::initBackendServer()
{
    m_backendServer.reset(new NameBasedServer<INotebookBackendFactory>);

    // Local Notebook Backend.
    auto localFactory = QSharedPointer<LocalNotebookBackendFactory>::create();
    m_backendServer->registerItem(localFactory->getName(), localFactory);
}

void NotebookMgr::initNotebookServer()
{
    m_notebookServer.reset(new NameBasedServer<INotebookFactory>);

    // Bundle Notebook.
    auto bundleFacotry = QSharedPointer<BundleNotebookFactory>::create();
    m_notebookServer->registerItem(bundleFacotry->getName(), bundleFacotry);
}

QSharedPointer<INotebookFactory> NotebookMgr::getBundleNotebookFactory() const
{
    return m_notebookServer->getItem(QStringLiteral("bundle.vnotex"));
}

QList<QSharedPointer<INotebookFactory>> NotebookMgr::getAllNotebookFactories() const
{
    return m_notebookServer->getAllItems();
}

QList<QSharedPointer<IVersionControllerFactory>> NotebookMgr::getAllVersionControllerFactories() const
{
    return m_versionControllerServer->getAllItems();
}

QList<QSharedPointer<INotebookConfigMgrFactory>> NotebookMgr::getAllNotebookConfigMgrFactories() const
{
    return m_configMgrServer->getAllItems();
}

QList<QSharedPointer<INotebookBackendFactory>> NotebookMgr::getAllNotebookBackendFactories() const
{
    return m_backendServer->getAllItems();
}

QSharedPointer<INotebookBackend> NotebookMgr::createNotebookBackend(const QString &p_backendName,
                                                                    const QString &p_rootFolderPath) const
{
    auto factory = m_backendServer->getItem(p_backendName);
    if (factory) {
        return factory->createNotebookBackend(p_rootFolderPath);
    } else {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to find notebook backend factory %1").arg(p_backendName));
    }

    return nullptr;
}

QSharedPointer<IVersionController> NotebookMgr::createVersionController(const QString &p_controllerName) const
{
    auto factory = m_versionControllerServer->getItem(p_controllerName);
    if (factory) {
        return factory->createVersionController();
    } else {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to find version controller factory %1").arg(p_controllerName));
    }

    return nullptr;
}

QSharedPointer<INotebookConfigMgr> NotebookMgr::createNotebookConfigMgr(const QString &p_mgrName,
                                                                       const QSharedPointer<INotebookBackend> &p_backend) const
{
    auto factory = m_configMgrServer->getItem(p_mgrName);
    if (factory) {
        return factory->createNotebookConfigMgr(p_backend);
    } else {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to find notebook config manager factory %1").arg(p_mgrName));
    }

    return nullptr;
}

void NotebookMgr::loadNotebooks()
{
    readNotebooksFromConfig();

    loadCurrentNotebookId();
}

static SessionConfig &getSessionConfig()
{
    return ConfigMgr::getInst().getSessionConfig();
}

void NotebookMgr::loadCurrentNotebookId()
{
    auto &rootFolderPath = getSessionConfig().getCurrentNotebookRootFolderPath();
    auto notebook = findNotebookByRootFolderPath(rootFolderPath);
    if (notebook) {
        m_currentNotebookId = notebook->getId();
    } else {
        m_currentNotebookId = Notebook::InvalidId;
    }

    emit currentNotebookChanged(notebook);
}

QSharedPointer<Notebook> NotebookMgr::newNotebook(const QSharedPointer<NotebookParameters> &p_parameters)
{
    auto factory = m_notebookServer->getItem(p_parameters->m_type);
    if (!factory) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to find notebook factory %1").arg(p_parameters->m_type));
    }

    auto notebook = factory->newNotebook(*p_parameters);
    addNotebook(notebook);

    saveNotebooksToConfig();

    emit notebooksUpdated();

    setCurrentNotebook(notebook->getId());

    return notebook;
}

void NotebookMgr::importNotebook(const QSharedPointer<Notebook> &p_notebook)
{
    Q_ASSERT(p_notebook);
    if (m_notebooks.indexOf(p_notebook) != -1) {
        return;
    }

    addNotebook(p_notebook);

    saveNotebooksToConfig();

    emit notebooksUpdated();

    setCurrentNotebook(p_notebook->getId());
}

static SessionConfig::NotebookItem notebookToSessionConfig(const QSharedPointer<const Notebook> &p_notebook)
{
    SessionConfig::NotebookItem item;
    item.m_type = p_notebook->getType();
    item.m_rootFolderPath = p_notebook->getRootFolderPath();
    item.m_backend = p_notebook->getBackend()->getName();
    return item;
}

void NotebookMgr::saveNotebooksToConfig() const
{
    QVector<SessionConfig::NotebookItem> items;
    items.reserve(m_notebooks.size());
    for (auto &nb : m_notebooks) {
        items.push_back(notebookToSessionConfig(nb));
    }

    getSessionConfig().setNotebooks(items);
}

void NotebookMgr::readNotebooksFromConfig()
{
    Q_ASSERT(m_notebooks.isEmpty());
    auto items = getSessionConfig().getNotebooks();
    for (auto &item : items) {
        try {
            auto nb = readNotebookFromConfig(item);
            addNotebook(nb);
        } catch (Exception &p_e) {
            qCritical("failed to read notebook (%s) from config (%s)",
                      item.m_rootFolderPath.toStdString().c_str(),
                      p_e.what());
        }
    }

    emit notebooksUpdated();
}

QSharedPointer<Notebook> NotebookMgr::readNotebookFromConfig(const SessionConfig::NotebookItem &p_item)
{
    auto factory = m_notebookServer->getItem(p_item.m_type);
    if (!factory) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to find notebook factory %1").arg(p_item.m_type));
    }

    auto backend = createNotebookBackend(p_item.m_backend, p_item.m_rootFolderPath);

    auto notebook = factory->createNotebook(*this, p_item.m_rootFolderPath, backend);
    return notebook;
}

const QVector<QSharedPointer<Notebook>> &NotebookMgr::getNotebooks() const
{
    return m_notebooks;
}

ID NotebookMgr::getCurrentNotebookId() const
{
    return m_currentNotebookId;
}

void NotebookMgr::setCurrentNotebook(ID p_notebookId)
{
    auto lastId = m_currentNotebookId;
    m_currentNotebookId = p_notebookId;
    auto nb = findNotebookById(p_notebookId);
    if (!nb) {
        m_currentNotebookId = Notebook::InvalidId;
    }

    if (lastId != m_currentNotebookId) {
        emit currentNotebookChanged(nb);
    }

    getSessionConfig().setCurrentNotebookRootFolderPath(nb ? nb->getRootFolderPath() : "");
}

QSharedPointer<Notebook> NotebookMgr::findNotebookByRootFolderPath(const QString &p_rootFolderPath) const
{
    for (auto &nb : m_notebooks) {
        if (PathUtils::areSamePaths(nb->getRootFolderPath(), p_rootFolderPath)) {
            return nb;
        }
    }

    return nullptr;
}

QSharedPointer<Notebook> NotebookMgr::findNotebookById(ID p_id) const
{
    for (auto &nb : m_notebooks) {
        if (nb->getId() == p_id) {
            return nb;
        }
    }

    return nullptr;
}

void NotebookMgr::closeNotebook(ID p_id)
{
    auto it = std::find_if(m_notebooks.begin(),
                           m_notebooks.end(),
                           [p_id](const QSharedPointer<Notebook> &p_nb) {
                               return p_nb->getId() == p_id;
                           });
    if (it == m_notebooks.end()) {
        qWarning() << "failed to find notebook of given id to close" << p_id;
        return;
    }

    auto notebookToClose = *it;
    emit notebookAboutToClose(notebookToClose.data());

    m_notebooks.erase(it);

    saveNotebooksToConfig();

    emit notebooksUpdated();
    setCurrentNotebookAfterUpdate();

    qInfo() << QString("notebook %1 (%2) is closed").arg(notebookToClose->getName(),
                                                         notebookToClose->getRootFolderPath());
}

void NotebookMgr::removeNotebook(ID p_id)
{
    auto it = std::find_if(m_notebooks.begin(),
                           m_notebooks.end(),
                           [p_id](const QSharedPointer<Notebook> &p_nb) {
                               return p_nb->getId() == p_id;
                           });
    if (it == m_notebooks.end()) {
        qWarning() << "failed to find notebook of given id to remove" << p_id;
        return;
    }

    auto nbToRemove = *it;
    emit notebookAboutToRemove(nbToRemove.data());

    m_notebooks.erase(it);

    saveNotebooksToConfig();

    emit notebooksUpdated();
    setCurrentNotebookAfterUpdate();

    try {
        nbToRemove->remove();
    } catch (Exception &p_e) {
        qWarning() << QString("failed to remove notebook %1 (%2) (%3)").arg(nbToRemove->getName(),
                                                                          nbToRemove->getRootFolderPath(),
                                                                          p_e.what());
        throw;
    }

    qInfo() << QString("notebook %1 (%2) is removed").arg(nbToRemove->getName(),
                                                          nbToRemove->getRootFolderPath());
}

void NotebookMgr::setCurrentNotebookAfterUpdate()
{
    if (!m_notebooks.isEmpty()) {
        setCurrentNotebook(m_notebooks.first()->getId());
    } else {
        setCurrentNotebook(Notebook::InvalidId);
    }
}

void NotebookMgr::addNotebook(const QSharedPointer<Notebook> &p_notebook)
{
    m_notebooks.push_back(p_notebook);
    connect(p_notebook.data(), &Notebook::updated,
            this, [this, notebook = p_notebook.data()]() {
                emit notebookUpdated(notebook);
            });
}
