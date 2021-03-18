#include "searchtoken.h"

#include <QCommandLineParser>
#include <QDebug>

#include <utils/processutils.h>
#include <widgets/searchpanel.h>

using namespace vnotex;

QScopedPointer<QCommandLineParser> SearchToken::s_parser;

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

bool SearchToken::matched(const QString &p_text) const
{
    const int consSize = constraintSize();
    if (consSize == 0) {
        return false;
    }

    bool isMatched = m_operator == Operator::And ? true : false;
    for (int i = 0; i < consSize; ++i) {
        bool consMatched = false;
        if (m_type == Type::PlainText) {
            consMatched = p_text.contains(m_keywords[i], m_caseSensitivity);
        } else {
            consMatched = p_text.contains(m_regularExpressions[i]);
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

bool SearchToken::matchedInBatchMode(const QString &p_text)
{
    bool isMatched = false;
    const int consSize = m_matchedConstraintsInBatchMode.size();
    for (int i = 0; i < consSize; ++i) {
        if (m_matchedConstraintsInBatchMode[i]) {
            continue;
        }

        bool consMatched = false;
        if (m_type == Type::PlainText) {
            consMatched = p_text.contains(m_keywords[i], m_caseSensitivity);
        } else {
            consMatched = p_text.contains(m_regularExpressions[i]);
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

void SearchToken::createCommandLineParser()
{
    if (s_parser) {
        return;
    }

    s_parser.reset(new QCommandLineParser());
    s_parser->setApplicationDescription(SearchPanel::tr("Full-text search."));

    QCommandLineOption caseSensitiveOpt(QStringList() << "c" << "case-sensitive", SearchPanel::tr("Search in case sensitive."));
    s_parser->addOption(caseSensitiveOpt);

    QCommandLineOption regularExpressionOpt(QStringList() << "r" << "regular-expression", SearchPanel::tr("Search by regular expression."));
    s_parser->addOption(regularExpressionOpt);

    QCommandLineOption wholeWordOnlyOpt(QStringList() << "w" << "whole-word-only", SearchPanel::tr("Search whole word only."));
    s_parser->addOption(wholeWordOnlyOpt);

    QCommandLineOption fuzzySearchOpt(QStringList() << "f" << "fuzzy-search", SearchPanel::tr("Do a fuzzy search (not applicable to content search)."));
    s_parser->addOption(fuzzySearchOpt);

    QCommandLineOption orOpt(QStringList() << "o" << "or", SearchPanel::tr("Do an OR combination of keywords."));
    s_parser->addOption(orOpt);

    s_parser->addPositionalArgument("keywords", SearchPanel::tr("Keywords to search."));
}

bool SearchToken::compile(const QString &p_keyword, FindOptions p_options, SearchToken &p_token)
{

    p_token.clear();

    if (p_keyword.isEmpty()) {
        return false;
    }

    createCommandLineParser();

    auto caseSensitivity = p_options & FindOption::CaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    bool isRegularExpression = p_options & FindOption::RegularExpression;
    bool isWholeWordOnly = p_options & FindOption::WholeWordOnly;
    bool isFuzzySearch = p_options & FindOption::FuzzySearch;

    auto args = ProcessUtils::parseCombinedArgString(p_keyword);
    // The parser needs the first arg to be the application name.
    args.prepend("vnotex");
    if (!s_parser->parse(args))
    {
        return false;
    }

    if (s_parser->isSet("c")) {
        caseSensitivity = Qt::CaseSensitive;
    }
    if (s_parser->isSet("r")) {
        isRegularExpression = true;
    }
    if (s_parser->isSet("w")) {
        isWholeWordOnly = true;
    }
    if (s_parser->isSet("f")) {
        isFuzzySearch = true;
    }

    args = s_parser->positionalArguments();
    if (args.isEmpty()) {
        return false;
    }

    p_token.m_caseSensitivity = caseSensitivity;
    if (isRegularExpression || isWholeWordOnly || isFuzzySearch) {
        p_token.m_type = Type::RegularExpression;
    } else {
        p_token.m_type = Type::PlainText;
    }
    p_token.m_operator = s_parser->isSet("o") ? Operator::Or : Operator::And;

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
    createCommandLineParser();
    auto text = s_parser->helpText();
    // Skip the first line containing the application name.
    return text.mid(text.indexOf('\n') + 1);
}
