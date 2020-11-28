#include "textutils.h"

#include <QStringList>

using namespace vnotex;

QString TextUtils::removeCodeBlockFence(const QString &p_text)
{
    auto text = unindentTextMultiLines(p_text);
    Q_ASSERT(text.startsWith(QStringLiteral("```")) || text.startsWith(QStringLiteral("~~~")));
    int idx = text.indexOf(QLatin1Char('\n')) + 1;
    int lidx = text.size() - 1;
    // Trim spaces at the end.
    while (lidx >= 0 && text[lidx].isSpace()) {
        --lidx;
    }

    Q_ASSERT(text[lidx] == QLatin1Char('`') || text[lidx] == QLatin1Char('~'));
    return text.mid(idx, lidx + 1 - idx - 3);
}

int TextUtils::fetchIndentation(const QString &p_text)
{
    int idx = firstNonSpace(p_text);
    return idx == -1 ? 0 : idx;
}

QString TextUtils::unindentText(const QString &p_text, int p_spaces)
{
    if (p_spaces == 0) {
        return p_text;
    }

    int idx = 0;
    while (idx < p_spaces && idx < p_text.size() && p_text[idx].isSpace()) {
        ++idx;
    }
    return p_text.right(p_text.size() - idx);
}

int TextUtils::firstNonSpace(const QString &p_text)
{
    for (int i = 0; i < p_text.size(); ++i) {
        if (!p_text.at(i).isSpace()) {
            return i;
        }
    }

    return -1;
}

QString TextUtils::unindentTextMultiLines(const QString &p_text)
{
    if (p_text.isEmpty()) {
        return p_text;
    }

    auto lines = p_text.split(QLatin1Char('\n'));
    Q_ASSERT(lines.size() > 0);

    const int indentation = fetchIndentation(lines[0]);
    if (indentation == 0) {
        return p_text;
    }

    QString res = lines[0].right(lines[0].size() - indentation);
    for (int i = 1; i < lines.size(); ++i) {
        const auto &line = lines[i];
        int idx = 0;
        while (idx < indentation && idx < line.size() && line[idx].isSpace()) {
            ++idx;
        }
        res = res + QLatin1Char('\n') + line.right(line.size() - idx);
    }

    return res;
}

QString TextUtils::purifyUrl(const QString &p_url)
{
    int idx = p_url.indexOf('?');
    if (idx > -1) {
        return p_url.left(idx);
    }

    return p_url;
}
