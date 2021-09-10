#ifndef BUNDLENOTEBOOK_H
#define BUNDLENOTEBOOK_H

#include "notebook.h"
#include "global.h"

namespace vnotex
{
    class BundleNotebookConfigMgr;
    class NotebookConfig;
    class NotebookDatabaseAccess;

    class BundleNotebook : public Notebook
    {
        Q_OBJECT
    public:
        BundleNotebook(const NotebookParameters &p_paras,
                       const QSharedPointer<NotebookConfig> &p_notebookConfig,
                       QObject *p_parent = nullptr);

        ~BundleNotebook();

        void updateNotebookConfig() Q_DECL_OVERRIDE;

        void removeNotebookConfig() Q_DECL_OVERRIDE;

        void remove() Q_DECL_OVERRIDE;

        const QVector<HistoryItem> &getHistory() const Q_DECL_OVERRIDE;
        void addHistory(const HistoryItem &p_item) Q_DECL_OVERRIDE;
        void clearHistory() Q_DECL_OVERRIDE;

        const QJsonObject &getExtraConfigs() const Q_DECL_OVERRIDE;
        void setExtraConfig(const QString &p_key, const QJsonObject &p_obj) Q_DECL_OVERRIDE;

        bool rebuildDatabase() Q_DECL_OVERRIDE;

        NotebookDatabaseAccess *getDatabaseAccess() const;

    protected:
        void initializeInternal() Q_DECL_OVERRIDE;

    private:
        BundleNotebookConfigMgr *getBundleNotebookConfigMgr() const;

        void setupDatabase();

        void fillNodeTableFromConfig(Node *p_node, bool p_ignoreId, int &p_totalCnt);

        void initDatabase();

        const int m_configVersion;

        QVector<HistoryItem> m_history;

        QJsonObject m_extraConfigs;

        // Managed by QObject.
        NotebookDatabaseAccess *m_dbAccess = nullptr;
    };
} // ns vnotex

#endif // BUNDLENOTEBOOK_H
