#ifndef SEARCHHELPER_H
#define SEARCHHELPER_H

#include <QSharedPointer>

#include "searchdata.h"
#include "searcher.h"

namespace vnotex
{
    class ISearchInfoProvider;

    class SearchHelper
    {
    public:
        SearchHelper() = delete;

        static SearchState searchOnProvider(Searcher *p_searcher,
                                            const QSharedPointer<SearchOption> &p_option,
                                            const QSharedPointer<ISearchInfoProvider> &p_provider,
                                            QString &p_msg);

    private:
        static bool isSearchOptionValid(const SearchOption &p_option, QString &p_msg);
    };
}

#endif // SEARCHHELPER_H
