#ifndef SEARCHOPTION_H
#define SEARCHOPTION_H

#include <QObject>
#include <QString>

#include <core/global.h>

namespace vnotex
{
    enum class SearchState
    {
        Idle = 0,
        Busy,
        Finished,
        Failed,
        Stopped
    };

    class SearchTranslate : public QObject
    {
        Q_OBJECT
    };

    inline QString SearchStateToString(SearchState p_state)
    {
        switch (p_state) {
        case SearchState::Idle:
            return SearchTranslate::tr("Idle");

        case SearchState::Busy:
            return SearchTranslate::tr("Busy");

        case SearchState::Finished:
            return SearchTranslate::tr("Finished");

        case SearchState::Failed:
            return SearchTranslate::tr("Failed");

        case SearchState::Stopped:
            return SearchTranslate::tr("Stopped");
        }

        return QString();
    }

    enum class SearchScope
    {
        Buffers = 0,
        CurrentFolder,
        CurrentNotebook,
        AllNotebooks
    };

    enum SearchObject
    {
        ObjectNone = 0,
        SearchName = 0x1UL,
        SearchContent = 0x2UL,
        SearchOutline = 0x4UL,
        SearchTag = 0x8UL,
        SearchPath = 0x10UL
    };
    Q_DECLARE_FLAGS(SearchObjects, SearchObject);

    enum SearchTarget
    {
        TargetNone = 0,
        SearchFile = 0x1UL,
        SearchFolder = 0x2UL,
        SearchNotebook = 0x4UL
    };
    Q_DECLARE_FLAGS(SearchTargets, SearchTarget);

    enum class SearchEngine
    {
        Internal = 0
    };

    struct SearchOption
    {
        SearchOption();

        QJsonObject toJson() const;
        void fromJson(const QJsonObject &p_obj);

        bool operator==(const SearchOption &p_other) const;

        QString m_keyword;

        QString m_filePattern;

        SearchScope m_scope = SearchScope::CurrentNotebook;

        // *nix requests to init in the constructor.
        SearchObjects m_objects;

        // *nix requests to init in the constructor.
        SearchTargets m_targets;

        SearchEngine m_engine = SearchEngine::Internal;

        FindOptions m_findOptions = FindOption::FindNone;
    };
}

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::SearchObjects);
Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::SearchTargets);

#endif // SEARCHOPTION_H
