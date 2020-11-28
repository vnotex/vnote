#include "notebookparameters.h"

#include "notebookmgr.h"

using namespace vnotex;

QSharedPointer<NotebookParameters> NotebookParameters::createNotebookParameters(
        const NotebookMgr &p_mgr,
        const QString &p_type,
        const QString &p_name,
        const QString &p_description,
        const QString &p_rootFolderPath,
        const QIcon &p_icon,
        const QString &p_imageFolder,
        const QString &p_attachmentFolder,
        const QDateTime &p_createdTimeUtc,
        const QString &p_backend,
        const QString &p_versionController,
        const QString &p_configMgr)
{
    auto backend = p_mgr.createNotebookBackend(p_backend, p_rootFolderPath);
    return createNotebookParameters(p_mgr,
                                    backend,
                                    p_type,
                                    p_name,
                                    p_description,
                                    p_rootFolderPath,
                                    p_icon,
                                    p_imageFolder,
                                    p_attachmentFolder,
                                    p_createdTimeUtc,
                                    p_versionController,
                                    p_configMgr);
}

QSharedPointer<NotebookParameters> NotebookParameters::createNotebookParameters(
                const NotebookMgr &p_mgr,
                const QSharedPointer<INotebookBackend> &p_backend,
                const QString &p_type,
                const QString &p_name,
                const QString &p_description,
                const QString &p_rootFolderPath,
                const QIcon &p_icon,
                const QString &p_imageFolder,
                const QString &p_attachmentFolder,
                const QDateTime &p_createdTimeUtc,
                const QString &p_versionController,
                const QString &p_configMgr)
{
    auto paras = QSharedPointer<NotebookParameters>::create();
    paras->m_type = p_type;
    paras->m_name = p_name;
    paras->m_description = p_description;
    paras->m_rootFolderPath = p_rootFolderPath;
    paras->m_icon = p_icon;
    paras->m_imageFolder = p_imageFolder;
    paras->m_attachmentFolder = p_attachmentFolder;
    paras->m_createdTimeUtc = p_createdTimeUtc;
    paras->m_notebookBackend = p_backend;
    paras->m_versionController = p_mgr.createVersionController(p_versionController);
    paras->m_notebookConfigMgr = p_mgr.createNotebookConfigMgr(p_configMgr,
                                                               paras->m_notebookBackend);
    return paras;
}
