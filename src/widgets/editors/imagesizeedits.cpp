#include "imagesizeedits.h"

#include <algorithm>

#include <vtextedit/htmlimgscanner.h>

using namespace vnotex;

namespace {
// Extend a removal backwards over the whitespace that separated the attribute
// from the one before it, so removing `width="5"` does not leave a double
// space.
//
// @p_content is the WHOLE document and @p_attrStart an ABSOLUTE position into
// it, so the character before the attribute is at p_attrStart - 1. It must not
// walk past @p_floor, which is the later of the tag's start and the point where
// a new attribute may be inserted -- otherwise a run of whitespace before the
// tag could swallow the tag itself.
int widenRemovalStart(const QString &p_content, int p_floor, int p_attrStart) {
  int start = p_attrStart;
  while (start > p_floor && p_content.at(start - 1).isSpace()) {
    --start;
  }
  return start;
}
} // namespace

void vnotex::sortSpanEditsForApplication(QVector<SpanEdit> &p_edits) {
  // Descending by start so an earlier span stays valid while a later one is
  // written. At an equal start the REMOVAL (a non-empty span) must run before
  // the INSERTION (a zero-length span): running the insertion first would leave
  // the removal's stored end pointing past the text just inserted, and it would
  // delete part of it. std::sort gives no tie order, so the comparator states
  // it explicitly.
  std::sort(p_edits.begin(), p_edits.end(), [](const SpanEdit &p_a, const SpanEdit &p_b) {
    if (p_a.m_start != p_b.m_start) {
      return p_a.m_start > p_b.m_start;
    }
    return p_a.m_end > p_b.m_end;
  });
}

QVector<SpanEdit> vnotex::planHtmlImageSizeEdits(const QString &p_content, int p_regionStart,
                                                 const vte::HtmlImgTag &p_tag, int p_width,
                                                 int p_height) {
  QVector<SpanEdit> edits;

  const auto *srcAttr = p_tag.attr("src");
  if (!srcAttr || srcAttr->m_attrEnd < 0) {
    return edits;
  }

  // Everything missing goes into ONE insertion right after `src`, in the
  // canonical order. Two separate zero-length insertions at the same position
  // would have no defined relative order.
  const int insertAt = srcAttr->m_attrEnd;
  QString inserted;

  const char *names[] = {"width", "height"};
  const int values[] = {p_width, p_height};
  for (int i = 0; i < 2; ++i) {
    const QLatin1String name(names[i]);
    bool first = true;
    for (const auto &attr : p_tag.m_attrs) {
      if (attr.m_name != name) {
        continue;
      }
      if (attr.m_attrStart < 0 || attr.m_attrEnd <= attr.m_attrStart) {
        continue;
      }

      if (first && values[i] > 0) {
        edits.append({attr.m_attrStart, attr.m_attrEnd,
                      QStringLiteral("%1=\"%2\"").arg(name).arg(values[i])});
      } else {
        // A removal never walks back past the insertion point, so it can never
        // overlap the coalesced insertion above.
        const int floor = qMax(p_regionStart, insertAt);
        edits.append({widenRemovalStart(p_content, floor, attr.m_attrStart), attr.m_attrEnd, {}});
      }
      first = false;
    }

    if (first && values[i] > 0) {
      inserted += QStringLiteral(" %1=\"%2\"").arg(name).arg(values[i]);
    }
  }

  if (!inserted.isEmpty()) {
    edits.append({insertAt, insertAt, inserted});
  }

  sortSpanEditsForApplication(edits);
  return edits;
}
