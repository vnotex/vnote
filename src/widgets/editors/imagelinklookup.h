#ifndef IMAGELINKLOOKUP_H
#define IMAGELINKLOOKUP_H

#include <QVector>

#include <vtextedit/markdownhighlighterdata.h>

namespace vnotex {
// Positional lookup over the image links published by the Markdown highlighter.
//
// Kept free of QTextBlock and of the editor so it is unit-testable without the
// widget stack, following the LinkInsertUtils precedent: the caller passes plain
// integers taken from the block it is interested in.
namespace ImageLinkLookup {

enum class ImageLinkHit {
  // No image link covers the cursor.
  None,
  // An image link covers the cursor, but its region runs past the end of the
  // block. The caller cannot resolve it against a single block's text, and the
  // context menu should offer the Image submenu without a target.
  SpansBeyondBlock,
  // An image link covers the cursor and lies wholly within the block.
  Found,
};

// Find the image link covering @p_cursorPos.
//
// @p_blockPos: document position of the start of the cursor's block.
// @p_blockTextSize: length of that block's text, excluding the separator.
// @p_index: set to the index into @p_links when the result is Found or
//           SpansBeyondBlock; untouched otherwise. May be nullptr.
//
// A cursor sitting exactly at the end of the block counts as inside a region
// that ends there, so right-clicking after the closing `)` still finds the
// image.
ImageLinkHit imageLinkAt(const QVector<vte::md::ImageLinkInfo> &p_links, int p_cursorPos,
                         int p_blockPos, int p_blockTextSize, int *p_index);

} // namespace ImageLinkLookup
} // namespace vnotex

#endif // IMAGELINKLOOKUP_H
