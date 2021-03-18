#include "searchdata.h"

#include <QJsonObject>

using namespace vnotex;

SearchOption::SearchOption()
    : m_objects(SearchObject::SearchName | SearchObject::SearchContent),
      m_targets(SearchTarget::SearchFile | SearchTarget::SearchFolder)
{
}

QJsonObject SearchOption::toJson() const
{
    QJsonObject obj;
    obj["file_pattern"] = m_filePattern;
    obj["scope"] = static_cast<int>(m_scope);
    obj["objects"] = static_cast<int>(m_objects);
    obj["targets"] = static_cast<int>(m_targets);
    obj["engine"] = static_cast<int>(m_engine);
    obj["find_options"] = static_cast<int>(m_findOptions);
    return obj;
}

void SearchOption::fromJson(const QJsonObject &p_obj)
{
    if (p_obj.isEmpty()) {
        return;
    }

    m_filePattern = p_obj["file_pattern"].toString();
    m_scope = static_cast<SearchScope>(p_obj["scope"].toInt());
    m_objects = static_cast<SearchObjects>(p_obj["objects"].toInt());
    m_targets = static_cast<SearchTargets>(p_obj["targets"].toInt());
    m_engine = static_cast<SearchEngine>(p_obj["engine"].toInt());
    m_findOptions = static_cast<FindOptions>(p_obj["find_options"].toInt());
}

bool SearchOption::operator==(const SearchOption &p_other) const
{
    return m_filePattern == p_other.m_filePattern
           && m_scope == p_other.m_scope
           && m_objects == p_other.m_objects
           && m_targets == p_other.m_targets
           && m_engine == p_other.m_engine
           && m_findOptions == p_other.m_findOptions;
}
