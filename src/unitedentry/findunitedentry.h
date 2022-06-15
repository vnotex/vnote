#ifndef FINDUNITEDENTRY_H
#define FINDUNITEDENTRY_H

#include "iunitedentry.h"

#include <QCommandLineParser>
#include <QSharedPointer>

#include <search/searchdata.h>

class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace vnotex
{
    class Searcher;
    class ISearchInfoProvider;
    struct ComplexLocation;
    class SearchToken;

    class FindUnitedEntry : public IUnitedEntry
    {
        Q_OBJECT
    public:
        FindUnitedEntry(const QSharedPointer<ISearchInfoProvider> &p_provider,
                        UnitedEntryMgr *p_mgr,
                        QObject *p_parent = nullptr);

        void stop() Q_DECL_OVERRIDE;

        QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

    protected:
        void initOnFirstProcess() Q_DECL_OVERRIDE;

        void processInternal(const QString &p_args,
                             const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) Q_DECL_OVERRIDE;

    private:
        QString getHelpText() const;

        QSharedPointer<SearchOption> collectOptions() const;

        Searcher *getSearcher();

        void handleSearchFinished(SearchState p_state);

        void prepareResultTree();

        void addLocation(const ComplexLocation &p_location);

        void doProcessInternal();

        void finish();

        void handleItemActivated(QTreeWidgetItem *p_item, int p_column);

        QSharedPointer<ISearchInfoProvider> m_provider;

        QCommandLineParser m_parser;

        Searcher *m_searcher = nullptr;

        QSharedPointer<QTreeWidget> m_resultTree;

        QTimer *m_processTimer = nullptr;

        QSharedPointer<SearchOption> m_searchOption;

        QSharedPointer<SearchToken> m_searchTokenOfSession;
    };
}

#endif // FINDUNITEDENTRY_H
