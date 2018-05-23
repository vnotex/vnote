#include "vmetawordmanager.h"

#include <QDebug>
#include <QWidget>
#include <QApplication>
#include <QToolTip>
#include <QFileInfo>

#include "vconfigmanager.h"
#include "vmainwindow.h"

extern VConfigManager *g_config;

extern VMainWindow *g_mainWin;


// Used as the function template for some date/time related meta words.
static QString formattedDateTime(const VMetaWord *p_metaWord,
                                 const QString &p_format)
{
    return p_metaWord->getManager()->getDateTime().toString(p_format);
}

static QString allMetaWordsInfo(const VMetaWord *p_metaWord)
{
    QString msg = QObject::tr("All magic words:");

    const VMetaWordManager *mgr = p_metaWord->getManager();
    QList<QString> keys = mgr->getAllMetaWords().keys();
    keys.sort(Qt::CaseInsensitive);

    for (auto const & key : keys) {
        const VMetaWord *word = mgr->findMetaWord(key);
        Q_ASSERT(word);
        msg += QString("\n%1:\t%2").arg(word->getWord()).arg(word->getDefinition());
    }

    QWidget *focusWid = QApplication::focusWidget();
    if (focusWid) {
        QPoint pos = focusWid->mapToGlobal(QPoint(0, focusWid->height()));
        QToolTip::showText(pos, msg, focusWid);
    }

    // Just return the same word.
    return QString("%1help%1").arg(VMetaWordManager::c_delimiter);
}

const QChar VMetaWordManager::c_delimiter = '%';

VMetaWordManager::VMetaWordManager(QObject *p_parent)
    : QObject(p_parent),
      m_initialized(false)
{
}

void VMetaWordManager::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    using namespace std::placeholders;

    // %d%.
    addMetaWord(MetaWordType::FunctionBased,
                "d",
                tr("the day as number without a leading zero (`1` to `31`)"),
                std::bind(formattedDateTime, _1, "d"));

    // %dd%.
    addMetaWord(MetaWordType::FunctionBased,
                "dd",
                tr("the day as number with a leading zero (`01` to `31`)"),
                std::bind(formattedDateTime, _1, "dd"));

    // %ddd%.
    addMetaWord(MetaWordType::FunctionBased,
                "ddd",
                tr("the abbreviated localized day name (e.g. `Mon` to `Sun`)"),
                std::bind(formattedDateTime, _1, "ddd"));

    // %dddd%.
    addMetaWord(MetaWordType::FunctionBased,
                "dddd",
                tr("the long localized day name (e.g. `Monday` to `Sunday`)"),
                std::bind(formattedDateTime, _1, "dddd"));

    // %M%.
    addMetaWord(MetaWordType::FunctionBased,
                "M",
                tr("the month as number without a leading zero (`1` to `12`)"),
                std::bind(formattedDateTime, _1, "M"));

    // %MM%.
    addMetaWord(MetaWordType::FunctionBased,
                "MM",
                tr("the month as number with a leading zero (`01` to `12`)"),
                std::bind(formattedDateTime, _1, "MM"));

    // %MMM%.
    addMetaWord(MetaWordType::FunctionBased,
                "MMM",
                tr("the abbreviated localized month name (e.g. `Jan` to `Dec`)"),
                std::bind(formattedDateTime, _1, "MMM"));

    // %MMMM%.
    addMetaWord(MetaWordType::FunctionBased,
                "MMMM",
                tr("the long localized month name (e.g. `January` to `December`)"),
                std::bind(formattedDateTime, _1, "MMMM"));

    // %yy%.
    addMetaWord(MetaWordType::FunctionBased,
                "yy",
                tr("the year as two digit number (`00` to `99`)"),
                std::bind(formattedDateTime, _1, "yy"));

    // %yyyy%.
    addMetaWord(MetaWordType::FunctionBased,
                "yyyy",
                tr("the year as four digit number"),
                std::bind(formattedDateTime, _1, "yyyy"));

    // %h%.
    addMetaWord(MetaWordType::FunctionBased,
                "h",
                tr("the hour without a leading zero (`0` to `23` or `1` to `12` if AM/PM display)"),
                std::bind(formattedDateTime, _1, "h"));

    // %hh%.
    addMetaWord(MetaWordType::FunctionBased,
                "hh",
                tr("the hour with a leading zero (`00` to `23` or `01` to `12` if AM/PM display)"),
                std::bind(formattedDateTime, _1, "hh"));

    // %H%.
    addMetaWord(MetaWordType::FunctionBased,
                "H",
                tr("the hour without a leading zero (`0` to `23` even with AM/PM display)"),
                std::bind(formattedDateTime, _1, "H"));

    // %HH%.
    addMetaWord(MetaWordType::FunctionBased,
                "HH",
                tr("the hour with a leading zero (`00` to `23` even with AM/PM display)"),
                std::bind(formattedDateTime, _1, "HH"));

    // %m%.
    addMetaWord(MetaWordType::FunctionBased,
                "m",
                tr("the minute without a leading zero (`0` to `59`)"),
                std::bind(formattedDateTime, _1, "m"));

    // %mm%.
    addMetaWord(MetaWordType::FunctionBased,
                "mm",
                tr("the minute with a leading zero (`00` to `59`)"),
                std::bind(formattedDateTime, _1, "mm"));

    // %s%.
    addMetaWord(MetaWordType::FunctionBased,
                "s",
                tr("the second without a leading zero (`0` to `59`)"),
                std::bind(formattedDateTime, _1, "s"));

    // %ss%.
    addMetaWord(MetaWordType::FunctionBased,
                "ss",
                tr("the second with a leading zero (`00` to `59`)"),
                std::bind(formattedDateTime, _1, "ss"));

    // %z%.
    addMetaWord(MetaWordType::FunctionBased,
                "z",
                tr("the milliseconds without leading zeroes (`0` to `999`)"),
                std::bind(formattedDateTime, _1, "z"));

    // %zzz%.
    addMetaWord(MetaWordType::FunctionBased,
                "zzz",
                tr("the milliseconds with leading zeroes (`000` to `999`)"),
                std::bind(formattedDateTime, _1, "zzz"));

    // %AP%.
    addMetaWord(MetaWordType::FunctionBased,
                "AP",
                tr("use AM/PM display (`AM` or `PM`)"),
                std::bind(formattedDateTime, _1, "AP"));

    // %A%.
    addMetaWord(MetaWordType::FunctionBased,
                "A",
                tr("use AM/PM display (`AM` or `PM`)"),
                std::bind(formattedDateTime, _1, "A"));

    // %ap%.
    addMetaWord(MetaWordType::FunctionBased,
                "ap",
                tr("use am/pm display (`am` or `pm`)"),
                std::bind(formattedDateTime, _1, "ap"));

    // %a%.
    addMetaWord(MetaWordType::FunctionBased,
                "a",
                tr("use am/pm display (`am` or `pm`)"),
                std::bind(formattedDateTime, _1, "a"));

    // %t%.
    addMetaWord(MetaWordType::FunctionBased,
                "t",
                tr("the timezone (e.g. `CEST`)"),
                std::bind(formattedDateTime, _1, "t"));

    // %random%.
    addMetaWord(MetaWordType::FunctionBased,
                "random",
                tr("a random number"),
                [](const VMetaWord *) {
                    return QString::number(qrand());
                });

    // %random_d%.
    addMetaWord(MetaWordType::Dynamic,
                "random_d",
                tr("dynamic version of `random`"),
                [](const VMetaWord *) {
                    return QString::number(qrand());
                });

    // %date%.
    addMetaWord(MetaWordType::Compound,
                "date",
                QString("%1yyyy%1-%1MM%1-%1dd%1").arg(c_delimiter));

    // %da%.
    addMetaWord(MetaWordType::Compound,
                "da",
                QString("%1yyyy%1%1MM%1%1dd%1").arg(c_delimiter));

    // %time%.
    addMetaWord(MetaWordType::Compound,
                "time",
                QString("%1hh%1:%1mm%1:%1ss%1").arg(c_delimiter));

    // %datetime%.
    addMetaWord(MetaWordType::Compound,
                "datetime",
                QString("%1date%1 %1time%1").arg(c_delimiter));

    // %dt%.
    addMetaWord(MetaWordType::Compound,
                "dt",
                QString("%1da%1-%1time%1").arg(c_delimiter));

    // %note%.
    addMetaWord(MetaWordType::FunctionBased,
                "note",
                tr("name of current note"),
                [](const VMetaWord *) {
                    const VFile *file = g_mainWin->getCurrentFile();
                    if (file) {
                        return file->getName();
                    }

                    return QString();
                });

    // %no%.
    addMetaWord(MetaWordType::FunctionBased,
                "no",
                tr("complete base name of current note"),
                [](const VMetaWord *) {
                    const VFile *file = g_mainWin->getCurrentFile();
                    if (file) {
                        return QFileInfo(file->getName()).completeBaseName();
                    }

                    return QString();
                });

    // Custom meta words.
    initCustomMetaWords();

    // %help% to print all metaword info.
    addMetaWord(MetaWordType::FunctionBased,
                "help",
                tr("information about all defined magic words"),
                allMetaWordsInfo);
}

void VMetaWordManager::initCustomMetaWords()
{
    QVector<VMagicWord> words = g_config->getCustomMagicWords();
    for (auto const & item : words) {
        addMetaWord(MetaWordType::Compound,
                    item.m_name,
                    item.m_definition);
    }
}

QString VMetaWordManager::evaluate(const QString &p_text,
                                   const QHash<QString, QString> &p_overriddenWords) const
{
    if (p_text.isEmpty()) {
        return p_text;
    }

    const_cast<VMetaWordManager *>(this)->init();

    // Update datetime for later parse.
    const_cast<VMetaWordManager *>(this)->m_dateTime = QDateTime::currentDateTime();

    // Update overriden table.
    const_cast<VMetaWordManager *>(this)->m_overriddenWords = p_overriddenWords;

    // Treat the text as a Compound meta word.
    const QString tmpWord("vnote_tmp_metaword");
    Q_ASSERT(!contains(tmpWord));
    VMetaWord metaWord(this,
                       MetaWordType::Compound,
                       tmpWord,
                       p_text,
                       nullptr,
                       true);

    QString val;
    if (metaWord.isValid()) {
        val = metaWord.evaluate();
    } else {
        val = p_text;
    }

    const_cast<VMetaWordManager *>(this)->m_overriddenWords.clear();

    return val;
}

bool VMetaWordManager::contains(const QString &p_word) const
{
    const_cast<VMetaWordManager *>(this)->init();

    return m_metaWords.contains(p_word);
}

const VMetaWord *VMetaWordManager::findMetaWord(const QString &p_word) const
{
    const_cast<VMetaWordManager *>(this)->init();

    auto it = m_metaWords.find(p_word);
    if (it != m_metaWords.end()) {
        return &it.value();
    }

    return NULL;
}

void VMetaWordManager::addMetaWord(MetaWordType p_type,
                                   const QString &p_word,
                                   const QString &p_definition,
                                   MetaWordFunc p_function)
{
    if (p_word.isEmpty() || contains(p_word)) {
        return;
    }

    VMetaWord metaWord(this,
                       p_type,
                       p_word,
                       p_definition,
                       p_function);

    if (metaWord.isValid()) {
        m_metaWords.insert(p_word, metaWord);
        qDebug() << QString("MetaWord %1%2%1[%3] added")
                           .arg(c_delimiter).arg(p_word).arg(p_definition);
    }
}

bool VMetaWordManager::findOverriddenValue(const QString &p_word,
                                           QString &p_value) const
{
    const_cast<VMetaWordManager *>(this)->init();

    auto it = m_overriddenWords.find(p_word);
    if (it != m_overriddenWords.end()) {
        p_value = it.value();
        return true;
    }

    return false;
}

VMetaWord::VMetaWord(const VMetaWordManager *p_manager,
                     MetaWordType p_type,
                     const QString &p_word,
                     const QString &p_definition,
                     MetaWordFunc p_function,
                     bool p_allowAllSpaces)
    : m_manager(p_manager),
      m_type(p_type),
      m_word(p_word),
      m_definition(p_definition),
      m_valid(false)
{
    m_function = p_function;

    if (checkType(MetaWordType::Simple)
        || checkType(MetaWordType::Compound)) {
        Q_ASSERT(!m_function);
    } else {
        Q_ASSERT(m_function);
    }

    checkAndParseDefinition(p_allowAllSpaces);
}

bool VMetaWord::checkType(MetaWordType p_type)
{
    return m_type == p_type;
}

void VMetaWord::checkAndParseDefinition(bool p_allowAllSpaces)
{
    if (m_word.contains(VMetaWordManager::c_delimiter)) {
        m_valid = false;
        return;
    }

    m_valid = true;
    m_tokens.clear();

    // We do not accept \n and \t in the definition.
    QRegExp defReg("[\\S ]*");
    if (!defReg.exactMatch(m_definition) && !p_allowAllSpaces) {
        m_valid = false;
        return;
    }

    if (checkType(MetaWordType::FunctionBased)
        || checkType(MetaWordType::Dynamic)) {
        if (!m_function) {
            m_valid = false;
        }
    } else if (checkType(MetaWordType::Compound)) {
        m_tokens = parseToTokens(m_definition);
        if (m_tokens.isEmpty()) {
            m_valid = false;
            return;
        }

        for (auto const & to : m_tokens) {
            if (to.isMetaWord()) {
                if (!m_manager->contains(to.m_value)) {
                    // Dependency not defined.
                    m_valid = false;
                    break;
                }
            }
        }
    }
}

bool VMetaWord::isValid() const
{
    return m_valid;
}

QString VMetaWord::evaluate() const
{
    Q_ASSERT(m_valid);

    // Check overridden table first.
    QString overriddenVal;
    if (m_manager->findOverriddenValue(m_word, overriddenVal)) {
        return overriddenVal;
    }

    switch (m_type) {
    case MetaWordType::Simple:
        return m_definition;

    case MetaWordType::FunctionBased:
    case MetaWordType::Dynamic:
        return m_function(this);

    case MetaWordType::Compound:
    {
        QHash<QString, QString> cache;
        return evaluateTokens(m_tokens, cache);
    }

    default:
        return "";
    }
}

MetaWordType VMetaWord::getType() const
{
    return m_type;
}

const QString &VMetaWord::getWord() const
{
    return m_word;
}

const QString &VMetaWord::getDefinition() const
{
    return m_definition;
}

const VMetaWordManager *VMetaWord::getManager() const
{
    return m_manager;
}


QString VMetaWord::toString() const
{
    QChar typeChar('U');
    switch (m_type) {
    case MetaWordType::Simple:
        typeChar = 'S';
        break;

    case MetaWordType::FunctionBased:
        typeChar = 'F';
        break;

    case MetaWordType::Dynamic:
        typeChar = 'D';
        break;

    case MetaWordType::Compound:
        typeChar = 'C';
        break;

    default:
        break;
    }

    return QString("%1%2%1[%3]: %4").arg(VMetaWordManager::c_delimiter)
                                     .arg(m_word)
                                     .arg(typeChar)
                                     .arg(m_definition);
}

QVector<VMetaWord::Token> VMetaWord::parseToTokens(const QString &p_text)
{
    QVector<Token> tokens;

    TokenType type = TokenType::Raw;
    QString value;
    value.reserve(p_text.size());
    for (int idx = 0; idx < p_text.size(); ++idx) {
        const QChar &ch = p_text[idx];
        if (ch == VMetaWordManager::c_delimiter) {
            // Check if it is single or double.
            int next = idx + 1;
            if (next == p_text.size()
                || p_text[next] != VMetaWordManager::c_delimiter) {
                // Single delimiter.
                if (type == TokenType::Raw) {
                    // End of a raw token, begin of a MetaWord token.
                    if (!value.isEmpty()) {
                        tokens.push_back(Token(type, value));
                    }

                    type = TokenType::MetaWord;
                } else {
                    // End of a MetaWord token, begin of a Raw token.
                    Q_ASSERT(!value.isEmpty());

                    tokens.push_back(Token(type, value));
                    type = TokenType::Raw;
                }

                value.clear();
            } else {
                // Double delimiter.
                // If now is parsing a MetaWord token, treat the first delimiter
                // as the end of a token.
                // Otherwise, store one single delimiter in value and skip next char.
                if (type == TokenType::MetaWord) {
                    Q_ASSERT(!value.isEmpty());
                    tokens.push_back(Token(type, value));
                    type = TokenType::Raw;
                    value.clear();
                } else {
                    value.push_back(ch);
                    ++idx;
                }
            }
        } else {
            // Push ch in value.
            value.push_back(ch);
        }
    }

    if (!value.isEmpty()) {
        if (type == TokenType::Raw) {
            tokens.push_back(Token(type, value));
        } else {
            // An imcomplete metaword token.
            // Treat it as raw.
            tokens.push_back(Token(TokenType::Raw, "%" + value));
        }

        value.clear();
    }

    return tokens;
}

QString VMetaWord::evaluateTokens(const QVector<VMetaWord::Token> &p_tokens,
                                  QHash<QString, QString> &p_cache) const
{
    QString val;

    for (auto const & to : p_tokens) {
        switch (to.m_type) {
        case TokenType::Raw:
            val += to.m_value;
            break;

        case TokenType::MetaWord:
        {
            const VMetaWord *metaWord = m_manager->findMetaWord(to.m_value);
            if (!metaWord) {
                // Invalid meta word. Treat it as literal value.
                val += VMetaWordManager::c_delimiter + to.m_value + VMetaWordManager::c_delimiter;
                break;
            }

            QString wordVal;
            switch (metaWord->getType()) {
            case MetaWordType::FunctionBased:
            {
                auto it = p_cache.find(metaWord->getWord());
                if (it != p_cache.end()) {
                    // Find it in the cache.
                    wordVal = it.value();
                } else {
                    // First evaluate this meta word.
                    wordVal = metaWord->evaluate();
                    p_cache.insert(metaWord->getWord(), wordVal);
                }

                break;
            }

            case MetaWordType::Compound:
                wordVal = evaluateTokens(metaWord->m_tokens, p_cache);
                break;

            default:
                wordVal = metaWord->evaluate();
                break;
            }

            val += wordVal;
            break;
        }

        default:
            Q_ASSERT(false);
            break;
        }
    }

    return val;
}
