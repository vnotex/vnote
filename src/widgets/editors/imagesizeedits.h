#ifndef IMAGESIZEEDITS_H
#define IMAGESIZEEDITS_H

#include <QString>
#include <QVector>

namespace vte {
struct HtmlImgTag;
}

namespace vnotex {

// One in-place text edit, in ABSOLUTE document positions, half open.
// An insertion is a zero-length span.
struct SpanEdit {
  int m_start = 0;
  int m_end = 0;
  QString m_text;
};

// Plan the edits that give an existing HTML `<img>` the declared size
// (@p_width, @p_height); 0 clears that dimension.
//
// The tag is NEVER regenerated (D9): `class`, `style`, `data-*`, `loading` and
// anything else the user wrote must survive, so only the `width` / `height`
// attributes are touched.
//
// Rules:
// - Setting a dimension updates its FIRST occurrence (the HTML5 first-wins
//   rule) and removes every later duplicate of that name.
// - Clearing a dimension removes EVERY occurrence. Unmasking only the second of
//   `width="100" width="200"` would leave the image silently still sized.
// - A dimension that is absent and wanted is inserted right after `src`, in the
//   canonical `width` then `height` order. Both are coalesced into ONE edit, so
//   an insertion can never share a start position with a removal.
// - A removal also swallows the whitespace that separated the attribute from
//   the one before it, so nothing is left with a double space -- but never past
//   @p_regionStart, and, for an attribute at or after the insertion point,
//   never past that point either.
//
// @p_content is the whole document; @p_regionStart is where the tag begins in
// it. Every span in @p_tag is already absolute.
//
// The result is ALREADY in application order, so a caller applies it as
// returned: descending by start, and for an equal start the REMOVAL first, so
// every span not yet applied stays valid.
QVector<SpanEdit> planHtmlImageSizeEdits(const QString &p_content, int p_regionStart,
                                         const vte::HtmlImgTag &p_tag, int p_width, int p_height);

} // namespace vnotex

#endif // IMAGESIZEEDITS_H
