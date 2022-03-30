#include "searchhelper.h"

#include <search/isearchinfoprovider.h>

using namespace vnotex;

bool SearchHelper::isSearchOptionValid(const SearchOption &p_option, QString &p_msg)
{
    if (p_option.m_keyword.isEmpty()) {
        p_msg = Searcher::tr("Invalid keyword");
        return false;
    }

    if (p_option.m_objects == SearchObject::ObjectNone) {
        p_msg = Searcher::tr("No object specified");
        return false;
    }

    if (p_option.m_targets == SearchTarget::TargetNone) {
        p_msg = Searcher::tr("No target specified");
        return false;
    }

    if (p_option.m_findOptions & FindOption::FuzzySearch
        && p_option.m_objects & SearchObject::SearchContent) {
        p_msg = Searcher::tr("Fuzzy search is not allowed when searching content");
        return false;
    }

    p_msg.clear();
    return true;
}

SearchState SearchHelper::searchOnProvider(Searcher *p_searcher,
                                           const QSharedPointer<SearchOption> &p_option,
                                           const QSharedPointer<ISearchInfoProvider> &p_provider,
                                           QString &p_msg)
{
    p_msg.clear();

    if (!isSearchOptionValid(*p_option, p_msg)) {
        return SearchState::Failed;
    }

    SearchState state = SearchState::Finished;

    switch (p_option->m_scope) {
    case SearchScope::Buffers:
    {
        auto buffers = p_provider->getBuffers();
        if (buffers.isEmpty()) {
            break;
        }
        state = p_searcher->search(p_option, buffers);
        break;
    }

    case SearchScope::CurrentFolder:
    {
        auto notebook = p_provider->getCurrentNotebook();
        if (!notebook) {
            break;
        }
        auto folder = p_provider->getCurrentFolder();
        if (!folder) {
            break;
        }

        state = p_searcher->search(p_option, folder);
        break;
    }

    case SearchScope::CurrentNotebook:
    {
        auto notebook = p_provider->getCurrentNotebook();
        if (!notebook) {
            break;
        }

        QVector<Notebook *> notebooks;
        notebooks.push_back(notebook);
        state = p_searcher->search(p_option, notebooks);
        break;
    }

    case SearchScope::AllNotebooks:
    {
        auto notebooks = p_provider->getNotebooks();
        if (notebooks.isEmpty()) {
            break;
        }

        state = p_searcher->search(p_option, notebooks);
        break;
    }
    }

    return state;
}
