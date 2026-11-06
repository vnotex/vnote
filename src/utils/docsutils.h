#ifndef DOCSUTILS_H
#define DOCSUTILS_H

#include <QLocale>
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
  static void setLocale(const QString &p_locale) {
    s_uiLanguages = QLocale(p_locale).uiLanguages();
  }

  static const QStringList &getUiLanguages() { return s_uiLanguages; }

private:
  static QStringList s_searchPaths;
  inline static QStringList s_uiLanguages{QStringLiteral("en_US")};
};
} // namespace vnotex

#endif // DOCSUTILS_H
