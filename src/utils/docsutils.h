#ifndef DOCSUTILS_H
#define DOCSUTILS_H

#include <QStringList>

namespace vnotex {
class DocsUtils {
public:
  DocsUtils() = delete;

  static QString getDocText(const QString &p_baseName);

  static QString getDocFile(const QString &p_baseName,
                            const QStringList &p_uiLanguages = s_uiLanguages);

  static void addSearchPath(const QString &p_path);

  // Language of the loaded VNote catalog, or en_US for untranslated source text.
  static void setLocale(const QString &p_locale);

private:
  static QStringList s_searchPaths;
  static QStringList s_uiLanguages;
};
} // namespace vnotex

#endif // DOCSUTILS_H
