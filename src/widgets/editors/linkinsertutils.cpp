#include "linkinsertutils.h"

namespace vnotex {
namespace LinkInsertUtils {

QString appendFragmentToLink(const QString &p_relativeLink, const QString &p_fragment) {
  if (p_fragment.isEmpty()) {
    return p_relativeLink;
  }
  return p_relativeLink + QLatin1Char('#') + p_fragment;
}

} // namespace LinkInsertUtils
} // namespace vnotex
