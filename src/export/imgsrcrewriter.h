#ifndef IMGSRCREWRITER_H
#define IMGSRCREWRITER_H

#include <QString>

#include <functional>

namespace vnotex {

// Rewrite the `src` of every `<img>` in RENDERED html, preserving every other
// attribute.
//
// This operates on the html the web view produced, NOT on note source. Note
// source has exactly one `<img>` parser, vte::scanHtmlImgTags(); do not use this
// for it, and do not grow it into one. It is shared by WebViewExporter's
// resource-embedding and resource-folder passes so the two cannot drift, and so
// the attribute-preservation guarantee is testable without a web engine.
//
// @p_resolve is called with the current src and returns the replacement, or an
// empty string to leave that tag alone. `data:` URIs are never offered.
//
// The attributes on EITHER SIDE of `src` are carried across verbatim, which is
// what makes an image's `width` / `height` survive an export.
//
// @p_quote is the quote character to spell the new value with.
//
// Returns whether anything was rewritten.
bool rewriteRenderedImgSrc(QString &p_html, QChar p_quote,
                           const std::function<QString(const QString &)> &p_resolve);

} // namespace vnotex

#endif // IMGSRCREWRITER_H
