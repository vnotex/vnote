#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include "isearchengine.h"

#include <QThread>
#include <QRegularExpression>
#include <QAtomicInt>
#include <QVector>

#include "searchtoken.h"
#include "searchdata.h"

namespace vnotex
{
    struct SearchResultItem;

    class FileSearchEngineWorker : public QThread
    {
        Q_OBJECT
        friend class FileSearchEngine;
    public:
        explicit FileSearchEngineWorker(QObject *p_parent = nullptr);

        ~FileSearchEngineWorker() = default;

        void setData(const QVector<SearchSecondPhaseItem> &p_items,
                     const QSharedPointer<SearchOption> &p_option,
                     const SearchToken &p_token);

    public slots:
        void stop();

    signals:
        void resultItemsReady(const QVector<QSharedPointer<SearchResultItem>> &p_items);

    protected:
        void run() Q_DECL_OVERRIDE;

    private:
        void appendError(const QString &p_err);

        void searchFile(const QString &p_filePath, const QString &p_displayPath);

        void processBatchResults();

        bool isAskedToStop() const;

        QAtomicInt m_askedToStop = 0;

        QVector<SearchSecondPhaseItem> m_items;

        SearchToken m_token;

        QSharedPointer<SearchOption> m_option;

        SearchState m_state = SearchState::Idle;

        QStringList m_errors;

        QVector<QSharedPointer<SearchResultItem>> m_results;
    };

    class FileSearchEngine : public ISearchEngine
    {
        Q_OBJECT
    public:
        FileSearchEngine();

        ~FileSearchEngine();

        void search(const QSharedPointer<SearchOption> &p_option,
                    const SearchToken &p_token,
                    const QVector<SearchSecondPhaseItem> &p_items);

        void stop() Q_DECL_OVERRIDE;

        void clear() Q_DECL_OVERRIDE;

    private slots:
        void handleWorkerFinished();

    private:
        void clearWorkers();

        // Need non-virtual version of this.
        void stopInternal();

        // Need non-virtual version of this.
        void clearInternal();

        int m_numOfFinishedWorkers = 0;

        QVector<QSharedPointer<FileSearchEngineWorker>> m_workers;
    };
}

#endif // SEARCHENGINE_H
