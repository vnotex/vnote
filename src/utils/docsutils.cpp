#include "docsutils.h"

#include <QDir>

#include "fileutils.h"

using namespace vnotex;

QStringList DocsUtils::s_searchPaths;

QString DocsUtils::s_locale = "en_US";

void DocsUtils::addSearchPath(const QString &p_path)
{
    s_searchPaths.append(p_path);
}

QString DocsUtils::getDocText(const QString &p_baseName)
{
    auto filePath = getDocFile(p_baseName);
    if (!filePath.isEmpty()) {
        return FileUtils::readTextFile(filePath);
    }

    return "";
}

QString DocsUtils::getDocFile(const QString &p_baseName)
{
    const auto shortLocale = s_locale.split('_')[0];

    const auto fullLocaleName = QString("%1/%2").arg(s_locale, p_baseName);
    const auto shortLocaleName = QString("%1/%2").arg(shortLocale, p_baseName);
    const auto defaultLocaleName = QString("%1/%2").arg(QStringLiteral("en"), p_baseName);

    for (const auto &pa : s_searchPaths) {
        QDir dir(pa);
        if (!dir.exists()) {
            continue;
        }

        if (dir.exists(fullLocaleName)) {
            return dir.filePath(fullLocaleName);
        } else if (dir.exists(shortLocaleName)) {
            return dir.filePath(shortLocaleName);
        } else if (dir.exists(defaultLocaleName)) {
            return dir.filePath(defaultLocaleName);
        }
    }

    return "";
}

void DocsUtils::setLocale(const QString &p_locale)
{
    s_locale = p_locale;
}
