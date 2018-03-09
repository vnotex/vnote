#ifndef VSEARCHCONFIG_H
#define VSEARCHCONFIG_H

#include <QString>
#include <QStringList>
#include <QSharedPointer>


struct VSearchConfig
{
    enum Scope
    {
        NoneScope = 0,
        CurrentNote,
        OpenedNotes,
        CurrentFolder,
        CurrentNotebook,
        AllNotebooks
    };

    enum Object
    {
        NoneObject = 0,
        Name = 0x1UL,
        Content = 0x2UL,
        Outline = 0x4UL,
        Tag = 0x8UL
    };

    enum Target
    {
        NoneTarget = 0,
        Note = 0x1UL,
        Folder = 0x2UL,
        Notebook = 0x4UL
    };

    enum Engine
    {
        Internal = 0
    };

    enum Option
    {
        NoneOption = 0,
        CaseSensitive = 0x1UL,
        WholeWordOnly = 0x2UL,
        Fuzzy = 0x4UL,
        RegularExpression = 0x8UL
    };

    VSearchConfig()
        : VSearchConfig(Scope::NoneScope,
                        Object::NoneObject,
                        Target::NoneTarget,
                        Engine::Internal,
                        Option::NoneOption,
                        "",
                        "")
    {
    }


    VSearchConfig(int p_scope,
                  int p_object,
                  int p_target,
                  int p_engine,
                  int p_option,
                  const QString &p_keyword,
                  const QString &p_pattern)
        : m_scope(p_scope),
          m_object(p_object),
          m_target(p_target),
          m_engine(p_engine),
          m_option(p_option),
          m_keyword(p_keyword),
          m_pattern(p_pattern)
    {
    }

    QStringList toConfig() const
    {
        QStringList str;
        str << QString::number(m_scope);
        str << QString::number(m_object);
        str << QString::number(m_target);
        str << QString::number(m_engine);
        str << QString::number(m_option);
        str << m_pattern;

        return str;
    }

    static VSearchConfig fromConfig(const QStringList &p_str)
    {
        VSearchConfig config;
        if (p_str.size() != 6) {
            return config;
        }

        config.m_scope = p_str[0].toInt();
        config.m_object = p_str[1].toInt();
        config.m_target = p_str[2].toInt();
        config.m_engine = p_str[3].toInt();
        config.m_option = p_str[4].toInt();
        config.m_pattern = p_str[5];

        return config;
    }

    int m_scope;
    int m_object;
    int m_target;
    int m_engine;
    int m_option;

    QString m_keyword;

    // Wildcard pattern to filter file.
    QString m_pattern;
};


struct VSearchResultSubItem
{
    VSearchResultSubItem()
        : m_lineNumber(-1)
    {
    }

    VSearchResultSubItem(int p_lineNumber,
                         const QString &p_text)
        : m_lineNumber(p_lineNumber),
          m_text(p_text)
    {
    }

    int m_lineNumber;

    QString m_text;
};


struct VSearchResultItem
{
    enum ItemType
    {
        None = 0,
        Note,
        Folder,
        Notebook
    };


    enum MatchType
    {
        LineNumber = 0,
        OutlineIndex
    };


    VSearchResultItem()
        : m_type(ItemType::None),
          m_matchType(MatchType::LineNumber)
    {
    }

    VSearchResultItem(VSearchResultItem::ItemType p_type,
                      VSearchResultItem::MatchType p_matchType,
                      const QString &p_text,
                      const QString &p_path)
        : m_type(p_type),
          m_matchType(p_matchType),
          m_text(p_text),
          m_path(p_path)
    {
    }

    bool isEmpty() const
    {
        return m_type == ItemType::None;
    }

    QString toString() const
    {
        return QString("item text: [%1] path: [%2] subitems: %3")
                      .arg(m_text)
                      .arg(m_path)
                      .arg(m_matches.size());
    }


    ItemType m_type;

    MatchType m_matchType;

    // Text to displayed. If empty, use @m_path instead.
    QString m_text;

    // Path of the target.
    QString m_path;

    // Matched places within this item.
    QList<VSearchResultSubItem> m_matches;
};


class VSearch;


enum class VSearchState
{
    Idle = 0,
    Busy,
    Success,
    Fail,
    Cancelled
};


struct VSearchResult
{
    friend class VSearch;

    VSearchResult(VSearch *p_search)
        : m_state(VSearchState::Idle),
          m_search(p_search)
    {
    }

    bool hasError() const
    {
        return !m_errMsg.isEmpty();
    }

    void logError(const QString &p_err)
    {
        if (m_errMsg.isEmpty()) {
            m_errMsg = p_err;
        } else {
            m_errMsg = "\n" + p_err;
        }
    }

    void addSecondPhaseItem(const QString &p_item)
    {
        m_secondPhaseItems.append(p_item);
    }

    QString toString() const
    {
        QString str = QString("search result: state %1 err %2")
                             .arg((int)m_state)
                             .arg(!m_errMsg.isEmpty());
        return str;
    }

    bool hasSecondPhaseItems() const
    {
        return !m_secondPhaseItems.isEmpty();
    }

    VSearchState m_state;

    QString m_errMsg;

    QStringList m_secondPhaseItems;

private:
    VSearch *m_search;
};

#endif // VSEARCHCONFIG_H
