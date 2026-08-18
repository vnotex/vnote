#ifndef IMAGEDESTINATIONREWRITER_H
#define IMAGEDESTINATIONREWRITER_H

#include <QString>

namespace vte {
struct MarkdownLink;
}

namespace vnotex {

// Rewrite ONE image reference's destination in @p_text to the source-neutral
// logical url @p_url, spelling it the way that reference's own syntax requires.
// Returns whether anything was written.
//
// There is exactly one implementation because there were two, and they had
// already drifted: the export path canonicalised a Markdown destination while
// the paste path wrote it bare, so the same renamed file could come out as
// `<a b.png>` in one and the broken `a b.png` in the other.
//
// The replacement is ALWAYS measured with a source span, never with the length
// of the cleaned destination: `a\_b.png` occupies 8 source characters and
// cleans to 7, so the cleaned length would eat the character after it.
//
// For HTML the WHOLE `src` attribute is replaced, not just its value -- an
// unquoted `src=old.png` renamed to a name containing a space would otherwise
// split into two attributes. MarkdownLink carries the VALUE span by contract,
// so the region is re-scanned for the attribute span; anything that does not
// yield exactly one tag with one usable `src` is skipped conservatively.
//
// Callers rewriting many references must visit them in DESCENDING destination
// order. The HTML attribute span starts before the reported destination span,
// but stays inside its own tag, so that order remains sufficient.
bool rewriteImageDestination(QString &p_text, const vte::MarkdownLink &p_link,
                             const QString &p_url);

// Spell @p_url as a Markdown destination. The caller has just renamed or
// created the file, so the name is under our control and may be written
// canonically: angle brackets when it contains characters a bare destination
// cannot hold, bare otherwise.
QString spellMarkdownDestination(const QString &p_url);

} // namespace vnotex

#endif // IMAGEDESTINATIONREWRITER_H
