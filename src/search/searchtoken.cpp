#include "searchtoken.h"

#include <QCommandLineParser>
#include <QDebug>

#include <utils/processutils.h>
#include <widgets/searchpanel.h>

using namespace vnotex;

void SearchToken::clear()
{
    m_type = Type::PlainText;
    m_operator = Operator::And;
    m_caseSensitivity = Qt::CaseInsensitive;
    m_keywords.clear();
    m_regularExpressions.clear();
    m_matchedConstraintsInBatchMode.clear();
    m_matchedConstraintsCountInBatchMode = 0;
}

void SearchToken::append(const QString &p_text)
{
    m_keywords.append(p_text);
}

void SearchToken::append(const QRegularExpression &p_regExp)
{
    m_regularExpressions.append(p_regExp);
}

bool SearchToken::matched(const QString &p_text, QList<Segment> *p_segments) const
{
    const int consSize = constraintSize();
    if (consSize == 0) {
        return false;
    }

    bool isMatched = m_operator == Operator::And ? true : false;
    for (int i = 0; i < consSize; ++i) {
        bool consMatched = false;
        if (m_type == Type::PlainText) {
            int idx = p_text.indexOf(m_keywords[i], 0, m_caseSensitivity);
            if (idx > -1) {
                consMatched = true;
                if (p_segments) {
                    p_segments->push_back(Segment(idx, m_keywords[i].size()));
                }
            }
        } else {
            QRegularExpressionMatch match;
            int idx = p_text.indexOf(m_regularExpressions[i], 0, &match);
            if (idx > -1) {
                consMatched = true;
                if (p_segments) {
                    p_segments->push_back(Segment(idx, match.capturedLength()));
                }
            }
        }

        if (consMatched) {
            if (m_operator == Operator::Or) {
                isMatched = true;
                break;
            }
        } else if (m_operator == Operator::And) {
            isMatched = false;
            break;
        }
    }

    return isMatched;
}

int SearchToken::constraintSize() const
{
    return (m_type == Type::PlainText ? m_keywords.size() : m_regularExpressions.size());
}

bool SearchToken::shouldStartBatchMode() const
{
    return constraintSize() > 1;
}

void SearchToken::startBatchMode()
{
    m_matchedConstraintsInBatchMode.fill(false, constraintSize());
    m_matchedConstraintsCountInBatchMode = 0;
}

bool SearchToken::matchedInBatchMode(const QString &p_text, QList<Segment> *p_segments)
{
    bool isMatched = false;
    const int consSize = m_matchedConstraintsInBatchMode.size();
    for (int i = 0; i < consSize; ++i) {
        if (m_matchedConstraintsInBatchMode[i]) {
            continue;
        }

        bool consMatched = false;
        if (m_type == Type::PlainText) {
            int idx = p_text.indexOf(m_keywords[i], 0, m_caseSensitivity);
            if (idx > -1) {
                consMatched = true;
                if (p_segments) {
                    p_segments->push_back(Segment(idx, m_keywords[i].size()));
                }
            }
        } else {
            QRegularExpressionMatch match;
            int idx = p_text.indexOf(m_regularExpressions[i], 0, &match);
            if (idx > -1) {
                consMatched = true;
                if (p_segments) {
                    p_segments->push_back(Segment(idx, match.capturedLength()));
                }
            }
        }

        if (consMatched) {
            m_matchedConstraintsInBatchMode[i] = true;
            ++m_matchedConstraintsCountInBatchMode;
            isMatched = true;
        }
    }

    return isMatched;
}

bool SearchToken::readyToEndBatchMode() const
{
    if (m_operator == Operator::And) {
        // We need all the tokens matched.
        if (m_matchedConstraintsCountInBatchMode == m_matchedConstraintsInBatchMode.size()) {
            return true;
        }
    } else {
        // We only need one match.
        if (m_matchedConstraintsCountInBatchMode > 0) {
            return true;
        }
    }

    return false;
}

void SearchToken::endBatchMode()
{
    m_matchedConstraintsInBatchMode.clear();
    m_matchedConstraintsCountInBatchMode = 0;
}

bool SearchToken::isEmpty() const
{
    return constraintSize() == 0;
}

QCommandLineParser *SearchToken::getCommandLineParser()
{
    static QScopedPointer<QCommandLineParser> parser;

    if (parser) {
        return parser.data();
    }

    parser.reset(new QCommandLineParser());
    parser->setApplicationDescription(SearchPanel::tr("Full-text search."));

    parser->addPositionalArgument("keywords", SearchPanel::tr("Keywords to search for."));

    addSearchOptionsToCommand(parser.data());

    return parser.data();
}

void SearchToken::addSearchOptionsToCommand(QCommandLineParser *p_parser)
{
    QCommandLineOption caseSensitiveOpt(QStringList() << "c" << "case-sensitive", SearchPanel::tr("Search in case sensitive."));
    p_parser->addOption(caseSensitiveOpt);

    QCommandLineOption regularExpressionOpt(QStringList() << "r" << "regular-expression", SearchPanel::tr("Search by regular expression."));
    p_parser->addOption(regularExpressionOpt);

    QCommandLineOption wholeWordOnlyOpt(QStringList() << "w" << "whole-word-only", SearchPanel::tr("Search whole word only."));
    p_parser->addOption(wholeWordOnlyOpt);

    QCommandLineOption fuzzySearchOpt(QStringList() << "f" << "fuzzy-search", SearchPanel::tr("Do a fuzzy search (not applicable to content search)."));
    p_parser->addOption(fuzzySearchOpt);

    QCommandLineOption orOpt(QStringList() << "o" << "or", SearchPanel::tr("Do an OR combination of keywords."));
    p_parser->addOption(orOpt);
}

bool SearchToken::compile(const QString &p_keyword, FindOptions p_options, SearchToken &p_token)
{

    p_token.clear();

    if (p_keyword.isEmpty()) {
        return false;
    }

    auto parser = getCommandLineParser();

    auto caseSensitivity = p_options & FindOption::CaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    bool isRegularExpression = p_options & FindOption::RegularExpression;
    bool isWholeWordOnly = p_options & FindOption::WholeWordOnly;
    bool isFuzzySearch = p_options & FindOption::FuzzySearch;

    auto args = ProcessUtils::parseCombinedArgString(p_keyword);
    // The parser needs the first arg to be the application name.
    args.prepend("vnotex");
    if (!parser->parse(args))
    {
        return false;
    }

    if (parser->isSet("c")) {
        caseSensitivity = Qt::CaseSensitive;
    }
    if (parser->isSet("r")) {
        isRegularExpression = true;
    }
    if (parser->isSet("w")) {
        isWholeWordOnly = true;
    }
    if (parser->isSet("f")) {
        isFuzzySearch = true;
    }

    args = parser->positionalArguments();
    if (args.isEmpty()) {
        return false;
    }

    p_token.m_caseSensitivity = caseSensitivity;
    if (isRegularExpression || isWholeWordOnly || isFuzzySearch) {
        p_token.m_type = Type::RegularExpression;
    } else {
        p_token.m_type = Type::PlainText;
    }
    p_token.m_operator = parser->isSet("o") ? Operator::Or : Operator::And;

    auto patternOptions = caseSensitivity == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                                                 : QRegularExpression::NoPatternOption;
    for (const auto &ar : args) {
        if (ar.isEmpty()) {
            continue;
        }

        if (isRegularExpression) {
            p_token.append(QRegularExpression(ar, patternOptions));
        } else if (isFuzzySearch) {
            // ABC -> *A*B*C*.
            QString wildcardText(ar.size() * 2 + 1, '*');
            for (int i = 0, j = 1; i < ar.size(); ++i, j += 2) {
                wildcardText[j] = ar[i];
            }

            p_token.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(wildcardText),
                                              patternOptions));
        } else if (isWholeWordOnly) {
            auto pattern = QRegularExpression::escape(ar);
            pattern = "\\b" + pattern + "\\b";
            p_token.append(QRegularExpression(pattern, patternOptions));
        } else {
            p_token.append(ar);
        }
    }

    return !p_token.isEmpty();
}

QString SearchToken::getHelpText()
{
    auto parser = getCommandLineParser();
    auto text = parser->helpText();
    // Skip the first line containing the application name.
    return text.mid(text.indexOf('\n') + 1);
}

QPair<QStringList, FindOptions> SearchToken::toPatterns() const
{
    QPair<QStringList, FindOptions> ret;

    ret.second = m_caseSensitivity == Qt::CaseSensitive ? FindOption::CaseSensitive : FindOption::FindNone;
    if (m_type == Type::RegularExpression) {
        ret.second |= FindOption::RegularExpression;

        for (const auto &reg : m_regularExpressions) {
            ret.first << reg.pattern();
        }
    } else {
        ret.first = m_keywords;
    }

    return ret;
}
