#include "urlutils.h"

#include <QUrl>

using namespace vnotex;

UrlFragmentResult vnotex::splitUrlFragment(const QString &p_url)
{
  UrlFragmentResult result;
  int hashIdx = p_url.indexOf(QLatin1Char('#'));
  if (hashIdx < 0) {
    result.path = p_url;
    return result;
  }
  result.path = p_url.left(hashIdx);
  QString rawFragment = p_url.mid(hashIdx + 1);
  if (!rawFragment.isEmpty()) {
    result.fragment = QUrl::fromPercentEncoding(rawFragment.toUtf8());
  }
  return result;
}
