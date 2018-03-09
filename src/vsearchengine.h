#ifndef VSEARCHENGINE_H
#define VSEARCHENGINE_H

#include <QThread>
#include <QRegExp>
#include <QAtomicInt>
#include "isearchengine.h"

class VSearchEngineWorker : public QThread
{
    Q_OBJECT

    friend class VSearchEngine;

public:
    explicit VSearchEngineWorker(QObject *p_parent = nullptr);

    void setData(const QStringList &p_files,
                 const QRegExp &p_reg,
                 const QString &p_keyword,
                 Qt::CaseSensitivity p_cs);

public slots:
    void stop();

signals:
    void resultItemReady(VSearchResultItem *p_item);

protected:
    void run() Q_DECL_OVERRIDE;

private:
    void appendError(const QString &p_err);

    VSearchResultItem *searchFile(const QString &p_fileName);

    QAtomicInt m_stop;

    QStringList m_files;

    QRegExp m_reg;

    QString m_keyword;

    Qt::CaseSensitivity m_caseSensitivity;

    VSearchState m_state;

    QString m_error;
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

    void search(const QSharedPointer<VSearchConfig> &p_config,
                const QSharedPointer<VSearchResult> &p_result) Q_DECL_OVERRIDE;

    void stop() Q_DECL_OVERRIDE;

    void clear() Q_DECL_OVERRIDE;

private slots:
    void handleWorkerFinished();

private:
    // Returns an empty object if raw string is preferred.
    QRegExp compileRegExpFromConfig(const QSharedPointer<VSearchConfig> &p_config) const;

    void clearAllWorkers();

    int m_finishedWorkers;

    QVector<VSearchEngineWorker *> m_workers;
};

#endif // VSEARCHENGINE_H
