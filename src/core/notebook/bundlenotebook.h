#ifndef BUNDLENOTEBOOK_H
#define BUNDLENOTEBOOK_H

#include "notebook.h"
#include "global.h"
#include "historyi.h"

namespace vnotex
{
    class BundleNotebookConfigMgr;
    class NotebookConfig;
    class NotebookDatabaseAccess;
    class NotebookTagMgr;

    class BundleNotebook : public Notebook,
                           public HistoryI
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

        const QString &getTagGraph() const;
        void updateTagGraph(const QString &p_tagGraph);

        const QJsonObject &getExtraConfigs() const Q_DECL_OVERRIDE;
        void setExtraConfig(const QString &p_key, const QJsonObject &p_obj) Q_DECL_OVERRIDE;

        bool rebuildDatabase() Q_DECL_OVERRIDE;

        NotebookDatabaseAccess *getDatabaseAccess() const;

        TagI *tag() Q_DECL_OVERRIDE;

        int getConfigVersion() const;

        // HistoryI.
    public:
        HistoryI *history() Q_DECL_OVERRIDE;

        const QVector<HistoryItem> &getHistory() const Q_DECL_OVERRIDE;

        void removeHistory(const QString &p_itemPath) Q_DECL_OVERRIDE;

        void addHistory(const HistoryItem &p_item) Q_DECL_OVERRIDE;

        void clearHistory() Q_DECL_OVERRIDE;

    protected:
        void initializeInternal() Q_DECL_OVERRIDE;

    private:
        BundleNotebookConfigMgr *getBundleNotebookConfigMgr() const;

        void setupDatabase();

        void fillNodeTableFromConfig(Node *p_node, bool p_ignoreId, int &p_totalCnt);

        void initDatabase();

        void fillTagTableFromTagGraph();

        void fillTagTableFromConfig(Node *p_node, int &p_totalCnt);

        NotebookTagMgr *getTagMgr() const;

        const int m_configVersion;

        QVector<HistoryItem> m_history;

        QString m_tagGraph;

        QJsonObject m_extraConfigs;

        // Managed by QObject.
        NotebookDatabaseAccess *m_dbAccess = nullptr;

        // Managed by QObject.
        NotebookTagMgr *m_tagMgr = nullptr;
    };
} // ns vnotex

#endif // BUNDLENOTEBOOK_H
