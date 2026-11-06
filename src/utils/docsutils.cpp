#include "docsutils.h"

#include <QDebug>
#include <QDir>

#include "fileutils2.h"
#include "localeutils.h"

using namespace vnotex;

QStringList DocsUtils::s_searchPaths;

void DocsUtils::addSearchPath(const QString &p_path) { s_searchPaths.append(p_path); }

QString DocsUtils::getDocText(const QString &p_baseName) {
  auto filePath = getDocFile(p_baseName);
  if (!filePath.isEmpty()) {
    QString text;
    Error err = FileUtils2::readTextFile(filePath, &text);
    if (err) {
      qWarning() << err.what();
      return "";
    }
    return text;
  }

  return "";
}

QString DocsUtils::getDocFile(const QString &p_baseName, const QStringList &p_uiLanguages) {
  const auto defaultLocaleName = QStringLiteral("%1/%2").arg(QStringLiteral("en_US"), p_baseName);

  for (const auto &pa : s_searchPaths) {
    QDir dir(pa);
    if (!dir.exists()) {
      continue;
    }

    const auto localized = findLocalizedValue(p_uiLanguages, [&](const QString &p_locale) {
      const auto name = QStringLiteral("%1/%2").arg(p_locale, p_baseName);
      return dir.exists(name) ? dir.filePath(name) : QString();
    });
    if (!localized.isEmpty()) {
      return localized;
    }
    if (dir.exists(defaultLocaleName)) {
      return dir.filePath(defaultLocaleName);
    }
  }

  return "";
}
