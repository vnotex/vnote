#ifndef VMETAWORDMANAGER_H
#define VMETAWORDMANAGER_H

#include <functional>

#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QDateTime>
#include <QHash>


enum class MetaWordType
{
    // Definition is plain text.
    Simple = 0,

    // Definition is a function call to get the value.
    FunctionBased,

    // Like FunctionBased, but should re-evaluate the value for each occurence.
    Dynamic,

    // Consists of other meta words.
    Compound,

    Invalid
};

// We call meta word "magic word" in user interaction.
struct VMagicWord
{
    QString m_name;
    QString m_definition;
};

class VMetaWordManager;
class VMetaWord;

typedef std::function<QString(const VMetaWord *)> MetaWordFunc;

// A Meta Word is surrounded by %.
// - Use %% for an escaped %;
// - Built-in or user-defined;
// - A meta word could contain other meta words as definition.
class VMetaWord
{
public:
    VMetaWord(const VMetaWordManager *p_manager,
              MetaWordType p_type,
              const QString &p_word,
              const QString &p_definition,
              MetaWordFunc p_function = nullptr,
              bool p_allowAllSpaces = false);

    bool isValid() const;

    QString evaluate() const;

    MetaWordType getType() const;

    const QString &getWord() const;

    const QString &getDefinition() const;

    const VMetaWordManager *getManager() const;

    QString toString() const;

    enum class TokenType
    {
        Raw = 0,
        MetaWord
    };

    struct Token
    {
        Token()
            : m_type(TokenType::Raw)
        {
        }

        Token(VMetaWord::TokenType p_type, const QString &p_value)
            : m_type(p_type),
              m_value(p_value)
        {
        }

        QString toString() const
        {
            return QString("token %1[%2]").arg(m_type == TokenType::Raw
                                              ? "Raw" : "MetaWord")
                                         .arg(m_value);
        }

        bool isRaw() const
        {
            return m_type == TokenType::Raw;
        }

        bool isMetaWord() const
        {
            return m_type == TokenType::MetaWord;
        }

        TokenType m_type;

        // For Raw type, m_value is the raw string of this token;
        // For MetaWord type, m_value is the word of the meta word pointed to by
        // this token.
        QString m_value;
    };

private:
    // Check if m_type is @p_type.
    bool checkType(MetaWordType p_type);

    // Parse children word from definition.
    // Children word MUST be defined in m_manager already.
    // @p_allowAllSpaces: if true then we allow all spaces including \n and \t
    // to appear in the definition.
    void checkAndParseDefinition(bool p_allowAllSpaces);

    // Parse @p_text to a list of tokens.
    static QVector<VMetaWord::Token> parseToTokens(const QString &p_text);

    // Used for Compound meta word with cache @p_cache.
    // @p_cache: value cache for FunctionBased Token.
    // For a meta word occuring more than once, we should evaluate it once for
    // FunctionBased meta word.
    QString evaluateTokens(const QVector<VMetaWord::Token> &p_tokens,
                           QHash<QString, QString> &p_cache) const;

    const VMetaWordManager *m_manager;

    MetaWordType m_type;

    // Word could contains spaces but no %.
    QString m_word;

    // For Simple/Compound meta word, this contains the definition;
    // For FunctionBased/Dynamic meta word, this makes no sense and is used
    // for description.
    QString m_definition;

    // For FunctionBased and Dynamic meta word.
    MetaWordFunc m_function;

    bool m_valid;

    // Tokens used for Compound meta word.
    QVector<Token> m_tokens;
};


// Manager of meta word.
class VMetaWordManager : public QObject
{
    Q_OBJECT
public:
    explicit VMetaWordManager(QObject *p_parent = nullptr);

    // Expand meta words in @p_text and return the expanded text.
    // @p_overriddenWords: a table containing overridden meta words.
    QString evaluate(const QString &p_text,
                     const QHash<QString, QString> &p_overriddenWords = QHash<QString, QString>()) const;

    const VMetaWord *findMetaWord(const QString &p_word) const;

    bool contains(const QString &p_word) const;

    const QDateTime &getDateTime() const;

    const QHash<QString, VMetaWord> &getAllMetaWords() const;

    // Find @p_word in the overridden table.
    // Return true if found and set @p_value to the overridden value.
    bool findOverriddenValue(const QString &p_word, QString &p_value) const;

    // % by default.
    static const QChar c_delimiter;

private:
    void init();

    void addMetaWord(MetaWordType p_type,
                     const QString &p_word,
                     const QString &p_definition,
                     MetaWordFunc p_function = nullptr);

    void initCustomMetaWords();

    bool m_initialized;

    // Map using word as key.
    QHash<QString, VMetaWord> m_metaWords;

    // Used for data/time related evaluate.
    // Will be updated before each evaluation.
    QDateTime m_dateTime;

    // Overridden table containing meta words with their designated value.
    // Will be updated before each evaluation and clear after the evaluation.
    QHash<QString, QString> m_overriddenWords;
};

inline const QDateTime &VMetaWordManager::getDateTime() const
{
    return m_dateTime;
}

inline const QHash<QString, VMetaWord> &VMetaWordManager::getAllMetaWords() const
{
    const_cast<VMetaWordManager *>(this)->init();

    return m_metaWords;
}

#endif // VMETAWORDMANAGER_H
