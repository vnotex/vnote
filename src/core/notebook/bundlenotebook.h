#ifndef BUNDLENOTEBOOK_H
#define BUNDLENOTEBOOK_H

#include "notebook.h"
#include "global.h"

namespace vnotex
{
    class BundleNotebookConfigMgr;
    class NotebookConfig;

    class BundleNotebook : public Notebook
    {
        Q_OBJECT
    public:
        BundleNotebook(const NotebookParameters &p_paras,
                       const QSharedPointer<NotebookConfig> &p_notebookConfig,
                       QObject *p_parent = nullptr);

        ID getNextNodeId() const Q_DECL_OVERRIDE;

        ID getAndUpdateNextNodeId() Q_DECL_OVERRIDE;

        void updateNotebookConfig() Q_DECL_OVERRIDE;

        void removeNotebookConfig() Q_DECL_OVERRIDE;

        void remove() Q_DECL_OVERRIDE;

        const QVector<HistoryItem> &getHistory() const Q_DECL_OVERRIDE;
        void addHistory(const HistoryItem &p_item) Q_DECL_OVERRIDE;
        void clearHistory() Q_DECL_OVERRIDE;

        const QJsonObject &getExtraConfigs() const Q_DECL_OVERRIDE;
        void setExtraConfig(const QString &p_key, const QJsonObject &p_obj) Q_DECL_OVERRIDE;

    private:
        BundleNotebookConfigMgr *getBundleNotebookConfigMgr() const;

        ID m_nextNodeId = 1;

        QVector<HistoryItem> m_history;

        QJsonObject m_extraConfigs;
    };
} // ns vnotex

#endif // BUNDLENOTEBOOK_H
