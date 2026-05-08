#include "imageresolutionutils.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>

#include <vtextedit/markdownutils.h>

using namespace vnotex;

QVector<RelativeImageInfo> ImageResolutionUtils::resolveRelativeImages(
    const QString &p_markdownText,
    const QString &p_sourceBasePath)
{
  auto imageInfos = vte::MarkdownUtils::fetchImageInfoViaCmark(p_markdownText);

  QVector<RelativeImageInfo> results;
  results.reserve(imageInfos.size());

  for (const auto &info : imageInfos) {
    if (info.m_url.isEmpty()) {
      continue;
    }

    // Skip network URLs.
    if (info.m_url.startsWith(QStringLiteral("http://"))
        || info.m_url.startsWith(QStringLiteral("https://"))
        || info.m_url.contains(QStringLiteral("://"))) {
      continue;
    }

    // Skip Qt resource paths.
    if (info.m_url.startsWith(QStringLiteral("qrc:"))
        || info.m_url.startsWith(QStringLiteral(":/"))) {
      continue;
    }

    // Skip absolute paths.
    if (QDir::isAbsolutePath(info.m_url)) {
      continue;
    }

    // Relative path — resolve against source base path.
    QString absPath = vte::MarkdownUtils::linkUrlToPath(p_sourceBasePath, info.m_url);

    RelativeImageInfo ri;
    ri.srcAbsolutePath = absPath;
    ri.urlInLink = info.m_url;
    ri.urlInLinkPos = info.m_urlPos;
    ri.regionStart = info.m_regionStart;
    ri.regionEnd = info.m_regionEnd;
    ri.alt = info.m_alt;
    ri.title = info.m_title;
    ri.exists = QFileInfo::exists(absPath);

    results.append(ri);
  }

  // Sort descending by urlInLinkPos for safe in-place text rewriting.
  std::sort(results.begin(), results.end(),
            [](const RelativeImageInfo &a, const RelativeImageInfo &b) {
              return a.urlInLinkPos > b.urlInLinkPos;
            });

  return results;
}
