#ifndef DUMMYNOTEBOOK_H
#define DUMMYNOTEBOOK_H

#include <notebook/notebook.h>

namespace tests
{
    class DummyNotebook : public vnotex::Notebook
    {
        Q_OBJECT
    public:
        DummyNotebook(const QString &p_name, QObject *p_parent = nullptr);

        void updateNotebookConfig() Q_DECL_OVERRIDE;

        void removeNotebookConfig() Q_DECL_OVERRIDE;

        void remove() Q_DECL_OVERRIDE;

        const QVector<vnotex::HistoryItem> &getHistory() const Q_DECL_OVERRIDE;
        void addHistory(const vnotex::HistoryItem &p_item) Q_DECL_OVERRIDE;
        void clearHistory() Q_DECL_OVERRIDE;

        const QJsonObject &getExtraConfigs() const Q_DECL_OVERRIDE;
        void setExtraConfig(const QString &p_key, const QJsonObject &p_obj) Q_DECL_OVERRIDE;

        QSharedPointer<vnotex::Node> loadNodeByPath(const QString &p_path) Q_DECL_OVERRIDE;

    protected:
        void initializeInternal() Q_DECL_OVERRIDE;

        QVector<vnotex::HistoryItem> m_history;

        QJsonObject m_extraConfigs;
    };
}

#endif // DUMMYNOTEBOOK_H
