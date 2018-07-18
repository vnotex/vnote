#ifndef VSEARCHCONFIG_H
#define VSEARCHCONFIG_H

#include <QString>
#include <QStringList>
#include <QSharedPointer>
#include <QVector>
#include <QRegExp>

#include "utils/vutils.h"


struct VSearchToken
{
    enum Type
    {
        RawString = 0,
        RegularExpression
    };

    enum Operator
    {
        And = 0,
        Or
    };

    VSearchToken()
        : m_type(Type::RawString),
          m_op(Operator::And),
          m_caseSensitivity(Qt::CaseSensitive)
    {
    }

    void clear()
    {
        m_keywords.clear();
        m_regs.clear();
    }

    void append(const QString &p_rawStr)
    {
        m_keywords.append(p_rawStr);
    }

    void append(const QRegExp &p_reg)
    {
        m_regs.append(p_reg);
    }

    QString toString() const
    {
        return QString("token %1 %2 %3 %4 %5").arg(m_type)
                                              .arg(m_op)
                                              .arg(m_caseSensitivity)
                                              .arg(m_keywords.size())
                                              .arg(m_regs.size());
    }

    // Whether @p_text match all the constraint.
    bool matched(const QString &p_text) const
    {
        int size = m_keywords.size();
        if (m_type == Type::RegularExpression) {
            size = m_regs.size();
        }

        if (size == 0) {
            return false;
        }

        bool ret = m_op == Operator::And ? true : false;
        for (int i = 0; i < size; ++i) {
            bool tmp = false;
            if (m_type == Type::RawString) {
                tmp = p_text.contains(m_keywords[i], m_caseSensitivity);
            } else {
                tmp = p_text.contains(m_regs[i]);
            }

            if (tmp) {
                if (m_op == Operator::Or) {
                    ret = true;
                    break;
                }
            } else {
                if (m_op == Operator::And) {
                    ret = false;
                    break;
                }
            }
        }

        return ret;
    }

    void startBatchMode()
    {
        int size = m_type == Type::RawString ? m_keywords.size() : m_regs.size();
        m_matchesInBatch.resize(size);
        m_matchesInBatch.fill(false);
        m_numOfMatches = 0;
    }

    // Match one string in batch mode.
    // Returns true if @p_text matches one.
    bool matchBatchMode(const QString &p_text)
    {
        bool ret = false;
        int size = m_matchesInBatch.size();
        for (int i = 0; i < size; ++i) {
            if (m_matchesInBatch[i]) {
                continue;
            }

            bool tmp = false;
            if (m_type == Type::RawString) {
                tmp = p_text.contains(m_keywords[i], m_caseSensitivity);
            } else {
                tmp = p_text.contains(m_regs[i]);
            }

            if (tmp) {
                m_matchesInBatch[i] = true;
                ++m_numOfMatches;
                ret = true;
            }
        }

        return ret;
    }

    // Whether it is OK to finished batch mode.
    // @p_matched: the overall match result.
    bool readyToEndBatchMode(bool &p_matched) const
    {
        if (m_op == VSearchToken::And) {
            // We need all the tokens matched.
            if (m_numOfMatches == m_matchesInBatch.size()) {
                p_matched = true;
                return true;
            } else {
                p_matched = false;
                return false;
            }
        } else {
            // We only need one match.
            if (m_numOfMatches > 0) {
                p_matched = true;
                return true;
            } else {
                p_matched = false;
                return false;
            }
        }
    }

    void endBatchMode()
    {
        m_matchesInBatch.clear();
        m_numOfMatches = 0;
    }

    int tokenSize() const
    {
        return m_type == Type::RawString ? m_keywords.size() : m_regs.size();
    }

    VSearchToken::Type m_type;

    VSearchToken::Operator m_op;

    Qt::CaseSensitivity m_caseSensitivity;

    // Valid at RawString.
    QVector<QString> m_keywords;

    // Valid at RegularExpression.
    QVector<QRegExp> m_regs;

    // Bitmap for batch mode.
    // True if m_regs[i] or m_keywords[i] has been matched.
    QVector<bool> m_matchesInBatch;

    int m_numOfMatches;
};


struct VSearchConfig
{
    enum Scope
    {
        NoneScope = 0,
        CurrentNote,
        OpenedNotes,
        CurrentFolder,
        CurrentNotebook,
        AllNotebooks,
        ExplorerDirectory
    };

    enum Object
    {
        NoneObject = 0,
        Name = 0x1UL,
        Content = 0x2UL,
        Outline = 0x4UL,
        Tag = 0x8UL,
        Path = 0x10UL
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
          m_pattern(p_pattern)
    {
        compileToken(p_keyword);
    }

    // We support some magic switch in the keyword which will suppress the specified
    // options:
    // \c: Case insensitive;
    // \C: Case sensitive;
    // \r: Turn off regular expression;
    // \R: Turn on regular expression;
    // \f: Turn off fuzzy search;
    // \F: Turn on fuzzy search (invalid when searching content);
    // \w: Turn off whole word only;
    // \W: Turn on whole word only;
    void compileToken(const QString &p_keyword)
    {
        m_token.clear();
        m_contentToken.clear();
        if (p_keyword.isEmpty()) {
            return;
        }

        // """ to input a ";
        // && for AND, || for OR;
        QStringList args = VUtils::parseCombinedArgString(p_keyword);

        Qt::CaseSensitivity cs = m_option & VSearchConfig::CaseSensitive
                                 ? Qt::CaseSensitive : Qt::CaseInsensitive;
        bool useReg = m_option & VSearchConfig::RegularExpression;
        bool wwo = m_option & VSearchConfig::WholeWordOnly;
        bool fuzzy = m_option & VSearchConfig::Fuzzy;

        // Read magic switch from keyword.
        for (int i = 0; i < args.size();) {
            const QString &arg = args[i];
            if (arg.size() != 2 || arg[0] != '\\') {
                ++i;
                continue;
            }

            if (arg == "\\c") {
                cs = Qt::CaseInsensitive;
            } else if (arg == "\\C") {
                cs = Qt::CaseSensitive;
            } else if (arg == "\\r") {
                useReg = false;
            } else if (arg == "\\R") {
                useReg = true;
            } else if (arg == "\\f") {
                fuzzy = false;
            } else if (arg == "\\F") {
                fuzzy = true;
            } else if (arg == "\\w") {
                wwo = false;
            } else if (arg == "\\W") {
                wwo = true;
            } else {
                ++i;
                continue;
            }

            args.removeAt(i);
        }

        if (args.isEmpty()) {
            return;
        }

        m_token.m_caseSensitivity = cs;
        m_contentToken.m_caseSensitivity = cs;

        if (useReg) {
            m_token.m_type = VSearchToken::RegularExpression;
            m_contentToken.m_type = VSearchToken::RegularExpression;
        } else {
            if (fuzzy) {
                m_token.m_type = VSearchToken::RegularExpression;
                m_contentToken.m_type = VSearchToken::RawString;
            } else if (wwo) {
                m_token.m_type = VSearchToken::RegularExpression;
                m_contentToken.m_type = VSearchToken::RegularExpression;
            } else {
                m_token.m_type = VSearchToken::RawString;
                m_contentToken.m_type = VSearchToken::RawString;
            }
        }

        VSearchToken::Operator op = VSearchToken::And;
        for (auto const & arg : args) {
            if (arg == QStringLiteral("&&")) {
                op = VSearchToken::And;
                continue;
            } else if (arg == QStringLiteral("||")) {
                op = VSearchToken::Or;
                continue;
            }

            if (useReg) {
                QRegExp reg(arg, cs);
                m_token.append(reg);
                m_contentToken.append(reg);
            } else {
                if (fuzzy) {
                    QString wildcardText(arg.size() * 2 + 1, '*');
                    for (int i = 0, j = 1; i < arg.size(); ++i, j += 2) {
                        wildcardText[j] = arg[i];
                    }

                    QRegExp reg(wildcardText, cs, QRegExp::Wildcard);
                    m_token.append(reg);
                    m_contentToken.append(arg);
                } else if (wwo) {
                    QString pattern = QRegExp::escape(arg);
                    pattern = "\\b" + pattern + "\\b";

                    QRegExp reg(pattern, cs);
                    m_token.append(reg);
                    m_contentToken.append(reg);
                } else {
                    m_token.append(arg);
                    m_contentToken.append(arg);
                }
            }
        }

        m_token.m_op = op;
        m_contentToken.m_op = op;
    }

    bool isEmpty() const
    {
        return m_token.tokenSize() == 0;
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

    // Wildcard pattern to filter file.
    QString m_pattern;

    // Token for name, outline, and tag.
    VSearchToken m_token;

    // Token for content.
    VSearchToken m_contentToken;
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
