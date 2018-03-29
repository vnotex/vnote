#ifndef VSEARCHENGINE_H
#define VSEARCHENGINE_H

#include "isearchengine.h"

#include <QThread>
#include <QRegExp>
#include <QAtomicInt>
#include <QList>

#include "vsearchconfig.h"

#define BATCH_ITEM_SIZE 100

class VSearchEngineWorker : public QThread
{
    Q_OBJECT

    friend class VSearchEngine;

public:
    explicit VSearchEngineWorker(QObject *p_parent = nullptr);

    void setData(const QStringList &p_files,
                 const VSearchToken &p_token);

public slots:
    void stop();

signals:
    void resultItemsReady(const QList<QSharedPointer<VSearchResultItem> > &p_items);

protected:
    void run() Q_DECL_OVERRIDE;

private:
    void appendError(const QString &p_err);

    VSearchResultItem *searchFile(const QString &p_fileName);

    void postAndClearResults();

    QAtomicInt m_stop;

    QStringList m_files;

    VSearchToken m_token;

    VSearchState m_state;

    QString m_error;

    QList<QSharedPointer<VSearchResultItem> > m_results;
};

inline void VSearchEngineWorker::appendError(const QString &p_err)
{
    if (m_error.isEmpty()) {
        m_error = p_err;
    } else {
        m_error = "\n" + p_err;
    }
}


class VSearchEngine : public ISearchEngine
{
    Q_OBJECT
public:
    explicit VSearchEngine(QObject *p_parent = nullptr);

    ~VSearchEngine();

    void search(const QSharedPointer<VSearchConfig> &p_config,
                const QSharedPointer<VSearchResult> &p_result) Q_DECL_OVERRIDE;

    void stop() Q_DECL_OVERRIDE;

    void clear() Q_DECL_OVERRIDE;

private slots:
    void handleWorkerFinished();

private:
    void clearAllWorkers();

    int m_finishedWorkers;

    QVector<VSearchEngineWorker *> m_workers;
};

#endif // VSEARCHENGINE_H
