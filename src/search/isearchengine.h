#ifndef ISEARCHENGINE_H
#define ISEARCHENGINE_H

#include <QObject>
#include <QVector>
#include <QList>
#include <QSharedPointer>

#include "searchdata.h"

namespace vnotex
{
    struct SearchResultItem;

    class SearchToken;

    struct SearchSecondPhaseItem
    {
        SearchSecondPhaseItem() = default;

        SearchSecondPhaseItem(const QString &p_filePath, const QString &p_displayPath)
            : m_filePath(p_filePath),
              m_displayPath(p_displayPath)
        {
        }

        QString m_filePath;

        QString m_displayPath;
    };

    class ISearchEngine : public QObject
    {
        Q_OBJECT
    public:
        ISearchEngine() = default;

        virtual ~ISearchEngine() = default;

        virtual void search(const QSharedPointer<SearchOption> &p_option,
                            const SearchToken &p_token,
                            const QVector<SearchSecondPhaseItem> &p_items) = 0;

        virtual void stop() = 0;

        virtual void clear() = 0;

    signals:
        void finished(SearchState p_state);

        void resultItemsAdded(const QVector<QSharedPointer<SearchResultItem>> &p_items);

        void logRequested(const QString &p_log);
    };
}

#endif // ISEARCHENGINE_H
