#ifndef NOTEBOOKCONFIG_H
#define NOTEBOOKCONFIG_H

#include <QJsonObject>
#include <QSharedPointer>
#include <QDateTime>

#include "bundlenotebookconfigmgr.h"
#include "global.h"

namespace vnotex
{
    class NotebookParameters;

    class NotebookConfig
    {
    public:
        virtual ~NotebookConfig() {}

        static QSharedPointer<NotebookConfig> fromNotebookParameters(const QString &p_version,
                                                                     const NotebookParameters &p_paras);

        static QSharedPointer<NotebookConfig> fromNotebook(const QString &p_version,
                                                           const Notebook *p_notebook);

        virtual QJsonObject toJson() const;

        virtual void fromJson(const QJsonObject &p_jobj);

        QString m_version;

        QString m_name;

        QString m_description;

        QString m_imageFolder;

        QString m_attachmentFolder;

        QDateTime m_createdTimeUtc;

        QString m_versionController;

        QString m_notebookConfigMgr;

        ID m_nextNodeId = BundleNotebookConfigMgr::RootNodeId + 1;

        static const QString c_version;

        static const QString c_name;

        static const QString c_description;

        static const QString c_imageFolder;

        static const QString c_attachmentFolder;

        static const QString c_createdTimeUtc;

        static const QString c_versionController;

        static const QString c_configMgr;

        static const QString c_nextNodeId;
    };
} // ns vnotex

#endif // NOTEBOOKCONFIG_H
