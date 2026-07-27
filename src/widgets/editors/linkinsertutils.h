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

// Compose the URL for an "Insert As Relative Link" paste.
//
// When the paste target is the file currently being edited AND the link carries
// a heading anchor, the file part is dropped so the result is a pure in-document
// reference ("#anchor") rather than a self-referential path
// ("CurrentFile.md#anchor"). Without a fragment the file part is kept, since a
// bare "#" would not be a usable link.
//
// @p_fragment must NOT include the leading '#'.
QString composeRelativeLink(const QString &p_relativeLink, const QString &p_fragment,
                            bool p_targetIsCurrentFile);
} // namespace LinkInsertUtils
} // namespace vnotex

#endif // LINKINSERTUTILS_H
