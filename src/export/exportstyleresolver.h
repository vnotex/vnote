#ifndef EXPORTSTYLERESOLVER_H
#define EXPORTSTYLERESOLVER_H

#include <QString>

namespace vnotex {

// Decide which syntax-highlight stylesheet an export should use.
//
// The export dialog persists a chosen syntax style, but a stale/incorrect selection (e.g. a
// web.css from before the web/syntax combo split) must never be honored, or exported code blocks
// lose their Prism .token colors and the copy-button toolbar positioning.
//
// Rule: honor p_optionFile ONLY when its basename is exactly "highlight.css" (any directory, so a
// non-current theme's highlight.css is respected); otherwise fall back to p_themeHighlightFile
// (the current theme's highlight.css). If both are empty the result is empty (degenerate case).
//
// GUI-free (uses only QString/QFileInfo) so it is unit-testable without WebEngine.
QString resolveSyntaxHighlightFile(const QString &p_optionFile,
                                   const QString &p_themeHighlightFile);

} // namespace vnotex

#endif // EXPORTSTYLERESOLVER_H
