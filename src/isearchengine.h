#ifndef ISEARCHENGINE_H
#define ISEARCHENGINE_H

#include <QObject>
#include <QVector>
#include <QList>

#include "vsearchconfig.h"

// Abstract class for search engine.
class ISearchEngine : public QObject
{
    Q_OBJECT
public:
    explicit ISearchEngine(QObject *p_parent = nullptr)
        : QObject(p_parent)
    {
    }

    virtual ~ISearchEngine()
    {
        m_result.clear();
    }

    virtual void search(const QSharedPointer<VSearchConfig> &p_config,
                        const QSharedPointer<VSearchResult> &p_result) = 0;

    virtual void stop() = 0;

    virtual void clear() = 0;

signals:
    void finished(const QSharedPointer<VSearchResult> &p_result);

    void resultItemsAdded(const QList<QSharedPointer<VSearchResultItem> > &p_items);

protected:
    QSharedPointer<VSearchResult> m_result;
};
#endif // ISEARCHENGINE_H
