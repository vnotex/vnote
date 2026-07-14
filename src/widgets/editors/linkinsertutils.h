#ifndef LINKINSERTUTILS_H
#define LINKINSERTUTILS_H

#include <QString>

namespace vnotex {
// Pure helpers for building links inserted into the editor.
// Kept dependency-free so they are unit-testable without the full editor.
namespace LinkInsertUtils {
// Append a heading anchor (URL fragment) to a relative link, preserving in-file
// navigation for "Insert As Relative Link" (issue #2656). Returns @p_relativeLink
// unchanged when @p_fragment is empty. @p_fragment must NOT include the leading '#'.
QString appendFragmentToLink(const QString &p_relativeLink, const QString &p_fragment);
} // namespace LinkInsertUtils
} // namespace vnotex

#endif // LINKINSERTUTILS_H
