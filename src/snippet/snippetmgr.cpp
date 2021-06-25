#include "snippetmgr.h"

#include <QDir>
#include <QJsonDocument>
#include <QDebug>
#include <QFileInfo>
#include <QSet>
#include <QTextCursor>
#include <QRegularExpression>
#include <QDateTime>

#include <core/configmgr.h>
#include <buffer/buffer.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/textutils.h>

using namespace vnotex;

const QString SnippetMgr::c_snippetSymbolRegExp = QString("%([^%]+)%");

SnippetMgr::SnippetMgr()
{
    loadSnippets();
}

QString SnippetMgr::getSnippetFolder() const
{
    return ConfigMgr::getInst().getUserSnippetFolder();
}

const QVector<QSharedPointer<Snippet>> &SnippetMgr::getSnippets() const
{
    return m_snippets;
}

void SnippetMgr::loadSnippets()
{
    Q_ASSERT(m_snippets.isEmpty());

    auto builtInSnippets = loadBuiltInSnippets();

    QSet<QString> names;
    for (const auto &snippet : builtInSnippets) {
        Q_ASSERT(!names.contains(snippet->getName()));
        names.insert(snippet->getName());
    }

    // Look for all the *.json files.
    QDir dir(getSnippetFolder());
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    const auto jsonFiles = dir.entryList(QStringList() << "*.json");
    for (const auto &jsonFile : jsonFiles) {
        auto snip = loadSnippet(dir.filePath(jsonFile));
        if (snip->isValid()) {
            if (names.contains(snip->getName())) {
                qWarning() << "skip loading snippet with name conflict" << snip->getName() << jsonFile;
                continue;
            }

            names.insert(snip->getName());

            addOneSnippet(snip);
        }
    }

    m_snippets.append(builtInSnippets);

    qDebug() << "loaded" << m_snippets.size() << "snippets";
}

QVector<int> SnippetMgr::getAvailableShortcuts(int p_exemption) const
{
    QVector<int> shortcuts;

    for (int i = 0; i < 100; ++i) {
        if (!m_shortcutToSnippet.contains(i) || i == p_exemption) {
            shortcuts.push_back(i);
        }
    }

    return shortcuts;
}

QSharedPointer<Snippet> SnippetMgr::find(const QString &p_name, Qt::CaseSensitivity p_cs) const
{
    if (p_cs == Qt::CaseInsensitive) {
        const auto lowerName = p_name.toLower();
        for (const auto &snip : m_snippets) {
            if (snip->getName().toLower() == lowerName) {
                return snip;
            }
        }
    } else {
        for (const auto &snip : m_snippets) {
            if (snip->getName() == p_name) {
                return snip;
            }
        }
    }

    return nullptr;
}

void SnippetMgr::addSnippet(const QSharedPointer<Snippet> &p_snippet)
{
    Q_ASSERT(!find(p_snippet->getName(), Qt::CaseInsensitive));
    saveSnippet(p_snippet);
    addOneSnippet(p_snippet);
}

void SnippetMgr::addOneSnippet(const QSharedPointer<Snippet> &p_snippet)
{
    m_snippets.push_back(p_snippet);
    addSnippetToShortcutMap(p_snippet);
}

QSharedPointer<Snippet> SnippetMgr::loadSnippet(const QString &p_snippetFile) const
{
    const auto obj = FileUtils::readJsonFile(p_snippetFile);
    auto snip = QSharedPointer<Snippet>::create(QFileInfo(p_snippetFile).completeBaseName());
    snip->fromJson(obj);
    return snip;
}

void SnippetMgr::saveSnippet(const QSharedPointer<Snippet> &p_snippet)
{
    Q_ASSERT(p_snippet->isValid()
             && !p_snippet->isReadOnly()
             && p_snippet->getType() != Snippet::Type::Dynamic);
    FileUtils::writeFile(getSnippetFile(p_snippet), p_snippet->toJson());
}

void SnippetMgr::removeSnippet(const QString &p_name)
{
    auto snippet = find(p_name);
    if (!snippet || snippet->isReadOnly()) {
        return;
    }

    removeSnippetFromShortcutMap(snippet);
    m_snippets.removeAll(snippet);
    FileUtils::removeFile(getSnippetFile(snippet));
}

QString SnippetMgr::getSnippetFile(const QSharedPointer<Snippet> &p_snippet) const
{
    return PathUtils::concatenateFilePath(getSnippetFolder(), p_snippet->getName() + QStringLiteral(".json"));
}

void SnippetMgr::updateSnippet(const QString &p_name, const QSharedPointer<Snippet> &p_snippet)
{
    auto snippet = find(p_name);
    Q_ASSERT(snippet);

    // If renamed, remove the old file first.
    if (p_name != p_snippet->getName()) {
        FileUtils::removeFile(getSnippetFile(snippet));
    }

    removeSnippetFromShortcutMap(snippet);

    *snippet = *p_snippet;
    saveSnippet(snippet);

    addSnippetToShortcutMap(snippet);
}

void SnippetMgr::removeSnippetFromShortcutMap(const QSharedPointer<Snippet> &p_snippet)
{
    if (p_snippet->getShortcut() != Snippet::InvalidShortcut) {
        auto iter = m_shortcutToSnippet.find(p_snippet->getShortcut());
        Q_ASSERT(iter != m_shortcutToSnippet.end());
        if (iter.value() == p_snippet) {
            // There may exist conflict in shortcut.
            m_shortcutToSnippet.erase(iter);
        }
    }
}

void SnippetMgr::addSnippetToShortcutMap(const QSharedPointer<Snippet> &p_snippet)
{
    if (p_snippet->getShortcut() != Snippet::InvalidShortcut) {
        m_shortcutToSnippet.insert(p_snippet->getShortcut(), p_snippet);
    }
}

void SnippetMgr::applySnippet(const QString &p_name,
                              vte::VTextEdit *p_textEdit,
                              const OverrideMap &p_overrides) const
{
    auto snippet = find(p_name);
    if (!snippet) {
        return;
    }
    Q_ASSERT(snippet->isValid());

    auto cursor = p_textEdit->textCursor();
    cursor.beginEditBlock();

    // Get selected text.
    const auto selectedText = p_textEdit->selectedText();
    p_textEdit->removeSelectedText();

    QString appliedText;
    int cursorOffset = 0;

    auto it = p_overrides.find(p_name);
    if (it != p_overrides.end()) {
        appliedText = it.value();
        cursorOffset = appliedText.size();
    } else {
        // Fetch indentation of first line.
        QString indentationSpaces;
        if (snippet->isIndentAsFirstLineEnabled()) {
            indentationSpaces = vte::TextEditUtils::fetchIndentationSpaces(cursor.block());
        }

        appliedText = snippet->apply(selectedText, indentationSpaces, cursorOffset);
        appliedText = applySnippetBySymbol(appliedText, selectedText, cursorOffset, p_overrides);
    }

    const int beforePos = cursor.position();
    cursor.insertText(appliedText);
    cursor.setPosition(beforePos + cursorOffset);

    cursor.endEditBlock();
    p_textEdit->setTextCursor(cursor);
}

QString SnippetMgr::applySnippetBySymbol(const QString &p_content) const
{
    int offset = 0;
    return applySnippetBySymbol(p_content, QString(), offset);
}

QString SnippetMgr::applySnippetBySymbol(const QString &p_content,
                                         const QString &p_selectedText,
                                         int &p_cursorOffset,
                                         const OverrideMap &p_overrides) const
{
    QString content(p_content);

    int maxTimes = 100;

    QRegularExpression regExp(c_snippetSymbolRegExp);
    int pos = 0;
    while (pos < content.size() && maxTimes-- > 0) {
        QRegularExpressionMatch match;
        int idx = content.indexOf(regExp, pos, &match);
        if (idx == -1) {
            break;
        }

        const auto snippetName = match.captured(1);
        auto snippet = find(snippetName);
        if (!snippet) {
            // Skip it.
            pos = idx + match.capturedLength(0);
            continue;
        }

        QString afterText;

        auto it = p_overrides.find(snippetName);
        if (it != p_overrides.end()) {
            afterText = it.value();
        } else {
            const auto indentationSpaces = vte::TextUtils::fetchIndentationSpacesInMultiLines(content, idx);

            // Ignore the cursor mark.
            int ignoredCursorOffset = 0;
            afterText = snippet->apply(p_selectedText, indentationSpaces, ignoredCursorOffset);
        }

        content.replace(idx, match.capturedLength(0), afterText);

        // Maintain the cursor offset.
        if (p_cursorOffset > idx) {
            if (p_cursorOffset < idx + match.capturedLength(0)) {
                p_cursorOffset = idx;
            } else {
                p_cursorOffset += (afterText.size() - match.capturedLength(0));
            }
        }

        // @afterText may still contains snippet symbol.
        pos = idx;
    }

    return content;
}

// Used as the function template for some date/time related dynamic snippets.
static QString formattedDateTime(const QString &p_format)
{
    return QDateTime::currentDateTime().toString(p_format);
}

QVector<QSharedPointer<Snippet>> SnippetMgr::loadBuiltInSnippets() const
{
    QVector<QSharedPointer<Snippet>> snippets;

    addDynamicSnippet(snippets,
                      "d",
                      tr("the day as number without a leading zero (`1` to `31`)"),
                      std::bind(formattedDateTime, "d"));
    addDynamicSnippet(snippets,
                      "dd",
                      tr("the day as number with a leading zero (`01` to `31`)"),
                      std::bind(formattedDateTime, "dd"));
    addDynamicSnippet(snippets,
                      "ddd",
                      tr("the abbreviated localized day name (e.g. `Mon` to `Sun`)"),
                      std::bind(formattedDateTime, "ddd"));
    addDynamicSnippet(snippets,
                      "dddd",
                      tr("the long localized day name (e.g. `Monday` to `Sunday`)"),
                      std::bind(formattedDateTime, "dddd"));
    addDynamicSnippet(snippets,
                      "M",
                      tr("the month as number without a leading zero (`1` to `12`)"),
                      std::bind(formattedDateTime, "M"));
    addDynamicSnippet(snippets,
                      "MM",
                      tr("the month as number with a leading zero (`01` to `12`)"),
                      std::bind(formattedDateTime, "MM"));
    addDynamicSnippet(snippets,
                      "MMM",
                      tr("the abbreviated localized month name (e.g. `Jan` to `Dec`)"),
                      std::bind(formattedDateTime, "MMM"));
    addDynamicSnippet(snippets,
                      "MMMM",
                      tr("the long localized month name (e.g. `January` to `December`)"),
                      std::bind(formattedDateTime, "MMMM"));
    addDynamicSnippet(snippets,
                      "yy",
                      tr("the year as two digit numbers (`00` to `99`)"),
                      std::bind(formattedDateTime, "yy"));
    addDynamicSnippet(snippets,
                      "yyyy",
                      tr("the year as four digit numbers"),
                      std::bind(formattedDateTime, "yyyy"));
    addDynamicSnippet(snippets,
                      "w",
                      tr("the week number (`1` to `53`)"),
                      [](const QString &) {
                          return QString::number(QDate::currentDate().weekNumber());
                      });
    addDynamicSnippet(snippets,
                      "H",
                      tr("the hour without a leading zero (`0` to `23` even with AM/PM display)"),
                      std::bind(formattedDateTime, "H"));
    addDynamicSnippet(snippets,
                      "HH",
                      tr("the hour with a leading zero (`00` to `23` even with AM/PM display)"),
                      std::bind(formattedDateTime, "HH"));
    addDynamicSnippet(snippets,
                      "m",
                      tr("the minute without a leading zero (`0` to `59`)"),
                      std::bind(formattedDateTime, "m"));
    addDynamicSnippet(snippets,
                      "mm",
                      tr("the minute with a leading zero (`00` to `59`)"),
                      std::bind(formattedDateTime, "mm"));
    addDynamicSnippet(snippets,
                      "s",
                      tr("the second without a leading zero (`0` to `59`)"),
                      std::bind(formattedDateTime, "s"));
    addDynamicSnippet(snippets,
                      "ss",
                      tr("the second with a leading zero (`00` to `59`)"),
                      std::bind(formattedDateTime, "ss"));
    addDynamicSnippet(snippets,
                      "date",
                      tr("date (`2021-02-24`)"),
                      std::bind(formattedDateTime, "yyyy-MM-dd"));
    addDynamicSnippet(snippets,
                      "da",
                      tr("the abbreviated date (`20210224`)"),
                      std::bind(formattedDateTime, "yyyyMMdd"));
    addDynamicSnippet(snippets,
                      "time",
                      tr("time (`16:51:02`)"),
                      std::bind(formattedDateTime, "hh:mm:ss"));
    addDynamicSnippet(snippets,
                      "datetime",
                      tr("date and time (`2021-02-24_16:51:02`)"),
                      std::bind(formattedDateTime, "yyyy-MM-dd_hh:mm:ss"));

    // These snippets need override to fill the real value.
    // Check generateOverrides().
    addDynamicSnippet(snippets,
                      QStringLiteral("note"),
                      tr("name of current note"),
                      [](const QString &) {
                          return tr("[Value Not Available]");
                      });
    addDynamicSnippet(snippets,
                      QStringLiteral("no"),
                      tr("complete base name of current note"),
                      [](const QString &) {
                          return tr("[Value Not Available]");
                      });
    return snippets;
}

void SnippetMgr::addDynamicSnippet(QVector<QSharedPointer<Snippet>> &p_snippets,
                                   const QString &p_name,
                                   const QString &p_description,
                                   const DynamicSnippet::Callback &p_callback)
{
    auto snippet = QSharedPointer<DynamicSnippet>::create(p_name, p_description, p_callback);
    p_snippets.push_back(snippet);
}

SnippetMgr::OverrideMap SnippetMgr::generateOverrides(const Buffer *p_buffer)
{
    OverrideMap overrides;
    overrides.insert(QStringLiteral("note"), p_buffer->getName());
    overrides.insert(QStringLiteral("no"), QFileInfo(p_buffer->getName()).completeBaseName());
    return overrides;
}

SnippetMgr::OverrideMap SnippetMgr::generateOverrides(const QString &p_fileName)
{
    OverrideMap overrides;
    overrides.insert(QStringLiteral("note"), p_fileName);
    overrides.insert(QStringLiteral("no"), QFileInfo(p_fileName).completeBaseName());
    return overrides;
}
