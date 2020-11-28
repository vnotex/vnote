#ifndef DOCSUTILS_H
#define DOCSUTILS_H

#include <QStringList>

namespace vnotex
{
    class DocsUtils
    {
    public:
        DocsUtils() = delete;

        static QString getDocText(const QString &p_baseName);

        static QString getDocFile(const QString &p_baseName);

        static void addSearchPath(const QString &p_path);

        static void setLocale(const QString &p_locale);

    private:
        static QStringList s_searchPaths;

        static QString s_locale;
    };
}

#endif // DOCSUTILS_H
