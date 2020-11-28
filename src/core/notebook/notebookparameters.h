#ifndef NOTEBOOKPARAMETERS_H
#define NOTEBOOKPARAMETERS_H

#include <QSharedPointer>
#include <QIcon>
#include <QDateTime>

namespace vnotex
{
    class NotebookMgr;
    class INotebookBackend;
    class IVersionController;
    class INotebookConfigMgr;

    // Used to new a notebook.
    class NotebookParameters
    {
    public:
        virtual ~NotebookParameters() {}

        static QSharedPointer<NotebookParameters> createNotebookParameters(
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
                const QString &p_configMgr);

        static QSharedPointer<NotebookParameters> createNotebookParameters(
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
                const QString &p_configMgr);

        QString m_type;
        QString m_name;
        QString m_description;
        QString m_rootFolderPath;
        QIcon m_icon;

        // Name of image folder.
        QString m_imageFolder;

        // Name of attachment folder.
        QString m_attachmentFolder;

        QDateTime m_createdTimeUtc;
        QSharedPointer<INotebookBackend> m_notebookBackend;
        QSharedPointer<IVersionController> m_versionController;
        QSharedPointer<INotebookConfigMgr> m_notebookConfigMgr;

        bool m_ensureEmptyRootFolder = true;
    };
} // ns vnotex

#endif // NOTEBOOKPARAMETERS_H
