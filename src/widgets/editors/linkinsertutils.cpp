#include "linkinsertutils.h"

namespace vnotex {
namespace LinkInsertUtils {

QString appendFragmentToLink(const QString &p_relativeLink, const QString &p_fragment) {
  if (p_fragment.isEmpty()) {
    return p_relativeLink;
  }
  return p_relativeLink + QLatin1Char('#') + p_fragment;
}

QString composeRelativeLink(const QString &p_relativeLink, const QString &p_fragment,
                            bool p_targetIsCurrentFile) {
  if (p_targetIsCurrentFile && !p_fragment.isEmpty()) {
    // Self-reference: linking to a heading in the file being edited, so the
    // file part is redundant and only makes the link brittle across renames.
    return QLatin1Char('#') + p_fragment;
  }

  return appendFragmentToLink(p_relativeLink, p_fragment);
}

} // namespace LinkInsertUtils
} // namespace vnotex
